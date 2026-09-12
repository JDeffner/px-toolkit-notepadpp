#include "framing.h"

#include <cctype>
#include <stdexcept>
#include <string_view>

namespace px {

void FrameParser::feed(const char* data, size_t len) {
    // The reader drains after every 8 KiB read. Allow one read beyond a frame.
    constexpr size_t maxBuffered = maxBodyBytes + maxHeaderBytes + 8192;
    if (len > maxBuffered - buf_.size()) throw std::runtime_error("LSP input buffer limit exceeded.");
    buf_.append(data, len);
}

bool FrameParser::next(std::string& body) {
    if (bodyStart_ == 0) {
        const size_t headerEnd = buf_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            if (buf_.size() > maxHeaderBytes) throw std::runtime_error("LSP header is too large.");
            return false;
        }
        if (headerEnd + 4 > maxHeaderBytes) throw std::runtime_error("LSP header is too large.");
        bool haveLength = false;
        size_t length = 0;
        for (size_t start = 0; start < headerEnd;) {
            const size_t end = buf_.find("\r\n", start);
            const size_t colon = buf_.find(':', start);
            if (colon == std::string::npos || colon >= end) throw std::runtime_error("Malformed LSP header.");
            std::string name = buf_.substr(start, colon - start);
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "content-length") {
                if (haveLength) throw std::runtime_error("Duplicate LSP Content-Length.");
                std::string_view value(buf_.data() + colon + 1, end - colon - 1);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
                if (value.empty()) throw std::runtime_error("Missing LSP Content-Length value.");
                for (char c : value) {
                    if (c < '0' || c > '9') throw std::runtime_error("Invalid LSP Content-Length.");
                    const size_t digit = c - '0';
                    if (length > (maxBodyBytes - digit) / 10) throw std::runtime_error("LSP body is too large.");
                    length = length * 10 + digit;
                }
                haveLength = true;
            }
            start = end + 2;
        }
        if (!haveLength) throw std::runtime_error("Missing LSP Content-Length.");
        bodyStart_ = headerEnd + 4;
        contentLength_ = length;
    }
    if (contentLength_ > buf_.size() - bodyStart_) return false;
    body.assign(buf_, bodyStart_, contentLength_);
    buf_.erase(0, bodyStart_ + contentLength_);
    bodyStart_ = 0;
    return true;
}

}  // namespace px
