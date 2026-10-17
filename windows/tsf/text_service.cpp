#include "text_service.h"

#include <algorithm>
#include <new>

#include "../engine/keys.h"

namespace zl::tsf {

namespace {
constexpr UINT kMsgServer = WM_APP + 1;  // lParam: std::string* from the pipe's reader thread
constexpr UINT kMsgLayout = WM_APP + 2;

class EditSession final : public ITfEditSession {
public:
    EditSession(std::function<HRESULT(TfEditCookie)> fn, TextService* owner) : fn_(std::move(fn)), owner_(owner) {
        owner_->AddRef();  // alive until an asynchronous session has run
        moduleAddRef();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) *ppv = static_cast<ITfEditSession*>(this);
        if (!*ppv) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return ULONG(n);
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return fn_ ? fn_(ec) : S_OK; }

private:
    ~EditSession() {
        owner_->Release();
        moduleRelease();
    }
    LONG ref_ = 1;
    std::function<HRESULT(TfEditCookie)> fn_;
    TextService* owner_;
};

ComPtr<ITfRange> subRange(TfEditCookie ec, ITfRange* range, LONG start, LONG length) {
    ComPtr<ITfRange> r;
    if (!range || FAILED(range->Clone(r.put()))) return r;
    r->Collapse(ec, TF_ANCHOR_START);
    LONG moved = 0;
    r->ShiftEnd(ec, start + length, &moved, nullptr);
    r->ShiftStart(ec, start, &moved, nullptr);
    return r;
}

void setSelection(TfEditCookie ec, ITfContext* ctx, ITfRange* range) {
    TF_SELECTION sel = {};
    sel.range = range;
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    ctx->SetSelection(ec, 1, &sel);
}

ComPtr<ITfRange> selection(TfEditCookie ec, ITfContext* ctx) {
    TF_SELECTION sel = {};
    ULONG n = 0;
    if (FAILED(ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) || n == 0) return {};
    return ComPtr<ITfRange>::adopt(sel.range);
}

std::wstring rangeText(TfEditCookie ec, ITfRange* range, ULONG max = 256) {
    std::wstring text(max, L'\0');
    ULONG got = 0;
    if (!range || FAILED(range->GetText(ec, 0, &text[0], max, &got))) return {};
    text.resize(got);
    return text;
}

void clearAttributes(TfEditCookie ec, ITfContext* ctx, ITfRange* range) {
    ComPtr<ITfProperty> prop;
    if (SUCCEEDED(ctx->GetProperty(kGuidPropAttribute, prop.put())) && prop) prop->Clear(ec, range);
}

RECT fallbackCaretRect() {
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof gti;
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
        RECT rc = gti.rcCaret;
        MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rc), 2);
        return rc;
    }
    POINT pt;
    GetCursorPos(&pt);
    return RECT{pt.x, pt.y, pt.x + 1, pt.y + 16};
}

LRESULT CALLBACK hiddenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<TextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == kMsgServer) {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (self && text) self->onServerMessage(*text);
        return 0;
    }
    if (msg == kMsgLayout) {
        if (self) self->onLayout();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::string programName() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    return narrow(p.substr(p.find_last_of(L'\\') + 1));
}
}  // namespace

TextService::TextService() { moduleAddRef(); }
TextService::~TextService() { moduleRelease(); }

// MARK: IUnknown

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor) || IsEqualIID(riid, kIIDTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, kIIDDisplayAttributeProvider)) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    }
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) TextService::Release() {
    const LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return ULONG(n);
}

// MARK: activation

STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) { return ActivateEx(ptim, tid, 0); }

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD) {
    threadMgr_ = ComPtr<ITfThreadMgr>(ptim);
    clientId_ = tid;
    if (auto src = threadMgr_.query<ITfSource>(IID_ITfSource))
        src->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie_);
    if (auto keys = threadMgr_.query<ITfKeystrokeMgr>(IID_ITfKeystrokeMgr))
        keys->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
    if (auto c = compartment(kGuidCompartmentOpenClose)) {
        if (auto src = c.query<ITfSource>(IID_ITfSource)) src->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), &openCloseCookie_);
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = 1;
        c->SetValue(clientId_, &v);
    }
    if (auto c = compartment(kGuidCompartmentConversion)) {
        if (auto src = c.query<ITfSource>(IID_ITfSource)) src->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), &conversionCookie_);
    }
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(kClsidCategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, categories.putVoid()))) {
        for (int i = 0; i < kStyleCount; ++i) categories->RegisterGUID(displayAttributeGuid(i), &atoms_[i]);
    }
    langBar_ = new LangBarButton(this);
    if (auto items = threadMgr_.query<ITfLangBarItemMgr>(IID_ITfLangBarItemMgr)) items->AddItem(langBar_);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = hiddenProc;
    wc.hInstance = g_module;
    wc.lpszClassName = kHiddenClass;
    RegisterClassExW(&wc);  // fails harmlessly when already registered
    window_ = CreateWindowExW(0, kHiddenClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_module, nullptr);
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    candidates_.onSelect = [this](int index) { selectCandidate(index); };

    HWND hwnd = window_;
    client_ = std::make_unique<PipeClient>([hwnd](std::string msg) {
        auto* p = new std::string(std::move(msg));
        if (!PostMessageW(hwnd, kMsgServer, 0, reinterpret_cast<LPARAM>(p))) delete p;
    });
    client_->start();
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    if (composition_) commitComposition();
    hideUI();
    if (client_) client_->stop();
    if (window_) {
        MSG m;  // messages the reader thread posted but nobody will handle
        while (PeekMessageW(&m, window_, kMsgServer, kMsgServer, PM_REMOVE)) delete reinterpret_cast<std::string*>(m.lParam);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    client_.reset();
    if (langBar_) {
        if (auto items = threadMgr_.query<ITfLangBarItemMgr>(IID_ITfLangBarItemMgr)) items->RemoveItem(langBar_);
        langBar_->detach();
        langBar_->Release();
        langBar_ = nullptr;
    }
    if (auto c = compartment(kGuidCompartmentOpenClose)) {
        if (auto src = c.query<ITfSource>(IID_ITfSource); src && openCloseCookie_ != TF_INVALID_COOKIE) src->UnadviseSink(openCloseCookie_);
    }
    if (auto c = compartment(kGuidCompartmentConversion)) {
        if (auto src = c.query<ITfSource>(IID_ITfSource); src && conversionCookie_ != TF_INVALID_COOKIE) src->UnadviseSink(conversionCookie_);
    }
    openCloseCookie_ = conversionCookie_ = TF_INVALID_COOKIE;
    if (auto keys = threadMgr_.query<ITfKeystrokeMgr>(IID_ITfKeystrokeMgr)) keys->UnadviseKeyEventSink(clientId_);
    if (auto src = threadMgr_.query<ITfSource>(IID_ITfSource); src && threadMgrCookie_ != TF_INVALID_COOKIE)
        src->UnadviseSink(threadMgrCookie_);
    threadMgrCookie_ = TF_INVALID_COOKIE;
    watches_.clear();
    lastFix_.reset();
    composition_.reset();
    compositionContext_.reset();
    threadMgr_.reset();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

// MARK: thread manager events

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnPushContext(ITfContext*) { return S_OK; }
STDMETHODIMP TextService::OnPopContext(ITfContext*) { return S_OK; }

/// Another document (or window) got the focus: finish what was being composed in the old one and
/// show the one global Chinese / English mode.
STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) {
    if (composition_) commitComposition();
    hideUI();
    lastFix_.reset();
    idle_ = true;
    if (client_ && client_->connected()) {
        json::Value r = call(json::Value(json::Value::Object{{"op", "state"}}), 200);
        if (r.has("mode")) setMode(r["mode"]);
    }
    syncCompartments();
    return S_OK;
}

// MARK: keys

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
    if (!foreground) hideUI();
    return S_OK;
}

/// Keys are processed here, in the test phase (as Weasel does): the server's answer says whether
/// the key is eaten, and OnKeyDown then only repeats that answer.
STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    noteKeyDown(wp);
    if (pendingKey_ && pendingKey_->wp == wp && pendingKey_->lp == lp) {
        *eaten = TRUE;
        return S_OK;
    }
    pendingKey_.reset();
    *eaten = handleKey(pic, wp) ? TRUE : FALSE;
    if (*eaten) pendingKey_ = PendingKey{wp, lp};
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    if (pendingKey_ && pendingKey_->wp == wp && pendingKey_->lp == lp) {
        pendingKey_.reset();
        *eaten = TRUE;
        return S_OK;
    }
    pendingKey_.reset();
    noteKeyDown(wp);
    *eaten = handleKey(pic, wp) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wp, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    noteKeyUp(pic, wp);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* pic, WPARAM wp, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    noteKeyUp(pic, wp);
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
    if (eaten) *eaten = FALSE;
    return S_OK;
}

