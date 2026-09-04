#include "classify.h"

#include <algorithm>
#include <cwctype>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace px {
namespace {

// The folder names that hold script in a mod, as the union of the first path
// segment of every entry in the three schema tables of packages/server/src/
// games/<ck3|vic3|eu5>/ (EU5's in_game/ main_menu/ loading_screen/ prefixes
// stripped, since those only nest the same folders). "gui" and "localization"
// are not here: they are matched by extension instead.
const wchar_t* const kScriptFolders[] = {
    L"common", L"events", L"gfx", L"history", L"map", L"setup",
};

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return s;
}

std::wstring normalize(const std::wstring& path) {
    std::wstring out = path;
    std::replace(out.begin(), out.end(), L'\\', L'/');
    while (out.size() > 1 && out.back() == L'/') out.pop_back();
    return out;
}

std::wstring parentOf(const std::wstring& path) {
    const size_t slash = path.rfind(L'/');
    if (slash == std::wstring::npos) return std::wstring();
    // "F:/x" -> "F:", and "F:" has no parent, which ends the walk.
    return path.substr(0, slash);
}

std::wstring extensionOf(const std::wstring& path) {
    const size_t slash = path.rfind(L'/');
    const size_t dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return std::wstring();
    if (slash != std::wstring::npos && dot < slash) return std::wstring();
    return lower(path.substr(dot));
}

// The directory segments of `path` below `root`, excluding the file name.
std::vector<std::wstring> dirSegmentsBelow(const std::wstring& path, const std::wstring& root) {
    std::vector<std::wstring> segments;
    std::wstring rel = path.substr(root.size());
    if (!rel.empty() && rel.front() == L'/') rel.erase(0, 1);
    size_t start = 0;
    while (true) {
        const size_t slash = rel.find(L'/', start);
        if (slash == std::wstring::npos) break;  // the tail is the file name
        segments.push_back(lower(rel.substr(start, slash - start)));
        start = slash + 1;
    }
    return segments;
}

bool hasSegment(const std::vector<std::wstring>& segments, const std::wstring& name) {
    return std::find(segments.begin(), segments.end(), name) != segments.end();
}

}  // namespace

bool isModRootOnDisk(const std::wstring& dir) {
#ifdef _WIN32
    const DWORD descriptor = ::GetFileAttributesW((dir + L"\\descriptor.mod").c_str());
    if (descriptor != INVALID_FILE_ATTRIBUTES && !(descriptor & FILE_ATTRIBUTE_DIRECTORY)) return true;
    const DWORD metadata = ::GetFileAttributesW((dir + L"\\.metadata").c_str());
    return metadata != INVALID_FILE_ATTRIBUTES && (metadata & FILE_ATTRIBUTE_DIRECTORY);
#else
    (void)dir;
    return false;
#endif
}

FileClass classify(const std::wstring& filePath, const ModRootProbe& probe) {
    FileClass result;
    const std::wstring path = normalize(filePath);
    if (path.find(L'/') == std::wstring::npos) return result;

    std::wstring root;
    for (std::wstring dir = parentOf(path); !dir.empty(); dir = parentOf(dir)) {
        if (probe(dir)) {
            root = dir;
            break;
        }
    }
    if (root.empty()) return result;

    const std::wstring ext = extensionOf(path);
    const std::vector<std::wstring> segments = dirSegmentsBelow(path, root);

    if (ext == L".gui") {
        result.lang = Lang::Gui;
    } else if (ext == L".yml" && hasSegment(segments, L"localization")) {
        result.lang = Lang::Loc;
    } else if (ext == L".txt") {
        for (const wchar_t* folder : kScriptFolders) {
            if (hasSegment(segments, folder)) {
                result.lang = Lang::Script;
                break;
            }
        }
    }

    if (result.lang != Lang::None) result.modRoot = root;
    return result;
}

const char* languageId(Lang lang) {
    switch (lang) {
        case Lang::Script: return "paradox";
        case Lang::Loc: return "paradox-loc";
        case Lang::Gui: return "paradox-gui";
        default: return "";
    }
}

}  // namespace px
