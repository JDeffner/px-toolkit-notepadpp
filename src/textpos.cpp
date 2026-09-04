#include "textpos.h"

namespace px {
namespace {

// UTF-16 code units a UTF-8 sequence starting with this lead byte occupies.
// A continuation byte occupies none: it belongs to the unit already counted.
int unitsForLeadByte(unsigned char b) {
    if (b < 0x80) return 1;
    if (b < 0xC0) return 0;   // continuation
    if (b < 0xF0) return 1;   // 2- and 3-byte sequences: one UTF-16 unit
    return 2;                 // 4-byte sequences: a surrogate pair
}

}  // namespace

Position offsetToPosition(const std::string& utf8, size_t offset) {
    Position pos;
    const size_t end = offset < utf8.size() ? offset : utf8.size();
    for (size_t i = 0; i < end; ++i) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        if (b == '\n') {
            pos.line += 1;
            pos.character = 0;
        } else if (b != '\r') {
            pos.character += unitsForLeadByte(b);
        }
    }
    return pos;
}

size_t positionToOffset(const std::string& utf8, Position pos) {
    size_t i = 0;
    for (int line = 0; line < pos.line && i < utf8.size(); ++i) {
        if (utf8[i] == '\n') line += 1;
    }
    int units = 0;
    while (i < utf8.size() && units < pos.character) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        if (b == '\n' || b == '\r') break;  // a character past the end of the line clamps
        units += unitsForLeadByte(b);
        ++i;
        // Swallow the continuation bytes of this sequence.
        while (i < utf8.size() && unitsForLeadByte(static_cast<unsigned char>(utf8[i])) == 0) ++i;
    }
    return i;
}

}  // namespace px
