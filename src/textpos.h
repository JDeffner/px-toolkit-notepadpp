// Scintilla byte offsets <-> LSP line/character positions.
//
// Scintilla stores UTF-8 bytes under Notepad++'s Unicode mode; LSP counts
// UTF-16 code units within a line. Neither is the other, and every position
// crossing the wire has to be converted or non-ASCII text drifts.
#pragma once

#include <cstddef>
#include <string>

namespace px {

struct Position {
    int line = 0;       // 0-based
    int character = 0;  // 0-based, UTF-16 code units
};

Position offsetToPosition(const std::string& utf8, size_t offset);
size_t positionToOffset(const std::string& utf8, Position pos);

}  // namespace px
