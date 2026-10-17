// COM entry points of the text service DLL and its registration (regsvr32): the COM class, the
// input profile for Chinese (Taiwan), and the TSF categories.

#include <new>

#include "common.h"
#include "resource.h"
#include "text_service.h"

namespace zl::tsf {

HINSTANCE g_module = nullptr;
std::atomic<long> g_objects{0};

std::wstring moduleDir() {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(g_module, path, DWORD(MAX_PATH * 2));
    std::wstring p(path, n);
    return p.substr(0, p.find_last_of(L'\\'));
}

namespace {
const wchar_t* kDescription = L"注音 AI";

class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) *ppv = static_cast<IClassFactory*>(this);
        if (!*ppv) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        moduleAddRef();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        moduleRelease();
        return 1;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* service = new (std::nothrow) TextService();
        if (!service) return E_OUTOFMEMORY;
        const HRESULT hr = service->QueryInterface(riid, ppv);
        service->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) moduleAddRef();
        else moduleRelease();
        return S_OK;
    }
};

ClassFactory g_factory;

std::wstring guidString(const GUID& g) {
    wchar_t buf[64] = {};
    StringFromGUID2(g, buf, 64);
    return buf;
}

std::wstring clsidKey() { return L"Software\\Classes\\CLSID\\" + guidString(kClsidTextService); }

bool setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t))) ==
           ERROR_SUCCESS;
}

bool registerComServer() {
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(g_module, path, DWORD(MAX_PATH * 2));
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKey().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool ok = setString(key, nullptr, kDescription);
    HKEY inproc = nullptr;
    if (RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, nullptr) == ERROR_SUCCESS) {
        ok = setString(inproc, nullptr, path) && setString(inproc, L"ThreadingModel", L"Apartment") && ok;
        RegCloseKey(inproc);
    } else {
        ok = false;
    }
    RegCloseKey(key);
    return ok;
}

void unregisterComServer() { RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey().c_str()); }

const GUID* const kCategories[] = {&kCatTipKeyboard, &kCatDisplayAttributeProvider, &kCatInputModeCompartment,
                                   &kCatImmersiveSupport, &kCatSystraySupport};

bool registerProfile() {
    ComPtr<ITfInputProcessorProfileMgr> mgr;
    if (FAILED(CoCreateInstance(kClsidInputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, mgr.putVoid())))
        return false;
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(g_module, path, DWORD(MAX_PATH * 2));
    return SUCCEEDED(mgr->RegisterProfile(kClsidTextService, kLangId, kGuidProfile, kDescription, ULONG(wcslen(kDescription)), path, n,
                                          UINT(-IDI_ZHUYINLLM), nullptr, 0, TRUE, 0));
}

void unregisterProfile() {
    ComPtr<ITfInputProcessorProfileMgr> mgr;
    if (SUCCEEDED(CoCreateInstance(kClsidInputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, mgr.putVoid())))
        mgr->UnregisterProfile(kClsidTextService, kLangId, kGuidProfile, 0);
}

bool registerCategories() {
    ComPtr<ITfCategoryMgr> cat;
    if (FAILED(CoCreateInstance(kClsidCategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, cat.putVoid()))) return false;
    bool ok = true;
    for (const GUID* g : kCategories) ok = SUCCEEDED(cat->RegisterCategory(kClsidTextService, *g, kClsidTextService)) && ok;
    return ok;
}

void unregisterCategories() {
    ComPtr<ITfCategoryMgr> cat;
    if (FAILED(CoCreateInstance(kClsidCategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, cat.putVoid()))) return;
    for (const GUID* g : kCategories) cat->UnregisterCategory(kClsidTextService, *g, kClsidTextService);
}

struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};
}  // namespace

}  // namespace zl::tsf

using namespace zl::tsf;

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        // classes a DLL registers outlive it: a reloaded copy would get the old window procedures
        UnregisterClassW(kPopupClass, instance);
        UnregisterClassW(kHiddenClass, instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, kClsidTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_objects.load() <= 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    ComScope com;
    if (!registerComServer() || !registerProfile() || !registerCategories()) {
        unregisterCategories();
        unregisterProfile();
        unregisterComServer();
        return SELFREG_E_CLASS;
    }
    return S_OK;
}

STDAPI DllUnregisterServer() {
    ComScope com;
    unregisterCategories();
    unregisterProfile();
    unregisterComServer();
    return S_OK;
}
