// The JSON-RPC layer over px-lsp's stdio pipes.
//
// The reader thread only decodes frames; every callback runs on the UI thread,
// because Scintilla and Notepad++ messages may not be sent from anywhere else.
// The handoff is a PostMessage to the plugin's message-only window.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

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
    bool running() const { return process_ != nullptr; }

    void setNotificationHandler(NotificationHandler handler) { onNotification_ = std::move(handler); }

    void notify(const std::string& method, const Json& params);
    void request(const std::string& method, const Json& params, ResponseHandler handler);

    // Drains what the reader thread decoded. UI thread only.
    void drain();

private:
    void send(const Json& message);
    static DWORD WINAPI readerThread(LPVOID self);
    void readLoop();

    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
    HANDLE reader_ = nullptr;
    HWND notifyWindow_ = nullptr;
    UINT notifyMessage_ = 0;

    std::mutex mutex_;
    std::deque<std::string> inbox_;

    int nextId_ = 1;
    std::map<int, ResponseHandler> pending_;  // UI thread only
    NotificationHandler onNotification_;
};

}  // namespace px
