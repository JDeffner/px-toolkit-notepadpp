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
#include <filesystem>
#include <share.h>
#include <shellapi.h>
#include <stdexcept>

#include "../sdk/PluginInterface.h"
#include "classify.h"
#include "highlight.h"
#include "lspclient.h"
#include "lspfeatures.h"
#include "panel.h"
#include "markdown.h"
#include "settings.h"
#include "textpos.h"

using px::Json;

namespace {

const wchar_t kPluginName[] = L"Paradox Modding Toolkit";
const wchar_t kWindowClass[] = L"PxToolkitLspSink";
const UINT WM_PX_LSP = WM_APP + 1;
const UINT_PTR TIMER_SYNC = 1;
const UINT_PTR TIMER_FEATURES = 2;
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
    CMD_PANEL,
    CMD_REFERENCES,
    CMD_SYMBOLS,
    CMD_ACTIONS,
    CMD_RENAME,
    CMD_SIGNATURE,
    CMD_OPTIONS,
    CMD_COUNT
};

struct DocState {
    std::string uri;
    std::wstring modRoot;
    px::Lang lang = px::Lang::None;
    int version = 1;
    bool dirty = false;
    bool opened = false;
    uptr_t buffer = 0;
    LRESULT pointer = 0;
    std::string text;
    std::vector<px::SemanticSpan> semantic;
    int semanticVersion = -1;
};

struct Diagnostic {
    px::Position start;
    px::Position end;
    int severity = 1;
    std::string message;
    Json raw;
};

struct CompletionItem {
    std::string label;
    std::string sortText;
    std::string insertText;
    Json textEdit;
};

NppData g_npp;
FuncItem g_funcs[CMD_COUNT];
ShortcutKey g_keyComplete = {true, false, false, VK_SPACE};
ShortcutKey g_keyDefinition = {false, false, false, VK_F12};
ShortcutKey g_keyReferences = {false, false, true, VK_F12};
ShortcutKey g_keyRename = {false, false, false, VK_F2};
ShortcutKey g_keySignature = {true, false, true, VK_SPACE};

HWND g_sink = nullptr;
std::wstring g_configDir;
std::wstring g_iniPath;
std::wstring g_logPath;
px::Settings g_settings;
px::LspClient g_client;
FILE* g_logFile = nullptr;
bool g_serverMissingReported = false;
bool g_ready = false;
bool g_initialized = false;
bool g_applying = false;
int g_revision = 0;
int g_generation = 0;
unsigned g_featureRequest = 0, g_signatureRequest = 0, g_navigationRequest = 0, g_completionRequest = 0;
Json g_capabilities;
std::vector<std::string> g_legend;
std::wstring g_status = L"PX: open a mod file";
bool g_signatureVisible = false;
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
    if (slashed.size() > 1 && slashed[1] == L':') slashed[0] = static_cast<wchar_t>(towlower(slashed[0]));
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
bool validUtf8(const std::string& text) {
    return text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}

void statusBar(const std::wstring& text) {
    ::SendMessage(g_npp._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE,
                  reinterpret_cast<LPARAM>(text.c_str()));
}

