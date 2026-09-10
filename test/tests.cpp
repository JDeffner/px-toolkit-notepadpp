// Tests for the pure logic only: the parts that decide what goes on the wire.
// Anything needing Notepad++ or a running server is tried in the editor.

#include <cstdio>
#include <set>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../src/classify.h"
#include "../src/framing.h"
#include "../src/highlight.h"
#include "../src/markdown.h"
#include "../src/textpos.h"
#include "../src/lspfeatures.h"
#include "../src/settings.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL %s\n", what);
        g_failures += 1;
    }
}

// A CK3 mod at F:/mods/ck3mod, an EU5 mod at F:/mods/eu5mod.
bool fakeModRoot(const std::wstring& dir) {
    static const std::set<std::wstring> roots = {L"F:/mods/ck3mod", L"F:/mods/eu5mod"};
    return roots.count(dir) > 0;
}

void testClassify() {
    using namespace px;

    FileClass script = classify(L"F:\\mods\\ck3mod\\events\\my_events.txt", fakeModRoot);
    check(script.lang == Lang::Script, "script .txt under events/");
    check(script.modRoot == L"F:/mods/ck3mod", "script mod root");
    check(std::string(languageId(script.lang)) == "paradox", "script language id");

    check(classify(L"F:\\mods\\ck3mod\\common\\traits\\00_traits.txt", fakeModRoot).lang == Lang::Script,
          "script .txt under common/");
    check(classify(L"F:\\mods\\ck3mod\\history\\characters\\a.txt", fakeModRoot).lang == Lang::Script,
          "script .txt under history/");

    FileClass loc = classify(L"F:\\mods\\ck3mod\\localization\\english\\my_l_english.yml", fakeModRoot);
    check(loc.lang == Lang::Loc, "loc .yml under localization/");
    check(std::string(languageId(loc.lang)) == "paradox-loc", "loc language id");

    FileClass gui = classify(L"F:\\mods\\ck3mod\\gui\\window_my.gui", fakeModRoot);
    check(gui.lang == Lang::Gui, "gui .gui");
    check(std::string(languageId(gui.lang)) == "paradox-gui", "gui language id");

    // EU5 nests the same folders under in_game/, main_menu/ and loading_screen/.
    FileClass nested = classify(L"F:\\mods\\eu5mod\\in_game\\common\\traits\\x.txt", fakeModRoot);
    check(nested.lang == Lang::Script, "EU5 in_game/common/ is script");
    check(nested.modRoot == L"F:/mods/eu5mod", "EU5 mod root");
    check(classify(L"F:\\mods\\eu5mod\\main_menu\\localization\\english\\x_l_english.yml", fakeModRoot).lang ==
              Lang::Loc,
          "EU5 main_menu/localization is loc");

    // Outside a mod root nothing is a Paradox file, whatever the folder is named.
    check(classify(L"C:\\Users\\me\\common\\traits\\x.txt", fakeModRoot).lang == Lang::None,
          "no mod root, no language");
    check(classify(L"C:\\Users\\me\\notes.txt", fakeModRoot).lang == Lang::None, "plain text file");
    // Inside a mod root, a folder that holds no script is still not script.
    check(classify(L"F:\\mods\\ck3mod\\readme.txt", fakeModRoot).lang == Lang::None,
          "mod root file outside a script folder");
    check(classify(L"F:\\mods\\ck3mod\\descriptor.mod", fakeModRoot).lang == Lang::None, "descriptor.mod");
}

void testPositions() {
    using namespace px;

    // "aé\n" + a 4-byte emoji: e-acute is 2 bytes / 1 unit, the emoji 4 / 2.
    const std::string text = "a\xC3\xA9 x\nb\xF0\x9F\x98\x80 y\n";

    check(offsetToPosition(text, 0).line == 0 && offsetToPosition(text, 0).character == 0, "offset 0");
    check(offsetToPosition(text, 3).character == 2, "two-byte char counts one UTF-16 unit");
    check(offsetToPosition(text, 6).line == 1 && offsetToPosition(text, 6).character == 0, "start of line 1");
    check(offsetToPosition(text, 11).line == 1 && offsetToPosition(text, 11).character == 3,
          "four-byte char counts two UTF-16 units");

    Position p;
    p.line = 1;
    p.character = 3;
    check(positionToOffset(text, p) == 11, "round trip through the emoji");
    p.character = 1;
    check(positionToOffset(text, p) == 7, "position before the emoji");
    p.line = 0;
    p.character = 2;
    check(positionToOffset(text, p) == 3, "position after the two-byte char");

    // CRLF: the terminator is not part of the line's characters.
    const std::string crlf = "ab\r\ncd\r\n";
    check(offsetToPosition(crlf, 2).character == 2, "end of a CRLF line");
    check(offsetToPosition(crlf, 4).line == 1 && offsetToPosition(crlf, 4).character == 0, "after CRLF");
    p.line = 1;
    p.character = 0;
    check(positionToOffset(crlf, p) == 4, "start of the second CRLF line");
}

