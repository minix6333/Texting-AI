// The text service's connection to the engine server: requests from the UI thread wait for their
// response; everything else the server sends (pushes) goes to `onMessage` on the reader thread.
// Connects in the background, retries, and starts the server when it is not running.
#pragma once

#include "common.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace zl::tsf {

class PipeClient {
public:
    using OnMessage = std::function<void(std::string)>;
    explicit PipeClient(OnMessage onMessage);
    ~PipeClient();

    void start();
    void stop();
    bool connected() const { return connected_; }

    /// Sends `request` (which carries "id": `id`) and waits up to `timeoutMs` for its response.
    /// Empty when not connected or too late (a late response goes to onMessage).
    std::string call(const std::string& request, int id, DWORD timeoutMs);
    /// Sends without waiting.
    bool send(const std::string& message);

private:
    void run();
    bool connectOnce();
    void launchServer();
    bool write(const std::string& message);
    bool readExact(void* buf, DWORD size);
    void closePipe();

    OnMessage onMessage_;
    std::wstring name_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
    HANDLE readEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    HANDLE responseEvent_ = nullptr;
    std::mutex writeMutex_;
    std::mutex responseMutex_;
    int waitingId_ = 0;
    std::string response_;
    std::atomic<bool> connected_{false};
    std::thread thread_;
    ULONGLONG lastLaunch_ = 0;
};

}  // namespace zl::tsf