void appendLog(const std::string& line) {
    if (g_logFile == nullptr) {
        g_logFile = ::_wfsopen(g_logPath.c_str(), L"ab", _SH_DENYNO);
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

struct RequestContext {
    std::wstring path;
    int version, generation;
    uptr_t buffer;
    bool valid(bool requireActive = true) const {
        const auto* doc = docFor(path);
        return g_initialized && generation == g_generation && doc && doc->opened && doc->version == version &&
               doc->buffer == buffer && (!requireActive || currentBuffer() == buffer);
    }
};
RequestContext context() {
    auto* doc = docFor(currentPath());
    return {currentPath(), doc ? doc->version : -1, g_generation, currentBuffer()};
}
struct CompletionContext {
    RequestContext document;
    Sci_Position caret = -1;
    unsigned ticket = 0;
    bool valid() const {
        return document.valid() && ticket == g_completionRequest && caret == sci(SCI_GETCURRENTPOS);
    }
};
CompletionContext g_completionContext;
void cancelCompletion() {
    ++g_completionRequest;
    g_completion.clear();
    sci(SCI_AUTOCCANCEL);
}
bool supports(const char* name) {
    if (!g_initialized || !g_capabilities.contains(name)) return false;
    const auto& value = g_capabilities[name];
    return !value.is_null() && value != false;
}
void refreshProblems();
void requestDocumentFeatures();
void requestReferences();
void requestSymbols(const std::wstring& query);
void requestCodeActions();
void requestRename(const std::wstring& name);
void requestSignature();
void showOptions();
void applyPreview();
void navigate(const px::PanelRow& row);
void previewWorkspaceEdit(const Json& edit);
void sendOpen(DocState& doc);
void openDocument(const std::wstring& path);
void resetSession();
void scheduleFeatures() { if (g_sink && !g_applying) ::SetTimer(g_sink, TIMER_FEATURES, 220, nullptr); }

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

const wchar_t* pluginArchitecture() {
#if defined(_M_ARM64)
    return L"arm64";
#elif defined(_M_IX86)
    return L"x86";
#else
    return L"x64";
#endif
}

std::wstring serverCache() {
    wchar_t local[MAX_PATH] = {};
    const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!len || len >= MAX_PATH) return {};
    return std::wstring(local) + L"\\PxToolkit\\servers\\" + pluginArchitecture();
}

std::wstring updatedServer(const std::wstring& dir) {
    const std::wstring cache = serverCache();
    if (cache.empty()) return {};
    // The updater never inherits the LSP pipes and never delays editor startup.
    static bool checked = false;
    if (!checked && g_settings.autoUpdateServer) {
        checked = true;
        wchar_t system[MAX_PATH] = {};
        ::GetSystemDirectoryW(system, MAX_PATH);
        const std::wstring exe = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring command = L"\"" + exe + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + dir + L"\\update-server.ps1\" -Architecture " + pluginArchitecture();
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
    ++g_generation;
    g_initialized = false;
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

    std::wstring nativeMod = modPath;
    std::replace(nativeMod.begin(), nativeMod.end(), L'/', L'\\');
    Json settings = {
        {"gameId", toUtf8(g_settings.gameId)},
        {"gamePath", g_settings.gamePath.empty() ? Json(nullptr) : Json(toUtf8(g_settings.gamePath))},
        {"logsPath", g_settings.logsPath.empty() ? Json(nullptr) : Json(toUtf8(g_settings.logsPath))},
        {"modPath", nativeMod.empty() ? Json(nullptr) : Json(toUtf8(nativeMod))},
        {"locLanguage", toUtf8(g_settings.locLanguage)},
        {"completionMode", toUtf8(g_settings.completionMode)},
        {"hoverDetail", toUtf8(g_settings.hoverDetail)},
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
            {"documentSymbol", {{"hierarchicalDocumentSymbolSupport", true}}},
            {"foldingRange", {{"lineFoldingOnly", true}}},
            {"semanticTokens", {{"requests", {{"full", true}}}, {"tokenTypes", Json::array({"method", "function", "variable", "property", "macro", "event", "enumMember", "string"})}, {"tokenModifiers", Json::array({"defaultLibrary"})}, {"formats", Json::array({"relative"})}}},
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
        const auto capabilities = result.at("capabilities");
        if (!capabilities.is_object()) throw std::runtime_error("Invalid server capabilities.");
        std::vector<std::string> legend;
        const auto semantic = capabilities.value("semanticTokensProvider", Json());
        if (semantic.is_object()) legend = semantic.at("legend").at("tokenTypes").get<std::vector<std::string>>();
        appendLog("px-lsp " + info.value("version", std::string("?")) + " initialized");
        g_capabilities = capabilities;
        g_legend = std::move(legend);
        g_client.notify("initialized", Json::object());
        g_initialized = true;
        for (auto& pair : g_docs) sendOpen(pair.second);
        scheduleFeatures();
        statusBar(L"PX: connected");
    });
}

void ensureServer(const std::wstring& modPath) {
    if (!g_startedForMod.empty() && lower(modPath) != lower(g_startedForMod)) resetSession();
    if (!g_client.running()) startServer(modPath);
}

// -------------------------------------------------------------- document sync

void styleDocument() {
    static bool styling = false;
    if (styling) return;
    const auto fc = px::classify(currentPath(), px::isModRootOnDisk);
    if (fc.lang == px::Lang::None) return;
    styling = true;
    const std::string text = bufferText();
    auto styles = g_settings.syntaxHighlighting ? px::highlight(text, fc.lang == px::Lang::Loc) : std::vector<unsigned char>(text.size(), 0);
    const auto* doc = docFor(currentPath());
    if (g_settings.syntaxHighlighting && g_settings.semanticHighlighting && doc && doc->semanticVersion == doc->version)
        for (const auto& span : doc->semantic) if (span.end <= styles.size()) std::fill(styles.begin() + span.start, styles.begin() + span.end, span.style);
    sci(SCI_STARTSTYLING, 0);
    if (!styles.empty()) sci(SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<LPARAM>(styles.data()));
    styling = false;
}

void setUpSyntax() {
    const COLORREF background = static_cast<COLORREF>(sci(SCI_STYLEGETBACK, STYLE_DEFAULT));
    const bool dark = GetRValue(background) + GetGValue(background) + GetBValue(background) < 384;
    const COLORREF lightColors[] = {
        RGB(82, 113, 63), RGB(153, 66, 21), RGB(132, 62, 151), RGB(86, 86, 86),
        RGB(0, 87, 160), RGB(139, 40, 118), RGB(145, 93, 0)
    };
    const COLORREF darkColors[] = {
        RGB(144, 169, 116), RGB(222, 162, 117), RGB(194, 159, 224), RGB(198, 198, 198),
        RGB(126, 190, 232), RGB(215, 153, 205), RGB(225, 190, 111)
    };
    char font[256] = {};
    sci(SCI_STYLEGETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>(font));
    for (int style = px::Comment; style <= px::Variable; ++style) {
        sci(SCI_STYLESETFONT, style, reinterpret_cast<LPARAM>(font));
        sci(SCI_STYLESETSIZEFRACTIONAL, style, sci(SCI_STYLEGETSIZEFRACTIONAL, STYLE_DEFAULT));
        sci(SCI_STYLESETBACK, style, background);
        sci(SCI_STYLESETFORE, style, (dark ? darkColors : lightColors)[style - px::Comment]);
        sci(SCI_STYLESETBOLD, style, style == px::Key);
        sci(SCI_STYLESETITALIC, style, style == px::Comment);
    }
    const COLORREF semanticLight[] = {RGB(0,90,155), RGB(133,64,133), RGB(144,97,0), RGB(20,109,105), RGB(132,65,0), RGB(151,43,87), RGB(78,80,151), RGB(125,82,25)};
    const COLORREF semanticDark[] = {RGB(113,190,225), RGB(198,155,220), RGB(226,195,125), RGB(112,198,184), RGB(230,176,114), RGB(228,150,168), RGB(178,177,230), RGB(216,186,145)};
    for (int i = 0; i < 8; ++i) {
        sci(SCI_STYLESETFONT, 80 + i, reinterpret_cast<LPARAM>(font));
        sci(SCI_STYLESETSIZEFRACTIONAL, 80 + i, sci(SCI_STYLEGETSIZEFRACTIONAL, STYLE_DEFAULT));
        sci(SCI_STYLESETBACK, 80 + i, background); sci(SCI_STYLESETFORE, 80 + i, (dark ? semanticDark : semanticLight)[i]);
        sci(SCI_STYLESETBOLD, 80 + i, 0); sci(SCI_STYLESETITALIC, 80 + i, 0);
    }
    sci(SCI_SETILEXER, 0, 0); // Container styling, independent of the LSP process.
    styleDocument();
}

void openDocument(const std::wstring& path) {
    // bufferText() reads the current view, so a buffer that is not on screen
    // waits for its NPPN_BUFFERACTIVATED.
    if (path.empty() || lower(path) != lower(currentPath())) return;
    for (auto it = g_docs.begin(); it != g_docs.end();) {
        if (it->second.buffer == currentBuffer() && it->first != lower(path)) {
            if (it->second.opened) g_client.notify("textDocument/didClose", {{"textDocument", {{"uri", it->second.uri}}}});
            g_diags.erase(it->second.uri); it = g_docs.erase(it);
        } else ++it;
    }
    const px::FileClass fc = px::classify(path, px::isModRootOnDisk);
    if (fc.lang == px::Lang::None) {
        px::setPanelRows(px::PanelTab::Outline, {}, L"Open a recognized mod file to see its outline.");
        return;
    }
    setUpSyntax();
    if (!validUtf8(bufferText())) { statusBar(L"PX: convert this file to UTF-8 to use language features"); return; }
    ensureServer(fc.modRoot);
    if (!g_client.running()) return;
    if (auto* existing = docFor(path)) {
        existing->modRoot = fc.modRoot;
        sendOpen(*existing);
        scheduleFeatures(); refreshProblems(); return;
    }

    DocState state;
    state.uri = pathToUri(path);
    state.modRoot = fc.modRoot;
    state.lang = fc.lang;
    state.version = ++g_revision;
    state.buffer = currentBuffer(); state.pointer = sci(SCI_GETDOCPOINTER); state.text = bufferText();
    g_docs[lower(path)] = state;
    sendOpen(g_docs[lower(path)]);
    scheduleFeatures();
    refreshProblems();
}

void sendOpen(DocState& state) {
    if (!g_initialized || state.opened || lower(state.modRoot) != lower(g_startedForMod)) return;
    g_client.notify("textDocument/didOpen",
                    {{"textDocument",
                      {{"uri", state.uri},
                       {"languageId", px::languageId(state.lang)},
                       {"version", state.version},
                       {"text", state.text}}}});
    state.opened = true; state.dirty = false;
}

void flushChange(const std::wstring& path) {
    DocState* doc = docFor(path);
    if (doc == nullptr || !doc->dirty || !g_initialized || !doc->opened) return;
    doc->dirty = false;
    g_client.notify("textDocument/didChange",
                    {{"textDocument", {{"uri", doc->uri}, {"version", doc->version}}},
                     // A content change with no range is a full replacement, and
                     // it is what an editor buffer produces for free.
                     {"contentChanges", Json::array({{{"text", doc->text}}})}});
}

void closeDocument(const std::wstring& path) {
    DocState* doc = docFor(path);
    if (doc == nullptr) return;
    if (doc->opened) g_client.notify("textDocument/didClose", {{"textDocument", {{"uri", doc->uri}}}});
    g_diags.erase(doc->uri);
    g_docs.erase(lower(path));
    refreshProblems();
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
        const auto* source = docFor(uriToPath(uri));
        if (source && params.contains("version") && params["version"].is_number_integer() && params["version"].get<int>() != source->version) return;
        std::vector<Diagnostic> list;
        const auto& diagnostics = params.at("diagnostics");
        if (!diagnostics.is_array()) throw std::runtime_error("Invalid diagnostics list.");
        for (const Json& d : diagnostics) {
            Diagnostic entry;
            entry.start.line = d.at("range").at("start").value("line", 0);
            entry.start.character = d.at("range").at("start").value("character", 0);
            entry.end.line = d.at("range").at("end").value("line", 0);
            entry.end.character = d.at("range").at("end").value("character", 0);
            entry.severity = d.value("severity", 1);
            entry.message = d.value("message", std::string());
            entry.raw = d;
            list.push_back(entry);
        }
        g_diags[uri] = list;
        applyDiagnostics(uri);
        refreshProblems();
    } else if (method == "paradox/status") {
        wchar_t line[128];
        if (params.value("indexing", false)) {
            ::swprintf_s(line, L"PX: indexing...");
        } else {
            ::swprintf_s(line, L"PX: %lld defs, %lld tokens", params.value("definitions", 0LL),
                         params.value("tokens", 0LL));
        }
        g_status = line;
        statusBar(g_status);
        if (!params.value("indexing", false)) scheduleFeatures();
    } else if (method == "$/clientError") {
        appendLog(params.at("message").get<std::string>());
        statusBar(L"PX: language server error. See server log.");
    } else if (method == "$/serverExited") {
        g_initialized = false;
        ++g_generation;
        for (auto& pair : g_docs) { pair.second.opened = false; pair.second.semantic.clear(); pair.second.semanticVersion = -1; }
        cancelCompletion();
        styleDocument();
        px::panelStatus(L"Language server stopped. Use Restart server to reconnect.");
        statusBar(L"PX: server stopped");
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
    if (g_signatureVisible) return;
    const auto request = context();
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
                     [position, prefix, request](const Json& result, const Json& error) {
                         if (!request.valid() || !g_dwelling || g_signatureVisible || !error.is_null()) return;
                         const std::string body = hoverToText(result);
                         if (prefix.empty() && body.empty()) return;
                         showCalltip(position, prefix + body);
                     });
}

