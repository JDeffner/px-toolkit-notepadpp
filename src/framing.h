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
    static constexpr size_t maxHeaderBytes = 8192;
    static constexpr size_t maxBodyBytes = 32 * 1024 * 1024;
    // Malformed or oversized input throws; the transport must end the session.
    void feed(const char* data, size_t len);
    bool next(std::string& body);

private:
    std::string buf_;
    size_t bodyStart_ = 0;
    size_t contentLength_ = 0;
};

}  // namespace px
