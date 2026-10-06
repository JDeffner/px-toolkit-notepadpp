// A test-only plugin build. Runs inside an isolated portable Notepad++ against
// real Scintilla controls and the bundled language server. Never ship this DLL.
#define beNotified productionBeNotified
#include "../src/plugin.cpp"
#undef beNotified
#include "../src/resource.h"
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace {
std::wstring smokeRoot;
int smokeStage = 0, smokeFailures = 0;
ULONGLONG smokeDue = 0, smokeDeadline = 0;
std::string smokeOriginal;
bool restartSymbolsReceived = false, modIsolationChecked = false, modReturnChecked = false;
bool startupIndexNotification = false;
int startupClientErrors = 0;
HWND smokePanel = nullptr;
void smokeCheck(bool ok, const char* name) {
    std::ofstream log(smokeRoot + L"\\results.txt", std::ios::app);
    log << (ok ? "PASS " : "FAIL ") << name << "\n";
    if (!ok) ++smokeFailures;
}
BOOL CALLBACK findPanel(HWND hwnd, LPARAM) {
    if (GetDlgItem(hwnd, IDC_RESULTS) && GetDlgItem(hwnd, IDC_TABS)) smokePanel = hwnd;
    return TRUE;
}
int rowCount() { EnumChildWindows(g_npp._nppHandle, findPanel, 0); return smokePanel ? static_cast<int>(SendDlgItemMessage(smokePanel, IDC_RESULTS, LVM_GETITEMCOUNT, 0, 0)) : -1; }
void capture(HWND hwnd, const wchar_t* name) {
    const HWND views[] = {g_npp._scintillaMainHandle, g_npp._scintillaSecondHandle};
    LRESULT caretStyles[2]{};
    for (int i = 0; i < 2; ++i) {
        caretStyles[i] = SendMessage(views[i], SCI_GETCARETSTYLE, 0, 0);
        SendMessage(views[i], SCI_SETCARETSTYLE, CARETSTYLE_INVISIBLE, 0);
    }
    const HWND focused = GetFocus();
    const BOOL hiddenCaret = HideCaret(focused);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token; Gdiplus::GdiplusStartup(&token, &input, nullptr);
    RECT rect; GetWindowRect(hwnd, &rect); HDC dc = GetDC(hwnd), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right - rect.left, rect.bottom - rect.top);
    auto old = SelectObject(memory, bitmap); PrintWindow(hwnd, memory, 2); SelectObject(memory, old);
    { Gdiplus::Bitmap image(bitmap, nullptr); CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}}; image.Save((smokeRoot + L"\\" + name).c_str(), &png, nullptr); }
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd, dc); Gdiplus::GdiplusShutdown(token);
    if (hiddenCaret) ShowCaret(focused);
    for (int i = 0; i < 2; ++i) SendMessage(views[i], SCI_SETCARETSTYLE, caretStyles[i], 0);
}
void openFixture(const wchar_t* relative) { const auto path = smokeRoot + L"\\mod\\" + relative; SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str())); }
void caretAt(const std::string& word, int delta = 0) { const auto at = bufferText().find(word); smokeCheck(at != std::string::npos, ("fixture contains " + word).c_str()); sci(SCI_GOTOPOS, at + delta); }
void next(int delay = 1200) { ++smokeStage; smokeDue = GetTickCount64() + delay; }
void CALLBACK smokeTick(HWND hwnd, UINT, UINT_PTR timer, DWORD) {
    if (GetTickCount64() < smokeDue) return;
    try {
        switch (smokeStage) {
        case 0:
            if (!g_initialized || !docFor(currentPath()) || docFor(currentPath())->semantic.empty()) {
                if (GetTickCount64() > smokeDeadline) { smokeCheck(false, "server initialized and semantic response within 60 seconds"); smokeStage = 99; }
                return;
            }
            smokeCheck(supports("referencesProvider") && supports("renameProvider") && supports("codeActionProvider") && supports("foldingRangeProvider") && supports("signatureHelpProvider") && supports("semanticTokensProvider"), "bundled LSP advertises all requested providers");
            smokeOriginal = bufferText();
            smokeCheck(sci(SCI_GETSTYLEAT, bufferText().find("add_gold")) == 80, "semantic command style applied to add_gold in Scintilla");
            smokeCheck(sci(SCI_GETSTYLEAT, bufferText().find('{')) == px::Operator, "punctuation retains its lexical operator style");
            smokeCheck(sci(SCI_GETMARGINWIDTHN, 2) > 0 && (sci(SCI_GETMARGINMASKN, 2) & 0x01E00000) == 0x01E00000, "change-history markers retain their visible margin");
            {
                int foldingMargins = 0;
                for (int margin = 0; margin < sci(SCI_GETMARGINS); ++margin)
                    if (sci(SCI_GETMARGINWIDTHN, margin) > 0 && (sci(SCI_GETMARGINMASKN, margin) & SC_MASK_FOLDERS)) ++foldingMargins;
                smokeCheck(foldingMargins == 1 && sci(SCI_GETMARGINWIDTHN, 3) > 0 && sci(SCI_GETMARGINSENSITIVEN, 3), "fold controls use only the host folding margin");
                sci(SCI_INSERTTEXT, sci(SCI_POSITIONFROMLINE, 1), reinterpret_cast<LPARAM>(" "));
                styleDocument(); // No event-loop turn: the server has not answered this edit yet.
                smokeCheck(sci(SCI_GETSTYLEAT, bufferText().find("add_gold")) == 80, "unchanged semantic color survives typing before the server responds");
                const auto markers = sci(SCI_MARKERGET, 1);
                smokeCheck((markers & (1 << 23)) && (sci(SCI_GETMARGINMASKN, 2) & markers), "editing a line keeps its unsaved-change marker in the gutter");
                capture(g_npp._nppHandle, L"change-history.png");
                sci(SCI_UNDO);
                styleDocument();
                smokeCheck(sci(SCI_GETSTYLEAT, bufferText().find("add_gold")) == 80, "unchanged semantic color survives undo before the server responds");
                flushChange(currentPath());
            }
            smokeCheck((sci(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG) != 0, "LSP fold header applied");
            sci(SCI_GOTOPOS, 0); sci(SCI_FOLDLINE, 0, SC_FOLDACTION_CONTRACT);
            smokeCheck(!sci(SCI_GETLINEVISIBLE, 1), "fold contracts before refresh"); scheduleFeatures(); next(); break;
        case 1:
            smokeCheck(!sci(SCI_GETLINEVISIBLE, 1), "collapsed fold survives feature refresh");
            sci(SCI_FOLDLINE, 0, SC_FOLDACTION_EXPAND);
            px::showPanel(px::PanelTab::Outline); requestDocumentFeatures(); next(); break;
        case 2:
            smokeCheck(startupIndexNotification, "real server index-change notification reaches the client");
            smokeCheck(startupClientErrors == 0, "real server startup produces no client protocol errors");
            g_client.setNotificationHandler(&handleNotification);
            smokeCheck(rowCount() > 0 && IsWindowVisible(smokePanel), "native docked outline contains symbols");
            capture(g_npp._nppHandle, L"outline.png");
            caretAt("px_smoke_effect", 4); requestReferences(); next(2500); break;
        case 3:
            smokeCheck(rowCount() >= 2, "references panel includes definition and cross-file use");
            capture(g_npp._nppHandle, L"references.png");
            requestSymbols(L"px_smoke"); next(); break;
        case 4:
            smokeCheck(rowCount() >= 1, "workspace symbol search returns fixture symbols");
            {
                const std::string unsaved = "\n# Unsaved text must survive rename.\n";
                sci(SCI_APPENDTEXT, unsaved.size(), reinterpret_cast<LPARAM>(unsaved.c_str()));
                flushChange(currentPath());
                smokeOriginal = bufferText();
            }
            caretAt("px_smoke_effect", 4);
            requestRename(L"px_smoke_renamed"); next(2000); break;
        case 5:
            smokeCheck(g_preview.size() >= 2, "real rename previews cross-file edits");
            smokeCheck(bufferText() == smokeOriginal, "rename preview leaves original text untouched");
            capture(g_npp._nppHandle, L"rename-preview.png");
            applyPreview(); next(); break;
        case 6:
            smokeCheck(bufferText().find("px_smoke_renamed") != std::string::npos, "rename applies to active buffer");
            smokeCheck(bufferText().find("# Unsaved text must survive rename.") != std::string::npos && sci(SCI_GETMODIFY), "rename preserves unrelated unsaved text without saving the buffer");
            sci(SCI_UNDO);
            smokeCheck(bufferText() == smokeOriginal, "one undo restores original declaration");
            openFixture(L"events\\px_smoke_events.txt");
            smokeCheck(bufferText().find("px_smoke_renamed") != std::string::npos, "rename applies to second buffer without saving");
            sci(SCI_UNDO); flushChange(currentPath());
            openFixture(L"common\\game_concepts\\px_smoke_concepts.txt"); caretAt("px_smoke_concept", 4); next(1800); break;
        case 7: {
            static bool requested = false;
            if (!requested) {
                std::ofstream debug(smokeRoot + L"\\diagnostics.json");
                Json data = Json::object(); for (const auto& pair : g_diags) { data[pair.first] = Json::array(); for (const auto& d : pair.second) data[pair.first].push_back(d.raw); }
                debug << data.dump(2);
                requestCodeActions(); requested = true; smokeDue = GetTickCount64() + 1500; return;
            }
            static bool activated = false;
            if (!activated) {
              smokeCheck(rowCount() > 0, "quick fixes panel receives real LSP actions");
              if (rowCount() > 0) {
                LVITEMW item{}; item.stateMask = LVIS_SELECTED; item.state = LVIS_SELECTED;
                SendDlgItemMessage(smokePanel, IDC_RESULTS, LVM_SETITEMSTATE, 0, reinterpret_cast<LPARAM>(&item));
                NMHDR note{GetDlgItem(smokePanel, IDC_RESULTS), IDC_RESULTS, NM_DBLCLK};
                SendMessage(smokePanel, WM_NOTIFY, IDC_RESULTS, reinterpret_cast<LPARAM>(&note));
              }
              activated = true; smokeDue = GetTickCount64() + 500; return;
            }
            smokeCheck(!g_preview.empty(), "quick fix activation produces edit preview");
            smokeCheck(smokePanel && IsWindowEnabled(GetDlgItem(smokePanel, IDC_APPLY)), "quick fix Apply preview button is enabled");
            capture(g_npp._nppHandle, L"quick-fix.png");
            SendMessage(smokePanel, WM_COMMAND, IDC_APPLY, 0); next(); break;
        }
        case 8: {
            bool found = false;
            const auto loc = std::filesystem::path(smokeRoot + L"\\mod\\localization");
            if (std::filesystem::exists(loc)) for (const auto& file : std::filesystem::recursive_directory_iterator(loc)) if (file.is_regular_file()) {
                std::ifstream stream(file.path(), std::ios::binary); std::string text((std::istreambuf_iterator<char>(stream)), {});
                if (text.find("game_concept_px_smoke_concept:") != std::string::npos) found = true;
            }
            smokeCheck(found, "localization quick fix creates a file on disk");
            openFixture(L"common\\scripted_effects\\px_smoke_effects.txt");
            beginEditRequest();
            const auto uri = docFor(currentPath())->uri;
            const Json edit{{"changes", {{uri, Json::array({{{"range", {{"start", {{"line",0},{"character",0}}},{"end",{{"line",0},{"character",15}}}}},{"newText","other_effect"}}})}}}};
            previewWorkspaceEdit(edit); sci(SCI_SETREADONLY, TRUE); applyPreview();
            smokeCheck(bufferText() == smokeOriginal, "read-only target refuses preview apply without changes"); sci(SCI_SETREADONLY, FALSE);
            beginEditRequest(); previewWorkspaceEdit(edit);
            sci(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("\n"));
            smokeCheck(g_preview.empty(), "typing invalidates a pending edit preview"); sci(SCI_UNDO);
            flushChange(currentPath());
            openFixture(L"events\\px_smoke_events.txt"); caretAt("AMOUNT = 10", 9); requestSignature(); next(); break;
        }
        case 9:
            smokeCheck(sci(SCI_CALLTIPACTIVE) != 0, "real signature help appears as a Scintilla calltip");
            capture(g_npp._nppHandle, L"signature.png");
            showOptions(); next(); break;
        case 10: {
            HWND options = GetWindow(g_npp._nppHandle, GW_HWNDPREV);
            EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM pointer) -> BOOL { wchar_t title[100]{}; GetWindowTextW(hwnd, title, 100); if (std::wstring(title) == L"Paradox Toolkit options") *reinterpret_cast<HWND*>(pointer) = hwnd; return TRUE; }, reinterpret_cast<LPARAM>(&options));
            smokeCheck(options && IsWindowVisible(options), "native options window opens");
            if (options) {
                smokeCheck(SendDlgItemMessage(options, IDC_LANGUAGE, CB_GETCOUNT, 0, 0) == 10 && !IsWindowEnabled(GetDlgItem(options, IDC_CUSTOMLANGUAGE)), "options offer nine languages plus Other with custom input disabled for a built-in language");
                wchar_t gameLabel[128]{}, pathHelp[256]{};
                GetDlgItemTextW(options, IDC_GAME, gameLabel, 128);
                GetDlgItemTextW(options, IDC_GAMEPATH_HELP, pathHelp, 256);
                smokeCheck(std::wstring(gameLabel) == L"Crusader Kings III" && std::wstring(pathHelp).find(L"Not detected automatically") != std::wstring::npos, "options use full game names and explain that blank game paths are not detected");
                capture(options, L"options.png");
                SendDlgItemMessage(options, IDC_LANGUAGE, CB_SETCURSEL, 1, 0);
                SendMessage(options, WM_COMMAND, MAKEWPARAM(IDC_LANGUAGE, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(options, IDC_LANGUAGE)));
                SendMessage(options, WM_COMMAND, IDOK, 0);
                smokeCheck(px::loadSettings(g_iniPath).locLanguage == L"french" && g_settings.gameId == L"ck3" && g_settings.completionMode == L"minimal", "friendly choices save canonical language, game and completion keys");
                SendDlgItemMessage(options, IDC_LANGUAGE, CB_SETCURSEL, 9, 0);
                SendMessage(options, WM_COMMAND, MAKEWPARAM(IDC_LANGUAGE, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(options, IDC_LANGUAGE)));
                smokeCheck(IsWindowEnabled(GetDlgItem(options, IDC_CUSTOMLANGUAGE)), "Other enables custom language input");
                SetDlgItemTextW(options, IDC_CUSTOMLANGUAGE, L"");
                SendMessage(options, WM_COMMAND, IDOK, 0);
                smokeCheck(px::loadSettings(g_iniPath).locLanguage == L"french", "empty custom language does not overwrite saved settings");
                SetDlgItemTextW(options, IDC_CUSTOMLANGUAGE, L"invalid name:");
                SendMessage(options, WM_COMMAND, IDOK, 0);
                smokeCheck(px::loadSettings(g_iniPath).locLanguage == L"french", "invalid custom language does not overwrite saved settings");
                SetDlgItemTextW(options, IDC_CUSTOMLANGUAGE, L"braz_por");
                CheckDlgButton(options, IDC_SEMANTIC, BST_UNCHECKED); CheckDlgButton(options, IDC_FOLDING, BST_UNCHECKED);
                SendMessage(options, WM_COMMAND, IDOK, 0);
                showOptions();
                wchar_t customLanguage[128]{};
                GetDlgItemTextW(options, IDC_CUSTOMLANGUAGE, customLanguage, 128);
                smokeCheck(SendDlgItemMessage(options, IDC_LANGUAGE, CB_GETCURSEL, 0, 0) == 9 && std::wstring(customLanguage) == L"braz_por" && IsWindowEnabled(GetDlgItem(options, IDC_CUSTOMLANGUAGE)), "saved custom language reopens as Other without losing its value");
                capture(options, L"options-custom-language.png");
                ShowWindow(options, SW_HIDE);
            }
            next(3000); break;
        }
        case 11:
            smokeCheck(!px::loadSettings(g_iniPath).semanticHighlighting && !g_settings.folding && px::loadSettings(g_iniPath).locLanguage == L"braz_por", "options and custom language saved and applied through native dialog");
            smokeCheck(sci(SCI_GETSTYLEAT, 2) < 80 && !(sci(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG), "disabled semantic highlighting and folding removed");
            g_settings.semanticHighlighting = true; g_settings.folding = true; g_settings.locLanguage = L"english"; px::saveSettings(g_iniPath, g_settings); cmdRestart(); next(2000); break;
        case 12: {
            openFixture(L"events\\px_smoke_events.txt");
            const std::string unsaved = "\npx_smoke.2 = { type = character_event }\n";
            sci(SCI_APPENDTEXT, unsaved.size(), reinterpret_cast<LPARAM>(unsaved.c_str()));
            flushChange(currentPath());
            openFixture(L"common\\scripted_effects\\px_smoke_effects.txt");
            cmdRestart(); next(2500); break;
        }
        case 13: {
            auto path = smokeRoot + L"\\mod\\events\\px_smoke_events.txt";
            const auto uri = pathToUri(path);
            g_client.request("textDocument/documentSymbol", {{"textDocument", {{"uri", uri}}}}, [](const Json& result, const Json& error) {
                restartSymbolsReceived = true;
                smokeCheck(error.is_null() && result.dump().find("px_smoke.2") != std::string::npos, "restart resends inactive unsaved declaration to real server");
            });
            std::ofstream(smokeRoot + L"\\mod\\events\\completion.txt") << "@example = 1\nvalue = @ex\nlast = untouched\n";
            openFixture(L"events\\completion.txt"); next(1000); break;
        }
        case 14: {
            smokeCheck(restartSymbolsReceived, "inactive document symbol response arrives after restart");
            smokeOriginal = bufferText();
            const auto caret = smokeOriginal.find("@ex", smokeOriginal.find("value")) + 3;
            sci(SCI_GOTOPOS, caret); requestCompletion();
            sci(SCI_GOTOPOS, smokeOriginal.size()); next(1000); break;
        }
        case 15:
            smokeCheck(!sci(SCI_AUTOCACTIVE), "late completion is discarded after caret moves");
            sci(SCI_AUTOCCOMPLETE);
            smokeCheck(bufferText() == smokeOriginal, "late completion cannot delete intervening text");
            sci(SCI_GOTOPOS, smokeOriginal.find("@ex", smokeOriginal.find("value")) + 3);
            requestCompletion(); next(1000); break;
        case 16: {
            smokeCheck(sci(SCI_AUTOCACTIVE) != 0, "current constant completion is visible including its at-sign prefix");
            // A late selection notification must also be rejected after movement.
            sci(SCI_GOTOPOS, smokeOriginal.size());
            SCNotification selection{}; selection.text = "@example"; selection.position = 0;
            applyCompletion(&selection);
            smokeCheck(bufferText() == smokeOriginal, "completion insertion rechecks its origin before editing");
            sci(SCI_GOTOPOS, smokeOriginal.find("@ex", smokeOriginal.find("value")) + 3);
            requestCompletion(); next(1000); break;
        }
        case 17: {
            sci(SCI_AUTOCCOMPLETE);
            std::string expected = smokeOriginal;
            expected.replace(expected.find("@ex", expected.find("value")), 3, "@example");
            smokeCheck(bufferText() == expected, "valid completion changes only the server range and preserves following text");
            sci(SCI_UNDO);
            smokeCheck(bufferText() == smokeOriginal, "completion is one undo step");
            px::showPanel(px::PanelTab::Problems); refreshProblems(); capture(g_npp._nppHandle, L"problems.png");
            sci(SCI_GOTOPOS, smokeOriginal.find("@ex", smokeOriginal.find("value")) + 3);
            requestCompletion(); sci(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>(" "));
            next(1000); break;
        }
        case 18:
            smokeCheck(!sci(SCI_AUTOCACTIVE) && bufferText() == smokeOriginal + " ", "typing discards an outstanding completion response");
            sci(SCI_UNDO);
            sci(SCI_GOTOPOS, smokeOriginal.find("@ex", smokeOriginal.find("value")) + 3);
            requestCompletion(); cmdRestart(); next(2000); break;
        case 19: {
            smokeCheck(!sci(SCI_AUTOCACTIVE) && bufferText() == smokeOriginal, "restart discards an outstanding completion response");
            const auto root = smokeRoot + L"\\other-mod";
            std::filesystem::create_directories(root + L"\\events");
            std::ofstream(root + L"\\descriptor.mod") << "name=\"Other smoke mod\"";
            const auto file = root + L"\\events\\other.txt";
            std::ofstream(file) << "namespace = other\nother.1 = { type = character_event }\n";
            SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(file.c_str()));
            next(2500); break;
        }
        case 20: {
            const auto uri = pathToUri(smokeRoot + L"\\mod\\events\\px_smoke_events.txt");
            g_client.request("textDocument/documentSymbol", {{"textDocument", {{"uri", uri}}}}, [](const Json& result, const Json& error) {
                modIsolationChecked = true;
                smokeCheck(error.is_null() && result.is_array() && result.empty(), "other mod session does not receive the first mod's tracked documents");
            });
            next(1000); break;
        }
        case 21:
            smokeCheck(modIsolationChecked, "mod isolation response arrives");
            openFixture(L"common\\scripted_effects\\px_smoke_effects.txt"); next(2500); break;
        case 22: {
            const auto uri = pathToUri(smokeRoot + L"\\mod\\events\\px_smoke_events.txt");
            g_client.request("textDocument/documentSymbol", {{"textDocument", {{"uri", uri}}}}, [](const Json& result, const Json& error) {
                modReturnChecked = true;
                smokeCheck(error.is_null() && result.dump().find("px_smoke.2") != std::string::npos, "returning to a mod restores its inactive unsaved document");
            });
            next(1000); break;
        }
        case 23:
            smokeCheck(modReturnChecked, "restored mod response arrives");
            smokeStage = 99; break;
        default:
            KillTimer(hwnd, timer);
            std::ofstream(smokeRoot + L"\\done.txt") << smokeFailures << " failures\n";
            break;
        }
    } catch (const std::exception& e) { smokeCheck(false, e.what()); smokeStage = 99; }
}
}
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    productionBeNotified(notification);
    if (notification->nmhdr.code == NPPN_READY) {
        wchar_t root[MAX_PATH]{}; GetEnvironmentVariableW(L"PX_SMOKE_ROOT", root, MAX_PATH); smokeRoot = root;
        if (smokeRoot.empty() || smokeRoot.find(L"px-toolkit-notepadpp\\build\\native-smoke") == std::wstring::npos) return;
        std::ofstream(smokeRoot + L"\\results.txt", std::ios::trunc);
        g_client.setNotificationHandler([](const std::string& method, const Json& params) {
            if (method == "paradox/indexChanged") startupIndexNotification = true;
            if (method == "$/clientError") ++startupClientErrors;
            handleNotification(method, params);
        });
        smokeDeadline = GetTickCount64() + 60000;
        SetTimer(g_sink, 44, 150, smokeTick);
    }
}