void requestCompletion() {
    cancelCompletion();
    if (!supports("completionProvider") || sci(SCI_GETREADONLY)) return;
    const auto* doc = docFor(currentPath());
    if (!doc || !g_client.running()) return;
    const auto caret = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    const CompletionContext request{context(), caret, g_completionRequest};
    const auto pos = px::offsetToPosition(doc->text, static_cast<size_t>(caret));
    g_client.request("textDocument/completion",
        {{"textDocument", {{"uri", doc->uri}}}, {"position", {{"line", pos.line}, {"character", pos.character}}}},
        [request](const Json& result, const Json& error) {
            if (!request.valid() || !error.is_null() || result.is_null()) return;
            const Json items = result.is_array() ? result : result.at("items");
            if (!items.is_array()) throw std::runtime_error("Invalid completion item list.");
            std::vector<CompletionItem> completion;
            const auto text = bufferText();
            const px::TextPositions positions(text);
            Sci_Position prefixStart = static_cast<Sci_Position>(sci(SCI_WORDSTARTPOSITION, request.caret, 1));
            for (const auto& item : items) {
                CompletionItem entry;
                entry.label = item.at("label").get<std::string>();
                if (entry.label.empty() || entry.label.find_first_of("\r\n") != std::string::npos) continue;
                entry.sortText = item.value("sortText", entry.label);
                entry.insertText = item.value("insertText", entry.label);
                if (item.contains("textEdit")) {
                    entry.textEdit = item.at("textEdit");
                    const auto edits = px::textEdits(positions, Json::array({entry.textEdit}));
                    const auto& edit = edits.front();
                    if (edit.start > static_cast<size_t>(request.caret) || edit.end < static_cast<size_t>(request.caret))
                        throw std::runtime_error("Completion range does not contain its request position.");
                    prefixStart = (std::min)(prefixStart, static_cast<Sci_Position>(edit.start));
                }
                completion.push_back(std::move(entry));
            }
            if (completion.empty()) return;
            std::stable_sort(completion.begin(), completion.end(), [](const auto& a, const auto& b) { return a.sortText < b.sortText; });
            std::string list;
            for (const auto& item : completion) { if (!list.empty()) list += "\n"; list += item.label; }
            g_completion = std::move(completion);
            g_completionContext = request;
            sci(SCI_AUTOCSETSEPARATOR, '\n');
            sci(SCI_AUTOCSETIGNORECASE, 1);
            sci(SCI_AUTOCSETORDER, SC_ORDER_CUSTOM);
            sci(SCI_AUTOCSETMAXHEIGHT, 12);
            sci(SCI_AUTOCSHOW, request.caret - prefixStart, reinterpret_cast<LPARAM>(list.c_str()));
        });
}

