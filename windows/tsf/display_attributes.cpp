#include "display_attributes.h"

#include <algorithm>
#include <new>

namespace zl::tsf {

namespace {
const GUID kGuids[kStyleCount] = {
    {0xb9752634, 0x0d77, 0x445d, {0xad, 0x89, 0x8d, 0xad, 0x11, 0x04, 0x2c, 0x23}},  // plain
    {0x07943522, 0xdc14, 0x4770, {0x84, 0x9a, 0xb9, 0x88, 0x1f, 0xe2, 0x4c, 0x30}},  // corrected
    {0xf36cb40d, 0x5bb5, 0x4ac5, {0xbe, 0x7b, 0x94, 0xfe, 0x57, 0xeb, 0x66, 0xde}},  // suspicious
    {0xef6037e8, 0x7098, 0x4d51, {0x94, 0xef, 0x7c, 0xe5, 0xdc, 0xbf, 0x6d, 0x5a}},  // plain, cursor
    {0x9573d49c, 0x0c3b, 0x4e02, {0x9a, 0xbf, 0xea, 0xf5, 0xaf, 0xdc, 0x2d, 0xbf}},  // corrected, cursor
    {0xd9de6cc6, 0x7950, 0x4bdb, {0xb4, 0xf2, 0xa7, 0xed, 0xb4, 0x8c, 0xf1, 0x9a}},  // suspicious, cursor
};
const wchar_t* kNames[kStyleCount] = {L"注音 AI：輸入中", L"注音 AI：AI 改過的字", L"注音 AI：AI 覺得可能有錯",
                                      L"注音 AI：游標所在的字", L"注音 AI：游標所在（AI 改過）", L"注音 AI：游標所在（可能有錯）"};

TF_DA_COLOR none() {
    TF_DA_COLOR c = {};
    c.type = TF_CT_NONE;
    return c;
}

TF_DA_COLOR rgb(COLORREF v) {
    TF_DA_COLOR c = {};
    c.type = TF_CT_COLORREF;
    c.cr = v;
    return c;
}

TF_DA_COLOR sys(int index) {
    TF_DA_COLOR c = {};
    c.type = TF_CT_SYSCOLOR;
    c.nIndex = index;
    return c;
}
}  // namespace

const GUID& displayAttributeGuid(int style) { return kGuids[(style >= 0 && style < kStyleCount) ? style : 0]; }

TF_DISPLAYATTRIBUTE displayAttribute(int style) {
    TF_DISPLAYATTRIBUTE da = {};
    da.crText = none();
    da.crBk = none();
    da.crLine = none();
    da.lsStyle = TF_LS_SOLID;
    da.fBoldLine = FALSE;
    da.bAttr = TF_ATTR_INPUT;
    const int base = style % 3;
    const bool focus = style >= 3;
    if (base == 1) {
        da.fBoldLine = TRUE;
        da.crLine = rgb(RGB(240, 140, 0));
        da.bAttr = TF_ATTR_CONVERTED;
    } else if (base == 2) {
        da.lsStyle = TF_LS_SQUIGGLE;
        da.crLine = rgb(RGB(224, 60, 60));
        da.bAttr = TF_ATTR_CONVERTED;
    }
    if (focus) {
        da.fBoldLine = TRUE;
        da.crBk = sys(COLOR_HIGHLIGHT);
        da.crText = sys(COLOR_HIGHLIGHTTEXT);
        da.bAttr = TF_ATTR_TARGET_CONVERTED;
    }
    return da;
}

// MARK: DisplayAttributeInfo

DisplayAttributeInfo::DisplayAttributeInfo(int style) : style_(style) { moduleAddRef(); }
DisplayAttributeInfo::~DisplayAttributeInfo() { moduleRelease(); }

STDMETHODIMP DisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) DisplayAttributeInfo::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) DisplayAttributeInfo::Release() {
    const LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return ULONG(n);
}

STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = displayAttributeGuid(style_);
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(kNames[style_]);
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    *pda = displayAttribute(style_);
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) { return E_NOTIMPL; }
STDMETHODIMP DisplayAttributeInfo::Reset() { return S_OK; }

// MARK: EnumDisplayAttributeInfo

EnumDisplayAttributeInfo::EnumDisplayAttributeInfo(int index) : index_(index) { moduleAddRef(); }
EnumDisplayAttributeInfo::~EnumDisplayAttributeInfo() { moduleRelease(); }

STDMETHODIMP EnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo))
        *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::Release() {
    const LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return ULONG(n);
}

STDMETHODIMP EnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new (std::nothrow) EnumDisplayAttributeInfo(index_);
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP EnumDisplayAttributeInfo::Next(ULONG count, ITfDisplayAttributeInfo** out, ULONG* fetched) {
    if (!out) return E_INVALIDARG;
    ULONG n = 0;
    while (n < count && index_ < kStyleCount) {
        out[n] = new (std::nothrow) DisplayAttributeInfo(index_);
        if (!out[n]) break;
        ++n;
        ++index_;
    }
    if (fetched) *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

STDMETHODIMP EnumDisplayAttributeInfo::Reset() {
    index_ = 0;
    return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Skip(ULONG count) {
    index_ = int(std::min<ULONG>(ULONG(kStyleCount), ULONG(index_) + count));
    return index_ < kStyleCount ? S_OK : S_FALSE;
}

}  // namespace zl::tsf
