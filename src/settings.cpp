#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <fstream>

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
    s.automaticCompletion = readValue(iniPath, L"automaticCompletion") != L"0";
    s.signatureHelp = readValue(iniPath, L"signatureHelp") != L"0";
    s.syntaxHighlighting = readValue(iniPath, L"syntaxHighlighting") != L"0";
    s.semanticHighlighting = readValue(iniPath, L"semanticHighlighting") != L"0";
    s.folding = readValue(iniPath, L"folding") != L"0";
    s.autoUpdateServer = readValue(iniPath, L"autoUpdateServer") != L"0";
    s.completionMode = readValue(iniPath, L"completionMode");
    if (s.completionMode != L"examples" && s.completionMode != L"names") s.completionMode = L"minimal";
    s.hoverDetail = readValue(iniPath, L"hoverDetail");
    if (s.hoverDetail != L"compact" && s.hoverDetail != L"full") s.hoverDetail = L"standard";
    return s;
}

bool saveSettings(const std::wstring& iniPath, const Settings& s) {
    // Win32 preserves Unicode values only when the INI has a UTF-16 BOM.
    std::ifstream input(iniPath, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    if (bytes.compare(0, 2, "\xff\xfe") != 0) {
        const bool utf8 = bytes.compare(0, 3, "\xef\xbb\xbf") == 0;
        const auto content = utf8 ? bytes.substr(3) : bytes;
        const int count = MultiByteToWideChar(utf8 ? CP_UTF8 : CP_ACP, 0, content.data(), static_cast<int>(content.size()), nullptr, 0);
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(utf8 ? CP_UTF8 : CP_ACP, 0, content.data(), static_cast<int>(content.size()), wide.data(), count);
        const std::wstring temporary = iniPath + L".pending";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write("\xff\xfe", 2); output.write(reinterpret_cast<const char*>(wide.data()), wide.size() * sizeof(wchar_t)); output.close();
        if (!output || !MoveFileExW(temporary.c_str(), iniPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(temporary.c_str()); return false; }
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, iniPath.c_str());
    }
    bool ok = true;
    auto write = [&](const wchar_t* key, const std::wstring& value) {
        if (!::WritePrivateProfileStringW(kSection, key, value.c_str(), iniPath.c_str())) ok = false;
    };
    write(L"serverCommand", s.serverCommand); write(L"gameId", s.gameId);
    write(L"gamePath", s.gamePath); write(L"logsPath", s.logsPath); write(L"locLanguage", s.locLanguage);
    write(L"completionMode", s.completionMode); write(L"hoverDetail", s.hoverDetail);
    write(L"automaticCompletion", s.automaticCompletion ? L"1" : L"0");
    write(L"signatureHelp", s.signatureHelp ? L"1" : L"0");
    write(L"syntaxHighlighting", s.syntaxHighlighting ? L"1" : L"0");
    write(L"semanticHighlighting", s.semanticHighlighting ? L"1" : L"0");
    write(L"folding", s.folding ? L"1" : L"0");
    write(L"autoUpdateServer", s.autoUpdateServer ? L"1" : L"0");
    return ok;
}

}  // namespace px
