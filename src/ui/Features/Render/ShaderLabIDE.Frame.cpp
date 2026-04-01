#include "ShaderLab/UI/ShaderLabIDE.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "ShaderLab/UI/UIConfig.h"
#include "ShaderLab/Graphics/Device.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <imgui_internal.h>

namespace ShaderLab {

namespace {
struct PerformanceOverlayModel {
    const char* modeName = "Demo";
    float fps = 0.0f;
    float frameMs = 0.0f;
    uint32_t previewWidth = 0;
    uint32_t previewHeight = 0;
    double vramUsageGB = 0.0;
    double vramBudgetGB = 0.0;
    double vramPercent = 0.0;
    int activeCompute = 0;
    bool showComputeLine = false;
};

struct PerformanceOverlayStyle {
    float padX = 8.0f;
    float padY = 6.0f;
    float boxWidth = 320.0f;
    float barHeight = 14.0f;
    float barGapFromText = 6.0f;
    float cornerRounding = 4.0f;
    ImU32 background = IM_COL32(0, 0, 0, 176);
    ImU32 border = IM_COL32(0, 200, 200, 210);
    ImU32 barBackground = IM_COL32(35, 35, 35, 220);
    ImU32 barBorder = IM_COL32(160, 160, 160, 220);
    ImU32 barText = IM_COL32(240, 240, 240, 255);
    ImU32 vramOk = IM_COL32(60, 220, 100, 255);
    ImU32 vramWarn = IM_COL32(240, 205, 70, 255);
    ImU32 vramCritical = IM_COL32(245, 90, 90, 255);
    float vramWarnThreshold = 70.0f;
    float vramCriticalThreshold = 90.0f;
};

PerformanceOverlayStyle BuildPerformanceOverlayStyle(const UIThemeColors& theme) {
    PerformanceOverlayStyle style;
    float dpiScale = 1.0f;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        dpiScale = (std::max)(1.0f, viewport->DpiScale);
    }

