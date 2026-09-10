#pragma once

#include <string>
#include <vector>

namespace px {
// Keep syntax styles away from Notepad++'s built-in lexer styles.
enum SyntaxStyle : unsigned char {
    Plain = 0, Comment = 64, String, Number, Operator, Key, Literal, Variable
};

// One style per UTF-8 byte, as required by Scintilla. No game token lists.
std::vector<unsigned char> highlight(const std::string& text, bool localization);
}
