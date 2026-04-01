#include "ShaderLab/UI/AboutAssets.h"
#include <filesystem>
#include <fstream>

namespace ShaderLab {

namespace fs = std::filesystem;

AboutAssets& AboutAssets::Get() {
    static AboutAssets instance;
    return instance;
}

void AboutAssets::Initialize(const std::string& workspaceRoot, const std::string& appRoot) {
    m_workspaceRoot = workspaceRoot;
    m_appRoot = appRoot;

    std::error_code ec;
    fs::path aboutDir = fs::path(workspaceRoot) / "about";
    fs::create_directories(aboutDir, ec);

    // Seed from backup if needed
    fs::path backupDir = fs::path(appRoot) / "editor_assets" / "about";
    if (fs::exists(backupDir, ec)) {
        for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const fs::path source = entry.path();
            const fs::path dest = aboutDir / source.filename();
            if (!fs::exists(dest, ec)) {
                fs::copy_file(source, dest, fs::copy_options::none, ec);
            }
        }
    }

    Refresh();
}

void AboutAssets::Refresh() {
    m_currentAsset.shaderCode.clear();
    m_currentAsset.postFxEffects.clear();

    // Load the about logo shader
    fs::path aboutDir = fs::path(m_workspaceRoot) / "about";
    fs::path shaderPath = aboutDir / "about_logo.hlsl";

    std::error_code ec;
    if (!fs::exists(shaderPath, ec)) {
        // Fall back to backup
        shaderPath = fs::path(m_appRoot) / "editor_assets" / "about" / "about_logo.hlsl";
    }

    LoadAboutShader(shaderPath.string());
    LoadAboutEffects();
}

void AboutAssets::LoadAboutShader(const std::string& shaderPath) {
    std::ifstream file(shaderPath, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (size > 0) {
        m_currentAsset.shaderCode.resize(size);
        file.read(m_currentAsset.shaderCode.data(), static_cast<std::streamsize>(size));
    }
}

void AboutAssets::LoadAboutEffects() {
    // Load about_glitch post-fx effect
    fs::path effectPath = fs::path(m_workspaceRoot) / "presets" / "postfx" / "about_glitch.hlsl";

    std::error_code ec;
    if (fs::exists(effectPath, ec)) {
        std::ifstream file(effectPath, std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            size_t size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            if (size > 0) {
                std::string code(size, '\0');
                file.read(code.data(), static_cast<std::streamsize>(size));
                m_currentAsset.postFxEffects.emplace_back("Glitch A", code);
                m_currentAsset.postFxEffects.emplace_back("Glitch B", code);
            }
        }
    }
}

} // namespace ShaderLab
