#include "lspclient.h"

#include <vector>

#include "framing.h"

namespace px {
namespace {

// The child's environment, with NODE_OPTIONS replaced. The server's definition
// index needs a raised heap ceiling (docs/EMBEDDING.md, "Heap ceiling"), and a
// .cmd launcher forwards its arguments to the script, not to node, so the
// ceiling has to travel in the environment.
std::wstring childEnvironment() {
    std::wstring block;
    wchar_t* env = ::GetEnvironmentStringsW();
    if (env != nullptr) {
        for (wchar_t* p = env; *p != L'\0';) {
            const size_t len = ::wcslen(p);
            const std::wstring entry(p, len);
            if (entry.compare(0, 13, L"NODE_OPTIONS=") != 0) {
                block.append(entry);
                block.push_back(L'\0');
            }
            p += len + 1;
        }
        ::FreeEnvironmentStringsW(env);
    }
    block.append(L"NODE_OPTIONS=--max-old-space-size=4096");
    block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

}  // namespace

LspClient::~LspClient() { stop(); }

bool LspClient::start(const std::wstring& commandLine, HWND notifyWindow, UINT notifyMessage) {
    stop();
    notifyWindow_ = notifyWindow;
    notifyMessage_ = notifyMessage;

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!::CreatePipe(&inRead, &inWrite, &sa, 0) || !::CreatePipe(&outRead, &outWrite, &sa, 0)) return false;
    ::SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    // stderr is log noise and is not part of the wire contract, so it goes to NUL.
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = nul != INVALID_HANDLE_VALUE ? nul : outWrite;

    std::wstring env = childEnvironment();
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    PROCESS_INFORMATION pi = {};
    const BOOL ok = ::CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, env.data(), nullptr,
                                     &si, &pi);

    ::CloseHandle(inRead);
    ::CloseHandle(outWrite);
    if (nul != INVALID_HANDLE_VALUE) ::CloseHandle(nul);
    if (!ok) {
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        return false;
    }
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job_) { SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)); AssignProcessToJobObject(job_, pi.hProcess); }
    ResumeThread(pi.hThread);
    ::CloseHandle(pi.hThread);

    process_ = pi.hProcess;
    stdinWrite_ = inWrite;
    stdoutRead_ = outRead;
    readerStopped_ = false;
    reader_ = ::CreateThread(nullptr, 0, &LspClient::readerThread, this, 0, nullptr);
    return true;
}

void LspClient::stop() {
    if (process_ == nullptr) return;

    // The announced sequence: shutdown, then exit. An exit that was never
    // announced makes the server report a crash (exit code 1).
    if (stdinWrite_ != nullptr) {
        send(Json{{"jsonrpc", "2.0"}, {"id", nextId_++}, {"method", "shutdown"}, {"params", nullptr}});
        send(Json{{"jsonrpc", "2.0"}, {"method", "exit"}});
        ::CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }
    ::WaitForSingleObject(process_, 3000);
    ::TerminateProcess(process_, 0);
    if (job_) { CloseHandle(job_); job_ = nullptr; }

    // The child held the only other handle to the write end, so its death ends
    // the reader's blocking ReadFile. Join before closing what it reads from.
    if (reader_ != nullptr) {
        CancelSynchronousIo(reader_);
        ::WaitForSingleObject(reader_, INFINITE);
        ::CloseHandle(reader_);
        reader_ = nullptr;
    }
    if (stdoutRead_ != nullptr) {
        ::CloseHandle(stdoutRead_);
        stdoutRead_ = nullptr;
    }
    ::CloseHandle(process_);
    process_ = nullptr;
    pending_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.clear();
}

void LspClient::send(const Json& message) {
    if (stdinWrite_ == nullptr) return;
    const std::string body = message.dump();
    const std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    DWORD written = 0;
    for (size_t sent = 0; sent < frame.size(); sent += written) {
        if (!::WriteFile(stdinWrite_, frame.data() + sent, static_cast<DWORD>(frame.size() - sent), &written,
                         nullptr) ||
            written == 0)
            return;
    }
}

void LspClient::notify(const std::string& method, const Json& params) {
    send(Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

void LspClient::request(const std::string& method, const Json& params, ResponseHandler handler) {
    if (process_ == nullptr) return;
    const int id = nextId_++;
    pending_[id] = std::move(handler);
    send(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
}

DWORD WINAPI LspClient::readerThread(LPVOID self) {
    static_cast<LspClient*>(self)->readLoop();
    return 0;
}

void LspClient::readLoop() {
    FrameParser parser;
    char chunk[8192];
    DWORD read = 0;
    while (::ReadFile(stdoutRead_, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
        parser.feed(chunk, read);
        bool any = false;
        std::string body;
        while (parser.next(body)) {
            std::lock_guard<std::mutex> lock(mutex_);
            inbox_.push_back(body);
            any = true;
        }
        if (any && notifyWindow_ != nullptr) ::PostMessage(notifyWindow_, notifyMessage_, 0, 0);
    }
    readerStopped_ = true;
    if (notifyWindow_ != nullptr) ::PostMessage(notifyWindow_, notifyMessage_, 0, 0);
}

void LspClient::drain() {
    std::deque<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(inbox_);
    }
    for (const std::string& body : batch) {
        Json message;
        try {
            message = Json::parse(body);
        } catch (const std::exception&) {
            continue;  // a frame we cannot read is dropped, never guessed at
        }
        if (message.contains("id") && message["id"].is_number_integer() && !message.contains("method")) {
            const auto it = pending_.find(message["id"].get<int>());
            if (it == pending_.end()) continue;
            ResponseHandler handler = it->second;
            pending_.erase(it);
            handler(message.value("result", Json()), message.value("error", Json()));
        } else if (message.contains("method")) {
            if (message.contains("id")) {
                // The server asks for progress tokens; an unanswered request
                // stalls its side, so every one gets a null result.
                const auto method = message.value("method", std::string());
                if (method == "window/workDoneProgress/create") send(Json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", nullptr}});
                else if (method == "workspace/applyEdit") send(Json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", {{"applied", false}, {"failureReason", "Use the plugin's edit preview to apply changes."}}}});
                else send(Json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"error", {{"code", -32601}, {"message", "Unsupported client request"}}}});
                continue;
            }
            if (onNotification_) {
                onNotification_(message["method"].get<std::string>(), message.value("params", Json::object()));
            }
        }
    }
    if (process_ && readerStopped_) {
        stop();
        if (onNotification_) onNotification_("$/serverExited", Json::object());
    }
}

}  // namespace px
