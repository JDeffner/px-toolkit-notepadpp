// The JSON-RPC layer over px-lsp's stdio pipes.
//
// Worker threads read and write frames; every callback runs on the UI thread,
// because Scintilla and Notepad++ messages may not be sent from anywhere else.
// The handoff is a PostMessage to the plugin's message-only window.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <deque>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <condition_variable>

#include "../third_party/nlohmann/json.hpp"

namespace px {

using Json = nlohmann::json;

class LspClient {
public:
    // Called on the UI thread for a response (`error` is null on success) and
    // for every server-initiated notification.
    using ResponseHandler = std::function<void(const Json& result, const Json& error)>;
    using NotificationHandler = std::function<void(const std::string& method, const Json& params)>;

    ~LspClient();

    // `commandLine` is passed to CreateProcessW verbatim. `notifyWindow` gets
    // `notifyMessage` posted whenever decoded messages are waiting.
    bool start(const std::wstring& commandLine, HWND notifyWindow, UINT notifyMessage);
    void stop();
    bool running() const { return process_ != nullptr && ::WaitForSingleObject(process_, 0) == WAIT_TIMEOUT; }

    void setNotificationHandler(NotificationHandler handler) { onNotification_ = std::move(handler); }

    void notify(const std::string& method, const Json& params);
    void request(const std::string& method, const Json& params, ResponseHandler handler);

    // Drains what the reader thread decoded. UI thread only.
    void drain();

private:
    bool send(const Json& message);
    void fail(const std::string& reason);
    void reportError(const std::string& reason);
    void respond(ResponseHandler handler, const Json& result, const Json& error);
    static DWORD WINAPI readerThread(LPVOID self);
    static DWORD WINAPI writerThread(LPVOID self);
    void readLoop();
    void writeLoop();

    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
    HANDLE reader_ = nullptr;
    HANDLE writer_ = nullptr;
    HANDLE job_ = nullptr;
    std::atomic<bool> failed_{false};
    std::atomic<bool> stopping_{true};
    HWND notifyWindow_ = nullptr;
    UINT notifyMessage_ = 0;

    std::mutex mutex_;
    std::deque<std::string> inbox_;
    std::deque<std::string> outbox_;
    size_t inboxBytes_ = 0, outboxBytes_ = 0;
    std::condition_variable outboundReady_;
    std::string failure_;
    unsigned session_ = 0;

    int nextId_ = 1;
    std::map<int, ResponseHandler> pending_;  // UI thread only
    NotificationHandler onNotification_;
};

}  // namespace px
