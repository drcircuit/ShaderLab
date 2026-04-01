#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UIConfig.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Audio/AudioSystem.h"
#include "ShaderLab/UI/UISystemAssets.h"
#include "ShaderLab/UI/AboutAssets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

#include <shlobj.h>

#pragma comment(lib, "Shell32.lib")

namespace ShaderLab {

namespace {
namespace fs = std::filesystem;

int CALLBACK WorkspaceBrowseCallback(HWND hwnd, UINT uMsg, LPARAM, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData != 0) {
        const char* initialPath = reinterpret_cast<const char*>(lpData);
        if (initialPath && initialPath[0] != '\0') {
            SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, reinterpret_cast<LPARAM>(initialPath));
        }
    }
    return 0;
}

void StoreWorkspaceFolderInRegistry(const std::string& workspaceFolder) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    const LSTATUS createStatus = RegCreateKeyExA(
        HKEY_CURRENT_USER,
        "Software\\ShaderLab",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        &disposition);
    (void)disposition;
    if (createStatus != ERROR_SUCCESS || key == nullptr) {
        return;
    }

    const DWORD bytes = static_cast<DWORD>(workspaceFolder.size() + 1);
    RegSetValueExA(
        key,
        "WorkspaceFolder",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(workspaceFolder.c_str()),
        bytes);
    RegCloseKey(key);
}
} // namespace

void ShaderLabIDE::ChooseWorkspaceFolder() {
    char displayName[MAX_PATH] = {};
    std::string initial = m_workspaceRootPath;

    const HRESULT coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool coInitializedHere = SUCCEEDED(coInitResult);

    HWND ownerWindow = m_hwnd;
    if (!ownerWindow && ImGui::GetCurrentContext()) {
        if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
            ownerWindow = static_cast<HWND>(viewport->PlatformHandleRaw);
        }
    }

    BROWSEINFOA browseInfo = {};
    browseInfo.hwndOwner = ownerWindow;
    browseInfo.pidlRoot = nullptr;
    browseInfo.pszDisplayName = displayName;
    browseInfo.lpszTitle = "Choose Shader Workspace Folder";
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX;
    browseInfo.lpfn = WorkspaceBrowseCallback;
    browseInfo.lParam = reinterpret_cast<LPARAM>(initial.c_str());

    PIDLIST_ABSOLUTE selectedItem = SHBrowseForFolderA(&browseInfo);
    if (!selectedItem) {
        if (coInitializedHere) {
            CoUninitialize();
        }
        return;
    }

    char selectedPath[MAX_PATH] = {};
    if (SHGetPathFromIDListA(selectedItem, selectedPath)) {
        const fs::path chosen = fs::path(selectedPath).lexically_normal();
        if (!chosen.empty()) {
            m_workspaceRootPath = chosen.string();
            m_workspaceExplicitlyConfigured = true;
            m_workspaceSelectionPromptPending = false;
            EnsureWorkspaceFolders();
            StoreWorkspaceFolderInRegistry(m_workspaceRootPath);
            InitializePresetService(m_workspaceRootPath, m_appRoot);
            AboutAssets::Get().Initialize(m_workspaceRootPath, m_appRoot);
            LoadGlobalSnippets();
            AppendDemoLog(std::string("[workspace] Active workspace set to ") + m_workspaceRootPath);
        }
    }

    CoTaskMemFree(selectedItem);
    if (coInitializedHere) {
        CoUninitialize();
    }
}

