#include "ShaderLab/UI/UISystemAssets.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ShaderLab {

namespace fs = std::filesystem;

const int kPostFxHistoryCount = 4;
const int kMaxPostFxChain = 32;
const int kAboutSrvIndex = 120;

namespace {

struct PresetFolderService {
    std::string category;
    fs::path workspaceRoot;
    fs::path appBackupRoot;
    std::vector<ShaderPreset> presets;

    fs::path WorkspaceDir() const {
        return workspaceRoot / "presets" / category;
    }

    fs::path BackupDir() const {
        return appBackupRoot / "editor_assets" / "presets" / category;
    }

    static std::string ReadAllText(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return {};
        }
        std::string content;
        in.seekg(0, std::ios::end);
        content.resize(static_cast<size_t>(in.tellg()));
        in.seekg(0, std::ios::beg);
        if (!content.empty()) {
            in.read(content.data(), static_cast<std::streamsize>(content.size()));
        }
        return content;
    }

    static std::string HumanizeStem(const std::string& stem) {
        std::string out;
        out.reserve(stem.size());
        bool capitalize = true;
        for (char c : stem) {
            if (c == '_' || c == '-' || c == '.') {
                out.push_back(' ');
                capitalize = true;
                continue;
            }
            if (capitalize) {
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
                capitalize = false;
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    void EnsureWorkspaceAndSeedFromBackup() const {
        std::error_code ec;
        fs::create_directories(WorkspaceDir(), ec);

        const fs::path backup = BackupDir();
        if (!fs::exists(backup, ec)) {
            return;
        }

        for (const auto& entry : fs::directory_iterator(backup, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const fs::path source = entry.path();
            if (source.extension() != ".hlsl") {
                continue;
            }
            const fs::path dest = WorkspaceDir() / source.filename();
            if (!fs::exists(dest, ec)) {
                fs::copy_file(source, dest, fs::copy_options::none, ec);
            }
        }
    }

    void Reload() {
        presets.clear();
        EnsureWorkspaceAndSeedFromBackup();

        std::error_code ec;
        std::vector<fs::path> files;
        if (fs::exists(WorkspaceDir(), ec)) {
            for (const auto& entry : fs::directory_iterator(WorkspaceDir(), ec)) {
                if (ec || !entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().extension() == ".hlsl") {
                    files.push_back(entry.path());
                }
            }
        }

        std::sort(files.begin(), files.end());

        for (const auto& path : files) {
            ShaderPreset preset;
            preset.stem = path.stem().string();
            preset.name = HumanizeStem(preset.stem);
            preset.filePath = path.string();
            preset.code = ReadAllText(path);
            if (!preset.code.empty()) {
                presets.push_back(std::move(preset));
            }
        }
    }

    std::string CodeByStem(const std::string& stem) const {
        for (const auto& preset : presets) {
            if (preset.stem == stem) {
                return preset.code;
            }
        }
        return {};
    }
};

struct PresetServiceState {
    PresetFolderService scenes{ "scenes" };
    PresetFolderService postFx{ "postfx" };
    PresetFolderService compute{ "compute" };
    PresetFolderService transitions{ "transitions" };
    std::chrono::steady_clock::time_point lastRefresh{};
    bool hasRefreshed = false;
};

PresetServiceState g_state;


} // namespace

void InitializePresetService(const std::string& workspaceRoot, const std::string& appRoot) {
    g_state.scenes.workspaceRoot = fs::path(workspaceRoot);
    g_state.postFx.workspaceRoot = fs::path(workspaceRoot);
    g_state.compute.workspaceRoot = fs::path(workspaceRoot);
    g_state.transitions.workspaceRoot = fs::path(workspaceRoot);

    g_state.scenes.appBackupRoot = fs::path(appRoot);
    g_state.postFx.appBackupRoot = fs::path(appRoot);
    g_state.compute.appBackupRoot = fs::path(appRoot);
    g_state.transitions.appBackupRoot = fs::path(appRoot);

    RefreshPresetService();
}

void RefreshPresetService() {
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kRefreshDebounce = std::chrono::milliseconds(200);
    if (g_state.hasRefreshed && (now - g_state.lastRefresh) < kRefreshDebounce) {
        return;
    }

    g_state.scenes.Reload();
    g_state.postFx.Reload();
    g_state.compute.Reload();
    g_state.transitions.Reload();
    g_state.lastRefresh = now;
    g_state.hasRefreshed = true;
}

const std::vector<ShaderPreset>& GetScenePresets() {
    return g_state.scenes.presets;
}

const std::vector<ShaderPreset>& GetPostFxPresets() {
    return g_state.postFx.presets;
}

const std::vector<ShaderPreset>& GetComputePresets() {
    return g_state.compute.presets;
}

const std::vector<ShaderPreset>& GetTransitionPresets() {
    return g_state.transitions.presets;
}

std::string GetScenePresetCodeByStem(const std::string& stem) {
    return g_state.scenes.CodeByStem(stem);
}

std::string GetPostFxPresetCodeByStem(const std::string& stem) {
    return g_state.postFx.CodeByStem(stem);
}

std::string GetComputePresetCodeByStem(const std::string& stem) {
    return g_state.compute.CodeByStem(stem);
}

std::string GetTransitionPresetCodeByStem(const std::string& stem) {
    return g_state.transitions.CodeByStem(stem);
}

std::string GetTransitionDisplayNameByStem(const std::string& stem) {
    for (const auto& preset : g_state.transitions.presets) {
        if (preset.stem == stem) {
            return preset.name;
        }
    }
    return PresetFolderService::HumanizeStem(stem);
}

std::string GetEditorTransitionShaderSourceByStem(const std::string& stem) {
    std::string code = GetTransitionPresetCodeByStem(stem);
    if (!code.empty()) {
        return code;
    }
    const auto& presets = GetTransitionPresets();
    if (!presets.empty()) {
        return presets[0].code;
    }
    return {};
}

} // namespace ShaderLab
