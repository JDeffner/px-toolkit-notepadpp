// px-toolkit.ini in the plugin config dir.
#pragma once

#include <string>

namespace px {

struct Settings {
    std::wstring serverCommand;  // empty = look for px-lsp.cmd / px-lsp on PATH
    std::wstring gameId;         // ck3 | vic3 | eu5
    std::wstring gamePath;
    std::wstring logsPath;
    std::wstring locLanguage;
};

// Reads the ini, writing it with defaults first when it does not exist.
Settings loadSettings(const std::wstring& iniPath);

}  // namespace px