void ShaderLabIDE::ShowMainMenuBar() {
    const float ds = m_dpiScale;
    const ImFont* menuFont = m_fontMenuSmall ? m_fontMenuSmall : ImGui::GetFont();
    const float menuFontPx = menuFont ? menuFont->LegacySize : ImGui::GetFontSize();
    const float kMinTitleStripHeight = menuFontPx + UIConfig::MenuFramePadY * ds * 2.0f + UIConfig::MenuTopPad * ds + UIConfig::MenuBottomPad * ds;
    float titlebarStripHeight = (std::max)(UIConfig::MenuBarHeight * ds, kMinTitleStripHeight);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(UIConfig::MenuFramePadX * ds, UIConfig::MenuFramePadY * ds));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(UIConfig::MenuItemSpacingX * ds, UIConfig::MenuItemSpacingY * ds));
    if (ImGui::BeginMenuBar()) {

        const float startY = ImGui::GetCursorPosY();
        const float frameHeight = ImGui::GetFrameHeight();
        const float minHeight = 0.0f;
        const float autoHeight = (std::max)(frameHeight + UIConfig::MenuTopPad * ds + UIConfig::MenuBottomPad * ds, minHeight);
        const float targetHeight = UIConfig::MenuBarHeight > 0.0f
            ? (std::max)(UIConfig::MenuBarHeight * ds, minHeight)
            : autoHeight;
        titlebarStripHeight = targetHeight;
        ImGui::SetCursorPosY(startY + UIConfig::MenuTopPad * ds);

        ImGui::Dummy(ImVec2(UIConfig::MenuLeftPad * ds, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);

        ImFont* compactMenuFont = m_fontMenuSmall
            ? m_fontMenuSmall
            : m_fontCodeSizes[static_cast<int>(CodeFontSize::S)];
        if (compactMenuFont) {
            ImGui::PushFont(compactMenuFont);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.LabelFontColor);

        float menuMaxX = ImGui::GetCursorScreenPos().x;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                RefreshPresetService();
                m_scenes.clear();
                CreateDefaultScene();
                m_activeSceneIndex = 0;
                SetActiveScene(0);
                m_track = DemoTrack();
                CreateDefaultTrack();
                m_audioLibrary.clear();
                m_demoTitle = "Untitled Demo";
                m_demoAuthor.clear();
                m_demoDescription.clear();
                CreateNewProjectInWorkspace(m_demoTitle);
                if (m_audioSystem) m_audioSystem->Stop();
            }
            if (ImGui::MenuItem("Open", "Ctrl+O")) {
                OpenProject();
                RefreshPresetService();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                SaveProject();
                RefreshPresetService();
            }
            if (ImGui::MenuItem("Save As...")) {
                SaveProjectAs();
                RefreshPresetService();
            }
            if (ImGui::MenuItem("Choose Workspace Folder...")) {
                ChooseWorkspaceFolder();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build Packaged Demo...")) {
                BuildPackagedDemoProject();
            }
            if (ImGui::MenuItem("Build Self-Contained Demo...")) {
                BuildProject();
            }
            if (ImGui::MenuItem("Build Self-Contained Screensaver...")) {
                BuildScreenSaverProject();
            }
            if (ImGui::MenuItem("Build Micro-Demo...")) {
                BuildMicroDemoProject();
            }
            if (ImGui::MenuItem("Build Micro-Demo (Developer Overlay)...")) {
                BuildMicroDeveloperDemoProject();
            }
            if (ImGui::MenuItem("Build Settings...")) {
                m_showBuildSettings = true;
                m_buildSettingsRefreshRequested = true;
            }
            if (ImGui::MenuItem("Export Runtime Package...")) {
                ExportRuntimePackage();
            }
            ImGui::Separator();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {}
            ImGui::EndMenu();
        }
        menuMaxX = (std::max)(menuMaxX, ImGui::GetItemRectMax().x);
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Demo Mode", nullptr, m_currentMode == UIMode::Demo)) {
                m_currentMode = UIMode::Demo;
            }
            if (ImGui::MenuItem("Scene Mode", nullptr, m_currentMode == UIMode::Scene)) {
                m_currentMode = UIMode::Scene;
            }
            if (ImGui::MenuItem("Post FX Mode", nullptr, m_currentMode == UIMode::PostFX)) {
                m_currentMode = UIMode::PostFX;
            }
            ImGui::EndMenu();
        }
        menuMaxX = (std::max)(menuMaxX, ImGui::GetItemRectMax().x);
        if (ImGui::BeginMenu("Theme")) {
            if (!m_customThemes.empty()) {
                for (const auto& theme : m_customThemes) {
                    const bool selected = (m_activeThemeName == theme.name);
                    if (ImGui::MenuItem(theme.name.c_str(), nullptr, selected)) {
                        m_uiThemeColors = theme.colors;
                        m_activeThemeName = theme.name;
                        ApplyUiTheme();
                        SaveUiThemeSettings();
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Edit Theme...")) {
                std::snprintf(m_themeNameBuffer, sizeof(m_themeNameBuffer), "%s", m_activeThemeName.c_str());
                m_showThemeEditor = true;
            }
            ImGui::EndMenu();
        }
        menuMaxX = (std::max)(menuMaxX, ImGui::GetItemRectMax().x);
        if (ImGui::BeginMenu("Device")) {
            auto adapters = Device::GetAvailableAdapters();
            int currentIdx = -1;
            if (m_deviceRef) currentIdx = m_deviceRef->GetAdapterIndex();
            std::unordered_set<std::string> seenAdapterKeys;

            for (const auto& adapter : adapters) {
                std::string name;
                name.reserve(adapter.name.length());
                for (wchar_t c : adapter.name) name.push_back(static_cast<char>(c));

                float vramGB = static_cast<float>(adapter.videoMemory) / (1024.0f * 1024.0f * 1024.0f);
                std::string displayLabel = name + " (" + std::to_string(vramGB).substr(0, 4) + " GB)";
                std::string adapterKey = name + "|" + std::to_string(adapter.videoMemory);
                if (!seenAdapterKeys.insert(adapterKey).second) {
                    continue;
                }
                std::string label = displayLabel + "###Adapter" + std::to_string(adapter.index);

                bool isSelected = (currentIdx != -1 && (int)adapter.index == currentIdx);

                if (ImGui::MenuItem(label.c_str(), nullptr, isSelected)) {
                    if (m_restartCallback && !isSelected) {
                        m_restartCallback((int)adapter.index);
                    }
                }
            }
            ImGui::EndMenu();
        }
        menuMaxX = (std::max)(menuMaxX, ImGui::GetItemRectMax().x);

        ShowTutsMenu();
        menuMaxX = (std::max)(menuMaxX, ImGui::GetItemRectMax().x);

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                m_showAbout = true;
            }
            ImGui::EndMenu();
        }
        menuMaxX = (std::max)(menuMaxX, ImGui::GetItemRectMax().x);

        const float controlsRightPad = menuFontPx * 5.0f;
        ImFont* statusFont = m_fontCodeSizes[static_cast<int>(CodeFontSize::S)]
            ? m_fontCodeSizes[static_cast<int>(CodeFontSize::S)]
            : m_fontOrbitronText;
        const float statusTextSize = statusFont ? statusFont->LegacySize : ImGui::GetFontSize();
        const float statusFrameHeight = statusTextSize + ImGui::GetStyle().FramePadding.y * 2.0f;
        const float vsyncSlotWidth = (std::max)(112.0f * ds, statusFrameHeight + 8.0f * ds + (statusTextSize * 4.4f));
        const float controlsSpacing = UIConfig::MenuItemSpacingX * ds * 1.5f;
        const float fpsSlotWidth = (std::max)(96.0f * ds, statusTextSize * 7.2f);
        const float controlsWidth = vsyncSlotWidth + controlsSpacing + fpsSlotWidth;
        const float controlsStartX = (std::max)(menuMaxX + UIConfig::MenuItemSpacingX * ds * 2.0f,
            ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - controlsRightPad - controlsWidth);
        const float controlsStartY = ImGui::GetCursorScreenPos().y;

        ImGui::SetCursorScreenPos(ImVec2(controlsStartX, controlsStartY));
        ImFont* vsyncFont = statusFont;
        if (vsyncFont) {
            ImGui::PushFont(vsyncFont);
        }
        ImGui::Checkbox("VSync", &m_previewVsyncEnabled);
        if (vsyncFont) {
            ImGui::PopFont();
        }

        const float fpsX = controlsStartX + vsyncSlotWidth + controlsSpacing;
        ImGui::SetCursorScreenPos(ImVec2(fpsX, controlsStartY));
        ImFont* fpsFont = statusFont;
        if (fpsFont) {
            ImGui::PushFont(fpsFont);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.StatusFontColor);
        ImGui::Text("FPS %.1f", ImGui::GetIO().Framerate);
        ImGui::PopStyleColor();
        if (fpsFont) {
            ImGui::PopFont();
        }

        const float controlsEndX = fpsX + fpsSlotWidth;

        if (compactMenuFont) {
            ImGui::PopFont();
        }
        ImGui::PopStyleColor();

        const float endY = ImGui::GetCursorPosY();
        const float extra = targetHeight - (endY - startY);
        if (extra > 0.0f) {
            ImGui::Dummy(ImVec2(0.0f, extra));
        }

        (void)controlsEndX;

        ImGui::EndMenuBar();
    }
    ImGui::PopStyleVar(2);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float stripHeight = (std::max)(titlebarStripHeight, kMinTitleStripHeight);
    if (viewport && stripHeight > 4.0f) {
        ImDrawList* fg = ImGui::GetWindowDrawList();
        const ImVec2 stripMin = viewport->Pos;
        const ImVec2 stripMax = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + stripHeight);
        fg->PushClipRect(stripMin, stripMax, false);
        ImVec4 stripColor = m_uiThemeColors.PanelTitleBackground;
        stripColor.w = (std::clamp)(m_uiThemeColors.PanelHeadingOpacity, 0.0f, 1.0f);
        fg->AddRectFilled(stripMin, stripMax, ImGui::GetColorU32(stripColor));
        fg->AddLine(ImVec2(stripMin.x, stripMax.y - 1.0f), ImVec2(stripMax.x, stripMax.y - 1.0f), ImGui::GetColorU32(m_uiThemeColors.LinesAccentColorDim), 1.0f);

        const float edgePad = UIConfig::MenuRightPad * ds;
        const float itemPad = UIConfig::MenuItemSpacingX * ds;
        const float buttonSize = (std::max)(menuFontPx, stripHeight - 8.0f * ds);
        const float buttonY = stripMin.y + (stripHeight - buttonSize) * 0.5f;
        const float closeX = stripMax.x - edgePad - buttonSize;
        const float minX = closeX - itemPad - buttonSize;

        auto DrawTitlebarButton = [&](float x, float y, float size, uint32_t iconCode, bool hovered) {
            const ImVec2 rectMin(x, y);
            const ImVec2 rectMax(x + size, y + size);
            const ImU32 bg = ImGui::GetColorU32(hovered ? m_uiThemeColors.ActivePanelBackground : m_uiThemeColors.ButtonBackgroundColor);
            fg->AddRectFilled(rectMin, rectMax, bg, 0.0f);
            fg->AddRect(rectMin, rectMax, ImGui::GetColorU32(m_uiThemeColors.LinesAccentColorDim), 0.0f);

            const bool drawCrossIcon = (iconCode == OpenFontIcons::kX);
            const std::string icon = drawCrossIcon ? std::string() : OpenFontIcons::ToUtf8(iconCode);
            ImFont* iconFont = m_fontMenuSmall ? m_fontMenuSmall : ImGui::GetFont();
            const float iconFontSize = (std::max)(ImGui::GetFontSize(), std::floor(size * 0.70f));

            if (drawCrossIcon) {
                const float pad = (std::max)(3.0f, size * 0.28f);
                const float thickness = (std::max)(1.6f, std::floor(size * 0.09f));
                const ImVec2 a(rectMin.x + pad, rectMin.y + pad);
                const ImVec2 b(rectMax.x - pad, rectMax.y - pad);
                const ImVec2 c(rectMin.x + pad, rectMax.y - pad);
                const ImVec2 d(rectMax.x - pad, rectMin.y + pad);
                const ImU32 color = ImGui::GetColorU32(m_uiThemeColors.ButtonIconColor);
                fg->AddLine(a, b, color, thickness);
                fg->AddLine(c, d, color, thickness);
                return;
            }

            float textX = x;
            float textY = y;
            bool usedGlyphBounds = false;
            if (iconFont) {
                if (ImFontBaked* baked = iconFont->GetFontBaked(iconFontSize)) {
                    if (ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(iconCode))) {
                        const float glyphW = glyph->X1 - glyph->X0;
                        const float glyphH = glyph->Y1 - glyph->Y0;
                        textX = x + (size - glyphW) * 0.5f - glyph->X0;
                        textY = y + (size - glyphH) * 0.5f - glyph->Y0 + 1.0f;
                        if (iconCode == OpenFontIcons::kChevronDown) {
                            textX -= 2.0f;
                            textY += 1.0f;
                        }
                        usedGlyphBounds = true;
                    }
                }
            }
            if (!usedGlyphBounds) {
                const ImVec2 textSize = ImGui::CalcTextSize(icon.c_str());
                textX = x + (size - textSize.x) * 0.5f;
                textY = y + (size - textSize.y) * 0.5f + 1.0f;
                if (iconCode == OpenFontIcons::kChevronDown) {
                    textX -= 2.0f;
                    textY += 1.0f;
                }
            }
            const ImU32 iconColor = ImGui::GetColorU32(m_uiThemeColors.ButtonIconColor);
            const ImVec2 basePos(std::floor(textX), std::floor(textY));
            fg->AddText(iconFont, iconFontSize, basePos, iconColor, icon.c_str());
        };

        const bool canUseWindow = m_hwnd != nullptr;
        const ImVec2 minButtonMin(minX, buttonY);
        const ImVec2 minButtonMax(minX + buttonSize, buttonY + buttonSize);
        const ImVec2 closeButtonMin(closeX, buttonY);
        const ImVec2 closeButtonMax(closeX + buttonSize, buttonY + buttonSize);

        const bool hoverMin = ImGui::IsMouseHoveringRect(minButtonMin, minButtonMax, false);
        const bool hoverClose = ImGui::IsMouseHoveringRect(closeButtonMin, closeButtonMax, false);
        const bool leftClicked = (GetAsyncKeyState(VK_LBUTTON) & 0x0001) != 0;
        const bool minPressed = hoverMin && leftClicked;
        const bool closePressed = hoverClose && leftClicked;

        DrawTitlebarButton(minX, buttonY, buttonSize, OpenFontIcons::kChevronDown, hoverMin);
        DrawTitlebarButton(closeX, buttonY, buttonSize, OpenFontIcons::kX, hoverClose);

        if (canUseWindow) {
            if (minPressed) {
                ShowWindow(m_hwnd, SW_MINIMIZE);
            }
            if (closePressed) {
                PostMessage(m_hwnd, WM_CLOSE, 0, 0);
            }
        }

        float iconRightX = stripMin.x + edgePad;
        if (m_titlebarIconTexture && m_titlebarIconSrvGpuHandle.ptr != 0) {
            const float iconSize = (std::min)(stripHeight - 4.0f, buttonSize + 2.0f);
            const float iconX = stripMin.x + edgePad;
            const float iconY = stripMin.y + (stripHeight - iconSize) * 0.5f;
            fg->AddImage((ImTextureID)m_titlebarIconSrvGpuHandle.ptr, ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize));
            iconRightX = iconX + iconSize + itemPad;
        }

        std::string demoTitle = m_demoTitle;
        if (demoTitle.empty()) {
            demoTitle = GetProjectName();
        }
        if (demoTitle.empty()) {
            demoTitle = "Untitled Demo";
        }

        if (m_fontMenuSmall) {
            ImGui::PushFont(m_fontMenuSmall);
        }
        const ImVec2 titleSize = ImGui::CalcTextSize(demoTitle.c_str());
        const float titleX = viewport->Pos.x + (viewport->Size.x - titleSize.x) * 0.5f;
        const float titleY = viewport->Pos.y + (stripHeight - titleSize.y) * 0.5f + 1.0f;
        fg->AddText(
            ImVec2(std::floor(titleX), std::floor(titleY)),
            ImGui::GetColorU32(m_uiThemeColors.PanelTitleFontColor),
            demoTitle.c_str());
        fg->PopClipRect();
        if (m_fontMenuSmall) {
            ImGui::PopFont();
        }

        (void)iconRightX;
    }
}

} // namespace ShaderLab