void testFraming() {
    using namespace px;

    // Two frames arriving in three chunks that all split mid-structure.
    const std::string first = "{\"id\":1}";
    const std::string second = "{\"id\":2}";
    const std::string stream = "Content-Length: " + std::to_string(first.size()) + "\r\n\r\n" + first +
                               "Content-Length: " + std::to_string(second.size()) +
                               "\r\nContent-Type: application/vscode-jsonrpc\r\n\r\n" + second;

    FrameParser parser;
    std::string body;
    parser.feed(stream.data(), 10);
    check(!parser.next(body), "no frame from a partial header");
    parser.feed(stream.data() + 10, 25);
    parser.feed(stream.data() + 35, stream.size() - 35);
    check(parser.next(body) && body == first, "first frame");
    check(parser.next(body) && body == second, "second frame with an extra header");
    check(!parser.next(body), "nothing left");

    // A body split byte by byte still comes out whole.
    FrameParser slow;
    for (char c : stream) slow.feed(&c, 1);
    check(slow.next(body) && body == first, "byte-at-a-time first frame");
    check(slow.next(body) && body == second, "byte-at-a-time second frame");
}

void testMarkdown() {
    using namespace px;

    const std::string md =
        "### add_trait\n"
        "\n"
        "**Adds a trait** to the `character` scope.\n"
        "\n"
        "```paradox\n"
        "add_trait = brave\n"
        "```\n"
        "\n"
        "---\n"
        "\n"
        "Defined in [00_traits.txt](file:///F:/game/common/traits/00_traits.txt)\n";

    const std::string plain = markdownToPlain(md);
    check(plain.find('#') == std::string::npos, "heading marks are gone");
    check(plain.find("**") == std::string::npos, "bold marks are gone");
    check(plain.find('`') == std::string::npos, "backticks are gone");
    check(plain.find("file:///") == std::string::npos, "link targets are gone");
    check(plain.find("add_trait") == 0, "the heading text survives");
    check(plain.find("Adds a trait to the character scope.") != std::string::npos, "inline text survives");
    check(plain.find("add_trait = brave") != std::string::npos, "fenced code survives");
    check(plain.find("00_traits.txt") != std::string::npos, "link text survives");
}

}  // namespace

void testHighlight() {
    using namespace px;
    const std::string text = "# comment = yes\r\nfoo = { amount >= -12.5 flag = yes scope:bar = @value name = \"a \\\"#b\\\"\" }";
    const auto styles = highlight(text, false);
    check(styles.size() == text.size(), "syntax styles use byte offsets");
    check(styles[2] == Comment && styles[text.find('\r')] == Plain, "comments stop at CRLF");
    check(styles[text.find("foo")] == Key && styles[text.find("scope:bar") + 5] == Key, "assignment keys and scopes");
    check(styles[text.find("-12.5")] == Number && styles[text.find("flag = yes") + 7] == Literal, "numbers and booleans");
    check(styles[text.find("@value")] == Variable && styles[text.find('{')] == Operator, "variables and operators");
    check(styles[text.find("#b")] == String && styles.back() == Operator, "escaped quotes keep hashes inside strings");
    const std::string loc = "l_english:\n key:0 \"Gr\xC3\xBC\xC3\x9F\x65 # text\" # note\n";
    const auto ls = highlight(loc, true);
    check(ls[0] == Key && ls[loc.find(':')] == Operator && ls[loc.find('0')] == Number, "localization keys and version");
    check(ls[loc.find("# text")] == String && ls[loc.find("# note")] == Comment, "UTF-8 localization and comments");
    check(highlight("\"unfinished\nstring", false).back() == String, "unfinished multiline string");
    check(highlight("", false).empty(), "empty document");
}

