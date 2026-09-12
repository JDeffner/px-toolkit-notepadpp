#include "../src/lspclient.h"
#include "../src/framing.h"
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

namespace {
using px::Json;
void writeBytes(const std::string& bytes) {
    DWORD written = 0;
    for (size_t sent = 0; sent < bytes.size(); sent += written)
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data() + sent, static_cast<DWORD>(bytes.size() - sent), &written, nullptr) || !written) return;
}
void writeMessage(const Json& message) {
    const auto body = message.dump();
    writeBytes("Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
}
std::wstring fixtureCommand(const wchar_t* mode) {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    return L"\"" + std::wstring(executable) + L"\" --lsp-fixture " + mode;
}
bool pump(px::LspClient& client, const std::function<bool()>& done) {
    const auto deadline = GetTickCount64() + 3000;
    while (!done() && GetTickCount64() < deadline) { client.drain(); Sleep(5); }
    return done();
}
}

int lspFixture(const std::string& mode) {
    if (mode == "stalled") { Sleep(INFINITE); return 0; }
    if (mode == "bad-frame") { writeBytes("Content-Length: -1\r\n\r\nx"); Sleep(INFINITE); return 0; }
    px::FrameParser parser;
    char bytes[8192]; DWORD count = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), bytes, sizeof(bytes), &count, nullptr) && count) {
        parser.feed(bytes, count);
        std::string body;
        while (parser.next(body)) {
            const auto message = Json::parse(body);
            const auto method = message.value("method", std::string());
            if (method == "exit") return 0;
            if (!message.contains("id")) continue;
            const auto id = message.at("id");
            if (method == "bad-envelope") {
                writeMessage({{"jsonrpc", "2.0"}, {"id", id}, {"error", nullptr}});
            } else if (method == "bad-payload") {
                writeMessage({{"jsonrpc", "2.0"}, {"id", id}, {"result", Json::object()}});
            } else {
                if (method == "notifications") {
                    writeMessage({{"jsonrpc", "2.0"}, {"method", 42}});
                    writeMessage({{"jsonrpc", "2.0"}, {"method", "bad-notification"}, {"params", Json::object()}});
                    writeMessage({{"jsonrpc", "2.0"}, {"method", "scalar-params"}, {"params", 42}});
                }
                if (method == "parameter-shapes") {
                    writeMessage({{"jsonrpc", "2.0"}, {"method", "paradox/indexChanged"}, {"params", Json::array({nullptr})}});
                    writeMessage({{"jsonrpc", "2.0"}, {"method", "empty-params"}, {"params", Json::array()}});
                    writeMessage({{"jsonrpc", "2.0"}, {"method", "omitted-params"}});
                }
                writeMessage({{"jsonrpc", "2.0"}, {"id", id}, {"result", message.value("params", Json())}});
            }
        }
    }
    return 0;
}

int testLspClient() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::printf("FAIL %s\n", name); } };
    px::LspClient client;
    int errors = 0;
    int validNotifications = 0;
    bool exited = false;
    const DWORD owner = GetCurrentThreadId();
    client.setNotificationHandler([&](const std::string& method, const Json& params) {
        check(GetCurrentThreadId() == owner, "LSP notifications stay on the owner thread");
        if (method == "$/clientError") ++errors;
        if (method == "$/serverExited") exited = true;
        if (method == "bad-notification") params.at("missing").get<std::string>();
        if (method == "paradox/indexChanged") { check(params == Json::array({nullptr}), "index notification preserves its positional parameter"); ++validNotifications; }
        if (method == "empty-params") { check(params.is_array() && params.empty(), "empty parameter array reaches handler"); ++validNotifications; }
        if (method == "omitted-params") { check(params.is_object() && params.empty(), "omitted parameters use empty object"); ++validNotifications; }
    });
    check(client.start(fixtureCommand(L"messages"), nullptr, 0), "start transport fixture");
    bool replied = false;
    client.request("notifications", {{"text", "echo"}}, [&](const Json& result, const Json& error) {
        check(GetCurrentThreadId() == owner, "LSP response stays on the owner thread");
        check(error.is_null() && result.at("text") == "echo", "valid response after malformed notifications");
        replied = true;
    });
    check(pump(client, [&] { return replied; }) && errors == 3, "malformed envelope, payload, and scalar parameters are contained per message");
    replied = false;
    const int errorsBeforeShapes = errors;
    client.request("parameter-shapes", Json::object(), [&](const Json&, const Json& error) { check(error.is_null(), "request completes after valid notifications"); replied = true; });
    check(pump(client, [&] { return replied; }) && validNotifications == 3 && errors == errorsBeforeShapes, "array and omitted parameters do not produce client errors");
    replied = false;
    client.request("bad-envelope", Json::object(), [&](const Json&, const Json& error) { check(error.is_object(), "malformed response completes request with error"); replied = true; });
    check(pump(client, [&] { return replied; }), "malformed response does not leave request pending");
    const int before = errors;
    client.request("bad-payload", Json::object(), [](const Json& result, const Json&) { result.at("missing").get<int>(); });
    check(pump(client, [&] { return errors > before; }), "malformed feature payload cannot escape dispatch");
    replied = false;
    client.request("echo", {{"sequence", 2}}, [&](const Json& result, const Json& error) { check(error.is_null() && result.at("sequence") == 2, "transport remains usable after malformed payload"); replied = true; });
    check(pump(client, [&] { return replied; }), "subsequent request completes");
    client.stop();

    check(client.start(fixtureCommand(L"stalled"), nullptr, 0), "start non-reading fixture");
    int cancelled = 0;
    const auto sendStart = GetTickCount64();
    client.request("large", {{"text", std::string(1024 * 1024, 'x')}}, [&](const Json&, const Json& error) { if (!error.is_null()) ++cancelled; });
    check(GetTickCount64() - sendStart < 1000, "large send returns while server does not read");
    Sleep(50); // let the worker enter its blocking pipe write
    const auto stopStart = GetTickCount64();
    client.stop();
    check(GetTickCount64() - stopStart < 1500 && cancelled == 1, "stop cancels blocked write and resolves pending request");
    replied = false;
    client.request("disconnected", Json(), [&](const Json&, const Json& error) { replied = !error.is_null(); });
    check(replied, "disconnected request reports failure");

    check(client.start(fixtureCommand(L"stalled"), nullptr, 0), "restart client for queue saturation");
    exited = false;
    client.notify("large", {{"text", std::string(1024 * 1024, 'x')}});
    for (int i = 0; i < 1100; ++i) client.notify("queued", Json::object());
    check(pump(client, [&] { return exited; }) && !client.running(), "queue saturation ends session without blocking owner");

    exited = false;
    check(client.start(fixtureCommand(L"bad-frame"), nullptr, 0), "restart client for framing failure");
    replied = false;
    client.request("pending", Json(), [&](const Json&, const Json& error) { replied = !error.is_null(); });
    check(pump(client, [&] { return exited; }) && replied && !client.running(), "invalid frame stops transport and resolves pending requests");
    check(client.start(fixtureCommand(L"messages"), nullptr, 0), "restart after malformed frame");
    replied = false;
    client.request("echo", Json::array({1, 2}), [&](const Json& result, const Json& error) { replied = error.is_null() && result == Json::array({1, 2}); });
    check(pump(client, [&] { return replied; }), "new session works after transport failure");
    client.stop();
    for (int i = 0; i < 5; ++i) {
        check(client.start(fixtureCommand(L"messages"), nullptr, 0), "repeat transport startup");
        client.stop();
        check(!client.running(), "immediate shutdown joins idle transport workers");
    }
    return failures;
}