void applyCompletion(const SCNotification* notify) {
    const std::string chosen = notify->text ? notify->text : "";
    const auto it = std::find_if(g_completion.begin(), g_completion.end(), [&](const auto& item) { return item.label == chosen; });
    if (it == g_completion.end()) return;
    const auto item = *it; // cancellation and insertion can deliver notifications
    const bool valid = g_completionContext.valid() && !sci(SCI_GETREADONLY);
    cancelCompletion();
    if (!valid) return;
    const auto text = bufferText();
    try {
        Json edit = item.textEdit;
        if (edit.is_null()) {
            const auto start = px::offsetToPosition(text, static_cast<size_t>(notify->position));
            const auto end = px::offsetToPosition(text, static_cast<size_t>(sci(SCI_GETCURRENTPOS)));
            edit = {{"range", {{"start", {{"line", start.line}, {"character", start.character}}},
                              {"end", {{"line", end.line}, {"character", end.character}}}}}, {"newText", item.insertText}};
        }
        const auto edits = px::textEdits(text, Json::array({edit}));
        const auto& e = edits.front();
        sci(SCI_BEGINUNDOACTION);
        sci(SCI_SETTARGETRANGE, e.start, e.end);
        sci(SCI_REPLACETARGET, e.text.size(), reinterpret_cast<LPARAM>(e.text.c_str()));
        sci(SCI_ENDUNDOACTION);
        sci(SCI_GOTOPOS, e.start + e.text.size());
    } catch (const std::exception& e) { appendLog(e.what()); statusBar(L"PX: invalid completion edit. See server log."); }
}

