// The engine server's pipe: one per user (\\.\pipe\ZhuyinLLM-<user SID>), shared by the server
// and the text service (also from inside app containers, whose tokens carry the same user SID).
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <string>
#include <vector>

namespace zl {

inline std::wstring userSid() {
    std::wstring out;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return out;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buf(size ? size : 1);
    if (GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
        LPWSTR s = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s)) {
            out = s;
            LocalFree(s);
        }
    }
    CloseHandle(token);
    return out;
}

inline std::wstring pipeName() { return L"\\\\.\\pipe\\ZhuyinLLM-" + userSid(); }

}  // namespace zl