/// Shift pressed and released on its own (within 0.6 s, nothing else pressed) switches Chinese /
/// English, as in Microsoft Bopomofo.
void TextService::noteKeyDown(WPARAM wp) {
    if (vk::isShiftKey(int(wp))) {
        if (!shiftDown_) {
            shiftDown_ = true;
            shiftAlone_ = GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_LWIN) >= 0 && GetKeyState(VK_RWIN) >= 0;
            shiftTime_ = GetTickCount64();
        }
        return;
    }
    shiftAlone_ = false;
}

void TextService::noteKeyUp(ITfContext* pic, WPARAM wp) {
    if (!vk::isShiftKey(int(wp)) || !shiftDown_) return;
    shiftDown_ = false;
    const bool tap = shiftAlone_ && GetTickCount64() - shiftTime_ < 600;
    shiftAlone_ = false;
    if (tap && mode_.shift && keyboardOpen() && client_ && client_->connected()) toggleLanguage(pic);
}

bool TextService::handleKey(ITfContext* pic, WPARAM wp) {
    if (!client_ || !client_->connected() || !keyboardOpen()) return false;
    const int code = int(wp);
    if (vk::isModifierKey(code)) return false;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) return false;
    json::Value req = json::Value::object();
    req.set("op", "key");
    req.set("code", code);
    req.set("shift", GetKeyState(VK_SHIFT) < 0);
    req.set("ctrl", GetKeyState(VK_CONTROL) < 0);
    req.set("alt", GetKeyState(VK_MENU) < 0);
    req.set("caps", (GetKeyState(VK_CAPITAL) & 1) != 0);
    if (idle_ && pic && vk::usKey(code)) {  // a key that may start a word or a composition
        if (auto ctx = readContext(pic)) req.set("ctx", narrow(*ctx));
    }
    json::Value r = call(req, 400);
    if (!r.isObject()) return false;
    bool revertOk = false;
    apply(pic, r, &revertOk);
    bool eat = r["eat"].asBool();
    for (const auto& a : r["actions"].items()) {
        if (a["type"].asString() == "revert" && !revertOk) eat = false;  // Backspace goes to the app after all
    }
    return eat;
}

json::Value TextService::call(json::Value request, DWORD timeoutMs) {
    if (!client_) return {};
    const int id = nextId_++;
    request.set("id", id);
    const std::string response = client_->call(request.dump(), id, timeoutMs);
    if (response.empty()) return {};
    try {
        return json::Value::parse(response);
    } catch (...) {
        return {};
    }
}

void TextService::post(json::Value request) {
    if (!client_) return;
    request.set("id", nextId_++);
    client_->send(request.dump());
}

/// Up to 400 characters before the caret, for the model's context.
std::optional<std::wstring> TextService::readContext(ITfContext* pic) {
    std::optional<std::wstring> out;
    editSession(pic, TF_ES_SYNC | TF_ES_READ, [&](TfEditCookie ec) -> HRESULT {
        auto sel = selection(ec, pic);
        if (!sel) return E_FAIL;
        ComPtr<ITfRange> r;
        if (FAILED(sel->Clone(r.put()))) return E_FAIL;
        r->Collapse(ec, TF_ANCHOR_START);
        LONG moved = 0;
        r->ShiftStart(ec, -400, &moved, nullptr);
        out = rangeText(ec, r.get(), 400);
        return S_OK;
    });
    return out;
}

// MARK: applying the server's answers

HRESULT TextService::editSession(ITfContext* ctx, DWORD flags, Edit fn, HRESULT* sessionResult) {
    if (!ctx) return E_INVALIDARG;
    auto* session = new EditSession(std::move(fn), this);
    HRESULT hrSession = E_FAIL;
    const HRESULT hr = ctx->RequestEditSession(clientId_, session, flags, &hrSession);
    session->Release();
    if (sessionResult) *sessionResult = SUCCEEDED(hr) ? hrSession : hr;
    return hr;
}

ComPtr<ITfContext> TextService::focusedContext() const {
    ComPtr<ITfDocumentMgr> doc;
    ComPtr<ITfContext> ctx;
    if (threadMgr_ && SUCCEEDED(threadMgr_->GetFocus(doc.put())) && doc) doc->GetTop(ctx.put());
    return ctx;
}

