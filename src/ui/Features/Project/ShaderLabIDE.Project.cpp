#include "ShaderLab/UI/ShaderLabIDE.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "ShaderLab/Audio/AudioSystem.h"
#include "ShaderLab/DevKit/RuntimeExporter.h"
#include "ShaderLab/Core/Serializer.h"
#include "ShaderLab/UI/UISystemAssets.h"

#include <nlohmann/json.hpp>

#include <commdlg.h>
#include <imgui.h>

namespace ShaderLab {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path GetProjectUiSettingsPath(const std::string& projectPath) {
    if (projectPath.empty()) {
        return {};
    }
    const fs::path project(projectPath);
    const fs::path parent = project.parent_path();
    const std::string stem = project.stem().string();
    return parent / (stem + ".shaderlab.user.json");
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

std::string SanitizeProjectFolderName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == ' ' || c == '_' || c == '-';
        out.push_back(ok ? c : '_');
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) {
        out = "Untitled Demo";
    }
    return out;
}

std::string NormalizePathSlashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string SanitizeFileStem(const std::string& name, const std::string& fallback) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';
        out.push_back(ok ? c : '_');
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = fallback;
    }
    return out;
}
} // namespace

bool ShaderLabIDE::EnsureProjectLayoutFolders() const {
    if (m_currentProjectPath.empty()) {
        return false;
    }

    const fs::path projectDir = fs::path(m_currentProjectPath).parent_path();
    if (projectDir.empty()) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(projectDir, ec);
    if (ec) {
        return false;
    }

    fs::create_directories(projectDir / "assets", ec);
    if (ec) {
        return false;
    }

    fs::create_directories(projectDir / "shaders" / "scenes", ec);
    if (ec) {
        return false;
    }

    fs::create_directories(projectDir / "shaders" / "postfx", ec);
    return !ec;
}

std::string ShaderLabIDE::MakeWorkspaceRelativePath(const std::string& absolutePath) const {
    if (absolutePath.empty()) {
        return {};
    }

    fs::path pathValue = fs::path(absolutePath).lexically_normal();
    if (!pathValue.is_absolute()) {
        return NormalizePathSlashes(pathValue.string());
    }

    if (!m_workspaceRootPath.empty()) {
        std::error_code ec;
        fs::path rel = fs::relative(pathValue, fs::path(m_workspaceRootPath), ec);
        if (!ec && !rel.empty()) {
            const std::string relStr = rel.string();
            const bool climbs = relStr == ".." || relStr.rfind("..\\", 0) == 0 || relStr.rfind("../", 0) == 0;
            if (!climbs) {
                return NormalizePathSlashes(rel.lexically_normal().string());
            }
        }
    }

    return NormalizePathSlashes(pathValue.string());
}

std::string ShaderLabIDE::ImportAssetIntoProject(const std::string& sourcePath) {
    if (sourcePath.empty() || m_currentProjectPath.empty()) {
        return sourcePath;
    }

    if (!EnsureProjectLayoutFolders()) {
        return sourcePath;
    }

    std::error_code ec;
    fs::path source = fs::path(sourcePath).lexically_normal();
    if (!source.is_absolute()) {
        source = fs::absolute(source, ec);
        if (ec) {
            return sourcePath;
        }
    }

    const fs::path assetsDir = fs::path(m_currentProjectPath).parent_path() / "assets";
    fs::path destination = assetsDir / source.filename();

    if (source != destination) {
        int suffix = 2;
        while (fs::exists(destination, ec) && !ec) {
            destination = assetsDir / (source.stem().string() + "_" + std::to_string(suffix) + source.extension().string());
            ++suffix;
        }

        ec.clear();
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return sourcePath;
        }
    }

    return destination.lexically_normal().string();
}

void ShaderLabIDE::LoadProjectUiSettings() {
    const fs::path settingsPath = GetProjectUiSettingsPath(m_currentProjectPath);
    if (settingsPath.empty()) {
        return;
    }

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
    m_buildSettingsRestrictedCompactTrack = build.value("restrictedCompactTrack", m_buildSettingsRestrictedCompactTrack);
    m_buildSettingsRuntimeDebugLog = build.value("runtimeDebugLog", m_buildSettingsRuntimeDebugLog);
    m_buildSettingsCompactTrackDebugLog = build.value("compactTrackDebugLog", m_buildSettingsCompactTrackDebugLog);
    m_buildSettingsMicroDeveloperBuild = build.value("microDeveloperBuild", m_buildSettingsMicroDeveloperBuild);
    m_buildSettingsCleanSolutionRootPath = build.value("cleanSolutionRootPath", m_buildSettingsCleanSolutionRootPath);
    m_microUbershaderKeepEntrypointsBySignature.clear();
    const json keepMap = build.value("microUbershaderKeepEntrypoints", json::object());
    if (keepMap.is_object()) {
        for (auto it = keepMap.begin(); it != keepMap.end(); ++it) {
            if (!it.value().is_array()) {
                continue;
            }

            std::vector<std::string> keep;
            for (const auto& entry : it.value()) {
                if (entry.is_string()) {
                    keep.push_back(entry.get<std::string>());
                }
            }
            if (!keep.empty()) {
                m_microUbershaderKeepEntrypointsBySignature[it.key()] = std::move(keep);
            }
        }
    }
    m_microUbershaderConflictsDirty = true;
    m_buildSettingsRefreshRequested = true;
}

