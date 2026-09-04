#include "framing.h"

#include <cctype>
#include <cstdlib>

namespace px {

void FrameParser::feed(const char* data, size_t len) { buf_.append(data, len); }

bool FrameParser::next(std::string& body) {
    const size_t headerEnd = buf_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    size_t contentLength = 0;
    bool haveLength = false;
    size_t lineStart = 0;
    while (lineStart < headerEnd) {
        size_t lineEnd = buf_.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > headerEnd) lineEnd = headerEnd;
        const size_t colon = buf_.find(':', lineStart);
        if (colon != std::string::npos && colon < lineEnd) {
            std::string name = buf_.substr(lineStart, colon - lineStart);
            // Header names are case-insensitive; only this one carries meaning.
            for (char& c : name) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            if (name == "content-length") {
                contentLength = static_cast<size_t>(::strtoull(buf_.c_str() + colon + 1, nullptr, 10));
                haveLength = true;
            }
        }
        lineStart = lineEnd + 2;
    }

    const size_t bodyStart = headerEnd + 4;
    if (!haveLength) {
        // A frame with no length is unrecoverable: drop the header and resync.
        buf_.erase(0, bodyStart);
        return false;
    }
    if (buf_.size() < bodyStart + contentLength) return false;

    body.assign(buf_, bodyStart, contentLength);
    buf_.erase(0, bodyStart + contentLength);
    return true;
}

}  // namespace px
