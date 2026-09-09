// Paradox Modding Toolkit for Notepad++: the editor glue.
//
// Notepad++ has no LSP client, so this file is the whole one: it decides which
// buffers are Paradox files, keeps the server's copy of them in sync, and turns
// LSP answers into Scintilla indicators, autocompletion lists and calltips.
// Everything here runs on the UI thread; see lspclient.h for why.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "../sdk/PluginInterface.h"
#include "classify.h"
#include "lspclient.h"
#include "markdown.h"
#include "settings.h"
#include "textpos.h"

using px::Json;

namespace {

const wchar_t kPluginName[] = L"Paradox Modding Toolkit";
const wchar_t kWindowClass[] = L"PxToolkitLspSink";
const UINT WM_PX_LSP = WM_APP + 1;
const UINT_PTR TIMER_SYNC = 1;
const UINT SYNC_DEBOUNCE_MS = 150;

// Scintilla indicator numbers. 8 upwards is the container range; Notepad++ uses
// numbers above 20 for its own marks, so these four sit at the low end of it.
const int INDIC_PX_ERROR = 12;
const int INDIC_PX_WARNING = 13;
const int INDIC_PX_INFO = 14;
const int INDIC_PX_HINT = 15;

enum MenuIndex {
    CMD_COMPLETE = 0,
    CMD_DEFINITION,
    CMD_FORMAT,
    CMD_SCOPE_AT,
    CMD_INSERT_SNIPPET,
    CMD_SEPARATOR,
    CMD_RELOAD_DOCS,
    CMD_SHOW_LOG,
    CMD_OPEN_SETTINGS,
    CMD_RESTART,
    CMD_COUNT
};

struct DocState {
    std::string uri;
    px::Lang lang = px::Lang::None;
    int version = 1;
    bool dirty = false;
};

struct Diagnostic {
    px::Position start;
    px::Position end;
    int severity = 1;
    std::string message;
};

struct CompletionItem {
    std::string label;
    std::string sortText;
    std::string insertText;
    bool hasRange = false;
    px::Position rangeStart;
    px::Position rangeEnd;
};

NppData g_npp;
FuncItem g_funcs[CMD_COUNT];
ShortcutKey g_keyComplete = {true, false, false, VK_SPACE};
ShortcutKey g_keyDefinition = {false, false, false, VK_F12};

HWND g_sink = nullptr;
std::wstring g_configDir;
std::wstring g_iniPath;
std::wstring g_logPath;
px::Settings g_settings;
px::LspClient g_client;
FILE* g_logFile = nullptr;
bool g_serverMissingReported = false;
bool g_ready = false;
std::wstring g_startedForMod;

std::map<std::wstring, DocState> g_docs;               // key: lowercased full path
std::map<std::string, std::vector<Diagnostic>> g_diags;  // key: uri
std::vector<CompletionItem> g_completion;
std::vector<std::pair<std::string, std::string>> g_snippets;  // label -> plain text
bool g_dwelling = false;

// ---------------------------------------------------------------- utilities

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return s;
}

std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// file:///F:/mods/my_mod/events/a.txt, percent-encoded. The URI string is the
// document identity on the wire, so it is built once and echoed verbatim.
std::string pathToUri(const std::wstring& path) {
    std::wstring slashed = path;
    std::replace(slashed.begin(), slashed.end(), L'\\', L'/');
    const std::string utf8 = toUtf8(slashed);
    std::string uri = "file:///";
    for (unsigned char c : utf8) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
        if (unreserved) {
            uri += static_cast<char>(c);
        } else {
            char hex[4];
            ::sprintf_s(hex, "%%%02X", c);
            uri += hex;
        }
    }
    return uri;
}

