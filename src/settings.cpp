#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace px {
namespace {

const wchar_t kSection[] = L"px-toolkit";

std::wstring readValue(const std::wstring& ini, const wchar_t* key) {
    wchar_t buf[1024] = {0};
    ::GetPrivateProfileStringW(kSection, key, L"", buf, ARRAYSIZE(buf), ini.c_str());
    return std::wstring(buf);
}

void writeDefaults(const std::wstring& ini) {
    // One commented template rather than bare keys: the file IS the
    // documentation of what the server accepts.
    FILE* f = nullptr;
    if (::_wfopen_s(&f, ini.c_str(), L"wb") != 0 || f == nullptr) return;
    static const char kTemplate[] =
        "; Paradox Modding Toolkit for Notepad++\r\n"
        "; Edit, then use Plugins > Paradox Modding Toolkit > Restart server.\r\n"
        "\r\n"
        "[px-toolkit]\r\n"
        "; Command that starts the language server. Empty = the bundled\r\n"
        "; px-lsp\\px-lsp.cmd next to the DLL, else px-lsp on PATH.\r\n"
        "serverCommand=\r\n"
        "; ck3, vic3 or eu5. One server instance serves one game.\r\n"
        "gameId=ck3\r\n"
        "; The game's game/ folder, source of vanilla definitions.\r\n"
        "gamePath=\r\n"
        "; Folder holding the script_docs dumps the game writes.\r\n"
        "logsPath=\r\n"
        "locLanguage=english\r\n";
    ::fwrite(kTemplate, 1, sizeof(kTemplate) - 1, f);
    ::fclose(f);
}

}  // namespace

Settings loadSettings(const std::wstring& iniPath) {
    if (::GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) writeDefaults(iniPath);

    Settings s;
    s.serverCommand = readValue(iniPath, L"serverCommand");
    s.gameId = readValue(iniPath, L"gameId");
    s.gamePath = readValue(iniPath, L"gamePath");
    s.logsPath = readValue(iniPath, L"logsPath");
    s.locLanguage = readValue(iniPath, L"locLanguage");
    if (s.gameId.empty()) s.gameId = L"ck3";
    if (s.locLanguage.empty()) s.locLanguage = L"english";
    return s;
}

}  // namespace px
