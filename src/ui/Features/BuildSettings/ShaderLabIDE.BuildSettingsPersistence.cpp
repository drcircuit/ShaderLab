#include "ShaderLab/UI/ShaderLabIDE.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

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

SizeTargetPreset ParseSizePresetFromString(const std::string& value) {
    if (value == "1k" || value == "2k" || value == "4k" || value == "16k" || value == "32k") return SizeTargetPreset::K64;
    if (value == "64k") return SizeTargetPreset::K64;
    if (value == "128k") return SizeTargetPreset::K128;
    if (value == "256k") return SizeTargetPreset::K256;
    if (value == "512k") return SizeTargetPreset::K512;
    if (value == "1024k" || value == "1m") return SizeTargetPreset::K1024;
    return SizeTargetPreset::None;
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

BuildTargetKind ParseBuildTargetKindFromString(const std::string& value) {
    if (value == "packaged") return BuildTargetKind::PackagedDemo;
    if (value == "selfcontained-screensaver") return BuildTargetKind::SelfContainedScreenSaver;
    if (value == "micro") return BuildTargetKind::MicroDemo;
    return BuildTargetKind::SelfContainedDemo;
}

std::string BuildTargetKindToString(BuildTargetKind kind) {
    switch (kind) {
        case BuildTargetKind::PackagedDemo: return "packaged";
        case BuildTargetKind::SelfContainedScreenSaver: return "selfcontained-screensaver";
        case BuildTargetKind::MicroDemo: return "micro";
        default: return "selfcontained";
    }
}
} // namespace

void ShaderLabIDE::LoadGlobalUiBuildSettings() {
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

    const json build = root.value("build", json::object());
    m_buildSettingsTargetKind = ParseBuildTargetKindFromString(build.value("targetKind", std::string("selfcontained")));
    const std::string modeStr = build.value("mode", std::string("release"));
    m_buildSettingsMode = (modeStr == "crinkled" || modeStr == "release-crinkled")
        ? BuildMode::ReleaseCrinkled
        : BuildMode::Release;

    m_buildSettingsSizeTarget = ParseSizePresetFromString(build.value("sizeTarget", std::string("none")));
    m_buildSettingsRestrictedCompactTrack = build.value("restrictedCompactTrack", false);
    m_buildSettingsRuntimeDebugLog = build.value("runtimeDebugLog", false);
    m_buildSettingsCompactTrackDebugLog = build.value("compactTrackDebugLog", false);
    m_buildSettingsMicroDeveloperBuild = build.value("microDeveloperBuild", false);
    m_buildSettingsCleanSolutionRootPath = build.value("cleanSolutionRootPath", std::string());
    m_buildSettingsCrinklerPath = build.value("crinklerPath", std::string());

    if (!m_buildSettingsCrinklerPath.empty()) {
        SetEnvironmentVariableA("SHADERLAB_CRINKLER", m_buildSettingsCrinklerPath.c_str());
    }
}

void ShaderLabIDE::SaveGlobalUiBuildSettings() const {
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

    json build;
    build["targetKind"] = BuildTargetKindToString(m_buildSettingsTargetKind);
    build["mode"] = (m_buildSettingsMode == BuildMode::ReleaseCrinkled) ? "crinkled" : "release";
    build["sizeTarget"] = SizePresetToString(m_buildSettingsSizeTarget);
    build["restrictedCompactTrack"] = m_buildSettingsRestrictedCompactTrack;
    build["runtimeDebugLog"] = m_buildSettingsRuntimeDebugLog;
    build["compactTrackDebugLog"] = m_buildSettingsCompactTrackDebugLog;
    build["microDeveloperBuild"] = m_buildSettingsMicroDeveloperBuild;
    build["cleanSolutionRootPath"] = m_buildSettingsCleanSolutionRootPath;
    build["crinklerPath"] = m_buildSettingsCrinklerPath;
    root["build"] = build;

    std::ofstream out(settingsPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << root.dump(2);
}

} // namespace ShaderLab