void ShaderLabIDE::SaveProjectUiSettings() const {
    if (m_currentProjectPath.empty()) {
        return;
    }

    const fs::path settingsPath = GetProjectUiSettingsPath(m_currentProjectPath);
    std::ofstream out(settingsPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    json root;
    json microKeep = json::object();
    for (const auto& [signature, keep] : m_microUbershaderKeepEntrypointsBySignature) {
        if (keep.empty()) {
            continue;
        }
        microKeep[signature] = keep;
    }
    root["build"] = {
        {"targetKind", BuildTargetKindToString(m_buildSettingsTargetKind)},
        {"mode", (m_buildSettingsMode == BuildMode::ReleaseCrinkled) ? "crinkled" : "release"},
        {"sizeTarget", SizePresetToString(m_buildSettingsSizeTarget)},
        {"restrictedCompactTrack", m_buildSettingsRestrictedCompactTrack},
        {"runtimeDebugLog", m_buildSettingsRuntimeDebugLog},
        {"compactTrackDebugLog", m_buildSettingsCompactTrackDebugLog},
        {"microDeveloperBuild", m_buildSettingsMicroDeveloperBuild},
        {"cleanSolutionRootPath", m_buildSettingsCleanSolutionRootPath},
        {"microUbershaderKeepEntrypoints", microKeep}
    };

    out << root.dump(2);
}

void ShaderLabIDE::SaveProject() {
    if (m_currentProjectPath.empty()) {
        SaveProjectAs();
        return;
    }

    ProjectData data;
    data.scenes = m_scenes;
    data.track = m_track;
    data.transport = m_transport;
    data.renderAspectRatioPreset =
        (m_aspectRatio == AspectRatio::Ratio_1_1) ? RenderAspectRatioPreset::Ratio_1_1 :
        (m_aspectRatio == AspectRatio::Ratio_16_10) ? RenderAspectRatioPreset::Ratio_16_10 :
        (m_aspectRatio == AspectRatio::Ratio_4_3) ? RenderAspectRatioPreset::Ratio_4_3 :
        RenderAspectRatioPreset::Ratio_16_9;
    data.fullscreenRenderResolutionPreset = m_fullscreenRenderResolutionPreset;
    data.audioLibrary = m_audioLibrary;
    data.demoTitle = m_demoTitle;
    data.demoAuthor = m_demoAuthor;
    data.demoDescription = m_demoDescription;

    if (!EnsureProjectLayoutFolders()) {
        return;
    }

    const fs::path projectRoot = fs::path(m_currentProjectPath).parent_path();
    const fs::path workspaceRoot = m_workspaceRootPath.empty() ? fs::path() : fs::path(m_workspaceRootPath);

    auto makeAbsolutePath = [&](std::string& value) {
        if (value.empty()) {
            return;
        }
        fs::path pathValue(value);
        if (!pathValue.is_absolute()) {
            const fs::path workspaceCandidate = workspaceRoot.empty() ? fs::path() : (workspaceRoot / pathValue);
            std::error_code ec;
            if (!workspaceRoot.empty() && fs::exists(workspaceCandidate, ec) && !ec) {
                pathValue = workspaceCandidate;
            } else {
                pathValue = projectRoot / pathValue;
            }
        }
        value = pathValue.lexically_normal().string();
    };

    auto writeTextFile = [](const fs::path& path, const std::string& content) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << content;
        return true;
    };

    for (auto& clip : data.audioLibrary) {
        makeAbsolutePath(clip.path);
        clip.path = MakeWorkspaceRelativePath(clip.path);
    }

    const fs::path sceneShaderDir = projectRoot / "shaders" / "scenes";
    const fs::path postFxShaderDir = projectRoot / "shaders" / "postfx";

    for (size_t sceneIndex = 0; sceneIndex < data.scenes.size(); ++sceneIndex) {
        auto& scene = data.scenes[sceneIndex];
        if (!scene.precompiledPath.empty()) {
            makeAbsolutePath(scene.precompiledPath);
            scene.precompiledPath = MakeWorkspaceRelativePath(scene.precompiledPath);
        }
        for (auto& binding : scene.bindings) {
            if (binding.bindingType == BindingType::File) {
                makeAbsolutePath(binding.filePath);
                binding.filePath = MakeWorkspaceRelativePath(binding.filePath);
            }
        }

        fs::path sceneShaderPath;
        if (!scene.shaderCodePath.empty()) {
            makeAbsolutePath(scene.shaderCodePath);
            sceneShaderPath = fs::path(scene.shaderCodePath);
        } else {
            const std::string stem = SanitizeFileStem(scene.name, "scene_" + std::to_string(sceneIndex + 1));
            sceneShaderPath = sceneShaderDir / (stem + ".hlsl");
        }
        if (writeTextFile(sceneShaderPath, scene.shaderCode)) {
            scene.shaderCodePath = MakeWorkspaceRelativePath(sceneShaderPath.lexically_normal().string());
        }

        for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
            auto& fx = scene.postFxChain[fxIndex];
            if (!fx.precompiledPath.empty()) {
                makeAbsolutePath(fx.precompiledPath);
                fx.precompiledPath = MakeWorkspaceRelativePath(fx.precompiledPath);
            }
            fs::path fxShaderPath;
            if (!fx.shaderCodePath.empty()) {
                makeAbsolutePath(fx.shaderCodePath);
                fxShaderPath = fs::path(fx.shaderCodePath);
            } else {
                const std::string sceneStem = SanitizeFileStem(scene.name, "scene_" + std::to_string(sceneIndex + 1));
                const std::string fxStem = SanitizeFileStem(fx.name, "postfx_" + std::to_string(fxIndex + 1));
                fxShaderPath = postFxShaderDir / (sceneStem + "_" + fxStem + ".hlsl");
            }
            if (writeTextFile(fxShaderPath, fx.shaderCode)) {
                fx.shaderCodePath = MakeWorkspaceRelativePath(fxShaderPath.lexically_normal().string());
            }
        }
    }

    for (auto& row : data.track.rows) {
        if (row.transitionPresetStem.empty()) {
            row.transitionShaderPath.clear();
            continue;
        }
        row.transitionShaderPath = NormalizePathSlashes((fs::path("presets") / "transitions" / (row.transitionPresetStem + ".hlsl")).string());
    }

    if (Serializer::SaveProject(data, m_currentProjectPath)) {
        SaveProjectUiSettings();
        RefreshPresetService();
    }
}

