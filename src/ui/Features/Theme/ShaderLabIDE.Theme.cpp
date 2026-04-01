#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/Graphics/Device.h"
#include <nlohmann/json.hpp>
#include "stb_image.h"

#include <imgui.h>

namespace ShaderLab {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path GetGlobalSnippetBaseDir(const std::string& appRoot) {
    fs::path baseDir;
    char* appData = nullptr;
    size_t appDataLen = 0;
    if (_dupenv_s(&appData, &appDataLen, "APPDATA") == 0 && appData && *appData) {
        baseDir = fs::path(appData) / "ShaderLab";
    } else {
        baseDir = fs::path(appRoot) / ".shaderlab";
    }
    if (appData) {
        free(appData);
    }
    return baseDir;
}

fs::path GetUiSettingsPath(const std::string& appRoot) {
    return GetGlobalSnippetBaseDir(appRoot) / "ui_settings.json";
}

ImVec4 JsonToColor(const json& value, const ImVec4& fallback) {
    if (!value.is_array() || value.size() < 4) {
        return fallback;
    }
    ImVec4 color = fallback;
    for (size_t i = 0; i < 4; ++i) {
        if (value[i].is_number()) {
            (&color.x)[i] = value[i].get<float>();
        }
    }
    return color;
}

json ColorToJson(const ImVec4& color) {
    return json::array({ color.x, color.y, color.z, color.w });
}

UIThemeColors DefaultUiThemeColors() {
    return UIThemeColors{};
}

void ApplyCurrentPresetFontScales(UIThemeColors& colors) {
    colors.ControlFontScale = 1.0f;
    colors.MenuFontScale = 1.0f;
    colors.NumericFontScale = 1.0f;
    colors.CodeFontScale = 1.0f;
    colors.ConsoleFontScale = 1.0f;
    colors.LogoFontScale = 1.0f;
    colors.StatusFontScale = 1.0f;
}

std::vector<NamedUITheme> BuiltInThemes() {
    std::vector<NamedUITheme> themes;

    {
        NamedUITheme t;
        t.name = "ShaderPunk";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.PanelOpacity = 0.90f;
        t.colors.PanelHeadingOpacity = 0.95f;
        themes.push_back(t);
    }

    {
        NamedUITheme t;
        t.name = "SandTracker";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.LinesAccentColorDim = ImVec4(0.46f, 0.36f, 0.23f, 0.75f);
        t.colors.ControlBackground = ImVec4(0.22f, 0.18f, 0.13f, 1.0f);
        t.colors.ControlFontColor = ImVec4(0.96f, 0.90f, 0.76f, 1.0f);
        t.colors.IconColor = ImVec4(0.92f, 0.75f, 0.43f, 1.0f);
        t.colors.ButtonIconColor = ImVec4(0.97f, 0.84f, 0.51f, 1.0f);
        t.colors.ButtonLabelColor = ImVec4(0.94f, 0.88f, 0.72f, 1.0f);
        t.colors.ButtonBackgroundColor = ImVec4(0.28f, 0.22f, 0.15f, 1.0f);
        t.colors.PanelBackground = ImVec4(0.15f, 0.12f, 0.09f, 0.95f);
        t.colors.WindowBackground = ImVec4(0.11f, 0.09f, 0.07f, 1.0f);
        t.colors.PanelTitleBackground = ImVec4(0.24f, 0.18f, 0.12f, 1.0f);
        t.colors.LogoFontColor = ImVec4(0.93f, 0.77f, 0.45f, 1.0f);
        t.colors.PanelOpacity = 0.86f;
        t.colors.PanelHeadingOpacity = 0.94f;
        themes.push_back(t);
    }

    {
        NamedUITheme t;
        t.name = "16BitWarrior";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.LinesAccentColorDim = ImVec4(0.22f, 0.77f, 0.42f, 0.75f);
        t.colors.ControlBackground = ImVec4(0.07f, 0.14f, 0.10f, 1.0f);
        t.colors.ControlFontColor = ImVec4(0.74f, 1.0f, 0.70f, 1.0f);
        t.colors.IconColor = ImVec4(0.37f, 1.0f, 0.58f, 1.0f);
        t.colors.ButtonBackgroundColor = ImVec4(0.12f, 0.24f, 0.16f, 1.0f);
        t.colors.PanelBackground = ImVec4(0.04f, 0.11f, 0.07f, 0.95f);
        t.colors.WindowBackground = ImVec4(0.03f, 0.08f, 0.05f, 1.0f);
        t.colors.PanelTitleBackground = ImVec4(0.08f, 0.20f, 0.12f, 1.0f);
        t.colors.ActiveTabBackground = ImVec4(0.14f, 0.40f, 0.23f, 1.0f);
        t.colors.LogoFontColor = ImVec4(0.44f, 1.0f, 0.65f, 1.0f);
        t.colors.PanelOpacity = 0.84f;
        t.colors.PanelHeadingOpacity = 0.92f;
        themes.push_back(t);
    }

    {
        NamedUITheme t;
        t.name = "CodeNinja";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.LinesAccentColorDim = ImVec4(0.24f, 0.25f, 0.27f, 0.75f);
        t.colors.ControlBackground = ImVec4(0.09f, 0.10f, 0.11f, 1.0f);
        t.colors.ControlFontColor = ImVec4(0.87f, 0.89f, 0.92f, 1.0f);
        t.colors.IconColor = ImVec4(0.61f, 0.67f, 0.74f, 1.0f);
        t.colors.ButtonIconColor = ImVec4(0.86f, 0.93f, 1.0f, 1.0f);
        t.colors.ButtonBackgroundColor = ImVec4(0.14f, 0.15f, 0.17f, 1.0f);
        t.colors.PanelBackground = ImVec4(0.07f, 0.08f, 0.10f, 0.95f);
        t.colors.WindowBackground = ImVec4(0.05f, 0.06f, 0.08f, 1.0f);
        t.colors.PanelTitleBackground = ImVec4(0.11f, 0.12f, 0.15f, 1.0f);
        t.colors.ActivePanelTitleColor = ImVec4(0.23f, 0.25f, 0.31f, 1.0f);
        t.colors.LogoFontColor = ImVec4(0.84f, 0.90f, 0.98f, 1.0f);
        t.colors.PanelOpacity = 0.88f;
        t.colors.PanelHeadingOpacity = 0.95f;
        themes.push_back(t);
    }

    {
        NamedUITheme t;
        t.name = "PinkHack";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.LinesAccentColorDim = ImVec4(0.72f, 0.18f, 0.50f, 0.80f);
        t.colors.ControlBackground = ImVec4(0.20f, 0.07f, 0.16f, 1.0f);
        t.colors.ControlFontColor = ImVec4(1.0f, 0.86f, 0.95f, 1.0f);
        t.colors.IconColor = ImVec4(1.0f, 0.45f, 0.86f, 1.0f);
        t.colors.ButtonIconColor = ImVec4(1.0f, 0.66f, 0.92f, 1.0f);
        t.colors.ButtonBackgroundColor = ImVec4(0.30f, 0.10f, 0.24f, 1.0f);
        t.colors.PanelBackground = ImVec4(0.12f, 0.04f, 0.10f, 0.95f);
        t.colors.WindowBackground = ImVec4(0.09f, 0.03f, 0.08f, 1.0f);
        t.colors.PanelTitleBackground = ImVec4(0.24f, 0.08f, 0.20f, 1.0f);
        t.colors.ActiveTabBackground = ImVec4(0.52f, 0.16f, 0.42f, 1.0f);
        t.colors.LogoFontColor = ImVec4(1.0f, 0.58f, 0.89f, 1.0f);
        t.colors.PanelOpacity = 0.84f;
        t.colors.PanelHeadingOpacity = 0.93f;
        themes.push_back(t);
    }

    {
        NamedUITheme t;
        t.name = "Galaxy";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.LinesAccentColorDim = ImVec4(0.32f, 0.36f, 0.69f, 0.72f);
        t.colors.ControlBackground = ImVec4(0.08f, 0.09f, 0.21f, 1.0f);
        t.colors.ControlFontColor = ImVec4(0.86f, 0.90f, 1.0f, 1.0f);
        t.colors.IconColor = ImVec4(0.51f, 0.72f, 1.0f, 1.0f);
        t.colors.ButtonBackgroundColor = ImVec4(0.12f, 0.13f, 0.29f, 1.0f);
        t.colors.PanelBackground = ImVec4(0.05f, 0.06f, 0.14f, 0.95f);
        t.colors.WindowBackground = ImVec4(0.03f, 0.04f, 0.10f, 1.0f);
        t.colors.PanelTitleBackground = ImVec4(0.10f, 0.12f, 0.30f, 1.0f);
        t.colors.ActiveTabBackground = ImVec4(0.20f, 0.30f, 0.62f, 1.0f);
        t.colors.LogoFontColor = ImVec4(0.69f, 0.82f, 1.0f, 1.0f);
        t.colors.BackgroundImage = "editor_assets/themes/galaxy_tile.ppm";
        t.colors.PanelOpacity = 0.80f;
        t.colors.PanelHeadingOpacity = 0.92f;
        themes.push_back(t);
    }

    {
        NamedUITheme t;
        t.name = "Noizr";
        t.colors = DefaultUiThemeColors();
        ApplyCurrentPresetFontScales(t.colors);
        t.colors.LinesAccentColorDim = ImVec4(0.25f, 0.26f, 0.27f, 0.65f);
        t.colors.ControlBackground = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
        t.colors.ControlFontColor = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
        t.colors.IconColor = ImVec4(0.72f, 0.76f, 0.80f, 1.0f);
        t.colors.ButtonBackgroundColor = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
        t.colors.PanelBackground = ImVec4(0.07f, 0.07f, 0.07f, 0.95f);
        t.colors.WindowBackground = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
        t.colors.PanelTitleBackground = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        t.colors.LogoFontColor = ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        t.colors.BackgroundImage = "editor_assets/themes/noizr_tile.ppm";
        t.colors.PanelOpacity = 0.78f;
        t.colors.PanelHeadingOpacity = 0.90f;
        themes.push_back(t);
    }

    return themes;
}

void ApplyThemeColorFromJson(UIThemeColors& colors, const json& src) {
    colors.LinesAccentColorDim = JsonToColor(src.value("LinesAccentColorDim", json::array()), colors.LinesAccentColorDim);
    colors.ControlBackground = JsonToColor(src.value("ControlBackground", json::array()), colors.ControlBackground);
    colors.ControlFontColor = JsonToColor(src.value("ControlFontColor", json::array()), colors.ControlFontColor);
    colors.IconColor = JsonToColor(src.value("IconColor", json::array()), colors.IconColor);
    colors.ButtonIconColor = JsonToColor(src.value("ButtonIconColor", json::array()), colors.ButtonIconColor);
    colors.ButtonLabelColor = JsonToColor(src.value("ButtonLabelColor", json::array()), colors.ButtonLabelColor);
    colors.ButtonBackgroundColor = JsonToColor(src.value("ButtonBackgroundColor", json::array()), colors.ButtonBackgroundColor);
    colors.PanelBackground = JsonToColor(src.value("PanelBackground", json::array()), colors.PanelBackground);
    colors.WindowBackground = JsonToColor(src.value("WindowBackground", json::array()), colors.WindowBackground);
    colors.TrackerHeadingBackground = JsonToColor(src.value("TrackerHeadingBackground", json::array()), colors.TrackerHeadingBackground);
    colors.TrackerHeadingFontColor = JsonToColor(src.value("TrackerHeadingFontColor", json::array()), colors.TrackerHeadingFontColor);
    colors.TrackerAccentBeatBackground = JsonToColor(src.value("TrackerAccentBeatBackground", json::array()), colors.TrackerAccentBeatBackground);
    colors.TrackerAccentBeatFontColor = JsonToColor(src.value("TrackerAccentBeatFontColor", json::array()), colors.TrackerAccentBeatFontColor);
    colors.TrackerBeatBackground = JsonToColor(src.value("TrackerBeatBackground", json::array()), colors.TrackerBeatBackground);
    colors.TrackerBeatFontColor = JsonToColor(src.value("TrackerBeatFontColor", json::array()), colors.TrackerBeatFontColor);
    colors.LabelFontColor = JsonToColor(src.value("LabelFontColor", json::array()), colors.LabelFontColor);
    colors.PanelTitleFontColor = JsonToColor(src.value("PanelTitleFontColor", json::array()), colors.PanelTitleFontColor);
    colors.PanelTitleBackground = JsonToColor(src.value("PanelTitleBackground", json::array()), colors.PanelTitleBackground);
    colors.ActiveTabFontColor = JsonToColor(src.value("ActiveTabFontColor", json::array()), colors.ActiveTabFontColor);
    colors.ActiveTabBackground = JsonToColor(src.value("ActiveTabBackground", json::array()), colors.ActiveTabBackground);
    colors.PassiveTabFontColor = JsonToColor(src.value("PassiveTabFontColor", json::array()), colors.PassiveTabFontColor);
    colors.PassiveTabBackground = JsonToColor(src.value("PassiveTabBackground", json::array()), colors.PassiveTabBackground);
    colors.ActivePanelTitleColor = JsonToColor(src.value("ActivePanelTitleColor", json::array()), colors.ActivePanelTitleColor);
    colors.ActivePanelBackground = JsonToColor(src.value("ActivePanelBackground", json::array()), colors.ActivePanelBackground);
    colors.PassivePanelTitleColor = JsonToColor(src.value("PassivePanelTitleColor", json::array()), colors.PassivePanelTitleColor);
    colors.PassivePanelBackground = JsonToColor(src.value("PassivePanelBackground", json::array()), colors.PassivePanelBackground);
    colors.LogoFontColor = JsonToColor(src.value("LogoFontColor", json::array()), colors.LogoFontColor);
    colors.ConsoleFontColor = JsonToColor(src.value("ConsoleFontColor", json::array()), colors.ConsoleFontColor);
    colors.ConsoleBackground = JsonToColor(src.value("ConsoleBackground", json::array()), colors.ConsoleBackground);
    colors.StatusFontColor = JsonToColor(src.value("StatusFontColor", json::array()), colors.StatusFontColor);
    colors.PerfOverlayFontColor = JsonToColor(src.value("PerfOverlayFontColor", json::array()), colors.PerfOverlayFontColor);
    colors.PerfOverlayBackground = JsonToColor(src.value("PerfOverlayBackground", json::array()), colors.PerfOverlayBackground);
    colors.PerfOverlayBorder = JsonToColor(src.value("PerfOverlayBorder", json::array()), colors.PerfOverlayBorder);
    colors.PerfOverlayBarBackground = JsonToColor(src.value("PerfOverlayBarBackground", json::array()), colors.PerfOverlayBarBackground);
    colors.PerfOverlayBarBorder = JsonToColor(src.value("PerfOverlayBarBorder", json::array()), colors.PerfOverlayBarBorder);
    colors.PerfOverlayBarText = JsonToColor(src.value("PerfOverlayBarText", json::array()), colors.PerfOverlayBarText);
    colors.PerfOverlayVramOk = JsonToColor(src.value("PerfOverlayVramOk", json::array()), colors.PerfOverlayVramOk);
    colors.PerfOverlayVramWarn = JsonToColor(src.value("PerfOverlayVramWarn", json::array()), colors.PerfOverlayVramWarn);
    colors.PerfOverlayVramCritical = JsonToColor(src.value("PerfOverlayVramCritical", json::array()), colors.PerfOverlayVramCritical);
    if (src.contains("ControlFontScale") && src["ControlFontScale"].is_number()) {
        colors.ControlFontScale = (std::clamp)(src["ControlFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("MenuFontScale") && src["MenuFontScale"].is_number()) {
        colors.MenuFontScale = (std::clamp)(src["MenuFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("NumericFontScale") && src["NumericFontScale"].is_number()) {
        colors.NumericFontScale = (std::clamp)(src["NumericFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("CodeFontScale") && src["CodeFontScale"].is_number()) {
        colors.CodeFontScale = (std::clamp)(src["CodeFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("ConsoleFontScale") && src["ConsoleFontScale"].is_number()) {
        colors.ConsoleFontScale = (std::clamp)(src["ConsoleFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("LogoFontScale") && src["LogoFontScale"].is_number()) {
        colors.LogoFontScale = (std::clamp)(src["LogoFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("StatusFontScale") && src["StatusFontScale"].is_number()) {
        colors.StatusFontScale = (std::clamp)(src["StatusFontScale"].get<float>(), 0.5f, 2.0f);
    }
    if (src.contains("ControlOpacity") && src["ControlOpacity"].is_number()) {
        colors.ControlOpacity = (std::clamp)(src["ControlOpacity"].get<float>(), 0.0f, 1.0f);
    }
    if (src.contains("PanelOpacity") && src["PanelOpacity"].is_number()) {
        colors.PanelOpacity = (std::clamp)(src["PanelOpacity"].get<float>(), 0.0f, 1.0f);
    }
    if (src.contains("PanelHeadingOpacity") && src["PanelHeadingOpacity"].is_number()) {
        colors.PanelHeadingOpacity = (std::clamp)(src["PanelHeadingOpacity"].get<float>(), 0.0f, 1.0f);
    }
    colors.BackgroundImage = src.value("BackgroundImage", colors.BackgroundImage);
}

json ThemeColorsToJson(const UIThemeColors& colors) {
    return {
        {"LinesAccentColorDim", ColorToJson(colors.LinesAccentColorDim)},
        {"ControlBackground", ColorToJson(colors.ControlBackground)},
        {"ControlFontColor", ColorToJson(colors.ControlFontColor)},
        {"IconColor", ColorToJson(colors.IconColor)},
        {"ButtonIconColor", ColorToJson(colors.ButtonIconColor)},
        {"ButtonLabelColor", ColorToJson(colors.ButtonLabelColor)},
        {"ButtonBackgroundColor", ColorToJson(colors.ButtonBackgroundColor)},
        {"PanelBackground", ColorToJson(colors.PanelBackground)},
        {"WindowBackground", ColorToJson(colors.WindowBackground)},
        {"TrackerHeadingBackground", ColorToJson(colors.TrackerHeadingBackground)},
        {"TrackerHeadingFontColor", ColorToJson(colors.TrackerHeadingFontColor)},
        {"TrackerAccentBeatBackground", ColorToJson(colors.TrackerAccentBeatBackground)},
        {"TrackerAccentBeatFontColor", ColorToJson(colors.TrackerAccentBeatFontColor)},
        {"TrackerBeatBackground", ColorToJson(colors.TrackerBeatBackground)},
        {"TrackerBeatFontColor", ColorToJson(colors.TrackerBeatFontColor)},
        {"LabelFontColor", ColorToJson(colors.LabelFontColor)},
        {"PanelTitleFontColor", ColorToJson(colors.PanelTitleFontColor)},
        {"PanelTitleBackground", ColorToJson(colors.PanelTitleBackground)},
        {"ActiveTabFontColor", ColorToJson(colors.ActiveTabFontColor)},
        {"ActiveTabBackground", ColorToJson(colors.ActiveTabBackground)},
        {"PassiveTabFontColor", ColorToJson(colors.PassiveTabFontColor)},
        {"PassiveTabBackground", ColorToJson(colors.PassiveTabBackground)},
        {"ActivePanelTitleColor", ColorToJson(colors.ActivePanelTitleColor)},
        {"ActivePanelBackground", ColorToJson(colors.ActivePanelBackground)},
        {"PassivePanelTitleColor", ColorToJson(colors.PassivePanelTitleColor)},
        {"PassivePanelBackground", ColorToJson(colors.PassivePanelBackground)},
        {"LogoFontColor", ColorToJson(colors.LogoFontColor)},
        {"ConsoleFontColor", ColorToJson(colors.ConsoleFontColor)},
        {"ConsoleBackground", ColorToJson(colors.ConsoleBackground)},
        {"StatusFontColor", ColorToJson(colors.StatusFontColor)},
        {"PerfOverlayFontColor", ColorToJson(colors.PerfOverlayFontColor)},
        {"PerfOverlayBackground", ColorToJson(colors.PerfOverlayBackground)},
        {"PerfOverlayBorder", ColorToJson(colors.PerfOverlayBorder)},
        {"PerfOverlayBarBackground", ColorToJson(colors.PerfOverlayBarBackground)},
        {"PerfOverlayBarBorder", ColorToJson(colors.PerfOverlayBarBorder)},
        {"PerfOverlayBarText", ColorToJson(colors.PerfOverlayBarText)},
        {"PerfOverlayVramOk", ColorToJson(colors.PerfOverlayVramOk)},
        {"PerfOverlayVramWarn", ColorToJson(colors.PerfOverlayVramWarn)},
        {"PerfOverlayVramCritical", ColorToJson(colors.PerfOverlayVramCritical)},
        {"ControlFontScale", colors.ControlFontScale},
        {"MenuFontScale", colors.MenuFontScale},
        {"NumericFontScale", colors.NumericFontScale},
        {"CodeFontScale", colors.CodeFontScale},
        {"ConsoleFontScale", colors.ConsoleFontScale},
        {"LogoFontScale", colors.LogoFontScale},
        {"StatusFontScale", colors.StatusFontScale},
        {"ControlOpacity", colors.ControlOpacity},
        {"PanelOpacity", colors.PanelOpacity},
        {"PanelHeadingOpacity", colors.PanelHeadingOpacity},
        {"BackgroundImage", colors.BackgroundImage}
    };
}
} // namespace

void ShaderLabIDE::SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(5, 3);
    style.ItemSpacing = ImVec2(6, 4);

    ApplyUiTheme();
}

void ShaderLabIDE::ApplyUiTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    const UIThemeColors& theme = m_uiThemeColors;
    const float controlOpacity = (std::clamp)(theme.ControlOpacity, 0.0f, 1.0f);
    const float panelOpacity = (std::clamp)(theme.PanelOpacity, 0.0f, 1.0f);
    const float headingOpacity = (std::clamp)(theme.PanelHeadingOpacity, 0.0f, 1.0f);
    auto WithAlpha = [](ImVec4 color, float alpha) {
        color.w = alpha;
        return color;
    };

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = theme.LabelFontColor;
    colors[ImGuiCol_TextDisabled] = theme.StatusFontColor;
    colors[ImGuiCol_TextLink] = theme.ButtonLabelColor;
    colors[ImGuiCol_WindowBg] = WithAlpha(theme.WindowBackground, panelOpacity);
    colors[ImGuiCol_ChildBg] = WithAlpha(theme.PanelBackground, panelOpacity);
    colors[ImGuiCol_PopupBg] = WithAlpha(theme.ControlBackground, controlOpacity);
    colors[ImGuiCol_Border] = theme.LinesAccentColorDim;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg] = WithAlpha(theme.ControlBackground, controlOpacity);
    colors[ImGuiCol_FrameBgHovered] = WithAlpha(theme.ActivePanelBackground, controlOpacity);
    colors[ImGuiCol_FrameBgActive] = WithAlpha(theme.ActivePanelTitleColor, controlOpacity);

    colors[ImGuiCol_TitleBg] = WithAlpha(theme.PassivePanelTitleColor, headingOpacity);
    colors[ImGuiCol_TitleBgActive] = WithAlpha(theme.ActivePanelTitleColor, headingOpacity);
    colors[ImGuiCol_TitleBgCollapsed] = WithAlpha(theme.PassivePanelTitleColor, headingOpacity * 0.9f);

    colors[ImGuiCol_MenuBarBg] = WithAlpha(theme.PanelTitleBackground, headingOpacity);

    colors[ImGuiCol_ScrollbarBg] = theme.PanelBackground;
    colors[ImGuiCol_ScrollbarGrab] = theme.LinesAccentColorDim;
    colors[ImGuiCol_ScrollbarGrabHovered] = theme.ActivePanelTitleColor;
    colors[ImGuiCol_ScrollbarGrabActive] = theme.ActiveTabBackground;

    colors[ImGuiCol_CheckMark] = theme.ButtonIconColor;
    colors[ImGuiCol_SliderGrab] = theme.IconColor;
    colors[ImGuiCol_SliderGrabActive] = theme.ButtonIconColor;

    colors[ImGuiCol_Button] = WithAlpha(theme.ButtonBackgroundColor, panelOpacity);
    colors[ImGuiCol_ButtonHovered] = WithAlpha(theme.ActivePanelBackground, panelOpacity);
    colors[ImGuiCol_ButtonActive] = WithAlpha(theme.ActivePanelTitleColor, headingOpacity);

    colors[ImGuiCol_Header] = WithAlpha(theme.PanelTitleBackground, headingOpacity);
    colors[ImGuiCol_HeaderHovered] = WithAlpha(theme.ActivePanelBackground, panelOpacity);
    colors[ImGuiCol_HeaderActive] = WithAlpha(theme.ActivePanelTitleColor, headingOpacity);

    colors[ImGuiCol_Separator] = theme.LinesAccentColorDim;
    colors[ImGuiCol_SeparatorHovered] = theme.ActivePanelTitleColor;
    colors[ImGuiCol_SeparatorActive] = theme.ActiveTabBackground;

    colors[ImGuiCol_ResizeGrip] = theme.LinesAccentColorDim;
    colors[ImGuiCol_ResizeGripHovered] = theme.ActivePanelTitleColor;
    colors[ImGuiCol_ResizeGripActive] = theme.ActiveTabBackground;

    colors[ImGuiCol_Tab] = WithAlpha(theme.PassiveTabBackground, headingOpacity);
    colors[ImGuiCol_TabHovered] = WithAlpha(theme.ActivePanelBackground, headingOpacity);
    colors[ImGuiCol_TabActive] = WithAlpha(theme.ActiveTabBackground, headingOpacity);
    colors[ImGuiCol_TabUnfocused] = WithAlpha(theme.PassiveTabBackground, headingOpacity);
    colors[ImGuiCol_TabUnfocusedActive] = WithAlpha(theme.ActiveTabBackground, headingOpacity);

    colors[ImGuiCol_TableHeaderBg] = WithAlpha(theme.TrackerHeadingBackground, headingOpacity);
    colors[ImGuiCol_TableBorderStrong] = theme.LinesAccentColorDim;
    colors[ImGuiCol_TableBorderLight] = theme.LinesAccentColorDim;
    colors[ImGuiCol_TableRowBg] = WithAlpha(theme.TrackerBeatBackground, panelOpacity);
    colors[ImGuiCol_TableRowBgAlt] = WithAlpha(theme.PanelBackground, panelOpacity);

    colors[ImGuiCol_TextSelectedBg] = theme.ActiveTabBackground;
    colors[ImGuiCol_DragDropTarget] = theme.IconColor;
    colors[ImGuiCol_NavHighlight] = theme.ActivePanelTitleColor;
    colors[ImGuiCol_NavWindowingHighlight] = theme.ControlFontColor;
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);

    colors[ImGuiCol_PlotLines] = theme.IconColor;
    colors[ImGuiCol_PlotLinesHovered] = theme.ButtonIconColor;
    colors[ImGuiCol_PlotHistogram] = theme.IconColor;
    colors[ImGuiCol_PlotHistogramHovered] = theme.ButtonIconColor;

    ApplyCodeEditorControlOpacity();
    EnsureThemeBackgroundTexture();
}

void ShaderLabIDE::ApplyCodeEditorControlOpacity() {
    const float controlOpacity = (std::clamp)(m_uiThemeColors.ControlOpacity, 0.0f, 1.0f);
    auto ApplyOpacityToEditorBackground = [controlOpacity](TextEditor& editor) {
        TextEditor::Palette palette = editor.GetPalette();
        const int bgIndex = static_cast<int>(TextEditor::PaletteIndex::Background);
        ImVec4 bg = ImGui::ColorConvertU32ToFloat4(palette[bgIndex]);
        bg.w = controlOpacity;
        palette[bgIndex] = ImGui::ColorConvertFloat4ToU32(bg);
        editor.SetPalette(palette);
    };

    ApplyOpacityToEditorBackground(m_textEditor);
    ApplyOpacityToEditorBackground(m_snippetTextEditor);
}

bool ShaderLabIDE::AddOrReplaceCustomTheme(const std::string& name, const UIThemeColors& colors) {
    if (name.empty()) {
        return false;
    }

    auto it = std::find_if(m_customThemes.begin(), m_customThemes.end(), [&](const NamedUITheme& item) {
        return item.name == name;
    });

    if (it != m_customThemes.end()) {
        it->colors = colors;
        return true;
    }

    m_customThemes.push_back({ name, colors });
    return true;
}

void ShaderLabIDE::LoadUiThemeSettings() {
    m_uiThemeColors = DefaultUiThemeColors();
    m_customThemes.clear();
    for (const auto& theme : BuiltInThemes()) {
        AddOrReplaceCustomTheme(theme.name, theme.colors);
    }
    m_activeThemeName = "ShaderPunk";
    std::snprintf(m_themeNameBuffer, sizeof(m_themeNameBuffer), "%s", "");

    const fs::path settingsPath = GetUiSettingsPath(m_appRoot);
    std::ifstream in(settingsPath, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    json root;
    try {
        in >> root;
    } catch (...) {
        return;
    }

    const json themeRoot = root.value("theme", json::object());
    m_activeThemeName = themeRoot.value("active", std::string("ShaderPunk"));
    ApplyThemeColorFromJson(m_uiThemeColors, themeRoot.value("activeColors", json::object()));

    const json themes = themeRoot.value("themes", json::array());
    if (themes.is_array()) {
        for (const auto& item : themes) {
            if (!item.is_object()) {
                continue;
            }
            const std::string name = item.value("name", std::string());
            if (name.empty()) {
                continue;
            }

            UIThemeColors colors = DefaultUiThemeColors();
            auto existing = std::find_if(m_customThemes.begin(), m_customThemes.end(), [&](const NamedUITheme& t) { return t.name == name; });
            if (existing != m_customThemes.end()) {
                colors = existing->colors;
            }
            ApplyThemeColorFromJson(colors, item.value("colors", json::object()));
            AddOrReplaceCustomTheme(name, colors);
        }
    }

    auto it = std::find_if(m_customThemes.begin(), m_customThemes.end(), [&](const NamedUITheme& item) {
        return item.name == m_activeThemeName;
    });
    if (it != m_customThemes.end()) {
        m_uiThemeColors = it->colors;
    } else {
        m_activeThemeName = "ShaderPunk";
        auto fallback = std::find_if(m_customThemes.begin(), m_customThemes.end(), [&](const NamedUITheme& item) {
            return item.name == m_activeThemeName;
        });
        if (fallback != m_customThemes.end()) {
            m_uiThemeColors = fallback->colors;
        }
    }
}

void ShaderLabIDE::SaveUiThemeSettings() const {
    const fs::path settingsPath = GetUiSettingsPath(m_appRoot);
    std::error_code ec;
    fs::create_directories(settingsPath.parent_path(), ec);

    json root;
    std::ifstream in(settingsPath, std::ios::binary);
    if (in.is_open()) {
        try {
            in >> root;
        } catch (...) {
            root = json::object();
        }
    }

    json themes = json::array();
    for (const auto& item : m_customThemes) {
        themes.push_back({
            {"name", item.name},
            {"colors", ThemeColorsToJson(item.colors)}
        });
    }

    root["theme"] = {
        {"active", m_activeThemeName},
        {"themes", themes},
        {"activeColors", ThemeColorsToJson(m_uiThemeColors)}
    };

    std::ofstream out(settingsPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << root.dump(2);
}

void ShaderLabIDE::EnsureThemeBackgroundTexture() {
    std::string requestedPath = m_uiThemeColors.BackgroundImage;
    if (requestedPath.empty()) {
        m_themeBackgroundTexture.Reset();
        m_themeBackgroundSrvGpuHandle = {};
        m_themeBackgroundWidth = 0;
        m_themeBackgroundHeight = 0;
        m_loadedThemeBackgroundPath.clear();
        return;
    }

    fs::path resolvedPath(requestedPath);
    if (resolvedPath.is_relative()) {
        resolvedPath = fs::path(m_appRoot) / resolvedPath;
    }
    resolvedPath = resolvedPath.lexically_normal();
    requestedPath = resolvedPath.string();

    if (requestedPath == m_loadedThemeBackgroundPath && m_themeBackgroundTexture && m_themeBackgroundSrvGpuHandle.ptr != 0) {
        return;
    }

    m_themeBackgroundTexture.Reset();
    m_themeBackgroundSrvGpuHandle = {};
    m_themeBackgroundWidth = 0;
    m_themeBackgroundHeight = 0;
    m_loadedThemeBackgroundPath.clear();

    if (!m_deviceRef || !m_srvHeap || !fs::exists(resolvedPath)) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(requestedPath.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
        if (data) stbi_image_free(data);
        return;
    }

    CreateTextureFromData(data, width, height, 4, m_themeBackgroundTexture);
    stbi_image_free(data);
    if (!m_themeBackgroundTexture) {
        return;
    }

    constexpr UINT kThemeBackgroundSrvIndex = 126;
    const UINT descriptorSize = m_deviceRef->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(kThemeBackgroundSrvIndex) * descriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_deviceRef->GetDevice()->CreateShaderResourceView(m_themeBackgroundTexture.Get(), &srvDesc, cpuHandle);

    m_themeBackgroundSrvGpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_themeBackgroundSrvGpuHandle.ptr += static_cast<SIZE_T>(kThemeBackgroundSrvIndex) * descriptorSize;
    m_themeBackgroundWidth = width;
    m_themeBackgroundHeight = height;
    m_loadedThemeBackgroundPath = requestedPath;
}

void ShaderLabIDE::DrawThemeBackgroundTiled() {
    if (!m_themeBackgroundTexture || m_themeBackgroundSrvGpuHandle.ptr == 0 || m_themeBackgroundWidth <= 0 || m_themeBackgroundHeight <= 0) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    ImDrawList* bg = ImGui::GetBackgroundDrawList(viewport);
    const ImVec2 origin = viewport->Pos;
    const ImVec2 maxPos = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    const float tileW = static_cast<float>(m_themeBackgroundWidth);
    const float tileH = static_cast<float>(m_themeBackgroundHeight);

    if (tileW < 1.0f || tileH < 1.0f) {
        return;
    }

    for (float y = origin.y; y < maxPos.y; y += tileH) {
        for (float x = origin.x; x < maxPos.x; x += tileW) {
            const ImVec2 pMin(x, y);
            const ImVec2 pMax((std::min)(x + tileW, maxPos.x), (std::min)(y + tileH, maxPos.y));

            const float uMax = (pMax.x - pMin.x) / tileW;
            const float vMax = (pMax.y - pMin.y) / tileH;
            bg->AddImage((ImTextureID)m_themeBackgroundSrvGpuHandle.ptr, pMin, pMax, ImVec2(0.0f, 0.0f), ImVec2(uMax, vMax), IM_COL32(255, 255, 255, 255));
        }
    }
}

} // namespace ShaderLab