void gotoDefinition() {
    const auto request = context();
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const std::string text = bufferText();
    const px::Position pos =
        px::offsetToPosition(text, static_cast<size_t>(sci(SCI_GETCURRENTPOS)));

    g_client.request("textDocument/definition",
                     {{"textDocument", {{"uri", doc->uri}}},
                      {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [request](const Json& result, const Json& error) {
                         if (!request.valid() || !error.is_null() || result.is_null()) return;
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
                         if (lower(currentPath()) != lower(path)) return;
                         px::Position target;
                         target.line = range.at("start").value("line", 0);
                         target.character = range.at("start").value("character", 0);
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
    if (!supports("documentFormattingProvider") || sci(SCI_GETREADONLY)) return;
    const auto request = context();
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    g_client.request("textDocument/formatting",
                     {{"textDocument", {{"uri", doc->uri}}},
                      {"options", {{"tabSize", 4}, {"insertSpaces", false}}}},
                     [request](const Json& result, const Json& error) {
                         if (!request.valid() || !error.is_null() || !result.is_array() || result.empty()) return;
                         const std::string text = bufferText();
                         std::vector<px::TextEdit> edits;
                         try { edits = px::textEdits(text, result); }
                         catch (const std::exception& e) { statusBar(toWide(e.what())); return; }
                         sci(SCI_BEGINUNDOACTION);
                         for (const auto& e : edits) {
                             sci(SCI_SETTARGETRANGE, static_cast<WPARAM>(e.start), static_cast<LPARAM>(e.end));
                             sci(SCI_REPLACETARGET, static_cast<WPARAM>(e.text.size()),
                                 reinterpret_cast<LPARAM>(e.text.c_str()));
                         }
                         sci(SCI_ENDUNDOACTION);
                     });
}

void scopeAtCaret() {
    flushChange(currentPath());
    const auto request = context();
    if (!request.valid()) return;
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const Sci_Position caret = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    const px::Position pos = px::offsetToPosition(bufferText(), static_cast<size_t>(caret));

    g_client.request("paradox/scopeAt",
                     {{"uri", doc->uri}, {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [request, caret](const Json& result, const Json& error) {
                         if (!request.valid() || !error.is_null() || result.is_null()) return;
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
    flushChange(currentPath());
    const auto request = context();
    if (!request.valid()) return;
    DocState* doc = docFor(currentPath());
    if (doc == nullptr || !g_client.running()) return;
    const px::Position pos =
        px::offsetToPosition(bufferText(), static_cast<size_t>(sci(SCI_GETCURRENTPOS)));

    g_client.request("paradox/snippets",
                     {{"uri", doc->uri}, {"position", {{"line", pos.line}, {"character", pos.character}}}},
                     [request](const Json& result, const Json& error) {
                         if (!request.valid() || !error.is_null()) return;
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

// ---------------------------------------------------------- editor workbench

std::wstring displayPath(const std::string& uri) {
    const auto path = uriToPath(uri);
    auto root = g_startedForMod;
    std::replace(root.begin(), root.end(), L'/', L'\\');
    if (!root.empty() && root.back() != L'\\') root += L'\\';
    if (!root.empty() && lower(path).compare(0, root.size(), lower(root)) == 0)
        return path.substr(root.size());
    return path;
}
px::PanelRow locationRow(const Json& item, const std::wstring& label) {
    px::PanelRow row;
    row.label = label;
    row.uri = item.value("uri", std::string());
    const auto range = item.value("range", Json::object());
    const auto start = range.value("start", Json::object());
    row.position = {start.value("line", 0), start.value("character", 0)};
    row.file = displayPath(row.uri);
    return row;
}
void navigate(const px::PanelRow& row) {
    if (row.uri.compare(0, 8, "file:///") != 0) return;
    const auto path = uriToPath(row.uri);
    ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
    if (lower(currentPath()) != lower(path)) { px::panelStatus(L"The target file could not be opened."); return; }
    sci(SCI_ENSUREVISIBLEENFORCEPOLICY, row.position.line);
    sci(SCI_GOTOPOS, px::positionToOffset(bufferText(), row.position));
    sci(SCI_SCROLLCARET); ::SetFocus(currentScintilla());
}
void refreshProblems() {
    std::vector<px::PanelRow> rows;
    const auto* active = docFor(currentPath());
    for (const auto& pair : g_diags) {
        if (px::currentFileOnly() && (!active || pair.first != active->uri)) continue;
        for (const auto& d : pair.second) {
            if (px::problemSeverity() > 0 && d.severity != px::problemSeverity()) continue;
            const wchar_t* severity = d.severity == 1 ? L"Error" : d.severity == 2 ? L"Warning" : d.severity == 3 ? L"Info" : L"Hint";
            px::PanelRow row;
            row.label = severity; row.detail = toWide(d.message); row.uri = pair.first;
            row.file = displayPath(pair.first); row.position = d.start; row.data = d.raw;
            rows.push_back(std::move(row));
        }
    }
    const auto count = rows.size();
    px::setPanelRows(px::PanelTab::Problems, std::move(rows), std::to_wstring(count) + L" issues from files reported by the server");
}
void addSymbols(const Json& symbols, const std::string& uri, int depth, std::vector<px::PanelRow>& rows) {
    if (!symbols.is_array()) return;
    for (const auto& symbol : symbols) {
        const auto loc = symbol.contains("location") ? symbol["location"] : Json{{"uri", uri}, {"range", symbol.value("selectionRange", symbol.value("range", Json::object()))}};
        auto row = locationRow(loc, std::wstring(depth * 2, L' ') + toWide(symbol.value("name", std::string())));
        row.detail = toWide(symbol.value("detail", std::string()));
        rows.push_back(std::move(row));
        if (symbol.contains("children")) addSymbols(symbol["children"], uri, depth + 1, rows);
    }
}
void applyFolds(const Json& ranges) {
    const int count = static_cast<int>(sci(SCI_GETLINECOUNT));
    const auto levels = px::foldingLevels(count, ranges);
    bool changed = false;
    for (int line = 0; line < count; ++line) if (sci(SCI_GETFOLDLEVEL, line) != levels[line]) { changed = true; break; }
    if (!changed) return;
    std::vector<bool> collapsed(count);
    for (int line = 0; line < count; ++line) collapsed[line] = (sci(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG) && !sci(SCI_GETFOLDEXPANDED, line);
    for (int line = 0; line < count; ++line) {
        const int previous = static_cast<int>(sci(SCI_GETFOLDLEVEL, line));
        if ((previous & SC_FOLDLEVELHEADERFLAG) && !(levels[line] & SC_FOLDLEVELHEADERFLAG))
            sci(SCI_FOLDLINE, line, SC_FOLDACTION_EXPAND);
        sci(SCI_SETFOLDLEVEL, line, levels[line]);
    }
    // Reconcile visibility after edits without discarding the user's collapsed headers.
    for (int line = count - 1; line >= 0; --line) if (levels[line] & SC_FOLDLEVELHEADERFLAG)
        sci(SCI_FOLDLINE, line, collapsed[line] ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
}
void requestDocumentFeatures() {
    for (const auto& pair : g_docs) flushChange(pair.first);
    const auto request = context();
    if (!request.valid()) return;
    const unsigned ticket = ++g_featureRequest;
    const auto* doc = docFor(request.path);
    const Json params{{"textDocument", {{"uri", doc->uri}}}};
    if (supports("documentSymbolProvider")) {
        const auto uri = doc->uri;
        g_client.request("textDocument/documentSymbol", params, [request, ticket, uri](const Json& result, const Json& error) {
            if (!request.valid() || ticket != g_featureRequest) return;
            std::vector<px::PanelRow> rows;
            if (error.is_null()) addSymbols(result, uri, 0, rows);
            const auto count = rows.size();
            px::setPanelRows(px::PanelTab::Outline, std::move(rows), error.is_null() ? std::to_wstring(count) + L" symbols in this file" : L"Outline request failed.");
        });
    }
    if (g_settings.folding && supports("foldingRangeProvider")) {
        // Notepad++ reserves margin 2 for change history and margin 3 for folds.
        // Removing the history mask makes its markers color entire lines.
        constexpr int foldingMargin = 3;
        sci(SCI_SETMARGINTYPEN, foldingMargin, SC_MARGIN_SYMBOL); sci(SCI_SETMARGINMASKN, foldingMargin, SC_MASK_FOLDERS);
        sci(SCI_SETMARGINWIDTHN, foldingMargin, 16); sci(SCI_SETMARGINSENSITIVEN, foldingMargin, 1);
        g_client.request("textDocument/foldingRange", params, [request, ticket](const Json& result, const Json& error) {
            if (request.valid() && ticket == g_featureRequest && error.is_null() && result.is_array()) applyFolds(result);
        });
    } else { sci(SCI_FOLDALL, SC_FOLDACTION_EXPAND); applyFolds(Json::array()); }
    if (g_settings.syntaxHighlighting && g_settings.semanticHighlighting && supports("semanticTokensProvider")) {
        const auto text = doc->text;
        g_client.request("textDocument/semanticTokens/full", params, [request, ticket, text](const Json& result, const Json& error) {
            if (!request.valid() || ticket != g_featureRequest || !error.is_null() || !result.is_object()) return;
            try {
                auto* current = docFor(request.path);
                current->semantic = px::semanticSpans(text, result.value("data", Json::array()), g_legend);
                current->semanticVersion = current->version;
                styleDocument();
            } catch (const std::exception& e) { appendLog(e.what()); }
        });
    }
}
void requestReferences() {
    px::showPanel(px::PanelTab::References);
    for (const auto& pair : g_docs) flushChange(pair.first);
    if (!supports("referencesProvider") || !docFor(currentPath())) { px::setPanelRows(px::PanelTab::References, {}, L"References are not available for the current document."); return; }
    const auto request = context(); const auto ticket = ++g_navigationRequest;
    const auto* doc = docFor(request.path);
    const auto pos = px::offsetToPosition(doc->text, sci(SCI_GETCURRENTPOS));
    px::setPanelRows(px::PanelTab::References, {}, L"Finding references...");
    g_client.request("textDocument/references", {{"textDocument", {{"uri", doc->uri}}}, {"position", {{"line", pos.line}, {"character", pos.character}}}, {"context", {{"includeDeclaration", true}}}},
        [request, ticket](const Json& result, const Json& error) {
            if (!request.valid() || ticket != g_navigationRequest) return;
            std::vector<px::PanelRow> rows;
            if (error.is_null() && result.is_array()) for (const auto& loc : result) rows.push_back(locationRow(loc, L"Reference"));
            const auto count = rows.size();
            px::setPanelRows(px::PanelTab::References, std::move(rows), error.is_null() ? std::to_wstring(count) + L" references (including declarations)" : toWide(error.value("message", std::string("Reference search failed."))));
        });
}
void requestSymbols(const std::wstring& query) {
    px::showPanel(px::PanelTab::Symbols);
    if (!supports("workspaceSymbolProvider")) { px::setPanelRows(px::PanelTab::Symbols, {}, L"Connect to a mod to search symbols."); return; }
    for (const auto& pair : g_docs) flushChange(pair.first);
    const int generation = g_generation; const auto ticket = ++g_navigationRequest;
    px::setPanelRows(px::PanelTab::Symbols, {}, L"Searching symbols...");
    g_client.request("workspace/symbol", {{"query", toUtf8(query)}}, [generation, ticket](const Json& result, const Json& error) {
        if (generation != g_generation || ticket != g_navigationRequest) return;
        std::vector<px::PanelRow> rows;
        if (error.is_null()) addSymbols(result, "", 0, rows);
        const auto count = rows.size();
        px::setPanelRows(px::PanelTab::Symbols, std::move(rows), error.is_null() ? std::to_wstring(count) + L" results; refine the query if needed" : L"Symbol search failed.");
    });
}
void requestSignature() {
    flushChange(currentPath());
    if (!supports("signatureHelpProvider") || !docFor(currentPath())) return;
    const auto request = context(); const auto ticket = ++g_signatureRequest;
    const auto* doc = docFor(request.path);
    const auto caret = sci(SCI_GETCURRENTPOS);
    const auto pos = px::offsetToPosition(doc->text, caret);
    g_client.request("textDocument/signatureHelp", {{"textDocument", {{"uri", doc->uri}}}, {"position", {{"line", pos.line}, {"character", pos.character}}}},
        [request, ticket, caret](const Json& result, const Json& error) {
            if (!request.valid() || ticket != g_signatureRequest || caret != sci(SCI_GETCURRENTPOS) || !error.is_null()) return;
            const auto signatures = result.is_object() ? result.value("signatures", Json::array()) : Json::array();
            if (signatures.empty()) { if (g_signatureVisible) sci(SCI_CALLTIPCANCEL); g_signatureVisible = false; return; }
            const int index = (std::min)(result.value("activeSignature", 0), static_cast<int>(signatures.size() - 1));
            if (index < 0) return;
            const auto& signature = signatures[index];
            std::string body = signature.value("label", std::string());
            const auto docs = signature.value("documentation", Json());
            const std::string docText = docs.is_string() ? docs.get<std::string>() : docs.is_object() ? docs.value("value", std::string()) : "";
            if (!docText.empty()) body += "\n" + px::markdownToPlain(docText);
            g_signatureVisible = true;
            showCalltip(caret, body);
            const auto parameters = signature.value("parameters", Json::array());
            const int active = signature.value("activeParameter", result.value("activeParameter", 0));
            if (active >= 0 && static_cast<size_t>(active) < parameters.size()) {
                const auto label = parameters[active].value("label", Json());
                if (label.is_string()) {
                    const auto part = label.get<std::string>(); const auto start = body.find(part);
                    if (start != std::string::npos) sci(SCI_CALLTIPSETHLT, start, start + part.size());
                }
            }
        });
}

struct PreparedFile {
    std::wstring path;
    std::string uri, before;
    std::vector<px::TextEdit> edits;
    uptr_t buffer = 0;
    bool create = false;
};
std::vector<PreparedFile> g_preview;
int g_previewGeneration = -1;
RequestContext g_editOrigin;
std::map<std::wstring, int> g_editVersions;
FILETIME g_editStarted{};
void invalidatePreview() { g_preview.clear(); px::enableApply(false); }
void beginEditRequest() {
    invalidatePreview();
    for (const auto& pair : g_docs) flushChange(pair.first);
    g_editOrigin = context(); g_editVersions.clear();
    GetSystemTimeAsFileTime(&g_editStarted);
    for (const auto& pair : g_docs) if (pair.second.opened) g_editVersions[pair.first] = pair.second.version;
}
bool editRequestValid() {
    if (!g_editOrigin.valid(false)) return false;
    for (const auto& pair : g_editVersions) { const auto* doc = docFor(pair.first); if (!doc || doc->version != pair.second) return false; }
    return true;
}
bool isEditableModPath(const std::wstring& path) {
    namespace fs = std::filesystem;
    const auto target = lower(fs::weakly_canonical(path).wstring());
    const auto root = lower(fs::weakly_canonical(g_startedForMod).wstring()) + L"\\";
    return target.compare(0, root.size(), root) == 0;
}
void previewWorkspaceEdit(const Json& edit) {
    px::showPanel(px::PanelTab::Actions);
    invalidatePreview();
    if (!editRequestValid()) { px::panelStatus(L"Files changed during the request. Request a fresh preview."); return; }
    const auto originalPath = currentPath();
    g_applying = true;
    try {
        std::vector<PreparedFile> prepared;
        std::vector<px::PanelRow> rows;
        for (const auto& file : px::workspaceEdits(edit)) {
            PreparedFile target;
            target.uri = file.uri; target.path = uriToPath(file.uri); target.create = file.create;
            if (!isEditableModPath(target.path)) throw std::runtime_error("Edits outside the current mod are not allowed.");
            if (target.create) {
                if (std::filesystem::exists(target.path)) throw std::runtime_error("A file to create already exists. Request a fresh fix.");
            } else {
                if (!docFor(target.path)) {
                    WIN32_FILE_ATTRIBUTE_DATA attributes{};
                    if (!GetFileAttributesExW(target.path.c_str(), GetFileExInfoStandard, &attributes) || CompareFileTime(&attributes.ftLastWriteTime, &g_editStarted) > 0)
                        throw std::runtime_error("A closed file changed during the request. Request a fresh preview.");
                }
                ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(target.path.c_str()));
                if (lower(currentPath()) != lower(target.path)) throw std::runtime_error("An edit target could not be opened.");
                if (sci(SCI_GETREADONLY)) throw std::runtime_error("An edit target is read-only.");
                if (edit.contains("documentChanges") && file.version < 0 && sci(SCI_GETMODIFY))
                    throw std::runtime_error("Save the target file before applying an unversioned file edit, then request a fresh fix.");
                target.before = bufferText(); target.buffer = currentBuffer();
                const auto* doc = docFor(target.path);
                if (file.version >= 0 && (!doc || doc->version != file.version)) throw std::runtime_error("An edit target has a different document version.");
            }
            target.edits = px::textEdits(target.before, file.edits);
            for (const auto& e : target.edits) {
                px::PanelRow row; row.uri = target.uri; row.file = displayPath(target.uri);
                row.position = px::offsetToPosition(target.before, e.start);
                row.label = target.create ? L"Create file" : L"Replace text";
                row.detail = L"Before: " + toWide(target.before.substr(e.start, (std::min)(e.end - e.start, size_t(100)))) + L"  After: " + toWide(e.text.substr(0, 150));
                std::replace(row.detail.begin(), row.detail.end(), L'\n', L' '); std::replace(row.detail.begin(), row.detail.end(), L'\r', L' ');
                rows.push_back(std::move(row));
            }
            prepared.push_back(std::move(target));
        }
        g_preview = std::move(prepared); g_previewGeneration = g_generation;
        const auto count = rows.size();
        px::setPanelRows(px::PanelTab::Actions, std::move(rows), std::to_wstring(count) + L" edits. Apply changes existing buffers; new files are created on disk.");
        px::enableApply(count > 0);
    } catch (const std::exception& e) { invalidatePreview(); px::setPanelRows(px::PanelTab::Actions, {}, toWide(e.what())); }
    if (!originalPath.empty()) ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(originalPath.c_str()));
    g_applying = false; scheduleFeatures();
}
void applyPreview() {
    if (g_preview.empty()) return;
    const auto original = currentPath();
    g_applying = true;
    std::vector<std::wstring> created;
    try {
        if (g_previewGeneration != g_generation) throw std::runtime_error("The server restarted. Request a fresh preview.");
        // Preflight all existing buffers before creating files or changing any text.
        for (const auto& file : g_preview) {
            if (!isEditableModPath(file.path)) throw std::runtime_error("The mod path changed.");
            if (file.create) { if (std::filesystem::exists(file.path)) throw std::runtime_error("A new file now exists. Request a fresh preview."); continue; }
            ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(file.path.c_str()));
            if (lower(currentPath()) != lower(file.path) || currentBuffer() != file.buffer || sci(SCI_GETREADONLY) || bufferText() != file.before)
                throw std::runtime_error("A target was changed, closed or made read-only. Nothing was applied.");
        }
        for (const auto& file : g_preview) if (file.create) {
            std::filesystem::create_directories(std::filesystem::path(file.path).parent_path());
            std::string contents = file.before;
            for (const auto& e : file.edits) contents.replace(e.start, e.end - e.start, e.text);
            HANDLE output = ::CreateFileW(file.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not create the new file. Existing buffers were not changed.");
            DWORD written = 0;
            const BOOL ok = ::WriteFile(output, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
            ::CloseHandle(output); created.push_back(file.path);
            if (!ok || written != contents.size()) throw std::runtime_error("Could not write the new file.");
        }
        for (const auto& file : g_preview) if (!file.create) {
            ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(file.path.c_str()));
            sci(SCI_BEGINUNDOACTION);
            for (const auto& e : file.edits) { sci(SCI_SETTARGETRANGE, e.start, e.end); sci(SCI_REPLACETARGET, e.text.size(), reinterpret_cast<LPARAM>(e.text.c_str())); }
            sci(SCI_ENDUNDOACTION); flushChange(file.path);
        }
        for (const auto& path : created) ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
        created.clear(); invalidatePreview();
        px::panelStatus(L"Applied. Save modified tabs when ready. Undo is available separately in each existing file.");
    } catch (const std::exception& e) {
        for (const auto& path : created) ::DeleteFileW(path.c_str());
        invalidatePreview(); px::panelStatus(toWide(e.what()));
    }
    if (!original.empty()) ::SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(original.c_str()));
    g_applying = false; scheduleFeatures();
}
void requestCodeActions() {
    px::showPanel(px::PanelTab::Actions); beginEditRequest();
    if (!supports("codeActionProvider") || !docFor(currentPath())) { px::setPanelRows(px::PanelTab::Actions, {}, L"Quick fixes are not available for this document."); return; }
    const auto request = context(); const auto ticket = ++g_navigationRequest;
    const auto* doc = docFor(request.path);
    const auto start = px::offsetToPosition(doc->text, sci(SCI_GETSELECTIONSTART)), end = px::offsetToPosition(doc->text, sci(SCI_GETSELECTIONEND));
    Json diagnostics = Json::array();
    for (const auto& d : g_diags[doc->uri]) if (d.start.line <= end.line && d.end.line >= start.line) diagnostics.push_back(d.raw);
    px::setPanelRows(px::PanelTab::Actions, {}, L"Requesting quick fixes...");
    g_client.request("textDocument/codeAction", {{"textDocument", {{"uri", doc->uri}}}, {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}}, {"context", {{"diagnostics", diagnostics}}}},
        [request, ticket](const Json& result, const Json& error) {
            if (!request.valid() || ticket != g_navigationRequest) return;
            std::vector<px::PanelRow> rows;
            if (error.is_null() && result.is_array()) for (const auto& action : result) {
                px::PanelRow row; row.label = toWide(action.value("title", std::string())); row.data = action;
                row.detail = action.contains("edit") ? L"Open to preview changes" : L"This action requires an unsupported editor command";
                rows.push_back(std::move(row));
            }
            const auto count = rows.size();
            px::setPanelRows(px::PanelTab::Actions, std::move(rows), error.is_null() ? std::to_wstring(count) + L" fixes for the selection; open one to preview" : toWide(error.value("message", std::string("Quick fix request failed."))));
        });
}
void requestRename(const std::wstring& name) {
    px::showPanel(px::PanelTab::Actions);
    if (name.empty()) { px::panelStatus(L"Enter the new name above, then choose Preview rename. The caret selects the original symbol."); return; }
    beginEditRequest();
    if (!supports("renameProvider") || !docFor(currentPath())) { px::panelStatus(L"Rename is not available for this document."); return; }
    const auto request = context(); const auto ticket = ++g_navigationRequest;
    const auto* doc = docFor(request.path); const auto pos = px::offsetToPosition(doc->text, sci(SCI_GETCURRENTPOS));
    const Json params{{"textDocument", {{"uri", doc->uri}}}, {"position", {{"line", pos.line}, {"character", pos.character}}}};
    px::setPanelRows(px::PanelTab::Actions, {}, L"Checking whether this symbol can be renamed...");
    g_client.request("textDocument/prepareRename", params, [request, ticket, params, name](const Json& result, const Json& error) {
        if (!request.valid() || ticket != g_navigationRequest) return;
        if (!error.is_null() || result.is_null()) { px::panelStatus(error.is_null() ? L"This symbol cannot be renamed." : toWide(error.value("message", std::string("Rename refused.")))); return; }
        Json rename = params; rename["newName"] = toUtf8(name);
        g_client.request("textDocument/rename", rename, [request, ticket](const Json& result, const Json& error) {
            if (!request.valid() || ticket != g_navigationRequest) return;
            if (!error.is_null()) { px::panelStatus(toWide(error.value("message", std::string("Rename failed.")))); return; }
            if (result.is_object()) previewWorkspaceEdit(result);
        });
    });
}

// ------------------------------------------------------------- menu commands

void cmdComplete() { flushChange(currentPath()); requestCompletion(); }
void cmdDefinition() { flushChange(currentPath()); gotoDefinition(); }
void cmdFormat() { flushChange(currentPath()); formatDocument(); }
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
    resetSession();
    openDocument(currentPath());
}

void resetSession() {
    invalidatePreview();
    ++g_generation;
    g_initialized = false;
    g_client.stop();
    for (auto& pair : g_docs) {
        pair.second.opened = false;
        pair.second.semantic.clear();
        pair.second.semanticVersion = -1;
    }
    cancelCompletion();
    g_diags.clear();
    g_signatureVisible = false;
    sci(SCI_CALLTIPCANCEL);
    refreshProblems();
}

HANDLE g_updateProcess = nullptr;
void showOptions() {
    px::showOptions(g_settings, [](const px::Settings& settings) {
        if (!px::saveSettings(g_iniPath, settings)) { px::optionsStatus(L"Could not save settings. Check the config folder permissions."); return false; }
        g_settings = settings;
        resetSession(); openDocument(currentPath());
        return true;
    }, [](bool check) {
        if (!check) { ShellExecuteW(g_npp._nppHandle, L"open", L"https://github.com/JDeffner/px-toolkit-notepadpp/releases", nullptr, nullptr, SW_SHOWNORMAL); return; }
        if (!g_settings.serverCommand.empty()) { px::optionsStatus(L"A custom server is selected. Its updates are managed separately."); return; }
        if (g_updateProcess) { px::optionsStatus(L"An update check is already running."); return; }
        const auto dir = moduleDir();
        wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
        const std::wstring exe = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring command = L"\"" + exe + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + dir + L"\\update-server.ps1\" -Force -Architecture " + pluginArchitecture();
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &startup, &process)) { px::optionsStatus(L"Could not start PowerShell. Update manually by installing a plugin package."); return; }
        CloseHandle(process.hThread); g_updateProcess = process.hProcess;
        SetTimer(g_sink, 3, 500, nullptr);
        px::optionsStatus(L"Checking for an LSP update...");
    });
}

void refreshPanel(const std::wstring& query) {
    switch (px::panelTab()) {
    case px::PanelTab::Problems: refreshProblems(); break;
    case px::PanelTab::Outline: requestDocumentFeatures(); break;
    case px::PanelTab::References: requestReferences(); break;
    case px::PanelTab::Symbols: requestSymbols(query); break;
    case px::PanelTab::Actions: requestCodeActions(); break;
    }
}

// --------------------------------------------------------------- window sink

LRESULT CALLBACK sinkProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PX_LSP) {
        g_client.drain();
        return 0;
    }
    if (msg == WM_TIMER && w == TIMER_SYNC) {
        ::KillTimer(hwnd, TIMER_SYNC);
        for (auto& entry : g_docs) flushChange(entry.first);
        return 0;
    }
    if (msg == WM_TIMER && w == TIMER_FEATURES) {
        KillTimer(hwnd, TIMER_FEATURES); requestDocumentFeatures();
        return 0;
    }
    if (msg == WM_TIMER && w == 3 && g_updateProcess && WaitForSingleObject(g_updateProcess, 0) != WAIT_TIMEOUT) {
        KillTimer(hwnd, 3); CloseHandle(g_updateProcess); g_updateProcess = nullptr;
        std::ifstream input(serverCache() + L"\\status.txt");
        std::string result; std::getline(input, result);
        px::optionsStatus(result.empty() ? L"Check finished without a status. See the update.log in the architecture-specific server cache." : toWide(result));
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
        {L"Problems and outline", [] { px::showPanel(px::PanelTab::Problems); refreshProblems(); requestDocumentFeatures(); }, nullptr},
        {L"Find references", requestReferences, &g_keyReferences},
        {L"Workspace symbols", [] { requestSymbols(L""); }, nullptr},
        {L"Quick fixes", requestCodeActions, nullptr},
        {L"Rename symbol", [] { requestRename(L""); }, &g_keyRename},
        {L"Signature help", requestSignature, &g_keySignature},
        {L"Options...", showOptions, nullptr},
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
    px::initPanel(g_instance, g_npp._nppHandle, CMD_PANEL, [](px::PanelAction action, const px::PanelRow* row, const std::wstring& query) {
        switch (action) {
        case px::PanelAction::Navigate:
            if (row && row->data.contains("edit")) previewWorkspaceEdit(row->data["edit"]);
            else if (row && row->data.contains("command")) px::panelStatus(L"This action requires an editor command that is not supported.");
            else if (row) navigate(*row);
            break;
        case px::PanelAction::Refresh: refreshPanel(query); break;
        case px::PanelAction::Search: requestSymbols(query); break;
        case px::PanelAction::Rename: requestRename(query); break;
        case px::PanelAction::Apply: applyPreview(); break;
        case px::PanelAction::Options: showOptions(); break;
        }
    });
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
        case SCN_STYLENEEDED:
            if (notify->nmhdr.hwndFrom == currentScintilla()) styleDocument();
            break;

        case NPPN_WORDSTYLESUPDATED:
        case NPPN_DARKMODECHANGED:
            px::themeOptions();
            openDocument(currentPath());
            break;

        case NPPN_READY:
            onReady();
            break;

        case NPPN_SHUTDOWN:
            g_ready = false;
            px::destroyPanels();
            if (g_updateProcess) { CloseHandle(g_updateProcess); g_updateProcess = nullptr; }
            g_client.stop();
            if (g_logFile != nullptr) {
                ::fclose(g_logFile);
                g_logFile = nullptr;
            }
            if (g_sink != nullptr) ::DestroyWindow(g_sink);
            break;

        case NPPN_FILEOPENED:
        case NPPN_BUFFERACTIVATED: {
            cancelCompletion();
            g_signatureVisible = false; g_dwelling = false;
            sci(SCI_CALLTIPCANCEL);
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
            if (doc->opened) g_client.notify("textDocument/didSave", {{"textDocument", {{"uri", doc->uri}}}});
            break;
        }

        case NPPN_FILEBEFORECLOSE:
            closeDocument(pathOfBuffer(notify->nmhdr.idFrom));
            break;

        case SCN_MODIFIED:
            if ((notify->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0) {
                HWND editor = static_cast<HWND>(notify->nmhdr.hwndFrom);
                const auto pointer = SendMessage(editor, SCI_GETDOCPOINTER, 0, 0);
                for (auto& entry : g_docs) if (entry.second.pointer == pointer) {
                    auto& doc = entry.second;
                    const auto size = SendMessage(editor, SCI_GETLENGTH, 0, 0);
                    const auto bytes = reinterpret_cast<const char*>(SendMessage(editor, SCI_GETCHARACTERPOINTER, 0, 0));
                    const std::string updated = bytes ? std::string(bytes, static_cast<size_t>(size)) : std::string();
                    if (updated == doc.text) break; // cloned views notify for the same document
                    if (!validUtf8(updated)) {
                        if (doc.opened) g_client.notify("textDocument/didClose", {{"textDocument", {{"uri", doc.uri}}}});
                        g_diags.erase(doc.uri);
                        const auto path = entry.first; g_docs.erase(path);
                        invalidatePreview(); statusBar(L"PX: convert this file to UTF-8 to use language features");
                        break;
                    }
                    cancelCompletion();
                    const bool styled = doc.semanticVersion == doc.version;
                    if (styled) px::rebaseSemanticSpans(doc.semantic, doc.text, updated);
                    doc.text = updated; doc.version = ++g_revision; doc.dirty = true;
                    doc.semanticVersion = styled ? doc.version : -1;
                    if (!g_applying) invalidatePreview();
                    SetTimer(g_sink, TIMER_SYNC, SYNC_DEBOUNCE_MS, nullptr);
                    scheduleFeatures();
                    break;
                }
            }
            break;

        case SCN_CHARADDED:
            if (g_settings.automaticCompletion && isIdentifierChar(notify->ch) && docFor(currentPath()) != nullptr) {
                flushChange(currentPath());
                requestCompletion();
            }
            if (g_settings.signatureHelp && (notify->ch == '{' || notify->ch == '(' || notify->ch == ',' || notify->ch == '=')) requestSignature();
            if (notify->ch == ')' || notify->ch == '}' || notify->ch == '\n') { g_signatureVisible = false; sci(SCI_CALLTIPCANCEL); }
            break;

        case SCN_AUTOCSELECTION:
            applyCompletion(notify);
            break;

        case SCN_USERLISTSELECTION:
            if (notify->listType == 1) applySnippet(notify);
            break;

        case SCN_DWELLSTART:
            if (!sci(SCI_CALLTIPACTIVE)) g_signatureVisible = false;
            if (docFor(currentPath()) != nullptr) {
                g_dwelling = true;
                requestHover(notify->position);
            }
            break;

        case SCN_DWELLEND:
            g_dwelling = false;
            if (!g_signatureVisible) sci(SCI_CALLTIPCANCEL);
            break;

        default:
            break;
    }
}