void ShaderLabIDE::CreateNewProjectInWorkspace(const std::string& projectNameHint) {
    fs::path projectsRoot = m_workspaceProjectsPath.empty()
        ? fs::path(m_workspaceRootPath) / "projects"
        : fs::path(m_workspaceProjectsPath);
    if (projectsRoot.empty()) {
        return;
    }

    std::error_code ec;
    fs::create_directories(projectsRoot, ec);

    const std::string baseName = SanitizeProjectFolderName(projectNameHint.empty() ? std::string("Untitled Demo") : projectNameHint);
    fs::path projectFolder = projectsRoot / baseName;
    fs::path projectFile = projectFolder / "project.json";

    int suffix = 2;
    while (fs::exists(projectFile)) {
        projectFolder = projectsRoot / (baseName + "_" + std::to_string(suffix));
        projectFile = projectFolder / "project.json";
        ++suffix;
    }

    fs::create_directories(projectFolder, ec);
    m_currentProjectPath = projectFile.lexically_normal().string();
    SaveProject();
    RefreshPresetService();
}

void ShaderLabIDE::SaveProjectAs() {
    char szFile[260] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "JSON Project (*.json)\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "json";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    char initialDir[260] = { 0 };
    const std::string preferredDir = m_workspaceProjectsPath.empty() ? m_appRoot : m_workspaceProjectsPath;
    strcpy_s(initialDir, preferredDir.c_str());
    ofn.lpstrInitialDir = initialDir;

    if (GetSaveFileNameA(&ofn)) {
        fs::path selectedPath(szFile);
        if (!selectedPath.has_extension()) {
            selectedPath.replace_extension(".json");
        }
        const std::string folderName = SanitizeProjectFolderName(selectedPath.stem().string());
        const fs::path projectFolder = selectedPath.parent_path() / folderName;
        std::error_code ec;
        fs::create_directories(projectFolder, ec);
        m_currentProjectPath = (projectFolder / "project.json").lexically_normal().string();
        SaveProject();
        SaveProjectUiSettings();
        RefreshPresetService();
    }
}

