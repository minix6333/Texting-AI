#include "pipe_client.h"

#include "../core/json.h"
#include "pipe_name.h"

namespace zl::tsf {

namespace {
bool inAppContainer() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return true;
    DWORD isContainer = 0, len = 0;
    const BOOL ok = GetTokenInformation(token, TOKEN_INFORMATION_CLASS(29) /* TokenIsAppContainer */, &isContainer,
                                        sizeof isContainer, &len);
    CloseHandle(token);
    return !ok || isContainer != 0;
}
}  // namespace

PipeClient::PipeClient(OnMessage onMessage) : onMessage_(std::move(onMessage)), name_(pipeName()) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    responseEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PipeClient::~PipeClient() {
    stop();
    CloseHandle(stopEvent_);
    CloseHandle(readEvent_);
    CloseHandle(writeEvent_);
    CloseHandle(responseEvent_);
}

void PipeClient::start() {
    if (thread_.joinable()) return;
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] { run(); });
}

void PipeClient::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
    closePipe();
}

void PipeClient::closePipe() {
    std::lock_guard<std::mutex> lock(writeMutex_);
    connected_ = false;
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_, nullptr);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::connectOnce() {
    HANDLE h = CreateFileW(name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(name_.c_str(), 200);
        return false;
    }
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    std::lock_guard<std::mutex> lock(writeMutex_);
    pipe_ = h;
    connected_ = true;
    return true;
}

/// Starts zhuyinllm-server.exe from this DLL's folder (normally it runs from login already).
/// Not from inside app containers, which cannot start processes.
void PipeClient::launchServer() {
    const ULONGLONG now = GetTickCount64();
    if (lastLaunch_ && now - lastLaunch_ < 15000) return;
    lastLaunch_ = now;
    if (inAppContainer()) return;
    const std::wstring dir = moduleDir();
    std::wstring exe = dir + L"\\zhuyinllm-server.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exe + L"\"";
    const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, dir.c_str(), &si, &pi) &&
        !CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(), &si, &pi)) {
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

bool PipeClient::readExact(void* buf, DWORD size) {
    DWORD done = 0;
    while (done < size) {
        OVERLAPPED ov = {};
        ov.hEvent = readEvent_;
        ResetEvent(readEvent_);
        DWORD n = 0;
        if (!ReadFile(pipe_, static_cast<char*>(buf) + done, size - done, &n, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            HANDLE events[2] = {readEvent_, stopEvent_};
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIoEx(pipe_, &ov);
                GetOverlappedResult(pipe_, &ov, &n, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe_, &ov, &n, FALSE)) return false;
        }
        if (n == 0) return false;
        done += n;
    }
    return true;
}

bool PipeClient::write(const std::string& message) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (pipe_ == INVALID_HANDLE_VALUE) return false;
    std::string frame(4, '\0');
    const uint32_t len = uint32_t(message.size());
    for (int i = 0; i < 4; ++i) frame[size_t(i)] = char((len >> (8 * i)) & 0xFF);
    frame += message;
    OVERLAPPED ov = {};
    ov.hEvent = writeEvent_;
    ResetEvent(writeEvent_);
    DWORD n = 0;
    if (!WriteFile(pipe_, frame.data(), DWORD(frame.size()), &n, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(writeEvent_, 1000) != WAIT_OBJECT_0) {
            CancelIoEx(pipe_, &ov);
            GetOverlappedResult(pipe_, &ov, &n, TRUE);
            return false;
        }
        if (!GetOverlappedResult(pipe_, &ov, &n, FALSE)) return false;
    }
    return n == frame.size();
}

bool PipeClient::send(const std::string& message) { return connected_ && write(message); }

std::string PipeClient::call(const std::string& request, int id, DWORD timeoutMs) {
    if (!connected_) return {};
    {
        std::lock_guard<std::mutex> lock(responseMutex_);
        waitingId_ = id;
        response_.clear();
        ResetEvent(responseEvent_);
    }
    if (!write(request)) {
        std::lock_guard<std::mutex> lock(responseMutex_);
        waitingId_ = 0;
        return {};
    }
    WaitForSingleObject(responseEvent_, timeoutMs);
    std::lock_guard<std::mutex> lock(responseMutex_);
    waitingId_ = 0;
    return std::move(response_);
}

void PipeClient::run() {
    int attempts = 0;
    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
        if (!connected_) {
            if (!connectOnce()) {
                if (GetLastError() == ERROR_FILE_NOT_FOUND || attempts == 2) launchServer();
                ++attempts;
                const DWORD wait = attempts < 20 ? 250 : 3000;
                if (WaitForSingleObject(stopEvent_, wait) == WAIT_OBJECT_0) break;
                continue;
            }
            attempts = 0;
            onMessage_("{\"ev\":\"connected\"}");
        }
        unsigned char header[4];
        std::string msg;
        bool ok = readExact(header, 4);
        if (ok) {
            const uint32_t len = uint32_t(header[0]) | uint32_t(header[1]) << 8 | uint32_t(header[2]) << 16 | uint32_t(header[3]) << 24;
            ok = len <= (1u << 20);
            if (ok) {
                msg.resize(len);
                ok = len == 0 || readExact(&msg[0], len);
            }
        }
        if (!ok) {
            closePipe();
            SetEvent(responseEvent_);  // a waiting call returns empty
            if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;
            onMessage_("{\"ev\":\"disconnected\"}");
            continue;
        }
        int id = 0;
        try {
            id = json::Value::parse(msg)["id"].asInt(0);
        } catch (...) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(responseMutex_);
            if (id != 0 && id == waitingId_) {
                response_ = std::move(msg);
                SetEvent(responseEvent_);
                continue;
            }
        }
        onMessage_(std::move(msg));
    }
}

}  // namespace zl::tsf
