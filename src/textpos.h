// Scintilla byte offsets <-> LSP line/character positions.
//
// Scintilla stores UTF-8 bytes under Notepad++'s Unicode mode; LSP counts
// UTF-16 code units within a line. Neither is the other, and every position
// crossing the wire has to be converted or non-ASCII text drifts.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace px {

struct Position {
    int line = 0;       // 0-based
    int character = 0;  // 0-based, UTF-16 code units
};

Position offsetToPosition(const std::string& utf8, size_t offset);
size_t positionToOffset(const std::string& utf8, Position pos);

// Checked positions for a fixed text snapshot. Line lookup never scans earlier
// lines; an ordered stream also reuses its last position within the line.
class TextPositions {
public:
    explicit TextPositions(const std::string& text);
    size_t offset(Position pos, bool append = false) const;
private:
    const std::string& text_;
    std::vector<size_t> lines_;
    mutable Position cursor_;
    mutable size_t cursorOffset_ = 0;
};

}  // namespace px
