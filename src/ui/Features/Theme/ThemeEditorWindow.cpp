#include "ShaderLab/UI/ShaderLabIDE.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")

namespace ShaderLab {

namespace fs = std::filesystem;

void ShaderLabIDE::ShowThemeEditorPopup() {
    if (m_showThemeEditor) {
        ImGui::OpenPopup("Theme Editor");
        m_showThemeEditor = false;
    }
    float dpiScale = 1.0f;

    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        dpiScale = viewport->DpiScale;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(800.0f * dpiScale, 600.0f * dpiScale),
            ImVec2(viewport->Size.x * 0.95f, viewport->Size.y * 0.95f));
        ImGui::SetNextWindowSize(ImVec2(820.0f * dpiScale, 620.0f * dpiScale), ImGuiCond_Appearing);
    }
    if (!ImGui::BeginPopupModal("Theme Editor", nullptr, ImGuiWindowFlags_NoCollapse)) {
        return;
    }

    ImFont* compactFont = m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)];
    const bool pushedCompactFont = (compactFont != nullptr);
    if (pushedCompactFont) {
        ImGui::PushFont(compactFont);
    }

    ImGui::TextUnformatted("Theme name");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("##ThemeName", m_themeNameBuffer, sizeof(m_themeNameBuffer));

    char backgroundPathBuffer[512];
    std::snprintf(backgroundPathBuffer, sizeof(backgroundPathBuffer), "%s", m_uiThemeColors.BackgroundImage.c_str());
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::InputText("BackgroundImage", backgroundPathBuffer, sizeof(backgroundPathBuffer))) {
        m_uiThemeColors.BackgroundImage = backgroundPathBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        char filePath[512] = {};
        if (!m_uiThemeColors.BackgroundImage.empty()) {
            fs::path currentPath(m_uiThemeColors.BackgroundImage);
            if (currentPath.is_relative()) {
                currentPath = fs::path(m_appRoot) / currentPath;
            }
            const std::string currentPathString = currentPath.lexically_normal().string();
            std::strncpy(filePath, currentPathString.c_str(), sizeof(filePath) - 1);
        }
        const std::string defaultThemeDir = (fs::path(m_appRoot) / "editor_assets" / "themes").lexically_normal().string();
        char initialDir[MAX_PATH] = {};
        std::strncpy(initialDir, defaultThemeDir.c_str(), sizeof(initialDir) - 1);
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = sizeof(filePath);
        ofn.lpstrInitialDir = initialDir;
        ofn.lpstrFilter = "Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.ppm)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.ppm\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) {
            fs::path selected = fs::path(filePath).lexically_normal();
            std::error_code ec;
            fs::path relative = fs::relative(selected, fs::path(m_appRoot), ec);
            const std::string relativeStr = relative.generic_string();
            if (!ec && !relative.empty() && relativeStr.rfind("..", 0) != 0) {
                m_uiThemeColors.BackgroundImage = relative.generic_string();
            } else {
                m_uiThemeColors.BackgroundImage = selected.string();
            }
        }
    }
    ImGui::TextDisabled("Relative paths resolve from app root (e.g. editor_assets/themes/tile.png)");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("ControlOpacity", &m_uiThemeColors.ControlOpacity, 0.15f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("PanelOpacity", &m_uiThemeColors.PanelOpacity, 0.15f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("PanelHeadingOpacity", &m_uiThemeColors.PanelHeadingOpacity, 0.15f, 1.0f, "%.2f");

    const ImGuiColorEditFlags pickerFlags = ImGuiColorEditFlags_AlphaBar |
                                            ImGuiColorEditFlags_AlphaPreviewHalf |
                                            ImGuiColorEditFlags_Uint8 |
                                            ImGuiColorEditFlags_DisplayRGB |
                                            ImGuiColorEditFlags_InputRGB; 
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiDir previousColorButtonPosition = style.ColorButtonPosition;
    style.ColorButtonPosition = ImGuiDir_Left;

    auto CompactColorEdit4 = [&](const char* label, ImVec4& color) {
        float width = 200.0f * dpiScale;
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushItemWidth(width);
        ImGui::ColorEdit4(label, &color.x, pickerFlags);
        ImGui::PopItemWidth();
    };
    ImGui::SeparatorText("Theme Contexts");
    if (ImGui::BeginTable("ThemeContextColumns", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Colors", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Font Scale", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        const float controlsFooterHeight = ImGui::GetFrameHeightWithSpacing() + 12.0f;
        const float contextListHeight = (std::max)(320.0f, ImGui::GetContentRegionAvail().y - controlsFooterHeight);

        ImGui::TableNextColumn();
        ImGui::BeginChild("ThemeColorList", ImVec2(0.0f, contextListHeight), true);
        ImGui::SeparatorText("Global");
        CompactColorEdit4("Accent Dim##LinesAccentColorDim", m_uiThemeColors.LinesAccentColorDim);

        ImGui::SeparatorText("Controls & Buttons");
        CompactColorEdit4("Control Bg##ControlBackground", m_uiThemeColors.ControlBackground);
        CompactColorEdit4("Control Text##ControlFontColor", m_uiThemeColors.ControlFontColor);
        CompactColorEdit4("Icon##IconColor", m_uiThemeColors.IconColor);
        CompactColorEdit4("Button Icon##ButtonIconColor", m_uiThemeColors.ButtonIconColor);
        CompactColorEdit4("Button Label##ButtonLabelColor", m_uiThemeColors.ButtonLabelColor);
        CompactColorEdit4("Button Bg##ButtonBackgroundColor", m_uiThemeColors.ButtonBackgroundColor);

        ImGui::SeparatorText("Panels & Tabs");
        CompactColorEdit4("Panel Bg##PanelBackground", m_uiThemeColors.PanelBackground);
        CompactColorEdit4("Window Bg##WindowBackground", m_uiThemeColors.WindowBackground);
        CompactColorEdit4("Title Bg##PanelTitleBackground", m_uiThemeColors.PanelTitleBackground);
        CompactColorEdit4("Title Text##PanelTitleFontColor", m_uiThemeColors.PanelTitleFontColor);
        CompactColorEdit4("Active Title Bg##ActivePanelTitleColor", m_uiThemeColors.ActivePanelTitleColor);
        CompactColorEdit4("Active Panel Bg##ActivePanelBackground", m_uiThemeColors.ActivePanelBackground);
        CompactColorEdit4("Passive Title Bg##PassivePanelTitleColor", m_uiThemeColors.PassivePanelTitleColor);
        CompactColorEdit4("Passive Panel Bg##PassivePanelBackground", m_uiThemeColors.PassivePanelBackground);
        CompactColorEdit4("Active Tab Text##ActiveTabFontColor", m_uiThemeColors.ActiveTabFontColor);
        CompactColorEdit4("Active Tab Bg##ActiveTabBackground", m_uiThemeColors.ActiveTabBackground);
        CompactColorEdit4("Passive Tab Text##PassiveTabFontColor", m_uiThemeColors.PassiveTabFontColor);
        CompactColorEdit4("Passive Tab Bg##PassiveTabBackground", m_uiThemeColors.PassiveTabBackground);

        ImGui::SeparatorText("Tracker");
        CompactColorEdit4("Heading Bg##TrackerHeadingBackground", m_uiThemeColors.TrackerHeadingBackground);
        CompactColorEdit4("Heading Text##TrackerHeadingFontColor", m_uiThemeColors.TrackerHeadingFontColor);
        CompactColorEdit4("Accent Beat Bg##TrackerAccentBeatBackground", m_uiThemeColors.TrackerAccentBeatBackground);
        CompactColorEdit4("Accent Beat Text##TrackerAccentBeatFontColor", m_uiThemeColors.TrackerAccentBeatFontColor);
        CompactColorEdit4("Beat Bg##TrackerBeatBackground", m_uiThemeColors.TrackerBeatBackground);
        CompactColorEdit4("Beat Text##TrackerBeatFontColor", m_uiThemeColors.TrackerBeatFontColor);

        ImGui::SeparatorText("Text & Console");
        CompactColorEdit4("Label Text##LabelFontColor", m_uiThemeColors.LabelFontColor);
        CompactColorEdit4("Logo Text##LogoFontColor", m_uiThemeColors.LogoFontColor);
        CompactColorEdit4("Console Text##ConsoleFontColor", m_uiThemeColors.ConsoleFontColor);
        CompactColorEdit4("Console Bg##ConsoleBackground", m_uiThemeColors.ConsoleBackground);
        CompactColorEdit4("Status Text##StatusFontColor", m_uiThemeColors.StatusFontColor);

        ImGui::SeparatorText("Performance Overlay");
        CompactColorEdit4("Perf Text##PerfOverlayFontColor", m_uiThemeColors.PerfOverlayFontColor);
        CompactColorEdit4("Perf Bg##PerfOverlayBackground", m_uiThemeColors.PerfOverlayBackground);
        CompactColorEdit4("Perf Border##PerfOverlayBorder", m_uiThemeColors.PerfOverlayBorder);
        CompactColorEdit4("Perf Bar Bg##PerfOverlayBarBackground", m_uiThemeColors.PerfOverlayBarBackground);
        CompactColorEdit4("Perf Bar Border##PerfOverlayBarBorder", m_uiThemeColors.PerfOverlayBarBorder);
        CompactColorEdit4("Perf Bar Text##PerfOverlayBarText", m_uiThemeColors.PerfOverlayBarText);
        CompactColorEdit4("Perf VRAM OK##PerfOverlayVramOk", m_uiThemeColors.PerfOverlayVramOk);
        CompactColorEdit4("Perf VRAM Warn##PerfOverlayVramWarn", m_uiThemeColors.PerfOverlayVramWarn);
        CompactColorEdit4("Perf VRAM Crit##PerfOverlayVramCritical", m_uiThemeColors.PerfOverlayVramCritical);
        ImGui::EndChild();

        ImGui::TableNextColumn();
        float sliderWidth = 200.0f * dpiScale;
        ImGui::BeginChild("ThemeScaleList", ImVec2(0.0f, contextListHeight), true);
        ImGui::SeparatorText("Primary UI");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Control Scale##ControlFontScale", &m_uiThemeColors.ControlFontScale, 0.50f, 2.00f, "%.2f");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Menu Scale##MenuFontScale", &m_uiThemeColors.MenuFontScale, 0.50f, 2.00f, "%.2f");

        ImGui::SeparatorText("Editor & Tracker");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Numeric Scale##NumericFontScale", &m_uiThemeColors.NumericFontScale, 0.50f, 2.00f, "%.2f");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Code Scale##CodeFontScale", &m_uiThemeColors.CodeFontScale, 0.50f, 2.00f, "%.2f");

        ImGui::SeparatorText("Console & Status");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Console Scale##ConsoleFontScale", &m_uiThemeColors.ConsoleFontScale, 0.50f, 2.00f, "%.2f");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Status Scale##StatusFontScale", &m_uiThemeColors.StatusFontScale, 0.50f, 2.00f, "%.2f");

        ImGui::SeparatorText("Brand");
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("Logo Scale##LogoFontScale", &m_uiThemeColors.LogoFontScale, 0.50f, 2.00f, "%.2f");
        ImGui::EndChild();

        ImGui::EndTable();
    }

    ApplyUiTheme();

    if (ImGui::Button("Save Theme", ImVec2(160.0f, 0.0f))) {
        std::string themeName = m_themeNameBuffer;
        if (!themeName.empty() && AddOrReplaceCustomTheme(themeName, m_uiThemeColors)) {
            m_activeThemeName = themeName;
            SaveUiThemeSettings();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Default", ImVec2(200.0f, 0.0f))) {
        auto shaderPunk = std::find_if(m_customThemes.begin(), m_customThemes.end(), [&](const NamedUITheme& t) {
            return t.name == "ShaderPunk";
        });
        m_uiThemeColors = (shaderPunk != m_customThemes.end()) ? shaderPunk->colors : UIThemeColors{};
        m_activeThemeName = "ShaderPunk";
        ApplyUiTheme();
        SaveUiThemeSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(140.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    if (pushedCompactFont) {
        ImGui::PopFont();
    }

    style.ColorButtonPosition = previousColorButtonPosition;

    ImGui::EndPopup();
}

} // namespace ShaderLab