std::wstring uriToPath(const std::string& uri) {
    std::string decoded;
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            decoded += static_cast<char>(::strtol(uri.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            decoded += uri[i];
        }
    }
    if (decoded.compare(0, 8, "file:///") == 0) decoded.erase(0, 8);
    std::wstring path = toWide(decoded);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

HWND currentScintilla() {
    int which = -1;
    ::SendMessage(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 1 ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
}

LRESULT sci(UINT msg, WPARAM w = 0, LPARAM l = 0) {
    return ::SendMessage(currentScintilla(), msg, w, l);
}

std::wstring pathOfBuffer(uptr_t bufferId) {
    const LRESULT len = ::SendMessage(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, bufferId, 0);
    if (len <= 0) return std::wstring();
    std::wstring path(static_cast<size_t>(len) + 1, L'\0');
    ::SendMessage(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, bufferId, reinterpret_cast<LPARAM>(&path[0]));
    path.resize(::wcslen(path.c_str()));
    return path;
}

uptr_t currentBuffer() {
    return static_cast<uptr_t>(::SendMessage(g_npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
}

std::wstring currentPath() { return pathOfBuffer(currentBuffer()); }

// The buffer's bytes. Notepad++ keeps Unicode documents as UTF-8 in Scintilla,
// which is what the LSP positions above are counted against.
std::string bufferText() {
    const LRESULT len = sci(SCI_GETLENGTH);
    const char* p = reinterpret_cast<const char*>(sci(SCI_GETCHARACTERPOINTER));
    if (p == nullptr || len <= 0) return std::string();
    return std::string(p, static_cast<size_t>(len));
}

void statusBar(const std::wstring& text) {
    ::SendMessage(g_npp._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE,
                  reinterpret_cast<LPARAM>(text.c_str()));
}

void appendLog(const std::string& line) {
    if (g_logFile == nullptr) {
        if (::_wfopen_s(&g_logFile, g_logPath.c_str(), L"ab") != 0) return;
    }
    if (g_logFile == nullptr) return;
    ::fwrite(line.data(), 1, line.size(), g_logFile);
    ::fwrite("\r\n", 1, 2, g_logFile);
    ::fflush(g_logFile);
}

DocState* docFor(const std::wstring& path) {
    const auto it = g_docs.find(lower(path));
    return it == g_docs.end() ? nullptr : &it->second;
}

// ------------------------------------------------------------ server startup

// The folder holding this DLL, taken from an address inside the module rather
// than from the HINSTANCE DllMain stored: it answers correctly whatever has run
// so far, and it is what the packaged server hangs off.
std::wstring moduleDir() {
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&moduleDir), &self)) {
        return std::wstring();
    }
    wchar_t path[MAX_PATH] = {0};
    const DWORD len = ::GetModuleFileNameW(self, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::wstring();
    const std::wstring full(path, len);
    const size_t slash = full.find_last_of(L'\\');
    if (slash == std::wstring::npos) return std::wstring();
    return full.substr(0, slash);
}

std::array<unsigned, 3> serverVersion(const std::wstring& path, std::wstring& text) {
    std::wifstream input(path);
    std::getline(input, text);
    if (!text.empty() && text.back() == L'\r') text.pop_back();
    std::array<unsigned, 3> version{};
    wchar_t extra;
    if (text.empty() || text.find_first_not_of(L"0123456789.") != std::wstring::npos ||
        swscanf_s(text.c_str(), L"%u.%u.%u%c", &version[0], &version[1], &version[2], &extra, 1u) != 3) {
        text.clear();
        return {};
    }
    return version;
}

std::wstring updatedServer(const std::wstring& dir) {
    wchar_t local[MAX_PATH] = {};
    const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!len || len >= MAX_PATH) return {};
    const std::wstring cache = std::wstring(local) + L"\\PxToolkit\\servers";
    // The updater never inherits the LSP pipes and never delays editor startup.
    static bool checked = false;
    if (!checked) {
        checked = true;
        wchar_t system[MAX_PATH] = {};
        ::GetSystemDirectoryW(system, MAX_PATH);
        const std::wstring exe = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring command = L"\"" + exe + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + dir + L"\\update-server.ps1\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (::CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, dir.c_str(), &startup, &process)) {
            ::CloseHandle(process.hThread);
            ::CloseHandle(process.hProcess);
        }
    }
    std::wstring cached, bundled;
    const auto cachedVersion = serverVersion(cache + L"\\current.txt", cached);
    const auto bundledVersion = serverVersion(dir + L"\\server-version.txt", bundled);
    if (cached.empty() || bundled.empty() || cachedVersion <= bundledVersion) return {};
    const std::wstring launcher = cache + L"\\" + cached + L"\\px-lsp.cmd";
    if (::GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    return launcher;
}

// Resolve the launcher: the setting, then an update, then the server the plugin zip lays down
// beside the DLL, then npm's global bin on PATH (which holds px-lsp.cmd).
std::wstring resolveServerCommand(const std::wstring& configured) {
    if (!configured.empty()) return configured;

    const std::wstring dir = moduleDir();
    if (!dir.empty()) {
        const std::wstring updated = updatedServer(dir);
        if (!updated.empty()) return updated;
        const std::wstring bundled = dir + L"\\px-lsp\\px-lsp.cmd";
        if (::GetFileAttributesW(bundled.c_str()) != INVALID_FILE_ATTRIBUTES) return bundled;
    }

    for (const wchar_t* name : {L"px-lsp.cmd", L"px-lsp.exe", L"px-lsp.bat"}) {
        wchar_t found[MAX_PATH] = {0};
        if (::SearchPathW(nullptr, name, nullptr, MAX_PATH, found, nullptr) > 0) return std::wstring(found);
    }
    return std::wstring();
}

std::wstring buildCommandLine(const std::wstring& launcher) {
    const std::wstring lowered = lower(launcher);
    const bool isBatch = lowered.size() > 4 && (lowered.compare(lowered.size() - 4, 4, L".cmd") == 0 ||
                                                lowered.compare(lowered.size() - 4, 4, L".bat") == 0);
    // A .cmd is a script, not an image: it needs the command processor.
    if (isBatch) return L"cmd.exe /c \"\"" + launcher + L"\" --stdio\"";
    return L"\"" + launcher + L"\" --stdio";
}

void applyDiagnostics(const std::string& uri);
void handleNotification(const std::string& method, const Json& params);

void startServer(const std::wstring& modPath) {
    const std::wstring launcher = resolveServerCommand(g_settings.serverCommand);
    if (launcher.empty()) {
        if (!g_serverMissingReported) {
            g_serverMissingReported = true;
            ::MessageBoxW(g_npp._nppHandle,
                          L"px-lsp was not found.\r\n\r\n"
                          L"The plugin zip carries the server in px-lsp\\ next to PxToolkit.dll. "
                          L"Extract the whole zip into the plugins folder, not the DLL alone.\r\n\r\n"
                          L"Otherwise install the server with:\r\n    npm install -g @px-lsp/server\r\n\r\n"
                          L"Or set serverCommand in px-toolkit.ini "
                          L"(Plugins > Paradox Modding Toolkit > Open settings).",
                          kPluginName, MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    g_client.setNotificationHandler(&handleNotification);
    if (!g_client.start(buildCommandLine(launcher), g_sink, WM_PX_LSP)) {
        statusBar(L"PX: server failed to start");
        return;
    }
    g_startedForMod = modPath;
    appendLog("Starting px-lsp: " + toUtf8(launcher));

    // storageDir must exist: the server swallows every cache write failure, so
    // a missing folder costs the script_docs cache silently.
    const std::wstring storageDir = g_configDir + L"\\px-lsp-storage";
    ::CreateDirectoryW(storageDir.c_str(), nullptr);

    Json settings = {
        {"gameId", toUtf8(g_settings.gameId)},
        {"gamePath", g_settings.gamePath.empty() ? Json(nullptr) : Json(toUtf8(g_settings.gamePath))},
        {"logsPath", g_settings.logsPath.empty() ? Json(nullptr) : Json(toUtf8(g_settings.logsPath))},
        {"modPath", modPath.empty() ? Json(nullptr) : Json(toUtf8(modPath))},
        {"locLanguage", toUtf8(g_settings.locLanguage)},
    };

    Json init = {
        // processId arms the server's orphan watchdog: without it a crashed
        // Notepad++ leaves the server holding its index.
        {"processId", static_cast<int>(::GetCurrentProcessId())},
        {"rootUri", modPath.empty() ? Json(nullptr) : Json(pathToUri(modPath))},
        {"capabilities",
         {{"textDocument",
           {{"synchronization", {{"didSave", true}}},
            // No snippetSupport: Scintilla has no tabstops, so the server sends
            // plain-text inserts and never a literal "${".
            {"completion", {{"completionItem", {{"snippetSupport", false}}}}},
            {"hover", {{"contentFormat", Json::array({"markdown", "plaintext"})}}},
            {"publishDiagnostics", Json::object()}}}}},
        {"initializationOptions",
         {{"storageDir", toUtf8(storageDir)}, {"settings", settings}}},
    };

    g_client.request("initialize", init, [](const Json& result, const Json& error) {
        if (!error.is_null()) {
            statusBar(L"PX: initialize failed");
            return;
        }
        const Json info = result.value("serverInfo", Json::object());
        appendLog("px-lsp " + info.value("version", std::string("?")) + " initialized");
        g_client.notify("initialized", Json::object());
        statusBar(L"PX: connected");
    });
}

void ensureServer(const std::wstring& modPath) {
    if (!g_client.running()) startServer(modPath);
}

// -------------------------------------------------------------- document sync

void openDocument(const std::wstring& path) {
    // bufferText() reads the current view, so a buffer that is not on screen
    // waits for its NPPN_BUFFERACTIVATED.
    if (path.empty() || lower(path) != lower(currentPath())) return;
    const px::FileClass fc = px::classify(path, px::isModRootOnDisk);
    if (fc.lang == px::Lang::None) return;
    ensureServer(fc.modRoot);
    if (!g_client.running()) return;
    if (docFor(path) != nullptr) return;

    DocState state;
    state.uri = pathToUri(path);
    state.lang = fc.lang;
    g_docs[lower(path)] = state;

    g_client.notify("textDocument/didOpen",
                    {{"textDocument",
                      {{"uri", state.uri},
                       {"languageId", px::languageId(fc.lang)},
                       {"version", state.version},
                       {"text", bufferText()}}}});
}

void flushChange(const std::wstring& path) {
    DocState* doc = docFor(path);
    if (doc == nullptr || !doc->dirty) return;
    doc->dirty = false;
    doc->version += 1;  // the parse cache is keyed by uri + version
    g_client.notify("textDocument/didChange",
                    {{"textDocument", {{"uri", doc->uri}, {"version", doc->version}}},
                     // A content change with no range is a full replacement, and
                     // it is what an editor buffer produces for free.
                     {"contentChanges", Json::array({{{"text", bufferText()}}})}});
}

void closeDocument(const std::wstring& path) {
    DocState* doc = docFor(path);
    if (doc == nullptr) return;
    g_client.notify("textDocument/didClose", {{"textDocument", {{"uri", doc->uri}}}});
    g_diags.erase(doc->uri);
    g_docs.erase(lower(path));
}

// ------------------------------------------------------------- diagnostics

int indicatorFor(int severity) {
    switch (severity) {
        case 1: return INDIC_PX_ERROR;
        case 2: return INDIC_PX_WARNING;
        case 3: return INDIC_PX_INFO;
        default: return INDIC_PX_HINT;
    }
}

void setUpIndicators(HWND view) {
    const struct {
        int id;
        COLORREF color;
    } styles[] = {{INDIC_PX_ERROR, RGB(224, 64, 64)},
                  {INDIC_PX_WARNING, RGB(216, 160, 32)},
                  {INDIC_PX_INFO, RGB(64, 128, 216)},
                  {INDIC_PX_HINT, RGB(128, 128, 128)}};
    for (const auto& s : styles) {
        ::SendMessage(view, SCI_INDICSETSTYLE, s.id, INDIC_SQUIGGLE);
        ::SendMessage(view, SCI_INDICSETFORE, s.id, s.color);
    }
}

// A zero-width range (a whole-file complaint such as a missing BOM) is widened
// to one character, or it would be neither visible nor hoverable.
void diagnosticSpan(const std::string& text, const Diagnostic& d, size_t& start, size_t& end) {
    start = px::positionToOffset(text, d.start);
    end = px::positionToOffset(text, d.end);
    if (end <= start) end = start < text.size() ? start + 1 : start;
}

// Indicators live on the view, so a publish is drawn only for the buffer the
// user is looking at; the rest is redrawn from g_diags on activation.
void applyDiagnostics(const std::string& uri) {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || doc->uri != uri) return;

    const std::string text = bufferText();
    const LRESULT length = static_cast<LRESULT>(text.size());
    for (int id : {INDIC_PX_ERROR, INDIC_PX_WARNING, INDIC_PX_INFO, INDIC_PX_HINT}) {
        sci(SCI_SETINDICATORCURRENT, id);
        sci(SCI_INDICATORCLEARRANGE, 0, length);
    }
    const auto it = g_diags.find(uri);
    if (it == g_diags.end()) return;
    for (const Diagnostic& d : it->second) {
        size_t start = 0, end = 0;
        diagnosticSpan(text, d, start, end);
        if (end <= start) continue;
        sci(SCI_SETINDICATORCURRENT, indicatorFor(d.severity));
        sci(SCI_INDICATORFILLRANGE, static_cast<WPARAM>(start), static_cast<LPARAM>(end - start));
    }
}

// -------------------------------------------------- server -> client messages

void handleNotification(const std::string& method, const Json& params) {
    if (method == "textDocument/publishDiagnostics") {
        const std::string uri = params.value("uri", std::string());
        std::vector<Diagnostic> list;
        for (const Json& d : params.value("diagnostics", Json::array())) {
            if (!d.is_object() || !d.contains("range")) continue;
            Diagnostic entry;
            entry.start.line = d["range"]["start"].value("line", 0);
            entry.start.character = d["range"]["start"].value("character", 0);
            entry.end.line = d["range"]["end"].value("line", 0);
            entry.end.character = d["range"]["end"].value("character", 0);
            entry.severity = d.value("severity", 1);
            entry.message = d.value("message", std::string());
            list.push_back(entry);
        }
        g_diags[uri] = list;
        applyDiagnostics(uri);
    } else if (method == "paradox/status") {
        wchar_t line[128];
        if (params.value("indexing", false)) {
            ::swprintf_s(line, L"PX: indexing...");
        } else {
            ::swprintf_s(line, L"PX: %lld defs, %lld tokens", params.value("definitions", 0LL),
                         params.value("tokens", 0LL));
        }
        statusBar(line);
    } else if (method == "window/logMessage") {
        appendLog(params.value("message", std::string()));
    }
}

// ------------------------------------------------------------------ features

void showCalltip(Sci_Position position, const std::string& text) {
    if (text.empty()) return;
    sci(SCI_CALLTIPSHOW, static_cast<WPARAM>(position), reinterpret_cast<LPARAM>(text.c_str()));
}

std::string hoverToText(const Json& result) {
    if (result.is_null()) return std::string();
    const Json contents = result.value("contents", Json());
    std::string markdown;
    if (contents.is_string()) {
        markdown = contents.get<std::string>();
    } else if (contents.is_object()) {
        markdown = contents.value("value", std::string());
    } else if (contents.is_array()) {
        for (const Json& part : contents) {
            markdown += part.is_string() ? part.get<std::string>() : part.value("value", std::string());
            markdown += "\n";
        }
    }
    return px::markdownToPlain(markdown);
}

void requestHover(Sci_Position position) {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const std::string text = bufferText();
    const px::Position pos = px::offsetToPosition(text, static_cast<size_t>(position));

    // The diagnostic under the mouse leads: it is why the squiggle is there.
    std::string prefix;
    const auto it = g_diags.find(doc->uri);
    if (it != g_diags.end()) {
        for (const Diagnostic& d : it->second) {
            size_t start = 0, end = 0;
            diagnosticSpan(text, d, start, end);
            if (static_cast<size_t>(position) >= start && static_cast<size_t>(position) < end) {
                prefix += d.message + "\n\n";
            }
        }
    }

    g_client.request("textDocument/hover",
                     {{"textDocument", {{"uri", doc->uri}}},
                      {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [position, prefix](const Json& result, const Json& error) {
                         if (!g_dwelling || !error.is_null()) return;
                         const std::string body = hoverToText(result);
                         if (prefix.empty() && body.empty()) return;
                         showCalltip(position, prefix + body);
                     });
}

void requestCompletion() {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const std::string text = bufferText();
    const Sci_Position caret = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    const px::Position pos = px::offsetToPosition(text, static_cast<size_t>(caret));

    g_client.request("textDocument/completion",
                     {{"textDocument", {{"uri", doc->uri}}},
                      {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [](const Json& result, const Json& error) {
                         if (!error.is_null()) return;
                         const Json items = result.is_array() ? result
                                                              : (result.is_object() ? result.value("items", Json::array())
                                                                                    : Json::array());
                         g_completion.clear();
                         for (const Json& item : items) {
                             CompletionItem entry;
                             entry.label = item.value("label", std::string());
                             if (entry.label.empty() || entry.label.find('\n') != std::string::npos) continue;
                             entry.sortText = item.value("sortText", entry.label);
                             entry.insertText = item.value("insertText", entry.label);
                             const Json edit = item.value("textEdit", Json());
                             if (edit.is_object() && edit.contains("range")) {
                                 entry.hasRange = true;
                                 entry.rangeStart.line = edit["range"]["start"].value("line", 0);
                                 entry.rangeStart.character = edit["range"]["start"].value("character", 0);
                                 entry.rangeEnd.line = edit["range"]["end"].value("line", 0);
                                 entry.rangeEnd.character = edit["range"]["end"].value("character", 0);
                                 entry.insertText = edit.value("newText", entry.insertText);
                             }
                             g_completion.push_back(entry);
                         }
                         if (g_completion.empty()) return;
                         std::stable_sort(g_completion.begin(), g_completion.end(),
                                          [](const CompletionItem& a, const CompletionItem& b) {
                                              return a.sortText < b.sortText;
                                          });

                         std::string list;
                         for (const CompletionItem& item : g_completion) {
                             if (!list.empty()) list += "\n";
                             list += item.label;
                         }
                         const Sci_Position here = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
                         const Sci_Position wordStart = static_cast<Sci_Position>(
                             sci(SCI_WORDSTARTPOSITION, static_cast<WPARAM>(here), 1));
                         sci(SCI_AUTOCSETSEPARATOR, '\n');
                         sci(SCI_AUTOCSETIGNORECASE, 1);
                         // The server already ranked the list; keep its order.
                         sci(SCI_AUTOCSETORDER, SC_ORDER_CUSTOM);
                         sci(SCI_AUTOCSETMAXHEIGHT, 12);
                         sci(SCI_AUTOCSHOW, static_cast<WPARAM>(here - wordStart),
                             reinterpret_cast<LPARAM>(list.c_str()));
                     });
}

void applyCompletion(const SCNotification* notify) {
    const std::string chosen = notify->text != nullptr ? notify->text : "";
    const auto it = std::find_if(g_completion.begin(), g_completion.end(),
                                 [&](const CompletionItem& c) { return c.label == chosen; });
    if (it == g_completion.end()) return;

    // Cancel Scintilla's own insertion; the server's edit is the authority.
    sci(SCI_AUTOCCANCEL);
    const std::string text = bufferText();
    size_t start = static_cast<size_t>(notify->position);
    size_t end = static_cast<size_t>(sci(SCI_GETCURRENTPOS));
    if (it->hasRange) {
        start = px::positionToOffset(text, it->rangeStart);
        end = px::positionToOffset(text, it->rangeEnd);
        if (end < static_cast<size_t>(sci(SCI_GETCURRENTPOS))) end = static_cast<size_t>(sci(SCI_GETCURRENTPOS));
    }
    sci(SCI_BEGINUNDOACTION);
    sci(SCI_SETTARGETRANGE, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
    sci(SCI_REPLACETARGET, static_cast<WPARAM>(it->insertText.size()),
        reinterpret_cast<LPARAM>(it->insertText.c_str()));
    sci(SCI_ENDUNDOACTION);
    sci(SCI_GOTOPOS, static_cast<WPARAM>(start + it->insertText.size()));
}

void gotoDefinition() {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const std::string text = bufferText();
    const px::Position pos =
        px::offsetToPosition(text, static_cast<size_t>(sci(SCI_GETCURRENTPOS)));

    g_client.request("textDocument/definition",
                     {{"textDocument", {{"uri", doc->uri}}},
                      {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [](const Json& result, const Json& error) {
                         if (!error.is_null() || result.is_null()) return;
                         Json first = result.is_array() ? (result.empty() ? Json() : result[0]) : result;
                         if (!first.is_object()) return;
                         const size_t count = result.is_array() ? result.size() : 1;
                         const std::string uri = first.contains("targetUri")
                                                     ? first.value("targetUri", std::string())
                                                     : first.value("uri", std::string());
                         const Json range = first.contains("targetSelectionRange") ? first["targetSelectionRange"]
                                                                                   : first.value("range", Json());
                         if (uri.empty() || !range.is_object()) return;

                         const std::wstring path = uriToPath(uri);
                         ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
                         px::Position target;
                         target.line = range["start"].value("line", 0);
                         target.character = range["start"].value("character", 0);
                         const size_t offset = px::positionToOffset(bufferText(), target);
                         sci(SCI_GOTOPOS, static_cast<WPARAM>(offset));
                         sci(SCI_SCROLLCARET);
                         if (count > 1) {
                             wchar_t line[64];
                             ::swprintf_s(line, L"PX: %zu definitions, opened the first", count);
                             statusBar(line);
                         }
                     });
}

void formatDocument() {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    g_client.request("textDocument/formatting",
                     {{"textDocument", {{"uri", doc->uri}}},
                      {"options", {{"tabSize", 4}, {"insertSpaces", false}}}},
                     [](const Json& result, const Json& error) {
                         if (!error.is_null() || !result.is_array() || result.empty()) return;
                         const std::string text = bufferText();
                         struct Edit {
                             size_t start;
                             size_t end;
                             std::string newText;
                         };
                         std::vector<Edit> edits;
                         for (const Json& e : result) {
                             px::Position s, t;
                             s.line = e["range"]["start"].value("line", 0);
                             s.character = e["range"]["start"].value("character", 0);
                             t.line = e["range"]["end"].value("line", 0);
                             t.character = e["range"]["end"].value("character", 0);
                             edits.push_back({px::positionToOffset(text, s), px::positionToOffset(text, t),
                                              e.value("newText", std::string())});
                         }
                         // End-first, so an earlier edit's offsets stay valid.
                         std::sort(edits.begin(), edits.end(),
                                   [](const Edit& a, const Edit& b) { return a.start > b.start; });
                         sci(SCI_BEGINUNDOACTION);
                         for (const Edit& e : edits) {
                             sci(SCI_SETTARGETRANGE, static_cast<WPARAM>(e.start), static_cast<LPARAM>(e.end));
                             sci(SCI_REPLACETARGET, static_cast<WPARAM>(e.newText.size()),
                                 reinterpret_cast<LPARAM>(e.newText.c_str()));
                         }
                         sci(SCI_ENDUNDOACTION);
                     });
}

void scopeAtCaret() {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const Sci_Position caret = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    const px::Position pos = px::offsetToPosition(bufferText(), static_cast<size_t>(caret));

    g_client.request("paradox/scopeAt",
                     {{"uri", doc->uri}, {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [caret](const Json& result, const Json& error) {
                         if (!error.is_null() || result.is_null()) return;
                         // scopes is an array, never one name: several stay
                         // ambiguous and none is a first-class "unknown".
                         std::string scopes;
                         for (const Json& s : result.value("scopes", Json::array())) {
                             if (!scopes.empty()) scopes += "|";
                             scopes += s.get<std::string>();
                         }
                         if (scopes.empty()) scopes = "unknown";
                         std::string tip = "scope: " + scopes;
                         const Json saved = result.value("savedScopes", Json::array());
                         if (!saved.empty()) {
                             tip += "\nsaved scopes:";
                             for (const Json& s : saved) tip += "\n  " + s.value("name", std::string());
                         }
                         showCalltip(caret, tip);
                     });
}

void insertSnippet() {
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const px::Position pos =
        px::offsetToPosition(bufferText(), static_cast<size_t>(sci(SCI_GETCURRENTPOS)));

    g_client.request("paradox/snippets",
                     {{"uri", doc->uri}, {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [](const Json& result, const Json& error) {
                         if (!error.is_null()) return;
                         g_snippets.clear();
                         std::string list;
                         for (const Json& s : result.value("snippets", Json::array())) {
                             const std::string label = s.value("label", std::string());
                             if (label.empty() || label.find('\n') != std::string::npos) continue;
                             // "plain" is the tabstop-free form, guaranteed
                             // free of "${". Scintilla cannot expand tabstops.
                             g_snippets.emplace_back(label, s.value("plain", std::string()));
                             if (!list.empty()) list += "\n";
                             list += label;
                         }
                         if (g_snippets.empty()) {
                             statusBar(L"PX: no snippets here");
                             return;
                         }
                         sci(SCI_AUTOCSETSEPARATOR, '\n');
                         sci(SCI_USERLISTSHOW, 1, reinterpret_cast<LPARAM>(list.c_str()));
                     });
}

void applySnippet(const SCNotification* notify) {
    const std::string chosen = notify->text != nullptr ? notify->text : "";
    const auto it = std::find_if(g_snippets.begin(), g_snippets.end(),
                                 [&](const std::pair<std::string, std::string>& s) { return s.first == chosen; });
    if (it == g_snippets.end()) return;
    sci(SCI_BEGINUNDOACTION);
    sci(SCI_INSERTTEXT, static_cast<WPARAM>(sci(SCI_GETCURRENTPOS)),
        reinterpret_cast<LPARAM>(it->second.c_str()));
    sci(SCI_GOTOPOS, static_cast<WPARAM>(sci(SCI_GETCURRENTPOS) + static_cast<LRESULT>(it->second.size())));
    sci(SCI_ENDUNDOACTION);
}

// ------------------------------------------------------------- menu commands

void cmdComplete() { requestCompletion(); }
void cmdDefinition() { gotoDefinition(); }
void cmdFormat() { formatDocument(); }
void cmdScopeAt() { scopeAtCaret(); }
void cmdInsertSnippet() { insertSnippet(); }

void cmdReloadDocs() {
    if (!g_client.running()) return;
    g_client.request("paradox/reloadDocs", {{"force", true}}, [](const Json& result, const Json& error) {
        if (!error.is_null()) return;
        wchar_t line[64];
        ::swprintf_s(line, L"PX: %lld tokens after reload", result.value("tokens", 0LL));
        statusBar(line);
    });
}

void cmdShowLog() {
    if (g_logFile != nullptr) ::fflush(g_logFile);
    ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(g_logPath.c_str()));
}

void cmdOpenSettings() {
    ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(g_iniPath.c_str()));
}

void cmdRestart() {
    g_settings = px::loadSettings(g_iniPath);
    const std::wstring mod = g_startedForMod;
    g_client.stop();
    g_docs.clear();
    g_diags.clear();
    startServer(mod);
    openDocument(currentPath());
}

// --------------------------------------------------------------- window sink

LRESULT CALLBACK sinkProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PX_LSP) {
        g_client.drain();
        return 0;
    }
    if (msg == WM_TIMER && w == TIMER_SYNC) {
        ::KillTimer(hwnd, TIMER_SYNC);
        flushChange(currentPath());
        return 0;
    }
    return ::DefWindowProc(hwnd, msg, w, l);
}

void createSink(HINSTANCE instance) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = sinkProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    ::RegisterClassW(&wc);
    g_sink = ::CreateWindowExW(0, kWindowClass, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
}

HINSTANCE g_instance = nullptr;

void setUpMenu() {
    const struct {
        const wchar_t* name;
        PFUNCPLUGINCMD fn;
        ShortcutKey* key;
    } items[CMD_COUNT] = {
        {L"Complete", cmdComplete, &g_keyComplete},
        {L"Go to definition", cmdDefinition, &g_keyDefinition},
        {L"Format document", cmdFormat, nullptr},
        {L"Scope at caret", cmdScopeAt, nullptr},
        {L"Insert snippet", cmdInsertSnippet, nullptr},
        {L"", nullptr, nullptr},  // a null function is what Notepad++ draws as a separator
        {L"Reload script_docs", cmdReloadDocs, nullptr},
        {L"Show server log", cmdShowLog, nullptr},
        {L"Open settings", cmdOpenSettings, nullptr},
        {L"Restart server", cmdRestart, nullptr},
    };
    for (int i = 0; i < CMD_COUNT; ++i) {
        ::wcscpy_s(g_funcs[i]._itemName, items[i].name);
        g_funcs[i]._pFunc = items[i].fn;
        g_funcs[i]._pShKey = items[i].key;
    }
}

void onReady() {
    wchar_t dir[MAX_PATH] = {0};
    ::SendMessage(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(dir));
    g_configDir = dir;
    ::CreateDirectoryW(g_configDir.c_str(), nullptr);
    g_iniPath = g_configDir + L"\\px-toolkit.ini";
    g_logPath = g_configDir + L"\\px-toolkit-server.log";
    g_settings = px::loadSettings(g_iniPath);

    createSink(g_instance);
    setUpIndicators(g_npp._scintillaMainHandle);
    setUpIndicators(g_npp._scintillaSecondHandle);
    ::SendMessage(g_npp._scintillaMainHandle, SCI_SETMOUSEDWELLTIME, 500, 0);
    ::SendMessage(g_npp._scintillaSecondHandle, SCI_SETMOUSEDWELLTIME, 500, 0);
    g_ready = true;
    openDocument(currentPath());
}

bool isIdentifierChar(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

}  // namespace

// ------------------------------------------------------------------- exports

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_instance = static_cast<HINSTANCE>(module);
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data) {
    g_npp = data;
    setUpMenu();
}

extern "C" __declspec(dllexport) const wchar_t* getName() { return kPluginName; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    *count = CMD_COUNT;
    return g_funcs;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }

extern "C" __declspec(dllexport) void beNotified(SCNotification* notify) {
    // Session restoration activates buffers before NPPN_READY. The config and
    // response window must exist before a buffer can start the language server.
    if (!g_ready && notify->nmhdr.code != NPPN_READY) return;
    switch (notify->nmhdr.code) {
        case NPPN_READY:
            onReady();
            break;

        case NPPN_SHUTDOWN:
            g_client.stop();
            if (g_logFile != nullptr) {
                ::fclose(g_logFile);
                g_logFile = nullptr;
            }
            if (g_sink != nullptr) ::DestroyWindow(g_sink);
            break;

        case NPPN_FILEOPENED:
        case NPPN_BUFFERACTIVATED: {
            const std::wstring path = pathOfBuffer(notify->nmhdr.idFrom);
            if (path.empty() || lower(path) != lower(currentPath())) break;
            openDocument(path);
            DocState* doc = docFor(path);
            if (doc != nullptr) applyDiagnostics(doc->uri);
            break;
        }

        case NPPN_FILESAVED: {
            const std::wstring path = pathOfBuffer(notify->nmhdr.idFrom);
            DocState* doc = docFor(path);
            if (doc == nullptr) {
                openDocument(path);
                break;
            }
            flushChange(path);
            // The server re-reads the file's BOM from disk on save, so this is
            // more than a re-validation trigger.
            g_client.notify("textDocument/didSave", {{"textDocument", {{"uri", doc->uri}}}});
            break;
        }

        case NPPN_FILEBEFORECLOSE:
            closeDocument(pathOfBuffer(notify->nmhdr.idFrom));
            break;

        case SCN_MODIFIED:
            if ((notify->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0) {
                DocState* doc = docFor(currentPath());
                if (doc != nullptr && g_sink != nullptr) {
                    doc->dirty = true;
                    ::SetTimer(g_sink, TIMER_SYNC, SYNC_DEBOUNCE_MS, nullptr);
                }
            }
            break;

        case SCN_CHARADDED:
            if (isIdentifierChar(notify->ch) && docFor(currentPath()) != nullptr) {
                flushChange(currentPath());
                requestCompletion();
            }
            break;

        case SCN_AUTOCSELECTION:
            applyCompletion(notify);
            break;

        case SCN_USERLISTSELECTION:
            if (notify->listType == 1) applySnippet(notify);
            break;

        case SCN_DWELLSTART:
            if (docFor(currentPath()) != nullptr) {
                g_dwelling = true;
                requestHover(notify->position);
            }
            break;

        case SCN_DWELLEND:
            g_dwelling = false;
            sci(SCI_CALLTIPCANCEL);
            break;

        default:
            break;
    }
}
