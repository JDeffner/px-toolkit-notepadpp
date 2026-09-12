#include "lspclient.h"

#include <vector>
#include <limits>
#include <stdexcept>

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
#if defined(_M_IX86)
    // A 4 GiB heap makes 32-bit Node fail during startup, before any LSP request.
    block.append(L"NODE_OPTIONS=--max-old-space-size=1024");
#else
    block.append(L"NODE_OPTIONS=--max-old-space-size=4096");
#endif
    block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

}  // namespace

LspClient::~LspClient() { onNotification_ = {}; pending_.clear(); stop(); }

bool LspClient::start(const std::wstring& commandLine, HWND notifyWindow, UINT notifyMessage) {
    stop();
    notifyWindow_ = notifyWindow;
    notifyMessage_ = notifyMessage;

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!::CreatePipe(&inRead, &inWrite, &sa, 0)) return false;
    if (!::CreatePipe(&outRead, &outWrite, &sa, 0)) {
        CloseHandle(inRead); CloseHandle(inWrite); return false;
    }
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
    if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job_, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        CloseHandle(inWrite); CloseHandle(outRead);
        if (job_) CloseHandle(job_);
        job_ = nullptr;
        return false;
    }
    process_ = pi.hProcess;
    stdinWrite_ = inWrite;
    stdoutRead_ = outRead;
    failed_ = false;
    stopping_ = false;
    failure_.clear();
    reader_ = ::CreateThread(nullptr, 0, &LspClient::readerThread, this, 0, nullptr);
    writer_ = ::CreateThread(nullptr, 0, &LspClient::writerThread, this, 0, nullptr);
    if (!reader_ || !writer_) {
        CloseHandle(pi.hThread);
        fail("Could not start LSP transport threads.");
        stop();
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return true;
}

void LspClient::stop() {
    if (process_ == nullptr) return;
    ++session_;
    if (!failed_) {
        // These writes are queued, so even a non-reading server cannot block stop.
        send(Json{{"jsonrpc", "2.0"}, {"id", nextId_++}, {"method", "shutdown"}, {"params", nullptr}});
        send(Json{{"jsonrpc", "2.0"}, {"method", "exit"}});
        WaitForSingleObject(process_, 250);
    }
    {
        // Use the waiter's mutex so it cannot miss the shutdown wakeup.
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    outboundReady_.notify_all();
    // Owning the entire job guarantees the other pipe ends close, including a
    // .cmd launcher's Node child. Cancel I/O before joining either worker.
    TerminateJobObject(job_, 0);
    for (HANDLE thread : {reader_, writer_}) if (thread) CancelSynchronousIo(thread);
    for (HANDLE thread : {reader_, writer_}) if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    reader_ = writer_ = nullptr;
    for (HANDLE handle : {stdinWrite_, stdoutRead_, process_, job_}) if (handle) CloseHandle(handle);
    stdinWrite_ = stdoutRead_ = process_ = job_ = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        inbox_.clear(); outbox_.clear();
        inboxBytes_ = outboxBytes_ = 0;
    }
    auto pending = std::move(pending_);
    pending_.clear();
    const Json error{{"code", -32800}, {"message", "Language server session ended."}};
    for (auto& entry : pending) respond(std::move(entry.second), Json(), error);
}

bool LspClient::send(const Json& message) {
    if (!process_ || stopping_ || failed_) return false;
    const std::string body = message.dump();
    if (body.size() > FrameParser::maxBodyBytes) { fail("Outgoing LSP message exceeds 32 MiB."); return false; }
    std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr size_t maxQueuedBytes = 64 * 1024 * 1024;
        if (frame.size() <= maxQueuedBytes - outboxBytes_ && outbox_.size() < 1024) {
            outboxBytes_ += frame.size();
            outbox_.push_back(std::move(frame));
            outboundReady_.notify_one();
            return true;
        }
    }
    fail("LSP output queue is full. Restart the server.");
    return false;
}

void LspClient::notify(const std::string& method, const Json& params) {
    send(Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

void LspClient::request(const std::string& method, const Json& params, ResponseHandler handler) {
    const int id = nextId_++;
    if (send(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}))
        pending_[id] = std::move(handler);
    else respond(std::move(handler), Json(), {{"code", -32000}, {"message", "Language server transport is unavailable."}});
}

void LspClient::fail(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || failed_) return;
        failure_ = reason;
        failed_ = true;
    }
    outboundReady_.notify_all();
    if (notifyWindow_) PostMessage(notifyWindow_, notifyMessage_, 0, 0);
}

DWORD WINAPI LspClient::readerThread(LPVOID self) {
    static_cast<LspClient*>(self)->readLoop();
    return 0;
}
DWORD WINAPI LspClient::writerThread(LPVOID self) {
    static_cast<LspClient*>(self)->writeLoop();
    return 0;
}

