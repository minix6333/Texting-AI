// zhuyinllm-server.exe — the engine server on Windows: one process per user holds the dictionaries
// and the model; the text service in every app connects to it through a named pipe
// (\\.\pipe\ZhuyinLLM-<user SID>). Messages are UTF-8 JSON, each preceded by its length
// (4 bytes, little endian). Started at login (HKCU Run) or by the text service on first use.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../engine/engine.h"
#include "../engine/service.h"
#include "../tsf/pipe_name.h"

namespace {

std::wstring g_stateDir;
std::mutex g_logMutex;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

/// Diagnostics only — numbers and states, never typed text.
void logLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const std::wstring path = g_stateDir + L"\\server.log";
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && info.nFileSizeLow > 512 * 1024) {
        MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    FILE* f = _wfopen(path.c_str(), L"ab");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, line.c_str());
    std::fclose(f);
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    std::wstring p(buf, n);
    return p.substr(0, p.find_last_of(L'\\'));
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

/// One connected text service. Reads and writes are overlapped, so a push can be written while
/// the connection thread waits for the next request.
struct Connection {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::mutex writeMutex;
    std::atomic<bool> alive{true};

    ~Connection() {
        if (pipe != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        CloseHandle(readEvent);
        CloseHandle(writeEvent);
    }

    bool io(bool write, void* data, DWORD size, DWORD timeoutMs) {
        DWORD done = 0;
        while (done < size) {
            OVERLAPPED ov = {};
            ov.hEvent = write ? writeEvent : readEvent;
            ResetEvent(ov.hEvent);
            DWORD n = 0;
            char* p = static_cast<char*>(data) + done;
            BOOL ok = write ? WriteFile(pipe, p, size - done, &n, &ov) : ReadFile(pipe, p, size - done, &n, &ov);
            if (!ok) {
                if (GetLastError() != ERROR_IO_PENDING) return false;
                if (WaitForSingleObject(ov.hEvent, timeoutMs) != WAIT_OBJECT_0) {
                    CancelIoEx(pipe, &ov);
                    GetOverlappedResult(pipe, &ov, &n, TRUE);
                    return false;
                }
                if (!GetOverlappedResult(pipe, &ov, &n, FALSE)) return false;
            }
            if (n == 0 && !write) return false;
            done += n;
        }
        return true;
    }

    bool send(const std::string& msg) {
        if (!alive) return false;
        std::lock_guard<std::mutex> lock(writeMutex);
        uint32_t len = uint32_t(msg.size());
        unsigned char header[4] = {uint8_t(len), uint8_t(len >> 8), uint8_t(len >> 16), uint8_t(len >> 24)};
        if (!io(true, header, 4, 2000) || !io(true, const_cast<char*>(msg.data()), len, 2000)) {
            alive = false;
            return false;
        }
        return true;
    }

    bool receive(std::string& msg) {
        unsigned char header[4];
        if (!io(false, header, 4, INFINITE)) return false;
        const uint32_t len = uint32_t(header[0]) | uint32_t(header[1]) << 8 | uint32_t(header[2]) << 16 | uint32_t(header[3]) << 24;
        if (len > (1u << 20)) return false;
        msg.resize(len);
        return len == 0 || io(false, &msg[0], len, 5000);
    }
};

void serve(std::shared_ptr<Connection> conn, zl::Service& service) {
    auto client = service.connect([conn](const std::string& msg) { conn->send(msg); });
    std::string msg;
    while (conn->alive && conn->receive(msg)) {
        if (!conn->send(service.handle(*client, msg))) break;
    }
    conn->alive = false;
    service.disconnect(client);
}

HANDLE createPipe(const std::wstring& name, SECURITY_ATTRIBUTES* sa, bool first) {
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES,
                            64 * 1024, 64 * 1024, 0, sa);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // one server per user session
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\ZhuyinLLM-Server");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    wchar_t* local = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local) == S_OK) {
        g_stateDir = std::wstring(local) + L"\\ZhuyinLLM";
        CoTaskMemFree(local);
    } else {
        g_stateDir = exeDir();
    }
    CreateDirectoryW(g_stateDir.c_str(), nullptr);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

    const std::wstring dir = exeDir();
    const std::wstring dataDir = exists(dir + L"\\data\\tsi.csv") ? dir + L"\\data" : dir;
    std::wstring model = dir + L"\\model.gguf";
    if (!exists(model)) model = g_stateDir + L"\\model.gguf";

    zl::EngineConfig cfg;
    cfg.dataDir = narrow(dataDir);
    cfg.modelPath = narrow(model);
    cfg.stateDir = narrow(g_stateDir);
    cfg.gpu = false;
    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    cfg.threads = int(std::min(8u, std::max(2u, hw / 2)));
    cfg.log = logLine;

    std::unique_ptr<zl::Engine> engine;
    try {
        engine = std::make_unique<zl::Engine>(cfg);
    } catch (const std::exception& e) {
        logLine(std::string("cannot start: ") + e.what());
        return 1;
    }
    zl::Service service(*engine);

    const std::wstring name = zl::pipeName();
    // the user, SYSTEM, and app containers (Store apps, sandboxed processes) at low integrity
    std::wstring sddl = L"D:P(A;;GA;;;" + zl::userSid() + L")(A;;GA;;;SY)(A;;GA;;;AC)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
        logLine("cannot build the pipe security descriptor");
        return 1;
    }
    SECURITY_ATTRIBUTES sa = {sizeof sa, sd, FALSE};
    logLine("listening");

    HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool first = true;
    while (true) {
        HANDLE pipe = createPipe(name, &sa, first);
        if (pipe == INVALID_HANDLE_VALUE) {
            logLine("CreateNamedPipe failed: " + std::to_string(GetLastError()));
            if (first) return 1;  // another server owns the name
            Sleep(1000);
            continue;
        }
        first = false;
        OVERLAPPED ov = {};
        ov.hEvent = connectEvent;
        ResetEvent(connectEvent);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD err = GetLastError();
        if (!connected && err == ERROR_IO_PENDING) {
            WaitForSingleObject(connectEvent, INFINITE);
            DWORD n;
            connected = GetOverlappedResult(pipe, &ov, &n, FALSE);
        } else if (!connected && err == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }
        auto conn = std::make_shared<Connection>();
        conn->pipe = pipe;
        std::thread([conn, &service] { serve(conn, service); }).detach();
    }
    (void)single;
    return 0;
}
