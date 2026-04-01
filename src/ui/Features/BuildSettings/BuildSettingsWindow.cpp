#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/OpenFontIcons.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "Comdlg32.lib")

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

std::string SizePresetToString(SizeTargetPreset preset) {
    switch (preset) {
        case SizeTargetPreset::K64: return "64k";
        case SizeTargetPreset::K128: return "128k";
        case SizeTargetPreset::K256: return "256k";
        case SizeTargetPreset::K512: return "512k";
        case SizeTargetPreset::K1024: return "1024k";
        default: return "none";
    }
}

std::string BuildTargetKindToString(BuildTargetKind kind) {
    switch (kind) {
        case BuildTargetKind::PackagedDemo: return "packaged";
        case BuildTargetKind::SelfContainedScreenSaver: return "selfcontained-screensaver";
        case BuildTargetKind::MicroDemo: return "micro";
        default: return "selfcontained";
    }
}

void SaveUiBuildSettings(
    const std::string& appRoot,
    BuildTargetKind targetKind,
    BuildMode mode,
    SizeTargetPreset sizeTarget,
    bool restrictedCompactTrack,
    bool runtimeDebugLog,
    bool compactTrackDebugLog,
    bool microDeveloperBuild,
    const std::string& cleanSolutionRootPath,
    const std::string& crinklerPath) {
    const fs::path settingsPath = GetUiSettingsPath(appRoot);
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

    json build;
    build["targetKind"] = BuildTargetKindToString(targetKind);
    build["mode"] = (mode == BuildMode::ReleaseCrinkled) ? "crinkled" : "release";
    build["sizeTarget"] = SizePresetToString(sizeTarget);
    build["restrictedCompactTrack"] = restrictedCompactTrack;
    build["runtimeDebugLog"] = runtimeDebugLog;
    build["compactTrackDebugLog"] = compactTrackDebugLog;
    build["microDeveloperBuild"] = microDeveloperBuild;
    build["cleanSolutionRootPath"] = cleanSolutionRootPath;
    build["crinklerPath"] = crinklerPath;
    root["build"] = build;

    std::ofstream out(settingsPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << root.dump(2);
}

const char* BuildModeLabel(BuildMode mode) {
    return mode == BuildMode::ReleaseCrinkled ? "Release Crinkled" : "Release";
}

const char* SizePresetLabel(SizeTargetPreset preset) {
    switch (preset) {
        case SizeTargetPreset::K64: return "64K";
        case SizeTargetPreset::K128: return "128K";
        case SizeTargetPreset::K256: return "256K";
        case SizeTargetPreset::K512: return "512K";
        case SizeTargetPreset::K1024: return "1024K";
        default: return "None";
    }
}

uint64_t SizePresetBytes(SizeTargetPreset preset) {
    switch (preset) {
        case SizeTargetPreset::K64: return 64ull * 1024ull;
        case SizeTargetPreset::K128: return 128ull * 1024ull;
        case SizeTargetPreset::K256: return 256ull * 1024ull;
        case SizeTargetPreset::K512: return 512ull * 1024ull;
        case SizeTargetPreset::K1024: return 1024ull * 1024ull;
        default: return 0ull;
    }
}

void OpenExternal(const std::string& target) {
    ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace

void ShaderLabIDE::ShowBuildSettingsWindow() {
    const BuildTargetKind prevTargetKind = m_buildSettingsTargetKind;
    const BuildMode prevMode = m_buildSettingsMode;
    const SizeTargetPreset prevSizeTarget = m_buildSettingsSizeTarget;
    const bool prevRestrictedCompactTrack = m_buildSettingsRestrictedCompactTrack;
    const bool prevRuntimeDebugLog = m_buildSettingsRuntimeDebugLog;
    const bool prevCompactTrackDebugLog = m_buildSettingsCompactTrackDebugLog;
    const bool prevMicroDeveloperBuild = m_buildSettingsMicroDeveloperBuild;
    const std::string prevCleanSolutionRootPath = m_buildSettingsCleanSolutionRootPath;

    if (m_buildSettingsRefreshRequested) {
        m_buildSettingsPrereq = BuildPipeline::CheckPrereqs(m_appRoot, m_buildSettingsMode);
        m_buildSettingsRefreshRequested = false;
    }
    if (m_buildSettingsTargetKind == BuildTargetKind::MicroDemo && m_microUbershaderConflictsDirty) {
        RefreshMicroUbershaderConflictCache();
    }

    int unresolvedMicroConflictCount = 0;
    if (m_buildSettingsTargetKind == BuildTargetKind::MicroDemo) {
        for (const auto& conflict : m_microUbershaderConflicts) {
            const auto it = m_microUbershaderKeepEntrypointsBySignature.find(conflict.signatureKey);
            if (it == m_microUbershaderKeepEntrypointsBySignature.end() || it->second.empty()) {
                ++unresolvedMicroConflictCount;
            }
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 windowSize(980.0f, 760.0f);
    ImGui::SetNextWindowPos(ImVec2(
        viewport->Pos.x + (viewport->Size.x - windowSize.x) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - windowSize.y) * 0.5f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);

    bool canCloseBuildWindow = !m_isBuilding;
    bool* openPtr = canCloseBuildWindow ? &m_showBuildSettings : nullptr;
    if (!ImGui::Begin("Build Settings", openPtr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    auto LabeledActionButton = [](const char* id, uint32_t iconCodepoint, const char* label, const char* tooltip, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
        const std::string icon = OpenFontIcons::ToUtf8(iconCodepoint);
        const std::string buttonId = std::string("##") + id;
        const char* safeLabel = (label && *label) ? label : "";
        const ImGuiStyle& style = ImGui::GetStyle();

        ImVec2 buttonSize = size;
        if (buttonSize.x <= 0.0f) {
            buttonSize.x = ImGui::CalcItemWidth();
        }
        if (buttonSize.y <= 0.0f) {
            buttonSize.y = ImGui::GetFrameHeight();
        }

        const ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
        const ImVec2 labelSize = ImGui::CalcTextSize(safeLabel);
        const float gap = safeLabel[0] ? style.ItemInnerSpacing.x : 0.0f;
        const float minWidth = iconSize.x + gap + labelSize.x + style.FramePadding.x * 2.5f;
        buttonSize.x = (std::max)(buttonSize.x, minWidth);

        const bool pressed = ImGui::InvisibleButton(buttonId.c_str(), buttonSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
        drawList->AddRectFilled(min, max, bg, style.FrameRounding);
        if (style.FrameBorderSize > 0.0f) {
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
        }

        const float contentWidth = iconSize.x + gap + labelSize.x;
        const float baseX = min.x + (buttonSize.x - contentWidth) * 0.5f;
        const float iconY = min.y + (buttonSize.y - iconSize.y) * 0.5f +
            ((iconCodepoint == OpenFontIcons::kPlus || iconCodepoint == OpenFontIcons::kMinus) ? 1.0f : 0.0f);
        const float labelY = min.y + (buttonSize.y - labelSize.y) * 0.5f;

        drawList->AddText(ImVec2(std::floor(baseX), std::floor(iconY)), ImGui::GetColorU32(ImGuiCol_CheckMark), icon.c_str());
        if (safeLabel[0]) {
            drawList->AddText(ImVec2(std::floor(baseX + iconSize.x + gap), std::floor(labelY)), ImGui::GetColorU32(ImGuiCol_TextLink), safeLabel);
        }

        if (ImGui::IsItemHovered() && tooltip && *tooltip) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    };

    bool buildModeChanged = false;
    int modeIndex = (m_buildSettingsMode == BuildMode::ReleaseCrinkled) ? 1 : 0;
    const char* modeLabels[] = { "Release (standard)", "Release Crinkled (smaller size, requires Crinkler + Ninja)" };
    ImGui::TextUnformatted("Build Mode");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##BuildMode", &modeIndex, modeLabels, 2)) {
        m_buildSettingsMode = modeIndex == 1 ? BuildMode::ReleaseCrinkled : BuildMode::Release;
        m_buildSettingsRefreshRequested = true;
        buildModeChanged = true;
        m_buildSettingsAutoSwitchedToCrinkled = false;
    }

    int targetIndex = 1;
    switch (m_buildSettingsTargetKind) {
        case BuildTargetKind::PackagedDemo: targetIndex = 0; break;
        case BuildTargetKind::SelfContainedDemo: targetIndex = 1; break;
        case BuildTargetKind::SelfContainedScreenSaver: targetIndex = 2; break;
        case BuildTargetKind::MicroDemo: targetIndex = 3; break;
    }
    const char* targetLabels[] = {
        "Packaged (.zip)",
        "Self-Contained (.exe)",
        "Screen Saver (.scr)",
        "Micro-Demo (.exe)"
    };
    ImGui::TextUnformatted("Build Target");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##BuildTarget", &targetIndex, targetLabels, 4)) {
        switch (targetIndex) {
            case 0: m_buildSettingsTargetKind = BuildTargetKind::PackagedDemo; break;
            case 2: m_buildSettingsTargetKind = BuildTargetKind::SelfContainedScreenSaver; break;
            case 3: m_buildSettingsTargetKind = BuildTargetKind::MicroDemo; break;
            default: m_buildSettingsTargetKind = BuildTargetKind::SelfContainedDemo; break;
        }
        m_microUbershaderConflictsDirty = true;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("File menu builds use this target.");
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("Size Budget");
    static const SizeTargetPreset presets[] = {
        SizeTargetPreset::None,
        SizeTargetPreset::K64,
        SizeTargetPreset::K128,
        SizeTargetPreset::K256,
        SizeTargetPreset::K512,
        SizeTargetPreset::K1024
    };
    bool sizePresetChanged = false;
    for (SizeTargetPreset preset : presets) {
        ImGui::PushID((int)preset);
        const bool selected = (m_buildSettingsSizeTarget == preset);
        const std::string label = std::string(SizePresetLabel(preset)) +
            (preset == SizeTargetPreset::None ? " (No target)" : " (" + std::to_string(SizePresetBytes(preset)) + " bytes)");
        if (ImGui::RadioButton(label.c_str(), selected)) {
            m_buildSettingsSizeTarget = preset;
            sizePresetChanged = true;
            m_buildSettingsAutoSwitchedToCrinkled = false;
        }
        ImGui::PopID();
    }

    const bool tinyTargetSelected = (m_buildSettingsSizeTarget != SizeTargetPreset::None) || m_buildSettingsTargetKind == BuildTargetKind::MicroDemo;
    bool autoSwitchedToCrinkled = false;
    const bool canAutoSwitchToCrinkled =
        tinyTargetSelected &&
        m_buildSettingsMode == BuildMode::Release &&
        m_buildSettingsPrereq.hasCrinkler &&
        m_buildSettingsPrereq.hasNinja;
    if (canAutoSwitchToCrinkled && (sizePresetChanged || buildModeChanged)) {
        m_buildSettingsMode = BuildMode::ReleaseCrinkled;
        m_buildSettingsRefreshRequested = true;
        autoSwitchedToCrinkled = true;
        m_buildSettingsAutoSwitchedToCrinkled = true;
    }

    if (autoSwitchedToCrinkled || m_buildSettingsAutoSwitchedToCrinkled) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetSemanticSuccessColor(), "Switched to Release Crinkled for this size budget.");
        ImGui::PopTextWrapPos();
    }

    if (m_buildSettingsMode == BuildMode::Release && m_buildSettingsSizeTarget != SizeTargetPreset::None) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Size budgets use MicroPlayer (x86). No budget uses full runtime (x64).");
        ImGui::PopTextWrapPos();
    }

    ImGui::Checkbox("Compact track mode", &m_buildSettingsRestrictedCompactTrack);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Stores track data in assets/track.bin and removes extra JSON fields.");
    ImGui::Checkbox("Runtime debug logs", &m_buildSettingsRuntimeDebugLog);
    ImGui::TextDisabled("Adds runtime log text (increases build size).");
    ImGui::Checkbox("Compact-track debug logs", &m_buildSettingsCompactTrackDebugLog);
    ImGui::TextDisabled("Adds compact-track diagnostics (increases build size).");
    ImGui::PopTextWrapPos();

    if (m_buildSettingsTargetKind == BuildTargetKind::MicroDemo) {
        ImGui::Checkbox("Micro developer mode (overlay)", &m_buildSettingsMicroDeveloperBuild);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Shows on-screen MicroPlayer diagnostics for debugging.");
        ImGui::PopTextWrapPos();
        if (m_buildSettingsMicroDeveloperBuild && !m_buildSettingsRuntimeDebugLog) {
            m_buildSettingsRuntimeDebugLog = true;
        }

        bool microConflictSelectionChanged = false;
        ImGui::SeparatorText("Micro Ubershader Conflicts");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Duplicate helper signatures are listed side by side. Pick what to keep.");
        ImGui::PopTextWrapPos();

        if (LabeledActionButton("ResetMicroConflictKeepAll", OpenFontIcons::kRefresh, "Reset All", "Keep all implementations for all conflicts")) {
            for (const auto& conflict : m_microUbershaderConflicts) {
                auto& keep = m_microUbershaderKeepEntrypointsBySignature[conflict.signatureKey];
                keep.clear();
                keep.reserve(conflict.options.size());
                for (const auto& option : conflict.options) {
                    keep.push_back(option.moduleEntrypoint);
                }
            }
            microConflictSelectionChanged = true;
        }

        if (m_currentProjectPath.empty()) {
            ImGui::TextDisabled("Save the project to analyze micro ubershader conflicts.");
        } else if (m_microUbershaderConflicts.empty()) {
            ImGui::TextColored(GetSemanticSuccessColor(), "No duplicate local function signatures found.");
        } else {
            for (size_t conflictIndex = 0; conflictIndex < m_microUbershaderConflicts.size(); ++conflictIndex) {
                auto& conflict = m_microUbershaderConflicts[conflictIndex];
                auto& keep = m_microUbershaderKeepEntrypointsBySignature[conflict.signatureKey];

                ImGui::PushID(static_cast<int>(conflictIndex));
                ImGui::SeparatorText(conflict.signatureDisplay.c_str());
                if (keep.empty()) {
                    ImGui::TextColored(GetSemanticErrorColor(), "Unresolved: choose one or more implementations.");
                }
                const int columnCount = (std::max)(1, static_cast<int>(conflict.options.size()));
                if (ImGui::BeginTable("MicroConflictOptions", columnCount, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& option : conflict.options) {
                        std::string colName = option.moduleLabel + " (" + option.moduleEntrypoint + ")";
                        ImGui::TableSetupColumn(colName.c_str());
                    }
                    ImGui::TableNextRow();

                    for (size_t optionIndex = 0; optionIndex < conflict.options.size(); ++optionIndex) {
                        const auto& option = conflict.options[optionIndex];
                        ImGui::TableSetColumnIndex(static_cast<int>(optionIndex));
                        const bool selected = std::find(keep.begin(), keep.end(), option.moduleEntrypoint) != keep.end();

                        std::string toggleLabel = std::string(selected ? "[KEEP] " : "[skip] ") + option.moduleEntrypoint;
                        if (ImGui::Selectable(toggleLabel.c_str(), selected, 0, ImVec2(-FLT_MIN, 0.0f))) {
                            if (selected) {
                                keep.erase(std::remove(keep.begin(), keep.end(), option.moduleEntrypoint), keep.end());
                                microConflictSelectionChanged = true;
                            } else {
                                keep.push_back(option.moduleEntrypoint);
                                microConflictSelectionChanged = true;
                            }
                        }

                        ImGui::BeginChild(("Snippet_" + std::to_string(optionIndex)).c_str(), ImVec2(0.0f, 110.0f), true);
                        ImGui::TextUnformatted(option.snippet.c_str());
                        ImGui::EndChild();
                    }

                    ImGui::EndTable();
                }

                if (conflict.options.size() >= 2) {
                    const std::string left = conflict.options[0].moduleEntrypoint;
                    const std::string right = conflict.options[1].moduleEntrypoint;
                    if (ImGui::Button("Keep Left")) {
                        keep = {left};
                        microConflictSelectionChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Keep Right")) {
                        keep = {right};
                        microConflictSelectionChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Keep Both")) {
                        keep = {left, right};
                        microConflictSelectionChanged = true;
                    }
                }
                ImGui::PopID();
            }
        }

        if (microConflictSelectionChanged && !m_currentProjectPath.empty()) {
            SaveProjectUiSettings();
        }
    } else {
        m_buildSettingsMicroDeveloperBuild = false;
    }

    ImGui::SeparatorText("Clean Solution Export");
    {
        static char cleanRootBuffer[1024] = {};
        static bool initialized = false;
        if (!initialized) {
            const size_t copyLen = (std::min)(m_buildSettingsCleanSolutionRootPath.size(), sizeof(cleanRootBuffer) - 1);
            if (copyLen > 0) {
                memcpy(cleanRootBuffer, m_buildSettingsCleanSolutionRootPath.c_str(), copyLen);
            }
            cleanRootBuffer[copyLen] = '\0';
            initialized = true;
        }

        ImGui::TextUnformatted("Solution Root");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##SolutionRoot", cleanRootBuffer, sizeof(cleanRootBuffer))) {
            m_buildSettingsCleanSolutionRootPath = cleanRootBuffer;
        }
        if (LabeledActionButton("ClearSolutionRoot", OpenFontIcons::kTrash2, "Clear", "Clear solution root")) {
            m_buildSettingsCleanSolutionRootPath.clear();
            cleanRootBuffer[0] = '\0';
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Required. Existing content is versioned and replaced each build.");
        ImGui::PopTextWrapPos();
    }

    if (m_buildSettingsRuntimeDebugLog && m_buildSettingsSizeTarget != SizeTargetPreset::None) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetSemanticWarningColor(), "Runtime debug logs can make you miss the size budget.");
        ImGui::PopTextWrapPos();
    }
    if (m_buildSettingsCompactTrackDebugLog && m_buildSettingsSizeTarget != SizeTargetPreset::None) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetSemanticWarningColor(), "Compact-track debug logs can make you miss the size budget.");
        ImGui::PopTextWrapPos();
    }

    ImGui::SeparatorText("Dependencies");
    if (LabeledActionButton("RefreshDeps", OpenFontIcons::kRefresh, "Refresh", "Refresh dependency status")) {
        m_buildSettingsRefreshRequested = true;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Mode: %s", BuildModeLabel(m_buildSettingsMode));
    const char* targetLabel = "Self-Contained Demo (.exe)";
    if (m_buildSettingsTargetKind == BuildTargetKind::PackagedDemo) targetLabel = "Packaged Demo (.zip)";
    else if (m_buildSettingsTargetKind == BuildTargetKind::SelfContainedScreenSaver) targetLabel = "Self-Contained Screen Saver (.scr)";
    else if (m_buildSettingsTargetKind == BuildTargetKind::MicroDemo) targetLabel = "Micro-Demo (.exe)";
    ImGui::TextDisabled("Target: %s", targetLabel);
    ImGui::PopTextWrapPos();

    const bool crinklerPossible =
        (m_buildSettingsMode == BuildMode::ReleaseCrinkled) &&
        m_buildSettingsPrereq.hasCrinkler &&
        m_buildSettingsPrereq.hasNinja;
    const char* activeLinker = (m_buildSettingsMode == BuildMode::Release)
        ? "MSVC link.exe"
        : (crinklerPossible ? "Crinkler" : "Unavailable (missing Crinkler or Ninja)");
    ImGui::Text("Linker: %s", activeLinker);

    struct DepRow {
        const char* name;
        bool present;
        bool required;
        const char* configureLabel;
        std::string configureTarget;
    };

    const bool crinklerRequired = (m_buildSettingsMode == BuildMode::ReleaseCrinkled);
    const bool ninjaRequired = (m_buildSettingsMode == BuildMode::ReleaseCrinkled);
    const std::vector<DepRow> deps = {
        { "Visual Studio C++ Build Tools", m_buildSettingsPrereq.hasVisualStudioTools, true, "Install", "https://visualstudio.microsoft.com/downloads/" },
        { "Windows SDK", m_buildSettingsPrereq.hasWindowsSdk, true, "Install", "https://developer.microsoft.com/windows/downloads/windows-sdk/" },
        { "CMake", m_buildSettingsPrereq.hasCMake, true, "Install", "https://cmake.org/download/" },
        { "DXC Runtime (dxcompiler.dll)", m_buildSettingsPrereq.hasDxcRuntime, true, "Download", "https://github.com/microsoft/DirectXShaderCompiler/releases" },
        { "Crinkler", m_buildSettingsPrereq.hasCrinkler, crinklerRequired, "Setup Guide", m_appRoot + "\\README-CRINKLER.txt" },
        { "Ninja", m_buildSettingsPrereq.hasNinja, ninjaRequired, "Install", "https://github.com/ninja-build/ninja/releases" }
    };

    if (ImGui::BeginTable("BuildDeps", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Dependency");
        ImGui::TableSetupColumn("Required");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Configure");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < deps.size(); ++i) {
            const auto& dep = deps[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(dep.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(dep.required ? "Yes" : "Optional");

            ImGui::TableSetColumnIndex(2);
            if (dep.present) {
                ImGui::TextColored(GetSemanticSuccessColor(), "Detected");
            } else if (dep.required) {
                ImGui::TextColored(GetSemanticErrorColor(), "Missing");
            } else {
                ImGui::TextColored(GetSemanticInfoColor(), "Missing (Optional)");
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::PushID((int)i + 1000);
            if (!dep.present && LabeledActionButton("Cfg", OpenFontIcons::kFolder, dep.configureLabel, dep.configureLabel, ImVec2(-FLT_MIN, 0.0f))) {
                OpenExternal(dep.configureTarget);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!m_buildSettingsPrereq.hasCrinkler) {
        if (LabeledActionButton("PickCrinklerExe", OpenFontIcons::kFolder, "Crinkler Override", "Use a custom crinkler.exe (bundled copy is preferred)")) {
            char pathBuf[512] = { 0 };
            OPENFILENAMEA ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
            ofn.lpstrFile = pathBuf;
            ofn.nMaxFile = sizeof(pathBuf);
            ofn.lpstrFilter = "Crinkler (crinkler.exe)\0crinkler.exe\0Executables (*.exe)\0*.exe\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                m_buildSettingsCrinklerPath = pathBuf;
                SetEnvironmentVariableA("SHADERLAB_CRINKLER", pathBuf);
                m_buildSettingsRefreshRequested = true;
            }
        }
    }

    if (!m_buildSettingsPrereq.hasNinja) {
        if (LabeledActionButton("CopyNinjaWinget", OpenFontIcons::kCopy, "Copy Ninja Command", "Copy Ninja install command")) {
            ImGui::SetClipboardText("winget install Ninja-build.Ninja");
        }
    }

    if (!m_buildSettingsPrereq.message.empty()) {
        ImGui::Separator();
        ImGui::BeginChild("PrereqMsg", ImVec2(0, 120), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_buildSettingsPrereq.message.c_str());
        ImGui::EndChild();
    }

    ImGui::Separator();
    if (!canCloseBuildWindow) {
        ImGui::BeginDisabled();
    }
    if (LabeledActionButton("CloseBuildSettings", OpenFontIcons::kXCircle, "Close", "Close", ImVec2(140.0f, 0.0f)) && canCloseBuildWindow) {
        m_showBuildSettings = false;
    }
    if (!canCloseBuildWindow) {
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Build in progress. This window will close when done.");
        ImGui::PopTextWrapPos();
    }

    const bool hasCleanRoot = !m_buildSettingsCleanSolutionRootPath.empty();
    const bool canBuild = m_buildSettingsPrereq.ok && hasCleanRoot;
    const bool canStartBuild = canBuild && !m_isBuilding;
    if (!canStartBuild) {
        ImGui::BeginDisabled();
    }
    if (LabeledActionButton("BuildFromSettings", OpenFontIcons::kPlay, "Build Now", "Build to the selected solution root", ImVec2(180.0f, 0.0f))) {
        if (m_currentMode == UIMode::PostFX && m_postFxSourceSceneIndex >= 0 && m_postFxSourceSceneIndex < (int)m_scenes.size()) {
            m_scenes[m_postFxSourceSceneIndex].postFxChain = m_postFxDraftChain;
        }

        if (m_currentProjectPath.empty()) {
            SaveProjectAs();
            if (m_currentProjectPath.empty()) {
                if (!canBuild) {
                    ImGui::EndDisabled();
                }
                ImGui::End();
                return;
            }
        } else {
            SaveProject();
        }

        const BuildTargetKind buildTargetKind = m_buildSettingsTargetKind;
        const bool buildScreenSaver = buildTargetKind == BuildTargetKind::SelfContainedScreenSaver;
        const bool buildPackaged = buildTargetKind == BuildTargetKind::PackagedDemo;
        std::string targetOutputPath;
        const std::string defaultBinaryName = (m_currentProjectPath.empty() ? "MyDemo" : fs::path(m_currentProjectPath).stem().string()) + (buildPackaged ? ".zip" : (buildScreenSaver ? ".scr" : ".exe"));

        fs::path outputDir = fs::path(m_buildSettingsCleanSolutionRootPath);
        std::error_code makeDirEc;
        fs::create_directories(outputDir, makeDirEc);
        if (makeDirEc) {
            std::lock_guard<std::mutex> lock(m_buildLogMutex);
            m_buildLog += "Error: Failed to create clean solution root directory for output binary.\n";
            m_buildLog += "Path: " + outputDir.string() + "\n";
            m_buildLog += "Details: " + makeDirEc.message() + "\n";
            ImGui::End();
            return;
        }
        targetOutputPath = (outputDir / defaultBinaryName).string();

        if (!targetOutputPath.empty()) {
            if (buildTargetKind == BuildTargetKind::MicroDemo) {
                {
                    std::lock_guard<std::mutex> lock(m_buildLogMutex);
                    m_buildLog += "[Micro Step 1/2] Preflight: assembling conflict state for ubershader...\n";
                }
                m_microUbershaderConflictsDirty = true;
                RefreshMicroUbershaderConflictCache();

                bool hasUnresolvedConflicts = false;
                for (const auto& conflict : m_microUbershaderConflicts) {
                    const auto it = m_microUbershaderKeepEntrypointsBySignature.find(conflict.signatureKey);
                    if (it == m_microUbershaderKeepEntrypointsBySignature.end() || it->second.empty()) {
                        hasUnresolvedConflicts = true;
                        break;
                    }
                }

                if (hasUnresolvedConflicts) {
                    std::lock_guard<std::mutex> lock(m_buildLogMutex);
                    m_buildLog += "[Micro Step 1/2] Conflict(s) detected and unresolved. Resolve them in 'Micro Ubershader Conflicts' before build.\n";
                    m_buildLog += "Build not started.\n";
                    ImGui::End();
                    return;
                }

                {
                    std::error_code cleanupEc;
                    const fs::path stalePackShaders = fs::path(m_buildSettingsCleanSolutionRootPath) / "build_selfcontained_pack" / "assets" / "shaders";
                    fs::remove(stalePackShaders / "ubershader.hlsl", cleanupEc);
                    cleanupEc.clear();
                    fs::remove(stalePackShaders / "ubershader.bin", cleanupEc);
                }

                {
                    std::lock_guard<std::mutex> lock(m_buildLogMutex);
                    m_buildLog += "[Micro Step 2/2] Conflicts resolved. Rebuilding ubershader + runtime from scratch...\n";
                }
            }

            m_isBuilding = true;
            m_buildComplete = false;
            m_buildSuccess = false;
            m_buildLog = "Initializing Build Process...\n";
            m_buildLog += std::string("Build Mode: ") + BuildModeLabel(m_buildSettingsMode) + "\n";
            m_buildLog += std::string("Size Target: ") + SizePresetLabel(m_buildSettingsSizeTarget) + "\n";
            m_buildLog += std::string("Restricted Compact Track: ") + (m_buildSettingsRestrictedCompactTrack ? "Enabled" : "Disabled") + "\n";
            m_buildLog += std::string("Runtime Debug Logs: ") + (m_buildSettingsRuntimeDebugLog ? "Enabled" : "Disabled") + "\n";
            m_buildLog += std::string("Compact Track Debug Logs: ") + (m_buildSettingsCompactTrackDebugLog ? "Enabled" : "Disabled") + "\n";
            m_buildLog += std::string("Output Type: ") + (buildPackaged ? "Packaged Demo (.zip)" : (buildScreenSaver ? "Screen Saver (.scr)" : "Executable (.exe)")) + "\n";
            m_buildLog += std::string("Clean Solution Root: ") + m_buildSettingsCleanSolutionRootPath + "\n";
            m_buildLog += std::string("Output Binary: ") + targetOutputPath + "\n";
            if (m_buildSettingsAutoSwitchedToCrinkled) {
                m_buildLog += "Build Mode Auto-Switch: Release -> Release Crinkled (size budget with Crinkler+Ninja detected)\n";
                m_buildSettingsAutoSwitchedToCrinkled = false;
            }

            const std::string targetExePath = targetOutputPath;
            const std::string projectPath = m_currentProjectPath;
            const std::string appRoot = m_appRoot;
            const BuildTargetKind selectedTargetKind = m_buildSettingsTargetKind;
            const BuildMode selectedMode = m_buildSettingsMode;
            const SizeTargetPreset selectedSizeTarget = m_buildSettingsSizeTarget;
            const bool selectedRestrictedCompactTrack = m_buildSettingsRestrictedCompactTrack;
            const bool selectedRuntimeDebugLog = m_buildSettingsRuntimeDebugLog;
            const bool selectedCompactTrackDebugLog = m_buildSettingsCompactTrackDebugLog;
            const bool selectedMicroDeveloperBuild = m_buildSettingsMicroDeveloperBuild;
            const std::string selectedCleanSolutionRootPath = m_buildSettingsCleanSolutionRootPath;
            const auto selectedMicroKeepEntrypointsBySignature = m_microUbershaderKeepEntrypointsBySignature;

            m_buildFuture = std::async(std::launch::async, [this, targetExePath, projectPath, appRoot, selectedTargetKind, selectedMode, selectedSizeTarget, selectedRestrictedCompactTrack, selectedRuntimeDebugLog, selectedCompactTrackDebugLog, selectedMicroDeveloperBuild, selectedCleanSolutionRootPath, selectedMicroKeepEntrypointsBySignature]() {
                auto Log = [&](const std::string& msg) {
                    std::lock_guard<std::mutex> lock(m_buildLogMutex);
                    m_buildLog += msg;
                    if (msg.empty() || msg.back() != '\n') {
                        m_buildLog += "\n";
                    }
                };

                BuildRequest request;
                request.appRoot = appRoot;
                request.projectPath = projectPath;
                request.targetExePath = targetExePath;
                request.targetKind = selectedTargetKind;
                request.mode = selectedMode;
                request.sizeTarget = selectedSizeTarget;
                request.restrictedCompactTrack = selectedRestrictedCompactTrack;
                request.runtimeDebugLog = selectedRuntimeDebugLog;
                request.compactTrackDebugLog = selectedCompactTrackDebugLog;
                request.microDeveloperBuild = selectedMicroDeveloperBuild;
                request.cleanSolutionRootPath = selectedCleanSolutionRootPath;
                if (selectedTargetKind == BuildTargetKind::MicroDemo) {
                    request.microUbershaderKeepEntrypointsBySignature = selectedMicroKeepEntrypointsBySignature;
                }

                BuildResult result = BuildPipeline::BuildSelfContained(request, Log);
                m_buildSuccess = result.success;
                if (result.success) {
                    m_lastSuccessfulBuildOutputPath = targetExePath;
                }
                m_buildComplete = true;
            });
        }
    }

    if (m_buildComplete && m_buildSuccess && !m_lastSuccessfulBuildOutputPath.empty()) {
        ImGui::SameLine();
        if (LabeledActionButton("OpenDemoFolder", OpenFontIcons::kFolder, "Demo Folder", "Open output folder for latest successful demo build", ImVec2(180.0f, 0.0f))) {
            fs::path outputPath = fs::path(m_lastSuccessfulBuildOutputPath);
            fs::path folderPath = outputPath.parent_path();
            OpenExternal((folderPath.empty() ? outputPath : folderPath).string());
        }
    }

    if (!canStartBuild) {
        ImGui::EndDisabled();
        if (!canBuild) {
            ImGui::PushTextWrapPos(0.0f);
            if (!hasCleanRoot) {
                ImGui::TextColored(GetSemanticErrorColor(), "Set Solution Root to enable build.");
            } else {
                ImGui::TextColored(GetSemanticErrorColor(), "Install missing required dependencies to enable build.");
            }
            ImGui::PopTextWrapPos();
        } else if (m_isBuilding) {
            ImGui::TextDisabled("Build already running.");
        }
    }
    if (m_buildSettingsTargetKind == BuildTargetKind::MicroDemo && unresolvedMicroConflictCount > 0) {
        ImGui::TextColored(GetSemanticErrorColor(), "Unresolved conflicts: %d", unresolvedMicroConflictCount);
    }

    if (m_isBuilding || !m_buildLog.empty()) {
        static bool autoCopyOnFailure = false;
        static bool didAutoCopy = false;

        ImGui::SeparatorText("Build Console");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, m_uiThemeColors.ConsoleBackground);
        ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.ConsoleFontColor);
        ImGui::BeginChild("BuildConsoleRegion", ImVec2(0.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(m_buildLogMutex);
            ImGui::TextUnformatted(m_buildLog.c_str());
            if (m_isBuilding && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        if (!m_isBuilding && m_buildComplete) {
            if (m_buildSuccess) {
                ImGui::TextColored(GetSemanticSuccessColor(), "Build Completed Successfully!");
            } else {
                ImGui::TextColored(GetSemanticErrorColor(), "Build Failed.");
                if (autoCopyOnFailure && !didAutoCopy) {
                    std::lock_guard<std::mutex> lock(m_buildLogMutex);
                    ImGui::SetClipboardText(m_buildLog.c_str());
                    didAutoCopy = true;
                }
            }
        } else if (m_isBuilding) {
            didAutoCopy = false;
            static float time = 0.0f;
            time += ImGui::GetIO().DeltaTime;
            const char* dots = (int(time * 2) % 4) == 0 ? ".   " : (int(time * 2) % 4) == 1 ? "..  " : (int(time * 2) % 4) == 2 ? "... " : "....";
            ImGui::Text("Building%s", dots);
        }

        if (LabeledActionButton("CopyBuildLogInline", OpenFontIcons::kCopy, "Copy Log", "Copy build log text", ImVec2(150.0f, 0.0f))) {
            std::lock_guard<std::mutex> lock(m_buildLogMutex);
            ImGui::SetClipboardText(m_buildLog.c_str());
        }
        ImGui::Checkbox("Auto copy on failure", &autoCopyOnFailure);
    }

    const bool settingsChanged =
        prevTargetKind != m_buildSettingsTargetKind ||
        prevMode != m_buildSettingsMode ||
        prevSizeTarget != m_buildSettingsSizeTarget ||
        prevRestrictedCompactTrack != m_buildSettingsRestrictedCompactTrack ||
        prevRuntimeDebugLog != m_buildSettingsRuntimeDebugLog ||
        prevCompactTrackDebugLog != m_buildSettingsCompactTrackDebugLog ||
        prevMicroDeveloperBuild != m_buildSettingsMicroDeveloperBuild ||
        prevCleanSolutionRootPath != m_buildSettingsCleanSolutionRootPath;
    if (settingsChanged) {
        SaveUiBuildSettings(
            m_appRoot,
            m_buildSettingsTargetKind,
            m_buildSettingsMode,
            m_buildSettingsSizeTarget,
            m_buildSettingsRestrictedCompactTrack,
            m_buildSettingsRuntimeDebugLog,
            m_buildSettingsCompactTrackDebugLog,
            m_buildSettingsMicroDeveloperBuild,
            m_buildSettingsCleanSolutionRootPath,
            m_buildSettingsCrinklerPath);
        if (!m_currentProjectPath.empty()) {
            SaveProjectUiSettings();
        }
    }

    ImGui::End();
}

} // namespace ShaderLab