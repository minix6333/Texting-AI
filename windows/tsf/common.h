// Shared pieces of the text service DLL: module globals, a small COM smart pointer, and string
// conversions.
#pragma once

#include "tsf_compat.h"

#include <atomic>
#include <string>
#include <utility>

namespace zl::tsf {

extern HINSTANCE g_module;
extern std::atomic<long> g_objects;  // live COM objects, for DllCanUnloadNow

inline void moduleAddRef() { ++g_objects; }
inline void moduleRelease() { --g_objects; }

/// Owns one reference to a COM interface.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    /// Takes a new reference.
    explicit ComPtr(T* p) : p_(p) {
        if (p_) p_->AddRef();
    }
    ComPtr(const ComPtr& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ~ComPtr() { reset(); }
    ComPtr& operator=(ComPtr o) noexcept {
        std::swap(p_, o.p_);
        return *this;
    }

    /// Takes over a reference the caller already owns (e.g. from TF_SELECTION).
    static ComPtr adopt(T* p) {
        ComPtr c;
        c.p_ = p;
        return c;
    }

    void reset() {
        if (p_) std::exchange(p_, nullptr)->Release();
    }
    /// For out-parameters: releases the current pointer and receives an already-owned one.
    T** put() {
        reset();
        return &p_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T* detach() { return std::exchange(p_, nullptr); }

    template <typename U>
    ComPtr<U> query(REFIID iid) const {
        ComPtr<U> out;
        if (p_) p_->QueryInterface(iid, out.putVoid());
        return out;
    }

private:
    T* p_ = nullptr;
};

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

/// The directory of this DLL (where zhuyinllm-server.exe is installed too).
std::wstring moduleDir();

}  // namespace zl::tsf
