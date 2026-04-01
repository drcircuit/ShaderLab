// Force include standard headers first to avoid namespace pollution
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include <system_error>
#include <future>
#include <mutex>
#include <cstdint>
#include <cstdlib>

#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UIConfig.h"
#include "ShaderLab/UI/UISystemDemoUtils.h"
#include "ShaderLab/UI/UISystemAssets.h"
#include "ShaderLab/UI/AboutAssets.h"
#include "ShaderLab/Core/CompilationService.h"
#include "ShaderLab/Audio/AudioSystem.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

namespace ShaderLab {

namespace fs = std::filesystem;

namespace {
std::string ReadEnvVar(const char* name) {
    char* value = nullptr;
    size_t length = 0;
    std::string out;
    if (_dupenv_s(&value, &length, name) == 0 && value && *value) {
        out = value;
    }
    if (value) {
        free(value);
    }
    return out;
}

std::string ReadInstalledWorkspaceFromRegistry() {
    char value[1024] = {};
    DWORD valueSize = static_cast<DWORD>(sizeof(value));
    const LSTATUS status = RegGetValueA(
        HKEY_CURRENT_USER,
        "Software\\ShaderLab",
        "WorkspaceFolder",
        RRF_RT_REG_SZ,
        nullptr,
        value,
        &valueSize);
    if (status != ERROR_SUCCESS || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}
} // namespace

void ShaderLabIDE::ResolveWorkspaceRootPath() {
    std::string workspaceRoot = ReadEnvVar("SHADERLAB_WORKSPACE");
    m_workspaceExplicitlyConfigured = !workspaceRoot.empty();
    if (workspaceRoot.empty()) {
        const std::string registryWorkspace = ReadInstalledWorkspaceFromRegistry();
        if (!registryWorkspace.empty()) {
            const fs::path registryPath = fs::path(registryWorkspace).lexically_normal();
            std::error_code ec;
            if (fs::exists(registryPath, ec) && !ec) {
                workspaceRoot = registryPath.string();
                m_workspaceExplicitlyConfigured = true;
            } else {
                m_workspaceExplicitlyConfigured = false;
            }
        }
    }
    if (workspaceRoot.empty()) {
        const std::string userProfile = ReadEnvVar("USERPROFILE");
        if (!userProfile.empty()) {
            workspaceRoot = (fs::path(userProfile) / "ShaderLabs").string();
        }
    }
    if (workspaceRoot.empty()) {
        workspaceRoot = (fs::current_path() / "ShaderLabs").string();
    }

    m_workspaceRootPath = fs::path(workspaceRoot).lexically_normal().string();
}

void ShaderLabIDE::EnsureWorkspaceFolders() {
    if (m_workspaceRootPath.empty()) {
        return;
    }

    const fs::path workspaceRoot(m_workspaceRootPath);
    const fs::path projectsDir = workspaceRoot / "projects";
    const fs::path snippetsDir = workspaceRoot / "snippets";
    const fs::path postFxDir = workspaceRoot / "postfx";

    std::error_code ec;
    fs::create_directories(workspaceRoot, ec);
    fs::create_directories(projectsDir, ec);
    fs::create_directories(snippetsDir, ec);
    fs::create_directories(postFxDir, ec);

    m_workspaceProjectsPath = projectsDir.lexically_normal().string();
    m_workspaceSnippetsPath = snippetsDir.lexically_normal().string();
    m_workspacePostFxPath = postFxDir.lexically_normal().string();
}

ShaderLabIDE::ShaderLabIDE() {
    // Resolve application root from executable location first (supports installed/portable layouts).
    char exePath[MAX_PATH] = {};
    DWORD exePathLen = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (exePathLen > 0 && exePathLen < MAX_PATH) {
        m_appRoot = fs::path(std::string(exePath, exePathLen)).parent_path().string();
    }
    if (m_appRoot.empty()) {
        m_appRoot = fs::current_path().string();
    }

    ResolveWorkspaceRootPath();
    m_workspaceSelectionPromptPending = !m_workspaceExplicitlyConfigured;
    EnsureWorkspaceFolders();
    InitializePresetService(m_workspaceRootPath, m_appRoot);
    AboutAssets::Get().Initialize(m_workspaceRootPath, m_appRoot);

    LoadGlobalUiBuildSettings();

    LoadUiThemeSettings();

    CreateDefaultScene();
    CreateDefaultTrack();

    InitializeCodeEditors();

    LoadGlobalSnippets();
}

std::string ShaderLabIDE::GetProjectName() const {
    if (m_currentProjectPath.empty()) {
        return "untitled";
    }

    fs::path path(m_currentProjectPath);
    std::string stem = path.stem().string();
    return stem.empty() ? "untitled" : stem;
}

ShaderLabIDE::~ShaderLabIDE() {
    Shutdown();
}

void ShaderLabIDE::Shutdown() {
    SaveUiThemeSettings();

    SaveGlobalUiBuildSettings();

    if (m_initialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
    }

    m_previewTexture.Reset();
    m_titlebarIconTexture.Reset();
    m_titlebarIconSrvGpuHandle = {};
    m_themeBackgroundTexture.Reset();
    m_themeBackgroundSrvGpuHandle = {};
    m_themeBackgroundWidth = 0;
    m_themeBackgroundHeight = 0;
    m_loadedThemeBackgroundPath.clear();
    m_previewRtvHeap.Reset();
    m_srvHeap.Reset();
    m_compilationService.reset();
    m_initialized = false;
}

} // namespace ShaderLab
