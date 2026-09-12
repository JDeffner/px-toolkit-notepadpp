#include "lspfeatures.h"
#include "textpos.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <limits>

namespace px {
using Json = nlohmann::json;
namespace {
int unsignedInt(const Json& value) {
    if (!value.is_number_integer() || value < 0 || value > (std::numeric_limits<int>::max)())
        throw std::runtime_error("Expected a nonnegative 32-bit integer.");
    return value.get<int>();
}
size_t offset(const TextPositions& positions, const Json& p, bool append) {
    return positions.offset({unsignedInt(p.at("line")), unsignedInt(p.at("character"))}, append);
}
}
std::vector<TextEdit> textEdits(const std::string& text, const Json& edits) {
    return textEdits(TextPositions(text), edits);
}
std::vector<TextEdit> textEdits(const TextPositions& positions, const Json& edits) {
    if (!edits.is_array()) throw std::runtime_error("Expected a list of text edits.");
    std::vector<TextEdit> result;
    for (const auto& edit : edits) {
        const auto& range = edit.at("range");
        const bool append = range.at("start") == range.at("end");
        const size_t start = offset(positions, range.at("start"), append), end = offset(positions, range.at("end"), append);
        if (end < start) throw std::runtime_error("Reversed edit range.");
        result.push_back({start, end, edit.at("newText").get<std::string>()});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
    for (size_t i = 1; i < result.size(); ++i)
        if (result[i].end > result[i - 1].start || result[i].start == result[i - 1].start)
            throw std::runtime_error("Overlapping edits cannot be applied.");
    return result;
}
std::vector<FileEdits> workspaceEdits(const Json& edit) {
    std::map<std::string, FileEdits> files;
    if (edit.contains("changes") && edit.contains("documentChanges")) throw std::runtime_error("Ambiguous workspace edit.");
    if (edit.contains("changes")) {
        for (auto it = edit.at("changes").begin(); it != edit.at("changes").end(); ++it)
            files[it.key()] = {it.key(), it.value(), -1, false};
    }
    if (edit.contains("documentChanges")) {
        for (const auto& change : edit.at("documentChanges")) {
            if (change.contains("kind")) {
                if (change.at("kind") != "create") throw std::runtime_error("File rename and deletion operations are not supported.");
                const auto uri = change.at("uri").get<std::string>();
                auto& file = files[uri]; file.uri = uri; file.create = true;
                if (change.value("options", Json::object()).value("overwrite", false)) throw std::runtime_error("Overwriting a file is not supported.");
            } else {
                const auto& doc = change.at("textDocument");
                const auto uri = doc.at("uri").get<std::string>();
                auto& file = files[uri]; file.uri = uri;
                if (!file.edits.is_null()) throw std::runtime_error("Multiple edit sets for one document are not supported.");
                file.edits = change.at("edits");
                if (doc.contains("version") && !doc["version"].is_null()) file.version = doc["version"].get<int>();
            }
        }
    }
    std::vector<FileEdits> result;
    for (auto& pair : files) {
        auto& file = pair.second;
        if (file.uri.compare(0, 8, "file:///") != 0) throw std::runtime_error("Only local file edits are supported.");
        if (file.edits.is_null()) file.edits = Json::array();
        result.push_back(std::move(file));
    }
    return result;
}
std::vector<int> foldingLevels(int lineCount, const Json& ranges) {
    const int base = 0x400, header = 0x2000;
    std::vector<int> delta(lineCount + 1), headers(lineCount);
    for (const auto& range : ranges) {
        const int start = range.value("startLine", -1), end = range.value("endLine", -1);
        if (start < 0 || end <= start || end >= lineCount) continue;
        headers[start] = header; ++delta[start + 1]; --delta[end + 1];
    }
    std::vector<int> levels(lineCount);
    int depth = 0;
    for (int i = 0; i < lineCount; ++i) { depth += delta[i]; levels[i] = (std::min)(base + depth, 0xfff) | headers[i]; }
    return levels;
}
void rebaseSemanticSpans(std::vector<SemanticSpan>& spans, const std::string& before, const std::string& after) {
    if (spans.empty() || before == after) return;
    size_t start = 0;
    while (start < before.size() && start < after.size() && before[start] == after[start]) ++start;
    size_t oldEnd = before.size(), newEnd = after.size();
    while (oldEnd > start && newEnd > start && before[oldEnd - 1] == after[newEnd - 1]) { --oldEnd; --newEnd; }
    const auto word = [](unsigned char c) {
        return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '@' || c == '$' || c == ':';
    };
    size_t retained = 0;
    for (auto span : spans) {
        if (span.end <= start) {
            // Inserting or joining identifier characters changes the adjacent token too.
            if (span.end == start && start < after.size() && word(after[start])) continue;
        } else if (span.start >= oldEnd) {
            if (span.start == oldEnd && newEnd > 0 && word(after[newEnd - 1])) continue;
            span.start = newEnd + (span.start - oldEnd);
            span.end = newEnd + (span.end - oldEnd);
        } else continue;
        spans[retained++] = span;
    }
    spans.resize(retained);
}

std::vector<SemanticSpan> semanticSpans(const std::string& text, const Json& data, const std::vector<std::string>& legend) {
    if (!data.is_array() || data.size() % 5) throw std::runtime_error("Invalid semantic token data.");
    const std::vector<std::string> known = {"method", "function", "variable", "property", "macro", "event", "enumMember", "string"};
    std::vector<SemanticSpan> result;
    const TextPositions positions(text);
    int line = 0, character = 0;
    for (size_t i = 0; i < data.size(); i += 5) {
        const int dl = unsignedInt(data[i]), dc = unsignedInt(data[i + 1]), length = unsignedInt(data[i + 2]), type = unsignedInt(data[i + 3]);
        unsignedInt(data[i + 4]);
        const int max = (std::numeric_limits<int>::max)();
        if (length == 0 || static_cast<size_t>(type) >= legend.size() || dl > max - line || (!dl && dc > max - character)) throw std::runtime_error("Invalid semantic token.");
        line += dl; character = dl ? dc : character + dc;
        if (length > max - character) throw std::runtime_error("Semantic token length overflow.");
        const auto it = std::find(known.begin(), known.end(), legend[type]);
        const size_t start = positions.offset({line, character});
        const size_t end = positions.offset({line, character + length});
        if (it == known.end()) continue;
        result.push_back({start, end, static_cast<unsigned char>(80 + (it - known.begin()))});
    }
    return result;
}
}