void testLspFeatures() {
    using nlohmann::json;
    auto edit = [](int a, int b, const char* text) { return json{{"range", {{"start", {{"line", 0}, {"character", a}}}, {"end", {{"line", 0}, {"character", b}}}}}, {"newText", text}}; };
    auto rejects = [](auto fn) { try { fn(); return false; } catch (const std::exception&) { return true; } };
    const std::string text = "a\xF0\x9F\x98\x80" "bc";
    auto edits = px::textEdits(text, json::array({edit(1, 3, "face"), edit(4, 5, "C")}));
    std::string result = text;
    for (const auto& e : edits) result.replace(e.start, e.end - e.start, e.text);
    check(result == "afacebC", "edits apply descending with astral UTF-16 positions");
    check(rejects([&] { px::textEdits(text, json::array({edit(2, 3, "bad")})); }), "reject edit inside surrogate pair");
    check(rejects([&] { px::textEdits(text, json::array({edit(0, 3, "a"), edit(1, 4, "b")})); }), "reject overlapping edits before mutation");
    check(rejects([&] { px::textEdits(text, json::array({edit(4, 1, "bad")})); }), "reject reversed ranges");
    check(rejects([&] { px::textEdits(text, json::array({edit(-1, 0, "bad")})); }), "reject negative ranges");
    auto create = px::workspaceEdits({{"documentChanges", json::array({{{"kind", "create"}, {"uri", "file:///F:/mod/a.yml"}}, {{"textDocument", {{"uri", "file:///F:/mod/a.yml"}, {"version", nullptr}}}, {"edits", json::array({edit(0, 0, "l_english:\n")})}}})}});
    check(create.size() == 1 && create[0].create && create[0].edits.size() == 1, "localization CreateFile merges with text edits");
    check(rejects([&] { px::workspaceEdits({{"documentChanges", json::array({{{"kind", "delete"}, {"uri", "file:///F:/mod/a"}}})}}); }), "reject destructive resource operations");
    check(rejects([&] { px::workspaceEdits({{"changes", {{"https://example.org", json::array()}}}}); }), "reject non-file edit targets");
    const auto levels = px::foldingLevels(6, json::array({{{"startLine", 0}, {"endLine", 4}}, {{"startLine", 1}, {"endLine", 3}}, {{"startLine", -1}, {"endLine", 10}}}));
    check(levels == std::vector<int>({0x2400, 0x2401, 0x402, 0x402, 0x401, 0x400}), "nested folds preserve closing lines and ignore invalid ranges");
    const auto spans = px::semanticSpans(text, json::array({0,1,2,0,0,0,2,1,1,0}), {"function", "property"});
    check(spans.size() == 2 && spans[0].start == 1 && spans[0].end == 5 && spans[1].start == 5 && spans[1].style == 83, "semantic token deltas use UTF-16 and server legend");
    check(rejects([&] { px::semanticSpans(text, json::array({0,0,1}), {"function"}); }), "reject malformed semantic token stream");
    wchar_t temp[MAX_PATH], path[MAX_PATH]; GetTempPathW(MAX_PATH, temp); GetTempFileNameW(temp, L"pxs", 0, path);
    WritePrivateProfileStringW(L"px-toolkit", L"gameId", L"vic3", path);
    auto settings = px::loadSettings(path);
    check(settings.automaticCompletion && settings.semanticHighlighting && settings.autoUpdateServer, "old INI receives enabled feature defaults");
    settings.autoUpdateServer = false; settings.folding = false; settings.gamePath = L"F:\\Games\\\u65e5\u672c"; settings.hoverDetail = L"full";
    check(px::saveSettings(path, settings), "save settings succeeds");
    const auto loaded = px::loadSettings(path);
    check(!loaded.autoUpdateServer && !loaded.folding && loaded.gamePath == settings.gamePath && loaded.hoverDetail == L"full", "options persist across reload");
    DeleteFileW(path);
}

int main() {
    testLspFeatures();
    testHighlight();
    testClassify();
    testPositions();
    testFraming();
    testMarkdown();
    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", g_failures);
    return 1;
}