void TextService::apply(ITfContext* pic, const json::Value& r, bool* revertOk) {
    if (r.has("mode")) setMode(r["mode"]);
    if (r.has("idle")) idle_ = r["idle"].asBool();
    const bool newView = r.has("view") && r["seq"].asInt() > lastSeq_;
    if (newView) lastSeq_ = r["seq"].asInt();
    const std::wstring commit = widen(r["commit"].asString());
    const json::Value actions = r["actions"];
    if (!commit.empty() || actions.size() || newView) {
        ComPtr<ITfContext> target = composition_ ? compositionContext_ : ComPtr<ITfContext>(pic);
        if (!target) target = focusedContext();
        if (target) {
            auto ok = std::make_shared<bool>(false);
            const json::Value comp = newView ? r["view"]["comp"] : json::Value();
            ITfContext* ctx = target.get();
            HRESULT session = S_OK;
            editSession(ctx, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                        [this, target, commit, actions, comp, newView, ok](TfEditCookie ec) -> HRESULT {
                            applyActions(ec, target.get(), actions, ok.get());
                            if (!commit.empty()) applyCommit(ec, target.get(), commit);
                            if (newView && !applyComposition(ec, target.get(), comp)) {
                                post(json::Value(json::Value::Object{{"op", "cancel"}}));  // the app does not take a composition here
                            }
                            return S_OK;
                        },
                        &session);
            if (revertOk) *revertOk = session == TF_S_ASYNC ? true : *ok;
        }
    }
    if (newView) {
        view_ = r["view"];
        scheduleLayout();
    }
    if (r.has("flash")) showFlash(widen(r["flash"]["text"].asString()), widen(r["flash"]["note"].asString()));
}

void TextService::endComposition(TfEditCookie ec, ITfContext* ctx, bool clearText) {
    if (!composition_) return;
    ComPtr<ITfRange> range;
    if (SUCCEEDED(composition_->GetRange(range.put())) && range) {
        clearAttributes(ec, ctx, range.get());
        if (clearText) range->SetText(ec, 0, L"", 0);
    }
    composition_->EndComposition(ec);
    composition_.reset();
    compositionContext_.reset();
}

/// Writes the finished text: into the composition (ending it), or at the caret.
void TextService::applyCommit(TfEditCookie ec, ITfContext* ctx, const std::wstring& text) {
    if (!composition_) {
        // a short composition of its own, so apps see committed text the usual way
        ComPtr<ITfInsertAtSelection> insert;
        ctx->QueryInterface(IID_ITfInsertAtSelection, insert.putVoid());
        ComPtr<ITfContextComposition> cc;
        ctx->QueryInterface(IID_ITfContextComposition, cc.putVoid());
        ComPtr<ITfRange> at;
        if (insert && cc && SUCCEEDED(insert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, at.put())) && at &&
            SUCCEEDED(cc->StartComposition(ec, at.get(), static_cast<ITfCompositionSink*>(this), composition_.put())) && composition_) {
            compositionContext_ = ComPtr<ITfContext>(ctx);
        } else {
            ComPtr<ITfRange> range;
            if (insert && SUCCEEDED(insert->InsertTextAtSelection(ec, 0, text.c_str(), LONG(text.size()), range.put())) && range) {
                range->Collapse(ec, TF_ANCHOR_END);
                setSelection(ec, ctx, range.get());
            }
            return;
        }
    }
    ComPtr<ITfRange> range;
    if (SUCCEEDED(composition_->GetRange(range.put())) && range) {
        range->SetText(ec, 0, text.c_str(), LONG(text.size()));
        clearAttributes(ec, ctx, range.get());
        ComPtr<ITfRange> end;
        if (SUCCEEDED(range->Clone(end.put()))) {
            end->Collapse(ec, TF_ANCHOR_END);
            setSelection(ec, ctx, end.get());
        }
    }
    endComposition(ec, ctx, false);
}