    style.background = ImGui::GetColorU32(theme.PerfOverlayBackground);
    style.border = ImGui::GetColorU32(theme.PerfOverlayBorder);
    style.barBackground = ImGui::GetColorU32(theme.PerfOverlayBarBackground);
    style.barBorder = ImGui::GetColorU32(theme.PerfOverlayBarBorder);
    style.barText = ImGui::GetColorU32(theme.PerfOverlayBarText);
    style.vramOk = ImGui::GetColorU32(theme.PerfOverlayVramOk);
    style.vramWarn = ImGui::GetColorU32(theme.PerfOverlayVramWarn);
    style.vramCritical = ImGui::GetColorU32(theme.PerfOverlayVramCritical);
    style.padX *= dpiScale;
    style.padY *= dpiScale;
    style.boxWidth *= dpiScale;
    style.barHeight *= dpiScale;
    style.barGapFromText *= dpiScale;
    style.cornerRounding *= dpiScale;
    return style;
}

ImU32 GetVramUsageColor(double vramPercent, const PerformanceOverlayStyle& style) {
    if (vramPercent < static_cast<double>(style.vramWarnThreshold)) {
        return style.vramOk;
    }
    if (vramPercent < static_cast<double>(style.vramCriticalThreshold)) {
        return style.vramWarn;
    }
    return style.vramCritical;
}

void DrawPerformanceOverlay(ImDrawList* drawList,
                            const ImVec2& overlayPos,
                            const PerformanceOverlayModel& model,
                            const PerformanceOverlayStyle& style,
                            ImU32 textColor) {
    if (!drawList) {
        return;
    }

    char line0[128] = {};
    char line1[128] = {};
    char line2[128] = {};
    char line3[128] = {};
    char line4[128] = {};
    char line5[128] = {};
    char line6[128] = {};
    std::snprintf(line0, sizeof(line0), "FPS: %.1f", model.fps);
    std::snprintf(line1, sizeof(line1), "Frame: %.2f ms", model.frameMs);
    std::snprintf(line2, sizeof(line2), "Preview: %ux%u", model.previewWidth, model.previewHeight);
    std::snprintf(line3, sizeof(line3), "Mode: %s", model.modeName);
    std::snprintf(line4,
                  sizeof(line4),
                  "VRAM: %.2f / %.2f GB (%.1f%%)",
                  model.vramUsageGB,
                  model.vramBudgetGB,
                  model.vramPercent);
    std::snprintf(line5, sizeof(line5), "Compute: %d active", model.activeCompute);
    std::snprintf(line6, sizeof(line6), "Alt+D stats | Alt+V vsync");

    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const int lineCount = model.showComputeLine ? 7 : 6;
    const float boxHeight = style.padY * 2.0f +
                            lineHeight * static_cast<float>(lineCount) +
                            style.barHeight +
                            style.barGapFromText;

    const ImU32 vramColor = GetVramUsageColor(model.vramPercent, style);
    const float vramRatio = static_cast<float>((std::clamp)(model.vramPercent / 100.0, 0.0, 1.0));

    drawList->AddRectFilled(
        overlayPos,
        ImVec2(overlayPos.x + style.boxWidth, overlayPos.y + boxHeight),
        style.background,
        style.cornerRounding);
    drawList->AddRect(
        overlayPos,
        ImVec2(overlayPos.x + style.boxWidth, overlayPos.y + boxHeight),
        style.border,
        style.cornerRounding,
        0,
        1.0f);

    ImVec2 textPos(overlayPos.x + style.padX, overlayPos.y + style.padY);
    drawList->AddText(textPos, textColor, line0); textPos.y += lineHeight;
    drawList->AddText(textPos, textColor, line1); textPos.y += lineHeight;
    drawList->AddText(textPos, textColor, line2); textPos.y += lineHeight;
    drawList->AddText(textPos, textColor, line3); textPos.y += lineHeight;
    if (model.showComputeLine) {
        drawList->AddText(textPos, textColor, line5);
        textPos.y += lineHeight;
    }
    drawList->AddText(textPos, textColor, line6);

    const ImVec2 barMin(overlayPos.x + style.padX, overlayPos.y + boxHeight - style.padY - style.barHeight);
    const ImVec2 barMax(overlayPos.x + style.boxWidth - style.padX, barMin.y + style.barHeight);
    drawList->AddRectFilled(barMin, barMax, style.barBackground, 3.0f);
    const float barFillWidth = (barMax.x - barMin.x) * vramRatio;
    if (barFillWidth > 0.0f) {
        drawList->AddRectFilled(barMin, ImVec2(barMin.x + barFillWidth, barMax.y), vramColor, 3.0f);
    }
    drawList->AddRect(barMin, barMax, style.barBorder, 3.0f, 0, 1.0f);

    char barText[96] = {};
    std::snprintf(barText,
                  sizeof(barText),
                  "VRAM %.1f%%  (%.2f / %.2f GB)",
                  model.vramPercent,
                  model.vramUsageGB,
                  model.vramBudgetGB);
    const ImVec2 barTextSize = ImGui::CalcTextSize(barText);
    const ImVec2 barTextPos(
        barMin.x + ((barMax.x - barMin.x) - barTextSize.x) * 0.5f,
        barMin.y + ((barMax.y - barMin.y) - barTextSize.y) * 0.5f);
    drawList->AddText(barTextPos, style.barText, barText);
}

bool IsModifierKey(ImGuiKey key) {
    return key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
           key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
           key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
           key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper;
}

std::string BaseScreenKeyName(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_Space: return "Space";
        case ImGuiKey_Tab: return "Tab";
        case ImGuiKey_Enter: return "Enter";
        case ImGuiKey_KeypadEnter: return "Numpad Enter";
        case ImGuiKey_Backspace: return "Backspace";

        case ImGuiKey_LeftCtrl:
        case ImGuiKey_RightCtrl:
            return "Ctrl";
        case ImGuiKey_LeftShift:
        case ImGuiKey_RightShift:
            return "Shift";
        case ImGuiKey_LeftAlt:
        case ImGuiKey_RightAlt:
            return "Alt";
        case ImGuiKey_LeftSuper:
        case ImGuiKey_RightSuper:
            return "Super";
        default:
            break;
    }

    const char* name = ImGui::GetKeyName(key);
    return (name && name[0] != '\0') ? std::string(name) : std::string();
}

std::string FormatScreenKeyEntry(ImGuiKey key, const ImGuiIO& io) {
    const std::string base = BaseScreenKeyName(key);
    if (base.empty()) {
        return {};
    }

    if (IsModifierKey(key)) {
        return base;
    }

    std::string out;
    if (io.KeyCtrl) out += "Ctrl+";
    if (io.KeyShift) out += "Shift+";
    if (io.KeyAlt) out += "Alt+";
    if (io.KeySuper) out += "Super+";
    out += base;
    return out;
}
}

