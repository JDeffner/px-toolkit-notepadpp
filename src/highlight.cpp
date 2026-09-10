#include "highlight.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace px {
std::vector<unsigned char> highlight(const std::string& text, bool localization) {
    std::vector<unsigned char> styles(text.size(), Plain);
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    auto op = [localization](char c) {
        return c == '=' || c == '{' || c == '}' || c == '<' || c == '>' || c == '!' ||
               c == '[' || c == ']' || (localization && c == ':');
    };
    for (size_t i = 0; i < text.size();) {
        const size_t start = i;
        unsigned char style = Plain;
        if (space(text[i])) { ++i; continue; }
        if (text[i] == '#') {
            style = Comment;
            while (i < text.size() && text[i] != '\r' && text[i] != '\n') ++i;
        } else if (text[i] == '"') {
            style = String;
            ++i;
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) { i += 2; continue; }
                if (text[i++] == '"') break;
            }
        } else if (op(text[i])) {
            style = Operator;
            ++i;
        } else {
            while (i < text.size() && !space(text[i]) && !op(text[i]) && text[i] != '#' && text[i] != '"') ++i;
            const std::string token = text.substr(start, i - start);
            size_t next = i;
            while (next < text.size() && space(text[next])) ++next;
            char* end = nullptr;
            std::strtod(token.c_str(), &end);
            const bool numericStart = std::isdigit(static_cast<unsigned char>(token[0])) ||
                                      token[0] == '-' || token[0] == '+' || token[0] == '.';
            if (token[0] == '@') style = Variable;
            else if (next < text.size() && (text[next] == '=' || text[next] == '<' || text[next] == '>' ||
                                           (localization && text[next] == ':'))) style = Key;
            else if (token == "yes" || token == "no" || token == "true" || token == "false") style = Literal;
            else if (numericStart && end != token.c_str() && *end == '\0') style = Number;
        }
        std::fill(styles.begin() + start, styles.begin() + i, style);
    }
    return styles;
}
}
