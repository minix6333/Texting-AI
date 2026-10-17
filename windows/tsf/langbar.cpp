#include "langbar.h"

#include "ui.h"

namespace zl::tsf {

namespace {
constexpr DWORD kSinkCookie = 0x5A4C;  // one sink at a time
}

LangBarButton::LangBarButton(LangBarOwner* owner) : owner_(owner) { moduleAddRef(); }

LangBarButton::~LangBarButton() {
    if (sink_) sink_->Release();
    moduleRelease();
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) || IsEqualIID(riid, kIIDLangBarItemButton)) {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(riid, IID_ITfSource)) {
        *ppv = static_cast<ITfSource*>(this);
    }
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) LangBarButton::Release() {
    const LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return ULONG(n);
}

void LangBarButton::setMode(bool english, bool fullWidth) {
    if (english == english_ && fullWidth == full_) return;
    english_ = english;
    full_ = fullWidth;
    if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
    if (!info) return E_INVALIDARG;
    info->clsidService = kClsidTextService;
    info->guidItem = kGuidLbiInputMode;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = 0;
    lstrcpynW(info->szDescription, L"注音 AI 中／英", TF_LBI_DESC_MAXLEN);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* status) {
    if (!status) return E_INVALIDARG;
    *status = 0;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) { return E_NOTIMPL; }

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tip) {
    if (!tip) return E_INVALIDARG;
    std::wstring s = english_ ? L"注音 AI：英文（AI 拼字修正）" : L"注音 AI：中文";
    if (full_) s += L"，全形";
    s += L"　按 Shift 切換";
    *tip = SysAllocString(s.c_str());
    return *tip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT, const RECT*) {
    if (click == TF_LBI_CLK_LEFT && owner_) owner_->langBarClicked();
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* menu) {
    if (menu && owner_) owner_->langBarInitMenu(menu);
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT id) {
    if (owner_) owner_->langBarMenuSelected(id);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* icon) {
    if (!icon) return E_INVALIDARG;
    *icon = makeModeIcon(english_ ? L"英" : L"中");
    return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* text) {
    if (!text) return E_INVALIDARG;
    *text = SysAllocString(english_ ? L"英" : L"中");
    return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* cookie) {
    if (!punk || !cookie) return E_INVALIDARG;
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(&sink_)))) {
        sink_ = nullptr;
        return E_NOINTERFACE;
    }
    *cookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
    if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
    sink_->Release();
    sink_ = nullptr;
    return S_OK;
}

}  // namespace zl::tsf