void ShaderLabIDE::BeginFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    UpdatePreviewVideoExportBeginFrame();

    m_aboutTimeSeconds = ImGui::GetTime();

    ImGuiIO& io = ImGui::GetIO();
    const bool altDown = io.KeyAlt;
    const bool ctrlDown = io.KeyCtrl;
    const bool shiftDown = io.KeyShift;
    if (altDown && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        m_shaderState.showPerformanceOverlay = !m_shaderState.showPerformanceOverlay;
    }
#if SHADERLAB_DEBUG
    if (altDown && !ctrlDown && !shiftDown && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        m_dbgShowFeatureTogglePanel = !m_dbgShowFeatureTogglePanel;
    }
#endif
    if (altDown && !ctrlDown && !shiftDown && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        m_previewFullscreen = !m_previewFullscreen;
    }
    if (altDown && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        m_previewVsyncEnabled = !m_previewVsyncEnabled;
    }
    if (ctrlDown && !altDown && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        OpenProject();
    }
    if (ctrlDown && !altDown && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        SaveProject();
    }
    if (ctrlDown && shiftDown && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        m_screenKeysOverlayEnabled = !m_screenKeysOverlayEnabled;
    }

    if (m_screenKeysOverlayEnabled && m_currentMode == UIMode::Scene) {
        for (int keyValue = (int)ImGuiKey_Keyboard_BEGIN; keyValue < (int)ImGuiKey_Keyboard_END; ++keyValue) {
            const ImGuiKey key = static_cast<ImGuiKey>(keyValue);
            if (!ImGui::IsKeyPressed(key, false)) {
                continue;
            }

            if (key == ImGuiKey_K && ctrlDown && shiftDown) {
                continue;
            }

            const bool isShortcutChord = io.KeyCtrl || io.KeyAlt || io.KeySuper;
            if (isShortcutChord) {
                continue;
            }

            std::string entry = FormatScreenKeyEntry(key, io);
            if (entry.empty()) {
                continue;
            }

            m_screenKeyLog.push_back(entry);
            if (m_screenKeyLog.size() > 160) {
                m_screenKeyLog.erase(m_screenKeyLog.begin(), m_screenKeyLog.begin() + (m_screenKeyLog.size() - 160));
            }
        }
    }

    // Setup fullscreen dockspace
    EnsureThemeBackgroundTexture();
    DrawThemeBackgroundTiled();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImFont* effectiveFont = m_fontMenuSmall ? m_fontMenuSmall : ImGui::GetFont();
    const float menuFontSize = effectiveFont ? effectiveFont->LegacySize : ImGui::GetFontSize();
    const float ds = m_dpiScale;
    const float titlebarPadY = (std::max)(
        UIConfig::MenuBarHeight * ds,
        menuFontSize + UIConfig::MenuFramePadY * ds * 2.0f + UIConfig::MenuTopPad * ds + UIConfig::MenuBottomPad * ds);
    ImVec2 titlebarPad(UIConfig::TitlebarPadX * ds, titlebarPadY);
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + titlebarPad.x, viewport->Pos.y + titlebarPad.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - titlebarPad.x * 2.0f, viewport->Size.y - titlebarPad.y * 2.0f));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    windowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("MainDockspace", nullptr, windowFlags);
    ImGui::PopStyleVar(2);

    // Show menu bar first
    ShowMainMenuBar();
    ShowThemeEditorPopup();

    // Show mode tabs below menu bar
    UIMode pendingMode = m_currentMode;
    UIMode requestedMode = m_currentMode;
    bool forceSelect = false;
    if (altDown && ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
        requestedMode = UIMode::Demo;
        forceSelect = true;
    } else if (altDown && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        requestedMode = UIMode::Scene;
        forceSelect = true;
    } else if (altDown && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        requestedMode = UIMode::PostFX;
        forceSelect = true;
    }

    if (forceSelect) {
        pendingMode = requestedMode;
    }

    if (ImGui::BeginTabBar("ModeTabBar", ImGuiTabBarFlags_None)) {
        const bool allowTabSwitch = !forceSelect;
        ImGuiTabItemFlags demoFlags = (forceSelect && requestedMode == UIMode::Demo) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        ImGui::PushStyleColor(ImGuiCol_Text, (m_currentMode == UIMode::Demo) ? m_uiThemeColors.ActiveTabFontColor : m_uiThemeColors.PassiveTabFontColor);
        if (ImGui::BeginTabItem("Demo", nullptr, demoFlags)) {
            if (allowTabSwitch) {
                pendingMode = UIMode::Demo;
            }
            ImGui::EndTabItem();
        }
        ImGui::PopStyleColor();
        ImGuiTabItemFlags sceneFlags = (forceSelect && requestedMode == UIMode::Scene) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        ImGui::PushStyleColor(ImGuiCol_Text, (m_currentMode == UIMode::Scene) ? m_uiThemeColors.ActiveTabFontColor : m_uiThemeColors.PassiveTabFontColor);
        if (ImGui::BeginTabItem("Scene", nullptr, sceneFlags)) {
            if (allowTabSwitch) {
                pendingMode = UIMode::Scene;
            }
            ImGui::EndTabItem();
        }
        ImGui::PopStyleColor();
        ImGuiTabItemFlags postFlags = (forceSelect && requestedMode == UIMode::PostFX) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        ImGui::PushStyleColor(ImGuiCol_Text, (m_currentMode == UIMode::PostFX) ? m_uiThemeColors.ActiveTabFontColor : m_uiThemeColors.PassiveTabFontColor);
        if (ImGui::BeginTabItem("Post FX", nullptr, postFlags)) {
            if (allowTabSwitch) {
                pendingMode = UIMode::PostFX;
            }
            ImGui::EndTabItem();
        }
        ImGui::PopStyleColor();
        ImGui::EndTabBar();
    }

    m_titlebarHeight = ImGui::GetCursorPosY();

    // Create dockspace below tabs
    ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Apply mode change immediately so layout + windows stay in sync
    if (pendingMode != m_currentMode) {
        m_currentMode = pendingMode;
    }

    // Build layout if mode changed or first run
    bool modeChanged = (m_currentMode != m_lastMode);
    const float kModeFlashDuration = 0.25f;
    if (!m_layoutBuilt || modeChanged) {
        BuildLayout(m_currentMode);
        m_layoutBuilt = true;
        m_lastMode = m_currentMode;
        m_modeChangeFlashSeconds = kModeFlashDuration;
    }

    ImGui::End();

    // Show transport controls
    ShowTransportControls();

    // Sync editor state on mode change to avoid cross-mode text bleeding
    if (modeChanged) {
        if (m_currentMode == UIMode::PostFX) {
            if (m_postFxSelectedIndex < 0 && !m_postFxDraftChain.empty()) {
                m_postFxSelectedIndex = 0;
            }
            SyncPostFxEditorToSelection();
        } else if (m_currentMode == UIMode::Scene || m_currentMode == UIMode::Demo) {
            SetActiveScene(m_activeSceneIndex);
        }
    }

    // Show mode-specific windows
    ShowModeWindows();

    if (m_showAbout) {
        ShowAboutWindow();
    }

    if (m_showBuildSettings) {
        ShowBuildSettingsWindow();
    }

    ShowTutsPopup();

