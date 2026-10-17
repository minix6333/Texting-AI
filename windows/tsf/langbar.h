// The input-mode button (中 / 英) that Windows shows in the taskbar for the active text service
// (GUID_LBI_INPUTMODE): a click switches Chinese / English, its menu holds the settings.
#pragma once

#include "common.h"

#include <string>

namespace zl::tsf {

struct LangBarOwner {
    virtual void langBarClicked() = 0;
    virtual void langBarInitMenu(ITfMenu* menu) = 0;
    virtual void langBarMenuSelected(UINT id) = 0;
};

class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
public:
    explicit LangBarButton(LangBarOwner* owner);
    /// The owner is going away: no more calls into it.
    void detach() { owner_ = nullptr; }
    void setMode(bool english, bool fullWidth);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;
    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;
    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

private:
    ~LangBarButton();
    LONG ref_ = 1;
    LangBarOwner* owner_;
    ITfLangBarItemSink* sink_ = nullptr;
    bool english_ = false;
    bool full_ = false;
};

}  // namespace zl::tsf
