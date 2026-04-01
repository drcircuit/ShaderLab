#pragma once

#include <string>
#include <vector>

namespace ShaderLab {

struct AboutAsset {
    std::string shaderCode;
    std::vector<std::pair<std::string, std::string>> postFxEffects; // (name, code) pairs
};

class AboutAssets {
public:
    static AboutAssets& Get();

    // Initialize with workspace and app root paths
    void Initialize(const std::string& workspaceRoot, const std::string& appRoot);

    // Get the current about assets (shader + effects)
    const AboutAsset& GetCurrentAsset() const { return m_currentAsset; }

    // Refresh from disk
    void Refresh();

private:
    AboutAssets() = default;

    void LoadAboutShader(const std::string& shaderPath);
    void LoadAboutEffects();

    AboutAsset m_currentAsset;
    std::string m_workspaceRoot;
    std::string m_appRoot;
};

} // namespace ShaderLab