#if SHADERLAB_DEBUG
    // ── Debug feature toggle panel (Alt+G) ──
    if (m_dbgShowFeatureTogglePanel) {
        ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug Feature Toggles (Alt+G)", &m_dbgShowFeatureTogglePanel)) {
            ImGui::TextDisabled("Toggle features to isolate perf regression");
            ImGui::Separator();
            ImGui::Text("UI Panels:");
            ImGui::Checkbox("Scene List", &m_dbgShowSceneList);
            ImGui::Checkbox("Snippet Bin", &m_dbgShowSnippetBin);
            ImGui::Checkbox("Scene Post Stack", &m_dbgShowScenePostStack);
            ImGui::Checkbox("Textures & Channels", &m_dbgShowTexturesAndChannels);
            ImGui::Checkbox("Palette Generator", &m_dbgShowPaletteGenerator);
            ImGui::Checkbox("Shader Editor", &m_dbgShowShaderEditor);
            ImGui::Checkbox("Diagnostics", &m_dbgShowDiagnostics);
            ImGui::Checkbox("Preview Window", &m_dbgShowPreviewWindow);
            ImGui::Checkbox("PostFX Windows", &m_dbgShowPostEffectsWindows);
            ImGui::Separator();
            ImGui::Text("Render Path:");
            ImGui::Checkbox("Render Preview Texture", &m_dbgEnableRenderPreview);
            ImGui::Checkbox("Auto-Compile in Render", &m_dbgEnableAutoCompile);
            ImGui::Separator();
            if (ImGui::Button("Enable All")) {
                m_dbgShowSceneList = m_dbgShowSnippetBin = m_dbgShowScenePostStack = true;
                m_dbgShowTexturesAndChannels = m_dbgShowPaletteGenerator = true;
                m_dbgShowShaderEditor = m_dbgShowDiagnostics = m_dbgShowPreviewWindow = true;
                m_dbgShowPostEffectsWindows = true;
                m_dbgEnableRenderPreview = m_dbgEnableAutoCompile = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable All UI")) {
                m_dbgShowSceneList = m_dbgShowSnippetBin = m_dbgShowScenePostStack = false;
                m_dbgShowTexturesAndChannels = m_dbgShowPaletteGenerator = false;
                m_dbgShowShaderEditor = m_dbgShowDiagnostics = m_dbgShowPreviewWindow = false;
                m_dbgShowPostEffectsWindows = false;
            }
        }
        ImGui::End();
    }
