// The text service (TSF text input processor) loaded into every app: a thin client of the engine
// server. It forwards keys, shows what the server says (the composition with its marks, the
// candidate window, the focus bubble, the mode), and edits the document for English fixes.
#pragma once

#include "common.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/json.h"
#include "display_attributes.h"
#include "langbar.h"
#include "pipe_client.h"
#include "ui.h"

namespace zl::tsf {

class TextService final : public ITfTextInputProcessorEx,
                    public ITfThreadMgrEventSink,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfDisplayAttributeProvider,
                    public ITfCompartmentEventSink,
                    public LangBarOwner {
public:
    TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // ITfTextInputProcessor(Ex)
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;
    STDMETHODIMP Deactivate() override;
    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;
    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;
    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;
    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;
    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;
    // LangBarOwner
    void langBarClicked() override;
    void langBarInitMenu(ITfMenu* menu) override;
    void langBarMenuSelected(UINT id) override;

    /// Messages from the engine server, on the UI thread.
    void onServerMessage(const std::string& message);
    void onLayout();

private:
    ~TextService();

    using Edit = std::function<HRESULT(TfEditCookie)>;
    /// Runs `fn` in an edit session; `sessionResult` gets DoEditSession's result (TF_S_ASYNC when queued).
    HRESULT editSession(ITfContext* ctx, DWORD flags, Edit fn, HRESULT* sessionResult = nullptr);
    ComPtr<ITfContext> focusedContext() const;

    // keys
    bool handleKey(ITfContext* pic, WPARAM wp);
    void noteKeyDown(WPARAM wp);
    void noteKeyUp(ITfContext* pic, WPARAM wp);
    json::Value call(json::Value request, DWORD timeoutMs = 400);
    void post(json::Value request);
    std::optional<std::wstring> readContext(ITfContext* pic);

    /// Applies a response or a push: the mode, English fixes, committed text, the composition and
    /// its marks, the candidate window and the bubble.
    void apply(ITfContext* pic, const json::Value& r, bool* revertOk = nullptr);
    void applyCommit(TfEditCookie ec, ITfContext* ctx, const std::wstring& text);
    bool applyComposition(TfEditCookie ec, ITfContext* ctx, const json::Value& comp);
    void applyActions(TfEditCookie ec, ITfContext* ctx, const json::Value& actions, bool* revertOk);
    void endComposition(TfEditCookie ec, ITfContext* ctx, bool clearText);
    ComPtr<ITfRange> wordBeforeCaret(TfEditCookie ec, ITfContext* ctx, const std::wstring& word, int* extra);
    void commitComposition();
    void toggleLanguage(ITfContext* pic);
    void selectCandidate(int index);

    // UI
    void scheduleLayout();
    void showFlash(const std::wstring& glyph, const std::wstring& note);
    void hideUI();

    // mode
    void setMode(const json::Value& mode);
    void syncCompartments();
    bool keyboardOpen() const;
    ComPtr<ITfCompartment> compartment(REFGUID guid) const;

    LONG ref_ = 1;
    ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrCookie_ = TF_INVALID_COOKIE;
    DWORD openCloseCookie_ = TF_INVALID_COOKIE;
    DWORD conversionCookie_ = TF_INVALID_COOKIE;
    TfGuidAtom atoms_[kStyleCount] = {};
    LangBarButton* langBar_ = nullptr;
    std::unique_ptr<PipeClient> client_;
    HWND window_ = nullptr;
    CandidateWindow candidates_;
    BubbleWindow bubble_;

    ComPtr<ITfComposition> composition_;
    ComPtr<ITfContext> compositionContext_;

    bool idle_ = true;  // the next key may start a word: send the document context
    int lastSeq_ = 0;   // newest view applied
    int nextId_ = 1;
    json::Value view_;
    bool layoutPending_ = false;
    struct Mode {
        bool english = false, full = false, ai = true, typo = true, shift = true, ctrl = true;
        std::string model;
    } mode_;
    bool settingCompartment_ = false;

    struct PendingKey {
        WPARAM wp;
        LPARAM lp;
    };
    std::optional<PendingKey> pendingKey_;  // eaten in OnTestKeyDown, OnKeyDown follows
    bool shiftDown_ = false, shiftAlone_ = false;
    ULONGLONG shiftTime_ = 0;

    struct Watch {  // an English word whose fix may still arrive
        int id;
        ComPtr<ITfContext> ctx;
        ComPtr<ITfRange> range;
        std::wstring original;
    };
    std::vector<Watch> watches_;
    struct LastFix {  // the fix Backspace can undo
        ComPtr<ITfContext> ctx;
        ComPtr<ITfRange> range;
        std::wstring original, corrected;
    };
    std::optional<LastFix> lastFix_;
};

}  // namespace zl::tsf
