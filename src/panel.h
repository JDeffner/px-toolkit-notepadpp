#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "settings.h"
#include "textpos.h"
#include "../third_party/nlohmann/json.hpp"

namespace px {
enum class PanelTab { Problems, Outline, References, Symbols, Actions };
struct PanelRow {
    std::wstring label, file, detail;
    std::string uri;
    Position position;
    nlohmann::json data;
};
enum class PanelAction { Navigate, Refresh, Search, Rename, Apply, Options };
using PanelHandler = std::function<void(PanelAction, const PanelRow*, const std::wstring&)>;
void initPanel(HINSTANCE instance, HWND parent, int menuIndex, PanelHandler handler);
void showPanel(PanelTab tab);
void setPanelRows(PanelTab tab, std::vector<PanelRow> rows, const std::wstring& status);
void panelStatus(const std::wstring& status);
PanelTab panelTab();
int problemSeverity();
bool currentFileOnly();
void enableApply(bool enable);
void showOptions(const Settings& settings, std::function<bool(const Settings&)> apply, std::function<void(bool)> update);
void optionsStatus(const std::wstring& status);
void themeOptions();
void destroyPanels();
}
