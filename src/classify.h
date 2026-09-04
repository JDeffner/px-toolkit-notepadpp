// Which files the plugin treats as Paradox script, and which mod they belong to.
#pragma once

#include <functional>
#include <string>

namespace px {

enum class Lang { None, Script, Loc, Gui };

struct FileClass {
    Lang lang = Lang::None;
    std::wstring modRoot;  // empty when the file sits under no mod root
};

// True when `dir` is a mod root. Injected so the classification can be tested
// without a filesystem; the plugin passes isModRootOnDisk.
using ModRootProbe = std::function<bool(const std::wstring& dir)>;

// A mod root holds descriptor.mod (CK3) or a .metadata folder (Victoria 3, EU5).
bool isModRootOnDisk(const std::wstring& dir);

FileClass classify(const std::wstring& filePath, const ModRootProbe& probe);

// The language ids the server keys didOpen on. Never anything else.
const char* languageId(Lang lang);

}  // namespace px
