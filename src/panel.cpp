#include "panel.h"
#include <commctrl.h>
#include <array>
#include <algorithm>
#include <shobjidl.h>
#include <shlobj.h>
#include <utility>
#include "resource.h"
#include "../sdk/Notepad_plus_msgs.h"
#include "../sdk/Docking.h"
#include "../sdk/dockingResource.h"

namespace px {
namespace {
HINSTANCE instance;
HWND parent, panel, options;
int menuIndex;
PanelHandler handler;
std::function<bool(const Settings&)> applyOptions;
std::function<void(bool)> updateAction;
std::array<std::vector<PanelRow>, 5> rows;
std::array<std::wstring, 5> messages;
PanelTab selected = PanelTab::Problems;
tTbData docking;
bool canApply = false;
void layout();
std::wstring text(HWND dlg, int id) {
    HWND item = GetDlgItem(dlg, id);
    std::wstring value(GetWindowTextLengthW(item) + 1, L'\0');
    GetWindowTextW(item, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str()));
    return value;
}
void combo(HWND dlg, int id, std::initializer_list<const wchar_t*> items, const std::wstring& value) {
    HWND control = GetDlgItem(dlg, id);
    SendMessage(control, CB_RESETCONTENT, 0, 0);
    int index = 0, chosen = 0;
    for (const auto* item : items) {
        SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        if (item == value) chosen = index;
        ++index;
    }
    SendMessage(control, CB_SETCURSEL, chosen, 0);
}
struct Choice { const wchar_t* label; const wchar_t* value; };
const Choice games[] = {{L"Crusader Kings III", L"ck3"}, {L"Victoria 3", L"vic3"}, {L"Europa Universalis V", L"eu5"}};
// Same nine language keys as px-lsp's protocol/translationCore.ts.
const Choice languages[] = {
    {L"English", L"english"}, {L"French", L"french"}, {L"German", L"german"},
    {L"Spanish", L"spanish"}, {L"Russian", L"russian"}, {L"Korean", L"korean"},
    {L"Chinese (Simplified)", L"simp_chinese"}, {L"Japanese", L"japanese"}, {L"Polish", L"polish"},
    {L"Other (custom language)", L""}
};
const Choice completionModes[] = {{L"Minimal structure", L"minimal"}, {L"Example blocks", L"examples"}, {L"Names only", L"names"}};
const Choice hoverDetails[] = {{L"Compact", L"compact"}, {L"Standard", L"standard"}, {L"Full", L"full"}};
template<size_t N> void choiceCombo(HWND dlg, int id, const Choice (&choices)[N], const std::wstring& value, int fallback = 0) {
    HWND control = GetDlgItem(dlg, id);
    SendMessage(control, CB_RESETCONTENT, 0, 0);
    int chosen = fallback;
    for (size_t i = 0; i < N; ++i) {
        const auto index = SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choices[i].label));
        SendMessage(control, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(choices[i].value));
        if (value == choices[i].value) chosen = static_cast<int>(i);
    }
    SendMessage(control, CB_SETCURSEL, chosen, 0);
}
std::wstring choiceValue(HWND dlg, int id) {
    const auto selected = SendDlgItemMessage(dlg, id, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return L"";
    const auto value = SendDlgItemMessage(dlg, id, CB_GETITEMDATA, selected, 0);
    return value == CB_ERR ? L"" : reinterpret_cast<const wchar_t*>(value);
}
std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
void updateOptionHelp(HWND hwnd) {
    const bool custom = choiceValue(hwnd, IDC_LANGUAGE).empty();
    EnableWindow(GetDlgItem(hwnd, IDC_CUSTOMLANGUAGE), custom);
    EnableWindow(GetDlgItem(hwnd, IDC_CUSTOMLANGUAGE_LABEL), custom);
    const auto language = custom ? trim(text(hwnd, IDC_CUSTOMLANGUAGE)) : choiceValue(hwnd, IDC_LANGUAGE);
    const auto locHelp = language.empty() ? L"Enter a custom language key, such as braz_por. Use lowercase letters and underscores." : L"Localization files: *_l_" + language + L".yml; header: l_" + language + L":";
    SetDlgItemTextW(hwnd, IDC_LANGUAGE_HELP, locHelp.c_str());
    for (const auto ids : {std::pair<int,int>{IDC_GAMEPATH, IDC_GAMEPATH_HELP}, {IDC_LOGSPATH, IDC_LOGSPATH_HELP}}) {
        const auto path = trim(text(hwnd, ids.first));
        const auto attributes = GetFileAttributesW(path.c_str());
        const wchar_t* help;
        if (path.empty()) help = ids.first == IDC_GAMEPATH ? L"Not detected automatically. Blank: vanilla game files are not indexed." : L"Blank: use bundled script docs. Choose your generated docs to match your game version.";
        else if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) help = L"Folder not found. Check the path or choose Browse.";
        else help = ids.first == IDC_GAMEPATH ? L"Use the game's game subfolder, not the launcher folder. Changes apply after Save." : L"Use the folder containing generated script_docs files. Changes apply after Save.";
        SetDlgItemTextW(hwnd, ids.second, help);
    }
    const auto mode = choiceValue(hwnd, IDC_MODE);
    SetDlgItemTextW(hwnd, IDC_MODE_HELP, mode == L"names" ? L"Insert names without a value or block." : mode == L"examples" ? L"Insert example values and blocks when available." : L"Insert a small value or block template when available.");
    const auto hover = choiceValue(hwnd, IDC_HOVER);
    SetDlgItemTextW(hwnd, IDC_HOVER_HELP, hover == L"compact" ? L"Short descriptions with fewer examples." : hover == L"full" ? L"Longer documentation and more examples." : L"A balance of descriptions and examples.");
}
void browseFolder(HWND hwnd, int id) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog* picker = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker));
    if (SUCCEEDED(result)) {
        DWORD flags = 0; picker->GetOptions(&flags);
        picker->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        picker->SetTitle(id == IDC_GAMEPATH ? L"Choose the game's game folder" : L"Choose the generated script docs folder");
        IShellItem* folder = nullptr;
        const auto current = trim(text(hwnd, id));
        if (!current.empty() && SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&folder)))) { picker->SetFolder(folder); folder->Release(); }
        result = picker->Show(hwnd);
        if (SUCCEEDED(result)) {
            IShellItem* selected = nullptr;
            result = picker->GetResult(&selected);
            if (SUCCEEDED(result)) {
                PWSTR path = nullptr;
                result = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(result)) { SetDlgItemTextW(hwnd, id, path); CoTaskMemFree(path); }
                selected->Release();
            }
        }
        picker->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) optionsStatus(L"Could not open the folder picker. Enter the folder path manually.");
}
void render() {
    if (!panel) return;
    HWND list = GetDlgItem(panel, IDC_RESULTS);
    SendMessage(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    const auto& items = rows[static_cast<int>(selected)];
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& r = items[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(r.label.c_str());
        item.lParam = static_cast<LPARAM>(i);
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        const std::wstring line = r.uri.empty() ? L"" : std::to_wstring(r.position.line + 1);
        const wchar_t* fields[] = {r.file.c_str(), line.c_str(), r.detail.c_str()};
        for (int col = 1; col <= 3; ++col) {
            item.iSubItem = col; item.pszText = const_cast<wchar_t*>(fields[col - 1]);
            SendMessageW(list, LVM_SETITEMTEXTW, i, reinterpret_cast<LPARAM>(&item));
        }
    }
    SendMessage(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    SetDlgItemTextW(panel, IDC_STATUS, messages[static_cast<int>(selected)].c_str());
    const bool problems = selected == PanelTab::Problems;
    EnableWindow(GetDlgItem(panel, IDC_CURRENT), problems);
    EnableWindow(GetDlgItem(panel, IDC_SEVERITY), problems);
    EnableWindow(GetDlgItem(panel, IDC_APPLY), canApply && selected == PanelTab::Actions);
    layout();
}
void layout() {
    if (!panel) return;
    RECT r; GetClientRect(panel, &r);
    const int scale = static_cast<int>(GetDpiForWindow(panel));
    auto px = [scale](int n) { return MulDiv(n, scale, 96); };
    const int w = r.right, h = r.bottom, pad = px(8), gap = px(5), button = px(92);
    MoveWindow(GetDlgItem(panel, IDC_TABS), pad, pad, w - pad * 2, px(26), TRUE);
    const bool query = selected == PanelTab::Symbols || selected == PanelTab::Actions;
    const bool problems = selected == PanelTab::Problems;
    ShowWindow(GetDlgItem(panel, IDC_QUERY), query ? SW_SHOW : SW_HIDE);
    std::vector<int> controls;
    if (selected == PanelTab::Symbols) controls.push_back(IDC_SEARCH);
    if (selected == PanelTab::Actions) { controls.push_back(IDC_RENAME); controls.push_back(IDC_APPLY); }
    controls.push_back(IDC_REFRESH); controls.push_back(IDC_OPTIONS);
    for (int id : {IDC_SEARCH, IDC_RENAME, IDC_APPLY, IDC_REFRESH, IDC_OPTIONS}) ShowWindow(GetDlgItem(panel, id), std::find(controls.begin(), controls.end(), id) != controls.end() ? SW_SHOW : SW_HIDE);
    int x = pad, y = px(40);
    if (query) {
        const int available = w - pad * 2 - static_cast<int>(controls.size()) * (button + gap);
        const int queryWidth = available >= px(150) ? available : w - pad * 2;
        MoveWindow(GetDlgItem(panel, IDC_QUERY), x, y, queryWidth, px(25), TRUE);
        if (available >= px(150)) x += queryWidth + gap; else y += px(30);
    }
    for (int id : controls) {
        if (x + button > w - pad) { x = pad; y += px(30); }
        MoveWindow(GetDlgItem(panel, id), x, y, button, px(25), TRUE); x += button + gap;
    }
    for (int id : {IDC_SEVERITY, IDC_CURRENT}) ShowWindow(GetDlgItem(panel, id), problems ? SW_SHOW : SW_HIDE);
    if (problems) {
        if (x + px(260) > w - pad) { x = pad; y += px(30); }
        MoveWindow(GetDlgItem(panel, IDC_SEVERITY), x, y, px(120), px(150), TRUE);
        MoveWindow(GetDlgItem(panel, IDC_CURRENT), x + px(130), y, px(130), px(22), TRUE);
    }
    const int listY = y + px(62);
    MoveWindow(GetDlgItem(panel, IDC_STATUS), pad, y + px(30), (std::max)(10, w - pad * 2), px(28), TRUE);
    HWND list = GetDlgItem(panel, IDC_RESULTS);
    MoveWindow(list, pad, listY, (std::max)(10, w - pad * 2), (std::max)(10, h - listY - pad), TRUE);
    const int width = (std::max)(px(400), w - pad * 2 - px(24));
    ListView_SetColumnWidth(list, 0, width * 34 / 100);
    ListView_SetColumnWidth(list, 1, width * 25 / 100);
    ListView_SetColumnWidth(list, 2, px(48));
    ListView_SetColumnWidth(list, 3, width * 41 / 100 - px(48));
}
void activate() {
    const int index = ListView_GetNextItem(GetDlgItem(panel, IDC_RESULTS), -1, LVNI_SELECTED);
    auto& items = rows[static_cast<int>(selected)];
    if (index >= 0 && static_cast<size_t>(index) < items.size()) {
        const auto row = items[index]; // handler may replace the list
        handler(PanelAction::Navigate, &row, text(panel, IDC_QUERY));
    }
}
INT_PTR CALLBACK panelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_INITDIALOG: {
        panel = hwnd;
        const wchar_t* names[] = {L"Problems", L"Outline", L"References", L"Symbols", L"Changes"};
        for (int i = 0; i < 5; ++i) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(names[i]); SendDlgItemMessageW(hwnd, IDC_TABS, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item)); }
        combo(hwnd, IDC_SEVERITY, {L"All severities", L"Errors", L"Warnings", L"Information", L"Hints"}, L"All severities");
        HWND list = GetDlgItem(hwnd, IDC_RESULTS);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        const wchar_t* columns[] = {L"Item", L"File", L"Line", L"Details"};
        for (int i = 0; i < 4; ++i) { LVCOLUMNW col{}; col.mask = LVCF_TEXT; col.pszText = const_cast<wchar_t*>(columns[i]); SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&col)); }
        SendDlgItemMessageW(hwnd, IDC_QUERY, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Symbol query or new name"));
        layout(); render(); return TRUE;
    }
    case WM_SIZE: layout(); return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDCANCEL) { SendMessage(parent, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(hwnd)); return TRUE; }
        if (LOWORD(w) == IDOK) { if (GetFocus() == GetDlgItem(hwnd, IDC_QUERY)) handler(selected == PanelTab::Actions ? PanelAction::Rename : PanelAction::Search, nullptr, text(hwnd, IDC_QUERY)); else activate(); return TRUE; }
        if ((LOWORD(w) == IDC_SEVERITY && HIWORD(w) == CBN_SELCHANGE) || LOWORD(w) == IDC_CURRENT || LOWORD(w) == IDC_REFRESH) handler(PanelAction::Refresh, nullptr, text(hwnd, IDC_QUERY));
        if (LOWORD(w) == IDC_SEARCH) handler(PanelAction::Search, nullptr, text(hwnd, IDC_QUERY));
        if (LOWORD(w) == IDC_RENAME) handler(PanelAction::Rename, nullptr, text(hwnd, IDC_QUERY));
        if (LOWORD(w) == IDC_APPLY) handler(PanelAction::Apply, nullptr, L"");
        if (LOWORD(w) == IDC_OPTIONS) handler(PanelAction::Options, nullptr, L"");
        return TRUE;
    case WM_NOTIFY: {
        const auto* note = reinterpret_cast<NMHDR*>(l);
        if (note->idFrom == IDC_TABS && note->code == TCN_SELCHANGE) {
            selected = static_cast<PanelTab>(TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_TABS))); render();
            handler(PanelAction::Refresh, nullptr, text(hwnd, IDC_QUERY));
        }
        if (note->idFrom == IDC_RESULTS && (note->code == NM_DBLCLK || note->code == NM_RETURN)) activate();
        return FALSE;
    }
    }
    return FALSE;
}
INT_PTR CALLBACK optionsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM) {
    if (msg == WM_COMMAND) {
        if (LOWORD(w) == IDC_GAME_BROWSE || LOWORD(w) == IDC_LOGS_BROWSE) { browseFolder(hwnd, LOWORD(w) == IDC_GAME_BROWSE ? IDC_GAMEPATH : IDC_LOGSPATH); return TRUE; }
        if (HIWORD(w) == CBN_SELCHANGE || ((LOWORD(w) == IDC_GAMEPATH || LOWORD(w) == IDC_LOGSPATH || LOWORD(w) == IDC_CUSTOMLANGUAGE) && HIWORD(w) == EN_CHANGE)) updateOptionHelp(hwnd);
        if (LOWORD(w) == IDCANCEL) { ShowWindow(hwnd, SW_HIDE); return TRUE; }
        if (LOWORD(w) == IDC_CHECK || LOWORD(w) == IDC_RELEASES) { updateAction(LOWORD(w) == IDC_CHECK); return TRUE; }
        if (LOWORD(w) == IDOK) {
            Settings s;
            s.gameId = choiceValue(hwnd, IDC_GAME); s.gamePath = trim(text(hwnd, IDC_GAMEPATH)); s.logsPath = trim(text(hwnd, IDC_LOGSPATH));
            s.locLanguage = choiceValue(hwnd, IDC_LANGUAGE);
            if (s.locLanguage.empty()) {
                s.locLanguage = trim(text(hwnd, IDC_CUSTOMLANGUAGE));
                if (s.locLanguage.empty() || !std::all_of(s.locLanguage.begin(), s.locLanguage.end(), [](wchar_t c) { return (c >= L'a' && c <= L'z') || c == L'_'; })) {
                    optionsStatus(L"Enter a custom language name using lowercase letters and underscores, for example braz_por. Do not include l_ or a colon.");
                    SetFocus(GetDlgItem(hwnd, IDC_CUSTOMLANGUAGE)); return TRUE;
                }
            }
            s.serverCommand = trim(text(hwnd, IDC_COMMAND));
            s.completionMode = choiceValue(hwnd, IDC_MODE); s.hoverDetail = choiceValue(hwnd, IDC_HOVER);
            s.automaticCompletion = IsDlgButtonChecked(hwnd, IDC_COMPLETION) == BST_CHECKED;
            s.signatureHelp = IsDlgButtonChecked(hwnd, IDC_SIGNATURE) == BST_CHECKED;
            s.syntaxHighlighting = IsDlgButtonChecked(hwnd, IDC_SYNTAX) == BST_CHECKED;
            s.semanticHighlighting = IsDlgButtonChecked(hwnd, IDC_SEMANTIC) == BST_CHECKED;
            s.folding = IsDlgButtonChecked(hwnd, IDC_FOLDING) == BST_CHECKED;
            s.autoUpdateServer = IsDlgButtonChecked(hwnd, IDC_AUTOUPDATE) == BST_CHECKED;
            if (applyOptions(s)) optionsStatus(L"Saved. Editor options applied; the server was restarted.");
            return TRUE;
        }
    }
    if (msg == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return TRUE; }
    return FALSE;
}
}
void initPanel(HINSTANCE inst, HWND owner, int commandIndex, PanelHandler fn) {
    instance = inst; parent = owner; menuIndex = commandIndex; handler = std::move(fn);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&controls);
}
void showPanel(PanelTab tab) {
    selected = tab;
    if (!panel) {
        CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_PANEL), parent, panelProc, 0);
        docking.hClient = panel; docking.pszName = L"Paradox Toolkit";
        docking.dlgID = menuIndex; docking.uMask = DWS_DF_CONT_BOTTOM; docking.pszModuleName = L"PxToolkit.dll";
        SendMessage(parent, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&docking));
        SendMessage(parent, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, reinterpret_cast<LPARAM>(panel));
    }
    TabCtrl_SetCurSel(GetDlgItem(panel, IDC_TABS), static_cast<int>(tab)); render();
    SendMessage(parent, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(panel));
}
void setPanelRows(PanelTab tab, std::vector<PanelRow> items, const std::wstring& status) {
    rows[static_cast<int>(tab)] = std::move(items); messages[static_cast<int>(tab)] = status;
    if (selected == tab) render();
}
void panelStatus(const std::wstring& status) { messages[static_cast<int>(selected)] = status; if (panel) SetDlgItemTextW(panel, IDC_STATUS, status.c_str()); }
PanelTab panelTab() { return selected; }
int problemSeverity() { return panel ? static_cast<int>(SendDlgItemMessage(panel, IDC_SEVERITY, CB_GETCURSEL, 0, 0)) : 0; }
bool currentFileOnly() { return panel && IsDlgButtonChecked(panel, IDC_CURRENT) == BST_CHECKED; }
void enableApply(bool enable) { canApply = enable; if (panel) EnableWindow(GetDlgItem(panel, IDC_APPLY), enable && selected == PanelTab::Actions); }
void showOptions(const Settings& s, std::function<bool(const Settings&)> apply, std::function<void(bool)> update) {
    applyOptions = std::move(apply); updateAction = std::move(update);
    if (!options) {
        options = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_OPTIONS), parent, optionsProc, 0);
        SendMessage(parent, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, reinterpret_cast<LPARAM>(options));
        SendMessage(parent, NPPM_DARKMODESUBCLASSANDTHEME, NppDarkMode::dmfInit, reinterpret_cast<LPARAM>(options));
    }
    choiceCombo(options, IDC_GAME, games, s.gameId);
    choiceCombo(options, IDC_MODE, completionModes, s.completionMode);
    choiceCombo(options, IDC_HOVER, hoverDetails, s.hoverDetail);
    choiceCombo(options, IDC_LANGUAGE, languages, s.locLanguage, 9);
    SetDlgItemTextW(options, IDC_GAMEPATH, s.gamePath.c_str()); SetDlgItemTextW(options, IDC_LOGSPATH, s.logsPath.c_str());
    SetDlgItemTextW(options, IDC_CUSTOMLANGUAGE, choiceValue(options, IDC_LANGUAGE).empty() ? s.locLanguage.c_str() : L"");
    SetDlgItemTextW(options, IDC_COMMAND, s.serverCommand.c_str());
    SendDlgItemMessage(options, IDC_GAMEPATH, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Optional: path to your game's game folder"));
    SendDlgItemMessage(options, IDC_LOGSPATH, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Optional: leave blank for bundled script docs"));
    SendDlgItemMessage(options, IDC_CUSTOMLANGUAGE, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Choose Other above to enter a custom language"));
    updateOptionHelp(options);
    const int ids[] = {IDC_COMPLETION, IDC_SIGNATURE, IDC_SYNTAX, IDC_SEMANTIC, IDC_FOLDING, IDC_AUTOUPDATE};
    const bool values[] = {s.automaticCompletion, s.signatureHelp, s.syntaxHighlighting, s.semanticHighlighting, s.folding, s.autoUpdateServer};
    for (int i = 0; i < 6; ++i) CheckDlgButton(options, ids[i], values[i] ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(options, SW_SHOW); SetForegroundWindow(options);
}
void optionsStatus(const std::wstring& status) { if (options) SetDlgItemTextW(options, IDC_OPTIONSTATUS, status.c_str()); }
void themeOptions() { if (options) SendMessage(parent, NPPM_DARKMODESUBCLASSANDTHEME, NppDarkMode::dmfHandleChange, reinterpret_cast<LPARAM>(options)); }
void destroyPanels() {
    for (HWND hwnd : {options, panel}) if (hwnd) { SendMessage(parent, NPPM_MODELESSDIALOG, MODELESSDIALOGREMOVE, reinterpret_cast<LPARAM>(hwnd)); DestroyWindow(hwnd); }
    options = panel = nullptr;
}
}
