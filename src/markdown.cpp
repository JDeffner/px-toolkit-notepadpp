#include "markdown.h"

#include <vector>

namespace px {
namespace {

bool isFence(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.compare(i, 3, "```") == 0;
}

bool isRule(const std::string& line) {
    if (line.empty()) return false;
    for (char c : line) {
        if (c != '-' && c != ' ') return false;
    }
    return line.find('-') != std::string::npos;
}

// [text](url) -> text, `code` -> code, **bold** -> bold, *em* -> em.
std::string stripInline(const std::string& line) {
    std::string out;
    for (size_t i = 0; i < line.size();) {
        if (line[i] == '`') {
            ++i;
            continue;
        }
        if (line.compare(i, 2, "**") == 0) {
            i += 2;
            continue;
        }
        if (line[i] == '[') {
            const size_t close = line.find(']', i);
            if (close != std::string::npos && close + 1 < line.size() && line[close + 1] == '(') {
                const size_t paren = line.find(')', close);
                if (paren != std::string::npos) {
                    out += stripInline(line.substr(i + 1, close - i - 1));
                    i = paren + 1;
                    continue;
                }
            }
        }
        out += line[i];
        ++i;
    }
    return out;
}

std::string stripLeadingMarks(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && line[i] == '#') ++i;
    if (i > 0 && i < line.size() && line[i] == ' ') return line.substr(i + 1);
    if (i > 0 && i == line.size()) return std::string();
    return line;
}

}  // namespace

std::string markdownToPlain(const std::string& markdown) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : markdown) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    lines.push_back(current);

    std::string out;
    bool blankPending = false;
    for (const std::string& raw : lines) {
        if (isFence(raw)) continue;  // the fence goes, the code inside stays
        std::string line = stripInline(stripLeadingMarks(raw));
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || isRule(line)) {
            blankPending = !out.empty();
            continue;
        }
        if (blankPending) {
            out += "\n";
            blankPending = false;
        }
        if (!out.empty()) out += "\n";
        out += line;
    }
    return out;
}

}  // namespace px
