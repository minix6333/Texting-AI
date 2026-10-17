// Text Services Framework headers, plus the Windows 8+ declarations that MinGW-w64's msctf.h
// lacks (values and layouts from the Windows SDK's msctf.h / ctfutb.h / ctffunc.h). With the SDK
// (MSVC) the SDK's own declarations are used.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <msctf.h>
#include <ctfutb.h>

// MARK: constants

#ifndef TF_CONVERSIONMODE_ALPHANUMERIC
#define TF_CONVERSIONMODE_ALPHANUMERIC 0x0000
#define TF_CONVERSIONMODE_NATIVE 0x0001
#define TF_CONVERSIONMODE_FULLSHAPE 0x0008
#endif
#ifndef TF_LBI_ICON
#define TF_LBI_ICON 0x00000001
#define TF_LBI_TEXT 0x00000002
#define TF_LBI_TOOLTIP 0x00000004
#endif
#ifndef TF_LBI_STYLE_SHOWNINTRAY
#define TF_LBI_STYLE_SHOWNINTRAY 0x00000002
#endif
#ifndef TF_LBI_STYLE_BTN_BUTTON
#define TF_LBI_STYLE_BTN_BUTTON 0x00010000
#endif
#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL 0
#endif
#ifndef TF_INVALID_COOKIE
#define TF_INVALID_COOKIE 0xffffffff
#endif
#ifndef TF_LBMENUF_CHECKED
#define TF_LBMENUF_CHECKED 0x1
#define TF_LBMENUF_SEPARATOR 0x4
#define TF_LBMENUF_GRAYED 0x10
#endif

// MARK: interfaces

#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__
struct ITfTextInputProcessorEx : public ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) = 0;
};
#endif

#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
struct ITfDisplayAttributeProvider : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) = 0;
};
#endif

#ifndef __ITfMenu_INTERFACE_DEFINED__
#define __ITfMenu_INTERFACE_DEFINED__
struct ITfMenu : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddMenuItem(UINT uId, DWORD dwFlags, HBITMAP hbmp, HBITMAP hbmpMask, const WCHAR* pch,
                                                  ULONG cch, ITfMenu** ppMenu) = 0;
};
#endif

#ifndef __ITfLangBarItemButton_INTERFACE_DEFINED__
#define __ITfLangBarItemButton_INTERFACE_DEFINED__
typedef enum { TF_LBI_CLK_RIGHT = 1, TF_LBI_CLK_LEFT = 2 } TfLBIClick;
struct ITfLangBarItemButton : public ITfLangBarItem {
    virtual HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu* pMenu) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT wID) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIcon(HICON* phIcon) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetText(BSTR* pbstrText) = 0;
};
#endif

namespace zl::tsf {

// MARK: GUIDs (the same values on every compiler; ours have their own names)

constexpr GUID kIIDTextInputProcessorEx = {0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
constexpr GUID kIIDDisplayAttributeProvider = {0xfee47777, 0x163c, 0x4769, {0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54}};
constexpr GUID kIIDLangBarItemButton = {0x28c7f1d0, 0xde25, 0x11d2, {0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}};

constexpr GUID kGuidLbiInputMode = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
constexpr GUID kGuidCompartmentConversion = {0xccf05dd8, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
constexpr GUID kGuidCompartmentOpenClose = {0x58273aad, 0x01bb, 0x4164, {0x95, 0xc6, 0x75, 0x5b, 0xa0, 0xb5, 0x16, 0x2d}};
constexpr GUID kGuidPropAttribute = {0x34b45670, 0x7526, 0x11d2, {0xa1, 0x47, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}};
constexpr GUID kCatTipKeyboard = {0x34745c63, 0xb2f0, 0x4784, {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}};
constexpr GUID kCatDisplayAttributeProvider = {0x046b8c80, 0x1647, 0x40f7, {0x9b, 0x21, 0xb9, 0x3b, 0x81, 0xaa, 0xbc, 0x1b}};
constexpr GUID kCatInputModeCompartment = {0xccf05dd7, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
constexpr GUID kCatImmersiveSupport = {0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}};
constexpr GUID kCatSystraySupport = {0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}};
constexpr GUID kClsidInputProcessorProfiles = {0x33c53a50, 0xf456, 0x4884, {0xb0, 0x49, 0x85, 0xfd, 0x64, 0x3e, 0xcf, 0xed}};
constexpr GUID kClsidCategoryMgr = {0xa4b544a1, 0x438d, 0x4b41, {0x93, 0x25, 0x86, 0x95, 0x23, 0xe2, 0xd6, 0xc7}};

// This text service
constexpr GUID kClsidTextService = {0x1b5bee20, 0x7f75, 0x40f4, {0xa6, 0xf8, 0x1b, 0x38, 0x0b, 0x75, 0x06, 0x67}};
constexpr GUID kGuidProfile = {0x34d9d8dd, 0x6a8b, 0x4b71, {0xb0, 0x4b, 0x62, 0xba, 0x54, 0x16, 0x24, 0x00}};
constexpr LANGID kLangId = 0x0404;  // Chinese (Traditional, Taiwan)

}  // namespace zl::tsf
