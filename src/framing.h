// LSP "Content-Length" frame reassembly.
//
// Reads from a pipe arrive in arbitrary chunks: a header split in the middle,
// several frames in one read, a body that needs three reads. feed() takes
// whatever came, next() hands back one complete body at a time.
#pragma once

#include <cstddef>
#include <string>

namespace px {

class FrameParser {
public:
    void feed(const char* data, size_t len);
    bool next(std::string& body);

private:
    std::string buf_;
};

}  // namespace px