void ShaderLabIDE::OpenProject() {
    char szFile[260] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "JSON Project (*.json)\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "json";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    char initialDir[260] = { 0 };
    const std::string preferredDir = m_workspaceProjectsPath.empty() ? m_appRoot : m_workspaceProjectsPath;
    strcpy_s(initialDir, preferredDir.c_str());
    ofn.lpstrInitialDir = initialDir;

    if (GetOpenFileNameA(&ofn)) {
        m_currentProjectPath = szFile;
        ProjectData data;
        if (Serializer::LoadProject(m_currentProjectPath, data)) {
            m_scenes = data.scenes;
            m_audioLibrary = data.audioLibrary;
            m_track = data.track;
            m_transport.bpm = data.transport.bpm;
            m_aspectRatio =
                (data.renderAspectRatioPreset == RenderAspectRatioPreset::Ratio_1_1) ? AspectRatio::Ratio_1_1 :
                (data.renderAspectRatioPreset == RenderAspectRatioPreset::Ratio_16_10) ? AspectRatio::Ratio_16_10 :
                (data.renderAspectRatioPreset == RenderAspectRatioPreset::Ratio_4_3) ? AspectRatio::Ratio_4_3 :
                AspectRatio::Ratio_16_9;
            m_fullscreenRenderResolutionPreset = data.fullscreenRenderResolutionPreset;
            m_demoTitle = data.demoTitle;
            m_demoAuthor = data.demoAuthor;
            m_demoDescription = data.demoDescription;
            if (m_demoTitle.empty()) {
                m_demoTitle = GetProjectName();
            }

            fs::current_path(fs::path(m_currentProjectPath).parent_path());

            if (m_audioSystem) {
                m_audioSystem->Stop();
            }

            if (m_deviceRef) {
                for(auto& scene : m_scenes) {
                    for(auto& bind : scene.bindings) {
                        if (bind.bindingType == BindingType::File && !bind.filePath.empty()) {
                            LoadTextureFromFile(bind.filePath, bind.textureResource);
                        }
                    }
                }
            }

            LoadProjectUiSettings();
            RefreshPresetService();
        }
    }
}

void ShaderLabIDE::BuildProject() {
    m_buildSettingsTargetKind = BuildTargetKind::SelfContainedDemo;
    m_showBuildSettings = true;
    m_buildSettingsRefreshRequested = true;
}

void ShaderLabIDE::BuildScreenSaverProject() {
    m_buildSettingsTargetKind = BuildTargetKind::SelfContainedScreenSaver;
    m_showBuildSettings = true;
    m_buildSettingsRefreshRequested = true;
}

void ShaderLabIDE::BuildPackagedDemoProject() {
    m_buildSettingsTargetKind = BuildTargetKind::PackagedDemo;
    m_showBuildSettings = true;
    m_buildSettingsRefreshRequested = true;
}

void ShaderLabIDE::BuildMicroDemoProject() {
    m_buildSettingsTargetKind = BuildTargetKind::MicroDemo;
    m_buildSettingsMicroDeveloperBuild = false;
    m_microUbershaderConflictsDirty = true;
    m_showBuildSettings = true;
    m_buildSettingsRefreshRequested = true;
}

void ShaderLabIDE::BuildMicroDeveloperDemoProject() {
    m_buildSettingsTargetKind = BuildTargetKind::MicroDemo;
    m_buildSettingsMicroDeveloperBuild = true;
    m_buildSettingsRuntimeDebugLog = true;
    m_microUbershaderConflictsDirty = true;
    m_showBuildSettings = true;
    m_buildSettingsRefreshRequested = true;
}

void ShaderLabIDE::ExportRuntimePackage() {
    char szFile[260] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Executable (*.exe)\0*.exe\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "exe";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!m_currentProjectPath.empty()) {
        std::string name = fs::path(m_currentProjectPath).stem().string();
        strcpy_s(szFile, name.c_str());
    } else {
        strcpy_s(szFile, "MyDemo");
    }

    if (GetSaveFileNameA(&ofn)) {
        ProjectData data;
        data.scenes = m_scenes;
        data.track = m_track;
        data.transport = m_transport;
        data.renderAspectRatioPreset =
            (m_aspectRatio == AspectRatio::Ratio_1_1) ? RenderAspectRatioPreset::Ratio_1_1 :
            (m_aspectRatio == AspectRatio::Ratio_16_10) ? RenderAspectRatioPreset::Ratio_16_10 :
            (m_aspectRatio == AspectRatio::Ratio_4_3) ? RenderAspectRatioPreset::Ratio_4_3 :
            RenderAspectRatioPreset::Ratio_16_9;
        data.fullscreenRenderResolutionPreset = m_fullscreenRenderResolutionPreset;
        data.audioLibrary = m_audioLibrary;

        RuntimeExportRequest request;
        request.appRoot = m_appRoot;
        request.destExePath = szFile;
        request.data = data;

        RuntimeExportResult result = RuntimeExporter::Export(request);
        if (result.success) {
            MessageBoxA(NULL, result.message.c_str(), "Export Complete", MB_ICONINFORMATION);
        } else {
            MessageBoxA(NULL, result.message.c_str(), "Export Error", MB_ICONERROR);
        }
    }
}

} // namespace ShaderLab