void LspClient::writeLoop() {
    try {
        while (!stopping_ && !failed_) {
            std::string frame;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                outboundReady_.wait(lock, [this] { return stopping_ || failed_ || !outbox_.empty(); });
                if (stopping_ || failed_) return;
                frame = std::move(outbox_.front());
                outbox_.pop_front();
                // Count the in-flight frame until its write completes.
            }
            for (size_t sent = 0; sent < frame.size() && !stopping_ && !failed_;) {
                DWORD written = 0;
                if (!WriteFile(stdinWrite_, frame.data() + sent, static_cast<DWORD>(frame.size() - sent), &written, nullptr) || !written) {
                    fail("Language server input pipe closed."); return;
                }
                sent += written;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            outboxBytes_ -= frame.size();
        }
    } catch (const std::exception& e) { fail(e.what()); }
}

void LspClient::readLoop() {
    try {
        FrameParser parser;
        char chunk[8192];
        DWORD read = 0;
        while (!stopping_ && !failed_ && ReadFile(stdoutRead_, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
            parser.feed(chunk, read);
            bool any = false;
            std::string body;
            while (parser.next(body)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (body.size() > 64 * 1024 * 1024 - inboxBytes_ || inbox_.size() >= 4096)
                    throw std::runtime_error("LSP input queue is full.");
                inboxBytes_ += body.size();
                inbox_.push_back(std::move(body));
                any = true;
            }
            if (any && notifyWindow_) PostMessage(notifyWindow_, notifyMessage_, 0, 0);
        }
        fail("Language server output pipe closed.");
    } catch (const std::exception& e) { fail(e.what()); }
}

void LspClient::reportError(const std::string& reason) {
    if (onNotification_) {
        try { onNotification_("$/clientError", {{"message", reason}}); }
        catch (const std::exception&) { OutputDebugStringA("LSP error handler failed.\n"); }
    }
}
void LspClient::respond(ResponseHandler handler, const Json& result, const Json& error) {
    try { handler(result, error); }
    catch (const std::exception& e) { reportError(std::string("Invalid LSP response: ") + e.what()); }
}

void LspClient::drain() {
    const unsigned session = session_;
    std::deque<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(inbox_);
        inboxBytes_ = 0;
    }
    for (const std::string& body : batch) {
        if (session != session_) return; // a callback may restart the client
        Json message;
        int responseId = 0;
        bool isResponse = false;
        try {
            message = Json::parse(body);
            if (!message.is_object()) throw std::runtime_error("Expected an LSP message object.");
            if (!message.contains("method") && message.contains("id") && message["id"].is_number_integer() &&
                message["id"] >= 1 && message["id"] <= (std::numeric_limits<int>::max)()) {
                responseId = message["id"].get<int>(); isResponse = true;
            }
            if (message.value("jsonrpc", std::string()) != "2.0") throw std::runtime_error("Invalid JSON-RPC version.");
            if (message.contains("method")) {
                const auto method = message.at("method").get<std::string>();
                if (method.empty() || message.contains("result") || message.contains("error")) throw std::runtime_error("Invalid LSP method message.");
                const Json params = message.value("params", Json::object());
                if (!params.is_object() && !params.is_array()) throw std::runtime_error("Expected LSP parameter object or array for " + method + ".");
                if (message.contains("id")) {
                    const auto& id = message["id"];
                    if (!id.is_string() && !(id.is_number_integer() && id >= (std::numeric_limits<int>::min)() && id <= (std::numeric_limits<int>::max)()))
                        throw std::runtime_error("Invalid server request id.");
                    if (method == "window/workDoneProgress/create") send({{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}});
                    else if (method == "workspace/applyEdit") send({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"applied", false}, {"failureReason", "Use the plugin's edit preview to apply changes."}}}});
                    else send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", "Unsupported client request"}}}});
                } else if (onNotification_) onNotification_(method, params);
            } else {
                if (!isResponse || message.contains("result") == message.contains("error")) throw std::runtime_error("Invalid LSP response envelope.");
                const Json error = message.value("error", Json());
                if (message.contains("error") && (!error.is_object() || !error.contains("code") || !error["code"].is_number_integer() ||
                    !error.contains("message") || !error["message"].is_string())) throw std::runtime_error("Invalid LSP error object.");
                const auto it = pending_.find(responseId);
                if (it == pending_.end()) continue;
                auto handler = std::move(it->second);
                pending_.erase(it);
                respond(std::move(handler), message.value("result", Json()), error);
            }
        } catch (const std::exception& e) {
            if (isResponse) {
                const auto it = pending_.find(responseId);
                if (it != pending_.end()) {
                    auto handler = std::move(it->second); pending_.erase(it);
                    respond(std::move(handler), Json(), {{"code", -32600}, {"message", e.what()}});
                }
            }
            reportError(e.what());
        }
    }
    if (session != session_) return;
    if (process_ && failed_) {
        std::string reason;
        { std::lock_guard<std::mutex> lock(mutex_); reason = failure_; }
        stop();
        reportError(reason);
        if (onNotification_) {
            try { onNotification_("$/serverExited", Json::object()); }
            catch (const std::exception& e) { reportError(e.what()); }
        }
    }
}

}  // namespace px
