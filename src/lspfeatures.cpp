#include "lspfeatures.h"
#include "textpos.h"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace px {
using Json = nlohmann::json;
namespace {
size_t offset(const std::string& text, const Json& p, bool append) {
    Position pos{p.at("line").get<int>(), p.at("character").get<int>()};
    if (pos.line < 0 || pos.character < 0) throw std::runtime_error("Negative edit position.");
    const size_t result = positionToOffset(text, pos);
    const auto actual = offsetToPosition(text, result);
    if (actual.line == pos.line && actual.character == pos.character) return result;
    if (append && result == text.size() && pos.character == 0 && pos.line == actual.line + 1) return result;
    throw std::runtime_error("Edit position is outside the document or splits a UTF-16 character.");
}
}
std::vector<TextEdit> textEdits(const std::string& text, const Json& edits) {
    if (!edits.is_array()) throw std::runtime_error("Expected a list of text edits.");
    std::vector<TextEdit> result;
    for (const auto& edit : edits) {
        const auto& range = edit.at("range");
        const bool append = range.at("start") == range.at("end");
        const size_t start = offset(text, range.at("start"), append), end = offset(text, range.at("end"), append);
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
std::vector<SemanticSpan> semanticSpans(const std::string& text, const Json& data, const std::vector<std::string>& legend) {
    if (!data.is_array() || data.size() % 5) throw std::runtime_error("Invalid semantic token data.");
    const std::vector<std::string> known = {"method", "function", "variable", "property", "macro", "event", "enumMember", "string"};
    std::vector<SemanticSpan> result;
    int line = 0, character = 0;
    for (size_t i = 0; i < data.size(); i += 5) {
        const int dl = data[i].get<int>(), dc = data[i + 1].get<int>(), length = data[i + 2].get<int>(), type = data[i + 3].get<int>();
        if (dl < 0 || dc < 0 || length <= 0 || type < 0 || static_cast<size_t>(type) >= legend.size()) throw std::runtime_error("Invalid semantic token.");
        line += dl; character = dl ? dc : character + dc;
        const auto it = std::find(known.begin(), known.end(), legend[type]);
        if (it == known.end()) continue;
        const size_t start = offset(text, {{"line", line}, {"character", character}}, false);
        const size_t end = offset(text, {{"line", line}, {"character", character + length}}, false);
        result.push_back({start, end, static_cast<unsigned char>(80 + (it - known.begin()))});
    }
    return result;
}
}