/// The composition as the server shows it: text, the caret, and the runs' display attributes.
bool TextService::applyComposition(TfEditCookie ec, ITfContext* ctx, const json::Value& comp) {
    if (comp.isNull()) {
        endComposition(ec, ctx, true);
        return true;
    }
    const std::wstring text = widen(comp["text"].asString());
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert;
        ctx->QueryInterface(IID_ITfInsertAtSelection, insert.putVoid());
        ComPtr<ITfContextComposition> cc;
        ctx->QueryInterface(IID_ITfContextComposition, cc.putVoid());
        ComPtr<ITfRange> at;
        if (!insert || !cc || FAILED(insert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, at.put())) || !at) return false;
        if (FAILED(cc->StartComposition(ec, at.get(), static_cast<ITfCompositionSink*>(this), composition_.put())) || !composition_) {
            composition_.reset();
            return false;
        }
        compositionContext_ = ComPtr<ITfContext>(ctx);
    }
    ComPtr<ITfRange> range;
    if (FAILED(composition_->GetRange(range.put())) || !range) return false;
    range->SetText(ec, 0, text.c_str(), LONG(text.size()));
    ComPtr<ITfProperty> prop;
    if (SUCCEEDED(ctx->GetProperty(kGuidPropAttribute, prop.put())) && prop) {
        prop->Clear(ec, range.get());
        for (const auto& run : comp["runs"].items()) {
            const int style = run[2].asInt();
            if (style < 0 || style >= kStyleCount || !atoms_[style]) continue;
            auto sub = subRange(ec, range.get(), run[0].asInt(), run[1].asInt());
            if (!sub) continue;
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = LONG(atoms_[style]);
            prop->SetValue(ec, sub.get(), &v);
        }
    }
    if (auto caret = subRange(ec, range.get(), comp["caret"].asInt(), 0)) setSelection(ec, ctx, caret.get());
    return true;
}

/// The typed word just before the caret, allowing one character typed after it (the boundary
/// key may have reached the app first). `extra` gets that count.
ComPtr<ITfRange> TextService::wordBeforeCaret(TfEditCookie ec, ITfContext* ctx, const std::wstring& word, int* extra) {
    auto sel = selection(ec, ctx);
    if (!sel || word.empty()) return {};
    BOOL empty = FALSE;
    if (FAILED(sel->IsEmpty(ec, &empty)) || !empty) return {};  // text is selected: leave it
    const LONG len = LONG(word.size());
    ComPtr<ITfRange> probe;
    if (FAILED(sel->Clone(probe.put()))) return {};
    probe->Collapse(ec, TF_ANCHOR_START);
    LONG moved = 0;
    probe->ShiftStart(ec, -(len + 1), &moved, nullptr);
    const std::wstring text = rangeText(ec, probe.get(), ULONG(len + 1));
    int e = -1;
    if (text.size() >= word.size() && text.compare(text.size() - word.size(), word.size(), word) == 0) e = 0;
    else if (text.size() == word.size() + 1 && text.compare(0, word.size(), word) == 0) e = 1;
    if (e < 0) return {};
    ComPtr<ITfRange> r;
    if (FAILED(sel->Clone(r.put()))) return {};
    r->Collapse(ec, TF_ANCHOR_START);
    r->ShiftStart(ec, -(len + e), &moved, nullptr);
    if (e) r->ShiftEnd(ec, -e, &moved, nullptr);
    if (extra) *extra = e;
    return r;
}

/// English mode: replace a word (verified to be exactly what was typed), watch a word whose fix
/// is still on its way, or undo the last fix.
void TextService::applyActions(TfEditCookie ec, ITfContext* ctx, const json::Value& actions, bool* revertOk) {
    for (const auto& a : actions.items()) {
        const std::string& type = a["type"].asString();
        const std::wstring original = widen(a["original"].asString());
        if (type == "fix") {
            const std::wstring corrected = widen(a["corrected"].asString());
            int extra = 0;
            auto r = wordBeforeCaret(ec, ctx, original, &extra);
            if (!r) continue;
            r->SetText(ec, 0, corrected.c_str(), LONG(corrected.size()));
            if (extra == 0) {
                ComPtr<ITfRange> end;
                if (SUCCEEDED(r->Clone(end.put()))) {
                    end->Collapse(ec, TF_ANCHOR_END);
                    setSelection(ec, ctx, end.get());
                }
            }
            r->SetGravity(ec, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD);
            lastFix_ = LastFix{ComPtr<ITfContext>(ctx), r, original, corrected};
        } else if (type == "watch") {
            int extra = 0;
            auto r = wordBeforeCaret(ec, ctx, original, &extra);
            if (!r) continue;
            r->SetGravity(ec, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD);
            watches_.push_back(Watch{a["id"].asInt(), ComPtr<ITfContext>(ctx), r, original});
            if (watches_.size() > 8) watches_.erase(watches_.begin());
        } else if (type == "revert") {
            const std::wstring corrected = widen(a["corrected"].asString());
            bool done = false;
            if (lastFix_ && lastFix_->ctx.get() == ctx && rangeText(ec, lastFix_->range.get()) == corrected) {
                // only right after the fix: the caret is at most one character past the word
                ComPtr<ITfRange> gap;
                auto sel = selection(ec, ctx);
                if (sel && SUCCEEDED(lastFix_->range->Clone(gap.put()))) {
                    gap->Collapse(ec, TF_ANCHOR_END);
                    gap->ShiftEndToRange(ec, sel.get(), TF_ANCHOR_START);
                    if (rangeText(ec, gap.get(), 3).size() <= 1) {
                        lastFix_->range->SetText(ec, 0, original.c_str(), LONG(original.size()));
                        done = true;
                    }
                }
            }
            if (!done) {
                int extra = 0;
                if (auto r = wordBeforeCaret(ec, ctx, corrected, &extra)) {
                    r->SetText(ec, 0, original.c_str(), LONG(original.size()));
                    done = true;
                }
            }
            lastFix_.reset();
            if (done) {
                if (revertOk) *revertOk = true;
                post(json::Value(json::Value::Object{{"op", "userword"}, {"word", a["original"]}}));
            }
        }
    }
}

