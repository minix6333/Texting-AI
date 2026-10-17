// How the composition is drawn by the app: plain input (thin underline), characters the AI
// corrected (thick orange underline), characters it doubts (red squiggle), and the character the
// cursor is on (highlighted, so the position is never lost).
#pragma once

#include "common.h"

namespace zl::tsf {

/// Styles as the engine sends them: 0 plain, 1 corrected, 2 suspicious; +3 = under the cursor.
constexpr int kStyleCount = 6;

const GUID& displayAttributeGuid(int style);
TF_DISPLAYATTRIBUTE displayAttribute(int style);

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    explicit DisplayAttributeInfo(int style);
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP Reset() override;

private:
    ~DisplayAttributeInfo();
    LONG ref_ = 1;
    int style_;
};

class EnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
public:
    explicit EnumDisplayAttributeInfo(int index = 0);
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~EnumDisplayAttributeInfo();
    LONG ref_ = 1;
    int index_;
};

}  // namespace zl::tsf