#endif

    if (m_shaderState.showPerformanceOverlay) {
        ImGuiViewport* overlayViewport = ImGui::GetMainViewport();
        if (overlayViewport) {
            ImDrawList* fg = ImGui::GetForegroundDrawList(overlayViewport);
            const ImVec2 overlayPos(overlayViewport->WorkPos.x + 10.0f, overlayViewport->WorkPos.y + 10.0f);

            PerformanceOverlayModel overlayModel;
            overlayModel.modeName = m_currentMode == UIMode::Demo
                ? "Demo"
                : (m_currentMode == UIMode::Scene ? "Scene" : "PostFX");
            overlayModel.showComputeLine = (m_currentMode == UIMode::PostFX);
            if (overlayModel.showComputeLine) {
                for (const auto& fx : m_computeEffectDraftChain) {
                    if (fx.enabled) {
                        ++overlayModel.activeCompute;
                    }
                }
            }

            overlayModel.fps = ImGui::GetIO().Framerate;
            overlayModel.frameMs = (overlayModel.fps > 0.0f) ? (1000.0f / overlayModel.fps) : 0.0f;
            overlayModel.previewWidth = m_previewTextureWidth;
            overlayModel.previewHeight = m_previewTextureHeight;
            if (m_deviceRef) {
                const auto mem = m_deviceRef->GetVideoMemoryInfo();
                constexpr double kBytesPerGB = 1024.0 * 1024.0 * 1024.0;
                overlayModel.vramUsageGB = static_cast<double>(mem.usage) / kBytesPerGB;
                overlayModel.vramBudgetGB = static_cast<double>(mem.budget) / kBytesPerGB;
                if (mem.budget > 0) {
                    overlayModel.vramPercent = (static_cast<double>(mem.usage) * 100.0) / static_cast<double>(mem.budget);
                }
            }

            const PerformanceOverlayStyle overlayStyle = BuildPerformanceOverlayStyle(m_uiThemeColors);
            DrawPerformanceOverlay(
                fg,
                overlayPos,
                overlayModel,
                overlayStyle,
                ImGui::GetColorU32(m_uiThemeColors.PerfOverlayFontColor));
        }
    }

    if (m_modeChangeFlashSeconds > 0.0f) {
        m_modeChangeFlashSeconds = (std::max)(0.0f, m_modeChangeFlashSeconds - ImGui::GetIO().DeltaTime);
        float t = (kModeFlashDuration > 0.0f) ? (m_modeChangeFlashSeconds / kModeFlashDuration) : 0.0f;
        float alpha = 0.35f * t;

        ImVec4 color = ImVec4(0.0f, 0.75f, 0.75f, alpha);
        if (m_currentMode == UIMode::Demo) {
            color = ImVec4(0.2f, 0.6f, 1.0f, alpha);
        } else if (m_currentMode == UIMode::PostFX) {
            color = ImVec4(1.0f, 0.6f, 0.2f, alpha);
        }

        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImVec2 min = mainViewport->WorkPos;
        ImVec2 max = ImVec2(min.x + mainViewport->WorkSize.x, min.y + mainViewport->WorkSize.y);
        ImU32 col = ImGui::GetColorU32(color);
        ImGui::GetForegroundDrawList()->AddRect(min, max, col, 0.0f, 0, 3.0f);
    }

    UpdateBuildLogic();
}