void TextService::commitComposition() {
    ComPtr<ITfContext> ctx = compositionContext_;
    json::Value r = call(json::Value(json::Value::Object{{"op", "commit"}}), 300);
    if (r.isObject()) {
        apply(ctx.get(), r);
    }
    if (composition_ && ctx) {  // no server: keep the text as shown
        editSession(ctx.get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, [this, ctx](TfEditCookie ec) -> HRESULT {
            endComposition(ec, ctx.get(), false);
            return S_OK;
        });
    }
}

void TextService::toggleLanguage(ITfContext* pic) {
    json::Value r = call(json::Value(json::Value::Object{{"op", "lang"}}), 500);
    if (r.isObject()) apply(pic ? pic : focusedContext().get(), r);
}

void TextService::selectCandidate(int index) {
    json::Value r = call(json::Value(json::Value::Object{{"op", "select"}, {"index", index}}), 400);
    if (r.isObject()) apply(compositionContext_.get(), r);
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) {
    // the app ended the composition (a click elsewhere, for example): the text stays as it is
    if (composition_ && composition == composition_.get()) {
        ComPtr<ITfRange> range;
        if (compositionContext_ && SUCCEEDED(composition_->GetRange(range.put())) && range) clearAttributes(ec, compositionContext_.get(), range.get());
        composition_.reset();
        compositionContext_.reset();
    }
    hideUI();
    post(json::Value(json::Value::Object{{"op", "cancel"}}));
    return S_OK;
}

// MARK: server messages

void TextService::onServerMessage(const std::string& message) {
    json::Value r;
    try {
        r = json::Value::parse(message);
    } catch (...) {
        return;
    }
    const std::string& ev = r["ev"].asString();
    if (ev == "connected") {
        lastSeq_ = 0;  // a new session on the server
        idle_ = true;
        json::Value hello = call(json::Value(json::Value::Object{{"op", "hello"}, {"program", programName()}}), 1000);
        if (hello.has("mode")) setMode(hello["mode"]);
        if (hello.has("seq")) lastSeq_ = hello["seq"].asInt();
        return;
    }
    if (ev == "disconnected") {
        if (composition_ && compositionContext_) {
            ComPtr<ITfContext> ctx = compositionContext_;
            editSession(ctx.get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, [this, ctx](TfEditCookie ec) -> HRESULT {
                endComposition(ec, ctx.get(), false);
                return S_OK;
            });
        }
        hideUI();
        return;
    }
    if (ev == "mode") {
        setMode(r["mode"]);
        return;
    }
    if (ev == "fix") {  // a late English fix for a watched word
        const int id = r["id"].asInt();
        auto it = std::find_if(watches_.begin(), watches_.end(), [&](const Watch& w) { return w.id == id; });
        if (it == watches_.end()) return;
        Watch w = *it;
        watches_.erase(it);
        if (!r.has("corrected")) return;
        const std::wstring corrected = widen(r["corrected"].asString());
        editSession(w.ctx.get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, [w, corrected](TfEditCookie ec) -> HRESULT {
            if (rangeText(ec, w.range.get()) == w.original) w.range->SetText(ec, 0, corrected.c_str(), LONG(corrected.size()));
            return S_OK;
        });
        return;
    }
    // a view pushed by the AI step, or a response that came too late
    json::Value copy = r;
    apply(composition_ ? compositionContext_.get() : focusedContext().get(), copy);
}

