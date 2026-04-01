#include "ShaderLab/UI/ShaderLabIDE.h"

#include <algorithm>
#include <unordered_set>

#include "ShaderLab/Audio/AudioSystem.h"
#include "ShaderLab/DevKit/BuildPipeline.h"

namespace ShaderLab {

ProjectState ShaderLabIDE::CaptureState() {
    ProjectState state;
    state.scenes = m_scenes;
    state.audioLibrary = m_audioLibrary;
    state.track = m_track;
    state.transport = m_transport;
    state.renderAspectRatioPreset =
        (m_aspectRatio == AspectRatio::Ratio_1_1) ? RenderAspectRatioPreset::Ratio_1_1 :
        (m_aspectRatio == AspectRatio::Ratio_16_10) ? RenderAspectRatioPreset::Ratio_16_10 :
        (m_aspectRatio == AspectRatio::Ratio_4_3) ? RenderAspectRatioPreset::Ratio_4_3 :
        RenderAspectRatioPreset::Ratio_16_9;
    state.fullscreenRenderResolutionPreset = m_fullscreenRenderResolutionPreset;
    state.demoTitle = m_demoTitle;
    state.demoAuthor = m_demoAuthor;
    state.demoDescription = m_demoDescription;
    state.currentMode = m_currentMode;
    state.shaderState = m_shaderState;
    state.activeSceneIndex = m_activeSceneIndex;

    // Strip GPU resources from the saved state to ensure they don't dangle
    for (auto& scene : state.scenes) {
        scene.texture.Reset();
        scene.srvHeap.Reset();
        scene.pipelineState.Reset();
        scene.textureValid = false;
        scene.postFxTextureA.Reset();
        scene.postFxTextureB.Reset();
        scene.postFxSrvHeap.Reset();
        scene.postFxRtvHeap.Reset();
        scene.postFxValid = false;
        for (auto& fx : scene.postFxChain) {
            fx.pipelineState.Reset();
            fx.isDirty = true;
            fx.historyIndex = 0;
            fx.historyInitialized = false;
            fx.historyTextures.clear();
        }

        for (auto& binding : scene.bindings) {
             binding.textureResource.Reset();
             binding.fileTextureValid = false;
        }
    }

    return state;
}

void ShaderLabIDE::RestoreState(const ProjectState& state) {
    m_scenes = state.scenes;
    m_audioLibrary = state.audioLibrary;
    m_track = state.track;
    m_transport = state.transport;
    m_aspectRatio =
        (state.renderAspectRatioPreset == RenderAspectRatioPreset::Ratio_1_1) ? AspectRatio::Ratio_1_1 :
        (state.renderAspectRatioPreset == RenderAspectRatioPreset::Ratio_16_10) ? AspectRatio::Ratio_16_10 :
        (state.renderAspectRatioPreset == RenderAspectRatioPreset::Ratio_4_3) ? AspectRatio::Ratio_4_3 :
        AspectRatio::Ratio_16_9;
    m_fullscreenRenderResolutionPreset = state.fullscreenRenderResolutionPreset;
    m_demoTitle = state.demoTitle;
    m_demoAuthor = state.demoAuthor;
    m_demoDescription = state.demoDescription;
    m_currentMode = state.currentMode;
    m_shaderState = state.shaderState;
    m_activeSceneIndex = state.activeSceneIndex;
    m_editingSceneIndex = state.activeSceneIndex;

    // Reload files and clear runtime resources (they belong to old device)
    for (auto& scene : m_scenes) {
        scene.pipelineState = nullptr;
        scene.texture = nullptr;
        scene.srvHeap = nullptr;
        scene.isDirty = true; // Force compile on next use
        scene.postFxTextureA = nullptr;
        scene.postFxTextureB = nullptr;
        scene.postFxSrvHeap = nullptr;
        scene.postFxRtvHeap = nullptr;
        scene.postFxValid = false;
        for (auto& fx : scene.postFxChain) {
            fx.pipelineState = nullptr;
            fx.isDirty = true;
            fx.historyIndex = 0;
            fx.historyInitialized = false;
            fx.historyTextures.clear();
        }

        for (auto& binding : scene.bindings) {
            binding.textureResource = nullptr;

            if (binding.bindingType == BindingType::File && !binding.filePath.empty()) {
                 if (LoadTextureFromFile(binding.filePath, binding.textureResource)) {
                     binding.fileTextureValid = true;
                 }
            }
        }
    }

    // Restore text editor
    m_textEditor.SetText(m_shaderState.text);

    // Mark as dirty so user knows to recompile
    if (m_shaderState.status == CompileStatus::Success) {
        m_shaderState.status = CompileStatus::Dirty;
    }

    // Force layout rebuild
    m_layoutBuilt = false;
}

void ShaderLabIDE::RefreshMicroUbershaderConflictCache() {
    m_microUbershaderConflicts.clear();

    if (m_currentProjectPath.empty()) {
        m_microUbershaderConflictsDirty = false;
        return;
    }

    m_microUbershaderConflicts = BuildPipeline::AnalyzeMicroUbershaderConflicts(m_currentProjectPath);

    std::unordered_set<std::string> activeKeys;
    for (const auto& conflict : m_microUbershaderConflicts) {
        activeKeys.insert(conflict.signatureKey);

        std::vector<std::string> validEntrypoints;
        validEntrypoints.reserve(conflict.options.size());
        for (const auto& option : conflict.options) {
            validEntrypoints.push_back(option.moduleEntrypoint);
        }

        auto it = m_microUbershaderKeepEntrypointsBySignature.find(conflict.signatureKey);
        if (it == m_microUbershaderKeepEntrypointsBySignature.end()) {
            m_microUbershaderKeepEntrypointsBySignature[conflict.signatureKey] = validEntrypoints;
            continue;
        }

        std::vector<std::string> filtered;
        filtered.reserve(it->second.size());
        for (const std::string& kept : it->second) {
            if (std::find(validEntrypoints.begin(), validEntrypoints.end(), kept) != validEntrypoints.end()) {
                filtered.push_back(kept);
            }
        }
        if (filtered.empty()) {
            filtered = validEntrypoints;
        }
        it->second = std::move(filtered);
    }

    for (auto it = m_microUbershaderKeepEntrypointsBySignature.begin(); it != m_microUbershaderKeepEntrypointsBySignature.end();) {
        if (activeKeys.find(it->first) == activeKeys.end()) {
            it = m_microUbershaderKeepEntrypointsBySignature.erase(it);
        } else {
            ++it;
        }
    }

    m_microUbershaderConflictsDirty = false;
}

} // namespace ShaderLab