void ShaderLabIDE::EndFrame() {
    ImGui::Render();
}

bool ShaderLabIDE::StartPreviewVideoExport(const std::string& outputPath, uint32_t width, uint32_t height, uint32_t fps) {
    if (outputPath.empty() || width == 0 || height == 0 || fps == 0) {
        m_previewVideoExportStatus = "Invalid export settings.";
        return false;
    }
    if (!m_deviceRef || !m_previewRenderer) {
        m_previewVideoExportStatus = "Preview renderer is not initialized.";
        return false;
    }

    CancelPreviewVideoExport(false);

    const float bpm = (m_track.bpm > 0.0f)
        ? m_track.bpm
        : ((m_transport.bpm > 0.0f) ? m_transport.bpm : 120.0f);
    double durationSeconds = (std::max)(0.1, static_cast<double>(m_previewVideoExportSeconds));
    if (m_previewVideoExportUseFullTimeline && m_currentMode == UIMode::Demo && m_track.lengthBeats > 0) {
        durationSeconds = static_cast<double>(m_track.lengthBeats) * 60.0 / static_cast<double>(bpm);
    }
    if (durationSeconds <= 0.0) {
        durationSeconds = 1.0;
    }

    m_previewVideoExportTotalFrames = (std::max)(uint64_t(1), static_cast<uint64_t>(std::ceil(durationSeconds * static_cast<double>(fps))));
    m_previewVideoExportCapturedFrames = 0;
    m_previewVideoExportPendingFrameIndex = -1;
    m_previewVideoExportPendingReadback = false;
    m_previewVideoExportWidth = width;
    m_previewVideoExportHeight = height;
    m_previewVideoExportFps = fps;
    m_previewVideoExportOutputPath = outputPath;
    m_previewVideoExportReadbackBuffer.Reset();
    m_previewVideoExportReadbackRowPitch = 0;
    m_previewVideoExportReadbackBytes = 0;

    char tempPath[MAX_PATH] = {};
    DWORD tempLen = GetTempPathA(MAX_PATH, tempPath);
    std::filesystem::path baseDir = (tempLen > 0 && tempLen < MAX_PATH)
        ? std::filesystem::path(tempPath)
        : std::filesystem::temp_directory_path();
    const auto stamp = static_cast<unsigned long long>(GetTickCount64());
    std::filesystem::path framesDir = baseDir / ("shaderlab_preview_export_" + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::create_directories(framesDir, ec);
    if (ec) {
        m_previewVideoExportStatus = "Failed to create temporary export folder.";
        return false;
    }
    m_previewVideoExportFramesDir = framesDir.string();

    m_previewVideoExportSavedTransport = m_transport;
    StopAudioAndClearMusicState();

    m_transport.state = TransportState::Paused;
    m_transport.freezeTime = true;
    m_transport.timeSeconds = 0.0;
    m_transport.lastFrameWallSeconds = 0.0;

    CreatePreviewTexture(width, height);

    std::ostringstream status;
    status << "Exporting preview video: 0/" << m_previewVideoExportTotalFrames
           << " frames (" << width << "x" << height << " @ " << fps << " fps)";
    m_previewVideoExportStatus = status.str();
    m_previewVideoExportActive = true;
    m_previewVideoExportEncoding = false;
    return true;
}

void ShaderLabIDE::CancelPreviewVideoExport(bool restoreTransportState) {
    m_previewVideoExportActive = false;
    m_previewVideoExportEncoding = false;
    m_previewVideoExportPendingReadback = false;
    m_previewVideoExportPendingFrameIndex = -1;
    m_previewVideoExportReadbackBuffer.Reset();
    m_previewVideoExportReadbackRowPitch = 0;
    m_previewVideoExportReadbackBytes = 0;

    if (restoreTransportState) {
        m_transport = m_previewVideoExportSavedTransport;
    }
}

void ShaderLabIDE::UpdatePreviewVideoExportBeginFrame() {
    if (!m_previewVideoExportActive) {
        return;
    }

    if (m_previewVideoExportPendingReadback) {
        if (!WritePreviewCaptureFrameToDisk()) {
            m_previewVideoExportStatus = "Failed while writing exported frame to disk.";
            CancelPreviewVideoExport(true);
            return;
        }

        m_previewVideoExportPendingReadback = false;
        m_previewVideoExportPendingFrameIndex = -1;
        ++m_previewVideoExportCapturedFrames;

        if (m_previewVideoExportCapturedFrames >= m_previewVideoExportTotalFrames) {
            FinalizePreviewVideoExport();
            return;
        }
    }

    const double frameTime = static_cast<double>(m_previewVideoExportCapturedFrames) / static_cast<double>(m_previewVideoExportFps);
    m_transport.state = TransportState::Paused;
    m_transport.freezeTime = true;
    m_transport.timeSeconds = frameTime;
    m_transport.lastFrameWallSeconds = 0.0;

    std::ostringstream status;
    status << "Exporting preview video: " << m_previewVideoExportCapturedFrames
           << "/" << m_previewVideoExportTotalFrames << " frames";
    m_previewVideoExportStatus = status.str();
}

bool ShaderLabIDE::WritePreviewCaptureFrameToDisk() {
    if (!m_previewVideoExportReadbackBuffer || m_previewVideoExportPendingFrameIndex < 0) {
        return false;
    }

    if (m_previewVideoExportReadbackBytes == 0 || m_previewVideoExportReadbackRowPitch == 0) {
        return false;
    }

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(m_previewVideoExportReadbackBytes) };
    if (FAILED(m_previewVideoExportReadbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped))) || !mapped) {
        return false;
    }

    char fileName[64] = {};
    std::snprintf(fileName, sizeof(fileName), "frame_%05lld.ppm", static_cast<long long>(m_previewVideoExportPendingFrameIndex));
    std::filesystem::path framePath = std::filesystem::path(m_previewVideoExportFramesDir) / fileName;

    std::ofstream out(framePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        m_previewVideoExportReadbackBuffer->Unmap(0, nullptr);
        return false;
    }

    out << "P6\n" << m_previewVideoExportWidth << " " << m_previewVideoExportHeight << "\n255\n";
    for (uint32_t y = 0; y < m_previewVideoExportHeight; ++y) {
        const uint8_t* srcRow = mapped + static_cast<size_t>(y) * static_cast<size_t>(m_previewVideoExportReadbackRowPitch);
        for (uint32_t x = 0; x < m_previewVideoExportWidth; ++x) {
            const uint8_t* pixel = srcRow + static_cast<size_t>(x) * 4u;
            out.put(static_cast<char>(pixel[0]));
            out.put(static_cast<char>(pixel[1]));
            out.put(static_cast<char>(pixel[2]));
        }
    }

    m_previewVideoExportReadbackBuffer->Unmap(0, nullptr);
    return out.good();
}