// MARK: UI

void TextService::scheduleLayout() {
    if (layoutPending_ || !window_) return;
    layoutPending_ = true;
    PostMessageW(window_, kMsgLayout, 0, 0);  // after the app has laid out the new text
}

/// Positions the candidate window and the bubble at their characters.
void TextService::onLayout() {
    layoutPending_ = false;
    const json::Value cand = view_["cand"], bubble = view_["bubble"];
    if ((cand.isNull() && bubble.isNull()) || !composition_ || !compositionContext_) {
        candidates_.hide();
        if (!bubble_.flashing()) bubble_.hide();
        return;
    }
    ComPtr<ITfContext> ctx = compositionContext_;
    ComPtr<ITfComposition> composition = composition_;
    editSession(ctx.get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, [this, ctx, composition, cand, bubble](TfEditCookie ec) -> HRESULT {
        ComPtr<ITfRange> range;
        ComPtr<ITfContextView> view;
        if (FAILED(composition->GetRange(range.put())) || !range || FAILED(ctx->GetActiveView(view.put())) || !view) return E_FAIL;
        auto rectOf = [&](const json::Value& at, RECT* out) {
            auto sub = subRange(ec, range.get(), at[0].asInt(), std::max(1, at[1].asInt()));
            BOOL clipped = FALSE;
            return sub && SUCCEEDED(view->GetTextExt(ec, sub.get(), out, &clipped)) && out->bottom > out->top;
        };
        RECT rc = {};
        if (!cand.isNull()) {
            if (!rectOf(cand["focus"], &rc)) rc = fallbackCaretRect();
            std::vector<std::wstring> items;
            for (const auto& item : cand["items"].items()) items.push_back(widen(item.asString()));
            bubble_.hide();
            candidates_.show(items, cand["sel"].asInt(), cand["pageSize"].asInt(9), rc);
            return S_OK;
        }
        candidates_.hide();
        if (!rectOf(bubble["at"], &rc)) rc = fallbackCaretRect();
        bubble_.show(widen(bubble["text"].asString()), widen(bubble["reading"].asString()), widen(bubble["note"].asString()),
                     bubble["dot"].asInt(), rc);
        return S_OK;
    });
}

void TextService::showFlash(const std::wstring& glyph, const std::wstring& note) {
    ComPtr<ITfContext> ctx = composition_ ? compositionContext_ : focusedContext();
    if (!ctx) {
        bubble_.flash(glyph, note, fallbackCaretRect());
        return;
    }
    editSession(ctx.get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, [this, ctx, glyph, note](TfEditCookie ec) -> HRESULT {
        RECT rc = fallbackCaretRect();
        ComPtr<ITfContextView> view;
        if (auto sel = selection(ec, ctx.get()); sel && SUCCEEDED(ctx->GetActiveView(view.put())) && view) {
            BOOL clipped = FALSE;
            RECT r = {};
            if (SUCCEEDED(view->GetTextExt(ec, sel.get(), &r, &clipped)) && r.bottom > r.top) rc = r;
        }
        candidates_.hide();
        bubble_.flash(glyph, note, rc);
        return S_OK;
    });
}

void TextService::hideUI() {
    candidates_.hide();
    bubble_.hide();
}

// MARK: mode

void TextService::setMode(const json::Value& m) {
    if (!m.isObject()) return;
    mode_.english = m["english"].asBool();
    mode_.full = m["full"].asBool();
    mode_.ai = m["ai"].asBool(true);
    mode_.typo = m["typo"].asBool(true);
    mode_.shift = m["shift"].asBool(true);
    mode_.ctrl = m["ctrl"].asBool(true);
    mode_.model = m["model"].asString();
    if (langBar_) langBar_->setMode(mode_.english, mode_.full);
    syncCompartments();
}

ComPtr<ITfCompartment> TextService::compartment(REFGUID guid) const {
    ComPtr<ITfCompartment> c;
    if (!threadMgr_) return c;
    if (auto mgr = threadMgr_.query<ITfCompartmentMgr>(IID_ITfCompartmentMgr)) mgr->GetCompartment(guid, c.put());
    return c;
}

