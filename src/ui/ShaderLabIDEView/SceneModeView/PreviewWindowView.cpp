#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/Core/CompilationService.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <sstream>
#include <string>

#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")

namespace ShaderLab {

namespace {

float GetAspectRatioValue(AspectRatio ratio) {
    switch (ratio) {
        case AspectRatio::Ratio_1_1:
            return 1.0f;
        case AspectRatio::Ratio_16_10:
            return 16.0f / 10.0f;
        case AspectRatio::Ratio_4_3:
            return 4.0f / 3.0f;
        case AspectRatio::Ratio_16_9:
        default:
            return 16.0f / 9.0f;
    }
}

ImVec2 FitAspect(ImVec2 avail, float aspect) {
    if (aspect <= 0.0f) {
        return avail;
    }

    float width = avail.x;
    float height = width / aspect;
    if (height > avail.y) {
        height = avail.y;
        width = height * aspect;
    }

    width = (std::max)(1.0f, width);
    height = (std::max)(1.0f, height);
    return ImVec2(width, height);
}

constexpr int kFullscreenRenderPresetCount = 10;
constexpr uint32_t kFullscreenRenderWidths[kFullscreenRenderPresetCount] = {
    0u, 4096u, 2560u, 2048u, 1920u, 1600u, 1280u, 1024u, 800u, 640u
};

int FullscreenPresetToIndex(FullscreenRenderResolutionPreset preset) {
    switch (preset) {
        case FullscreenRenderResolutionPreset::W4096: return 1;
        case FullscreenRenderResolutionPreset::W2560: return 2;
        case FullscreenRenderResolutionPreset::W2048: return 3;
        case FullscreenRenderResolutionPreset::W1920: return 4;
        case FullscreenRenderResolutionPreset::W1600: return 5;
        case FullscreenRenderResolutionPreset::W1280: return 6;
        case FullscreenRenderResolutionPreset::W1024: return 7;
        case FullscreenRenderResolutionPreset::W800: return 8;
        case FullscreenRenderResolutionPreset::W640: return 9;
        case FullscreenRenderResolutionPreset::Full:
        default:
            return 0;
    }
}

FullscreenRenderResolutionPreset IndexToFullscreenPreset(int index) {
    switch (index) {
        case 1: return FullscreenRenderResolutionPreset::W4096;
        case 2: return FullscreenRenderResolutionPreset::W2560;
        case 3: return FullscreenRenderResolutionPreset::W2048;
        case 4: return FullscreenRenderResolutionPreset::W1920;
        case 5: return FullscreenRenderResolutionPreset::W1600;
        case 6: return FullscreenRenderResolutionPreset::W1280;
        case 7: return FullscreenRenderResolutionPreset::W1024;
        case 8: return FullscreenRenderResolutionPreset::W800;
        case 9: return FullscreenRenderResolutionPreset::W640;
        case 0:
        default:
            return FullscreenRenderResolutionPreset::Full;
    }
}

int ComputeAspectHeight(uint32_t width, AspectRatio ratio) {
    if (width == 0u) {
        return 0;
    }
    const float aspect = GetAspectRatioValue(ratio);
    if (aspect <= 0.0f) {
        return static_cast<int>(width);
    }
    return (std::max)(1, static_cast<int>(std::lround(static_cast<double>(width) / static_cast<double>(aspect))));
}

void ResolvePreviewRenderSize(FullscreenRenderResolutionPreset preset,
                              AspectRatio aspectRatio,
                              uint32_t fallbackWidth,
                              uint32_t fallbackHeight,
                              bool halfScale,
                              uint32_t& outWidth,
                              uint32_t& outHeight) {
    const int presetIndex = FullscreenPresetToIndex(preset);
    const uint32_t baseWidth = (presetIndex >= 0 && presetIndex < kFullscreenRenderPresetCount)
        ? kFullscreenRenderWidths[presetIndex]
        : 0u;

    if (baseWidth == 0u) {
        outWidth = (std::max)(1u, fallbackWidth);
        outHeight = (std::max)(1u, fallbackHeight);
    } else {
        outWidth = baseWidth;
        outHeight = static_cast<uint32_t>(ComputeAspectHeight(baseWidth, aspectRatio));
    }

    if (halfScale) {
        outWidth = (std::max)(1u, outWidth / 2u);
        outHeight = (std::max)(1u, outHeight / 2u);
    }
}

using EditorActionWidgets::LabeledActionButton;

}

void ShaderLabIDE::ShowPreviewWindow() {
    if (!ImGui::Begin("Preview")) {
        ImGui::End();
        return;
    }

    int aspectIndex = 0;
    switch (m_aspectRatio) {
        case AspectRatio::Ratio_1_1: aspectIndex = 1; break;
        case AspectRatio::Ratio_16_10: aspectIndex = 2; break;
        case AspectRatio::Ratio_4_3: aspectIndex = 3; break;
        case AspectRatio::Ratio_16_9:
        default: aspectIndex = 0; break;
    }

    const char* aspectLabels[] = { "16:9", "1:1", "16:10", "4:3" };

    char fullscreenResLabels[kFullscreenRenderPresetCount][32] = {};
    std::snprintf(fullscreenResLabels[0], sizeof(fullscreenResLabels[0]), "full");
    for (int i = 1; i < kFullscreenRenderPresetCount; ++i) {
        const uint32_t width = kFullscreenRenderWidths[i];
        const int height = ComputeAspectHeight(width, m_aspectRatio);
        std::snprintf(fullscreenResLabels[i], sizeof(fullscreenResLabels[i]), "%ux%d", width, height);
    }
    const char* fullscreenResLabelPtrs[kFullscreenRenderPresetCount] = {};
    for (int i = 0; i < kFullscreenRenderPresetCount; ++i) {
        fullscreenResLabelPtrs[i] = fullscreenResLabels[i];
    }

    int fullscreenPresetIndex = FullscreenPresetToIndex(m_fullscreenRenderResolutionPreset);
    if (ImGui::BeginTabBar("##PreviewWindowTabs")) {
        if (ImGui::BeginTabItem("Preview")) {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const bool wideLayout = availableWidth >= 760.0f;
            const float comboWidth = wideLayout
                ? (std::clamp)((availableWidth - 230.0f) * 0.5f, 120.0f, 260.0f)
                : (std::clamp)(availableWidth, 160.0f, 320.0f);

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Aspect");
            if (wideLayout) {
                ImGui::SameLine();
            }
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::Combo("##AspectRatio", &aspectIndex, aspectLabels, 4)) {
                m_aspectRatio = (aspectIndex == 1) ? AspectRatio::Ratio_1_1 :
                                (aspectIndex == 2) ? AspectRatio::Ratio_16_10 :
                                (aspectIndex == 3) ? AspectRatio::Ratio_4_3 : AspectRatio::Ratio_16_9;
            }

            if (wideLayout) {
                ImGui::SameLine();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Fullscreen 3D");
            if (wideLayout) {
                ImGui::SameLine();
            }
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::Combo("##Fullscreen3DResolution", &fullscreenPresetIndex, fullscreenResLabelPtrs, kFullscreenRenderPresetCount)) {
                m_fullscreenRenderResolutionPreset = IndexToFullscreenPreset(fullscreenPresetIndex);
            }

            if (m_currentMode == UIMode::Demo) {
                if (wideLayout) {
                    ImGui::SameLine();
                }
                const float buttonWidth = wideLayout
                    ? (std::clamp)(ImGui::GetContentRegionAvail().x, 180.0f, 300.0f)
                    : (std::clamp)(availableWidth, 200.0f, 320.0f);
                if (LabeledActionButton("ViewUbershader", OpenFontIcons::kCode, "View Ubershader", "Open current ubershader source and compile diagnostics", ImVec2(buttonWidth, 0.0f))) {
                    OpenUbershaderViewer();
                }
            }

            ImGui::Separator();

            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 drawSize = FitAspect(avail, GetAspectRatioValue(m_aspectRatio));
            uint32_t renderWidth = m_previewVideoExportActive
                ? m_previewVideoExportWidth
                : static_cast<uint32_t>((std::max)(1.0f, drawSize.x));
            uint32_t renderHeight = m_previewVideoExportActive
                ? m_previewVideoExportHeight
                : static_cast<uint32_t>((std::max)(1.0f, drawSize.y));
            if (!m_previewVideoExportActive) {
                ResolvePreviewRenderSize(
                    m_fullscreenRenderResolutionPreset,
                    m_aspectRatio,
                    renderWidth,
                    renderHeight,
                    true,
                    renderWidth,
                    renderHeight);
            }
            CreatePreviewTexture(renderWidth, renderHeight);

            ImVec2 cursorStart = ImGui::GetCursorPos();
            ImVec2 screenStart = ImGui::GetCursorScreenPos();
            ImVec2 screenEnd = ImVec2(screenStart.x + avail.x, screenStart.y + avail.y);
            ImVec4 previewBackdrop = m_uiThemeColors.WindowBackground;
            previewBackdrop.w = (std::clamp)(m_uiThemeColors.PanelOpacity, 0.0f, 1.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(screenStart, screenEnd, ImGui::GetColorU32(previewBackdrop));

            ImGui::SetCursorPos(ImVec2(cursorStart.x + (avail.x - drawSize.x) * 0.5f, cursorStart.y + (avail.y - drawSize.y) * 0.5f));

            if (m_previewTexture && m_previewSrvGpuHandle.ptr != 0) {
                ImGui::Image((ImTextureID)m_previewSrvGpuHandle.ptr, drawSize);
            } else {
                ImGui::Dummy(drawSize);
            }

            if (m_currentMode == UIMode::Scene && m_screenKeysOverlayEnabled) {
                ImGui::SetNextWindowPos(ImVec2(screenStart.x + 8.0f, screenStart.y + 8.0f), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.68f);
                ImGuiWindowFlags overlayFlags =
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoDocking |
                    ImGuiWindowFlags_NoNav |
                    ImGuiWindowFlags_NoFocusOnAppearing;

                ImGui::SetNextWindowSize(ImVec2(330.0f, 200.0f), ImGuiCond_Always);

                if (ImGui::Begin("##ScreenKeysOverlay", nullptr, overlayFlags)) {
                    if (m_fontMenuSmall) {
                        ImGui::PushFont(m_fontMenuSmall);
                    }

                    ImGui::TextUnformatted("Screen Keys");
                    ImGui::SameLine();
                    if (LabeledActionButton("ScreenKeysCopy", OpenFontIcons::kCopy, "Copy", "Copy key log", ImVec2(100.0f, 0.0f))) {
                        std::string clipboard;
                        clipboard.reserve(m_screenKeyLog.size() * 8);
                        for (size_t i = 0; i < m_screenKeyLog.size(); ++i) {
                            if (i > 0) {
                                clipboard.push_back('\n');
                            }
                            clipboard += m_screenKeyLog[i];
                        }
                        if (clipboard.empty()) {
                            clipboard = "(empty)";
                        }
                        ImGui::SetClipboardText(clipboard.c_str());
                    }
                    ImGui::SameLine();
                    if (LabeledActionButton("ScreenKeysClear", OpenFontIcons::kTrash2, "Clear", "Clear key log", ImVec2(100.0f, 0.0f))) {
                        m_screenKeyLog.clear();
                    }

                    ImGui::Separator();
                    if (m_screenKeyLog.empty()) {
                        ImGui::TextDisabled("No keys yet");
                    } else {
                        ImGui::BeginChild("ScreenKeyLogScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                        for (int i = 0; i < (int)m_screenKeyLog.size(); ++i) {
                            ImGui::TextUnformatted(m_screenKeyLog[i].c_str());
                        }
                        ImGui::SetScrollHereY(1.0f);
                        ImGui::EndChild();
                    }

                    if (m_fontMenuSmall) {
                        ImGui::PopFont();
                    }
                }
                ImGui::End();
            }

            ImGui::SetCursorPos(cursorStart);
            ImGui::Dummy(avail);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Export Video")) {
            ShowPreviewExportControls();
            if (m_previewVideoExportActive) {
                CreatePreviewTexture(m_previewVideoExportWidth, m_previewVideoExportHeight);
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ShowUbershaderPopup();

    ImGui::End();
}

void ShaderLabIDE::ShowPreviewExportControls() {
    static const char* kResolutionLabels[] = {
        "640x360",
        "1280x720",
        "1920x1080",
        "1080x1080",
        "1920x1920",
        "2560x1440"
    };
    static const uint32_t kResolutionWidths[] = { 640, 1280, 1920, 1080, 1920, 2560 };
    static const uint32_t kResolutionHeights[] = { 360, 720, 1080, 1080, 1920, 1440 };
    constexpr int kResolutionCount = 6;
    static const uint32_t kFpsOptions[] = { 24, 30, 60 };
    static const char* kFpsLabels[] = { "24", "30", "60" };

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool wideLayout = availableWidth >= 760.0f;
    const float controlWidth = wideLayout
        ? (std::clamp)(availableWidth * 0.28f, 140.0f, 240.0f)
        : (std::clamp)(availableWidth, 160.0f, 320.0f);
    const float fpsWidth = wideLayout ? 90.0f : controlWidth;
    const float secondsWidth = wideLayout ? 100.0f : controlWidth;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Resolution");
    if (wideLayout) {
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(controlWidth);
    ImGui::Combo("##PreviewExportResolution", &m_previewVideoResolutionPresetIndex, kResolutionLabels, kResolutionCount);

    if (m_previewVideoResolutionPresetIndex < 0) m_previewVideoResolutionPresetIndex = 0;
    if (m_previewVideoResolutionPresetIndex >= kResolutionCount) {
        m_previewVideoResolutionPresetIndex = kResolutionCount - 1;
    }

    if (wideLayout) {
        ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("FPS");
    if (wideLayout) {
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(fpsWidth);
    ImGui::Combo("##PreviewExportFps", &m_previewVideoFpsPresetIndex, kFpsLabels, static_cast<int>(std::size(kFpsLabels)));
    if (m_previewVideoFpsPresetIndex < 0) m_previewVideoFpsPresetIndex = 0;
    if (m_previewVideoFpsPresetIndex >= static_cast<int>(std::size(kFpsOptions))) {
        m_previewVideoFpsPresetIndex = static_cast<int>(std::size(kFpsOptions)) - 1;
    }

    if (wideLayout) {
        ImGui::SameLine();
    }
    ImGui::Checkbox("Full timeline", &m_previewVideoExportUseFullTimeline);

    if (!m_previewVideoExportUseFullTimeline) {
        if (wideLayout) {
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Seconds");
        if (wideLayout) {
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(secondsWidth);
        if (ImGui::InputFloat("##PreviewExportSeconds", &m_previewVideoExportSeconds, 0.0f, 0.0f, "%.1f")) {
            if (m_previewVideoExportSeconds < 0.1f) {
                m_previewVideoExportSeconds = 0.1f;
            }
            if (m_previewVideoExportSeconds > 3600.0f) {
                m_previewVideoExportSeconds = 3600.0f;
            }
        }
    }

    const uint32_t selectedWidth = kResolutionWidths[m_previewVideoResolutionPresetIndex];
    const uint32_t selectedHeight = kResolutionHeights[m_previewVideoResolutionPresetIndex];
    const uint32_t selectedFps = kFpsOptions[m_previewVideoFpsPresetIndex];

    const bool exportBusy = m_previewVideoExportActive || m_previewVideoExportEncoding;
    ImGui::Spacing();
    ImGui::BeginDisabled(exportBusy);
    if (LabeledActionButton("PreviewExportVideo", OpenFontIcons::kSave, "Export Preview Video", "Export timeline preview to MP4 using ffmpeg", ImVec2(260.0f, 0.0f))) {
        char outputPath[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd ? m_hwnd : (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
        ofn.lpstrFile = outputPath;
        ofn.nMaxFile = sizeof(outputPath);
        ofn.lpstrFilter = "MP4 Video (*.mp4)\0*.mp4\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        std::snprintf(outputPath, sizeof(outputPath), "preview_export.mp4");

        if (GetSaveFileNameA(&ofn)) {
            std::string output = outputPath;
            if (output.find('.') == std::string::npos) {
                output += ".mp4";
            }
            if (!StartPreviewVideoExport(output, selectedWidth, selectedHeight, selectedFps)) {
                if (m_previewVideoExportStatus.empty()) {
                    m_previewVideoExportStatus = "Failed to start preview export.";
                }
            }
        }
    }
    ImGui::EndDisabled();

    if (m_previewVideoExportActive) {
        ImGui::SameLine();
        if (LabeledActionButton("PreviewExportCancel", OpenFontIcons::kX, "Cancel", "Cancel preview video export", ImVec2(140.0f, 0.0f))) {
            CancelPreviewVideoExport(true);
            m_previewVideoExportStatus = "Preview export canceled.";
        }
    }

    if (!m_previewVideoExportStatus.empty()) {
        ImGui::TextWrapped("%s", m_previewVideoExportStatus.c_str());
    }
}

void ShaderLabIDE::OpenUbershaderViewer() {
    if (m_currentProjectPath.empty()) {
        m_ubershaderPath.clear();
        m_ubershaderSource = "// Save the project first to generate the ubershader source from project.json.";
        m_ubershaderStatus = "No project path available";
        m_ubershaderTextEditor.SetText(m_ubershaderSource);
        m_showUbershaderPopup = true;
        ImGui::OpenPopup("Ubershader Viewer");
        return;
    }

    std::string source;
    std::string buildError;
    if (!BuildPipeline::GenerateMicroUbershaderSource(m_currentProjectPath, source, buildError)) {
        m_ubershaderPath = m_currentProjectPath;
        m_ubershaderSource = "// Failed to generate micro ubershader source from project.json.";
        m_ubershaderStatus = buildError.empty() ? "Generation failed" : buildError;
        m_ubershaderTextEditor.SetText(m_ubershaderSource);
        m_showUbershaderPopup = true;
        ImGui::OpenPopup("Ubershader Viewer");
        return;
    }

    std::string compileSource = source;
    if (compileSource.rfind("U:", 0) == 0) {
        const size_t firstNewline = compileSource.find('\n');
        if (firstNewline != std::string::npos && firstNewline + 1 < compileSource.size()) {
            compileSource = compileSource.substr(firstNewline + 1);
        }
    }

    std::string entryPoint = "main";
    std::regex entryRegex(R"(float4\s+([A-Za-z_]\w*)\s*\(\s*float2\s+fragCoord\s*,\s*float2\s+iResolution\s*,\s*float\s+iTime\s*\))");
    std::smatch match;
    if (std::regex_search(compileSource, match, entryRegex) && match.size() >= 2) {
        entryPoint = match[1].str();
    }

    std::ostringstream status;
    status << "Generated from: " << m_currentProjectPath;
    status << "\nGeneration status: OK";

    if (m_compilationService) {
        const std::vector<CompilationTextureBinding> bindings = { {0, "Texture2D"}, {1, "Texture2D"} };
        const ShaderCompileResult result = m_compilationService->CompileFromSource(
            compileSource,
            entryPoint,
            "ps_6_0",
            L"ubershader.hlsl",
            ShaderCompileMode::Live,
            bindings);

        status << "\nCompile entry: " << entryPoint;
        status << "\nCompile status: " << (result.success ? "OK" : "FAILED");
        for (size_t i = 0; i < result.diagnostics.size() && i < 12; ++i) {
            status << "\n- " << result.diagnostics[i].message;
        }
    } else {
        status << "\nCompile status: unavailable (compilation service not initialized)";
    }

    m_ubershaderPath = m_currentProjectPath;
    m_ubershaderSource = source;
    m_ubershaderStatus = status.str();
    m_ubershaderTextEditor.SetText(m_ubershaderSource);
    m_showUbershaderPopup = true;
    ImGui::OpenPopup("Ubershader Viewer");
}

void ShaderLabIDE::ShowUbershaderPopup() {
    if (!m_showUbershaderPopup) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(980.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Ubershader Viewer", &m_showUbershaderPopup, ImGuiWindowFlags_NoCollapse)) {
        if (!m_ubershaderStatus.empty()) {
            ImGui::TextWrapped("%s", m_ubershaderStatus.c_str());
            ImGui::Separator();
        }

        ImVec2 editorSize = ImGui::GetContentRegionAvail();
        editorSize.y -= ImGui::GetFrameHeightWithSpacing() + 6.0f;
        if (editorSize.y < 120.0f) {
            editorSize.y = 120.0f;
        }
        m_ubershaderTextEditor.Render("##UbershaderSource", editorSize, true);

        if (LabeledActionButton("CloseUbershaderViewer", OpenFontIcons::kX, "Close", "Close ubershader viewer", ImVec2(110.0f, 0.0f))) {
            m_showUbershaderPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}
