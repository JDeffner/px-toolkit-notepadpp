#pragma once
#include <string>
#include <vector>
#include "../third_party/nlohmann/json.hpp"
namespace px {
class TextPositions;
struct TextEdit { size_t start, end; std::string text; };
struct SemanticSpan { size_t start, end; unsigned char style; };
struct FileEdits { std::string uri; nlohmann::json edits; int version = -1; bool create = false; };
std::vector<TextEdit> textEdits(const std::string& text, const nlohmann::json& edits);
std::vector<TextEdit> textEdits(const TextPositions& positions, const nlohmann::json& edits);
std::vector<FileEdits> workspaceEdits(const nlohmann::json& edit);
std::vector<int> foldingLevels(int lineCount, const nlohmann::json& ranges);
std::vector<SemanticSpan> semanticSpans(const std::string& text, const nlohmann::json& data, const std::vector<std::string>& legend);
// Keep unchanged tokens in place while awaiting a new server snapshot.
void rebaseSemanticSpans(std::vector<SemanticSpan>& spans, const std::string& before, const std::string& after);
}