/// Shows the one global mode in this thread's conversion compartment (read by the system).
void TextService::syncCompartments() {
    auto c = compartment(kGuidCompartmentConversion);
    if (!c) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = (mode_.english ? TF_CONVERSIONMODE_ALPHANUMERIC : TF_CONVERSIONMODE_NATIVE) | (mode_.full ? TF_CONVERSIONMODE_FULLSHAPE : 0);
    VARIANT cur;
    VariantInit(&cur);
    if (SUCCEEDED(c->GetValue(&cur)) && cur.vt == VT_I4 && cur.lVal == v.lVal) return;
    settingCompartment_ = true;
    c->SetValue(clientId_, &v);
    settingCompartment_ = false;
}

bool TextService::keyboardOpen() const {
    auto c = compartment(kGuidCompartmentOpenClose);
    if (!c) return true;
    VARIANT v;
    VariantInit(&v);
    if (FAILED(c->GetValue(&v)) || v.vt != VT_I4) return true;
    return v.lVal != 0;
}

STDMETHODIMP TextService::OnChange(REFGUID rguid) {
    if (settingCompartment_) return S_OK;
    if (IsEqualGUID(rguid, kGuidCompartmentOpenClose) && !keyboardOpen()) {
        // the system switched the input method off (Ctrl+Space): finish and pass keys through
        if (composition_) commitComposition();
        hideUI();
    }
    // A conversion-mode change from elsewhere (an app asking for a mode) is not followed: the
    // Chinese / English mode is one switch for every app, changed only by the user.
    return S_OK;
}

// MARK: language bar

void TextService::langBarClicked() { toggleLanguage(composition_ ? compositionContext_.get() : focusedContext().get()); }

void TextService::langBarInitMenu(ITfMenu* menu) {
    auto add = [&](UINT id, bool checked, const wchar_t* text, DWORD extra = 0) {
        menu->AddMenuItem(id, (checked ? TF_LBMENUF_CHECKED : 0) | extra, nullptr, nullptr, text, ULONG(wcslen(text)), nullptr);
    };
    add(1, mode_.english, L"英文模式（AI 拼字修正）\tShift");
    add(2, mode_.full, L"全形\tShift+Space");
    add(0, false, L"", TF_LBMENUF_SEPARATOR);
    add(3, mode_.ai, L"AI 即時糾錯");
    add(4, mode_.typo, L"注音打錯鍵容錯");
    add(5, mode_.shift, L"單按 Shift 切換中／英");
    add(6, mode_.ctrl, L"Ctrl + 符號鍵輸入全形符號");
    add(0, false, L"", TF_LBMENUF_SEPARATOR);
    const wchar_t* model = mode_.model == "ready"     ? L"AI 模型：已載入"
                           : mode_.model == "loading" ? L"AI 模型：載入中…"
                           : mode_.model == "missing" ? L"AI 模型：找不到模型檔（只用字典）"
                           : client_ && client_->connected() ? L"AI 模型：無法載入（只用字典）"
                                                           : L"引擎未連線";
    add(7, false, model, TF_LBMENUF_GRAYED);
}

void TextService::langBarMenuSelected(UINT id) {
    json::Value r;
    switch (id) {
        case 1: toggleLanguage(focusedContext().get()); return;
        case 2: r = call(json::Value(json::Value::Object{{"op", "width"}})); break;
        case 3: r = call(json::Value(json::Value::Object{{"op", "set"}, {"ai", !mode_.ai}})); break;
        case 4: r = call(json::Value(json::Value::Object{{"op", "set"}, {"typo", !mode_.typo}})); break;
        case 5: r = call(json::Value(json::Value::Object{{"op", "set"}, {"shift", !mode_.shift}})); break;
        case 6: r = call(json::Value(json::Value::Object{{"op", "set"}, {"ctrl", !mode_.ctrl}})); break;
        default: return;
    }
    if (r.isObject()) apply(focusedContext().get(), r);
}

// MARK: display attributes

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new (std::nothrow) zl::tsf::EnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;
    for (int i = 0; i < kStyleCount; ++i) {
        if (IsEqualGUID(guid, displayAttributeGuid(i))) {
            *ppInfo = new (std::nothrow) DisplayAttributeInfo(i);
            return *ppInfo ? S_OK : E_OUTOFMEMORY;
        }
    }
    return E_INVALIDARG;
}

}  // namespace zl::tsf