void ShaderLabIDE::FinalizePreviewVideoExport() {
    if (!m_previewVideoExportActive) {
        return;
    }

    m_previewVideoExportActive = false;
    m_previewVideoExportEncoding = true;

    std::string message;
    const std::string inputPattern = (std::filesystem::path(m_previewVideoExportFramesDir) / "frame_%05d.ppm").string();
    const bool ok = RunFfmpegEncode(inputPattern, m_previewVideoExportOutputPath, m_previewVideoExportFps, message);

    m_transport = m_previewVideoExportSavedTransport;
    m_previewVideoExportEncoding = false;
    m_previewVideoExportPendingReadback = false;
    m_previewVideoExportPendingFrameIndex = -1;
    m_previewVideoExportReadbackBuffer.Reset();
    m_previewVideoExportReadbackRowPitch = 0;
    m_previewVideoExportReadbackBytes = 0;

    if (ok) {
        m_previewVideoExportStatus = "Preview export complete: " + m_previewVideoExportOutputPath;
    } else {
        m_previewVideoExportStatus = "Preview export failed: " + message;
    }
}

bool ShaderLabIDE::RunFfmpegEncode(const std::string& inputPattern,
                                   const std::string& outputPath,
                                   uint32_t fps,
                                   std::string& outMessage) {
    std::ostringstream cmd;
    cmd << "cmd.exe /C ffmpeg -y -framerate " << fps
        << " -i \"" << inputPattern << "\""
        << " -c:v libx264 -pix_fmt yuv420p \"" << outputPath << "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::string commandLine = cmd.str();
    if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        outMessage = "Could not launch ffmpeg. Ensure ffmpeg is installed and available on PATH.";
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        outMessage = "ffmpeg exited with code " + std::to_string(exitCode) + ".";
        return false;
    }

    outMessage = "ok";
    return true;
}

} // namespace ShaderLab
