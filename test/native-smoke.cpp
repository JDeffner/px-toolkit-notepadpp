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
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token; Gdiplus::GdiplusStartup(&token, &input, nullptr);
    RECT rect; GetWindowRect(hwnd, &rect); HDC dc = GetDC(hwnd), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right - rect.left, rect.bottom - rect.top);
    auto old = SelectObject(memory, bitmap); PrintWindow(hwnd, memory, 2); SelectObject(memory, old);
    { Gdiplus::Bitmap image(bitmap, nullptr); CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}}; image.Save((smokeRoot + L"\\" + name).c_str(), &png, nullptr); }
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd, dc); Gdiplus::GdiplusShutdown(token);
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
            smokeCheck(sci(SCI_GETSTYLEAT, 2) >= 80, "real semantic styles applied in Scintilla");
            smokeCheck((sci(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG) != 0, "LSP fold header applied");
            sci(SCI_GOTOPOS, 0); sci(SCI_FOLDLINE, 0, SC_FOLDACTION_CONTRACT);
            smokeCheck(!sci(SCI_GETLINEVISIBLE, 1), "fold contracts before refresh"); scheduleFeatures(); next(); break;
        case 1:
            smokeCheck(!sci(SCI_GETLINEVISIBLE, 1), "collapsed fold survives feature refresh");
            sci(SCI_FOLDLINE, 0, SC_FOLDACTION_EXPAND);
            px::showPanel(px::PanelTab::Outline); requestDocumentFeatures(); next(); break;
        case 2:
            smokeCheck(rowCount() > 0 && IsWindowVisible(smokePanel), "native docked outline contains symbols");
            capture(g_npp._nppHandle, L"outline.png");
            caretAt("px_smoke_effect", 4); requestReferences(); next(2500); break;
        case 3:
            smokeCheck(rowCount() >= 2, "references panel includes definition and cross-file use");
            capture(g_npp._nppHandle, L"references.png");
            requestSymbols(L"px_smoke"); next(); break;
        case 4:
            smokeCheck(rowCount() >= 1, "workspace symbol search returns fixture symbols");
            caretAt("px_smoke_effect", 4);
            requestRename(L"px_smoke_renamed"); next(2000); break;
        case 5:
            smokeCheck(g_preview.size() >= 2, "real rename previews cross-file edits");
            smokeCheck(bufferText() == smokeOriginal, "rename preview leaves original text untouched");
            capture(g_npp._nppHandle, L"rename-preview.png");
            applyPreview(); next(); break;
        case 6:
            smokeCheck(bufferText().find("px_smoke_renamed") != std::string::npos, "rename applies to active buffer");
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
                capture(options, L"options.png");
                CheckDlgButton(options, IDC_SEMANTIC, BST_UNCHECKED); CheckDlgButton(options, IDC_FOLDING, BST_UNCHECKED);
                SendMessage(options, WM_COMMAND, IDOK, 0); ShowWindow(options, SW_HIDE);
            }
            next(3000); break;
        }
        case 11:
            smokeCheck(!px::loadSettings(g_iniPath).semanticHighlighting && !g_settings.folding, "options saved and applied through native dialog");
            smokeCheck(sci(SCI_GETSTYLEAT, 2) < 80 && !(sci(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG), "disabled semantic highlighting and folding removed");
            g_settings.semanticHighlighting = true; g_settings.folding = true; px::saveSettings(g_iniPath, g_settings); cmdRestart(); next(2000); break;
        case 12:
            px::showPanel(px::PanelTab::Problems); refreshProblems(); capture(g_npp._nppHandle, L"problems.png");
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
        smokeDeadline = GetTickCount64() + 60000;
        SetTimer(g_sink, 44, 150, smokeTick);
    }
}
