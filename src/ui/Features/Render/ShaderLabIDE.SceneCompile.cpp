#include "ShaderLab/UI/ShaderLabIDE.h"

#include <string>
#include <vector>

#include "ShaderLab/Audio/AudioSystem.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"
#include "ShaderLab/Core/CompilationService.h"

namespace ShaderLab {

bool ShaderLabIDE::CompileScene(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= (int)m_scenes.size()) return false;
    auto& scene = m_scenes[sceneIndex];

    // Only compile if we have a renderer
    if (!m_previewRenderer || !m_compilationService) return false;

    // Collect texture declarations
    std::vector<CompilationTextureBinding> bindings;
    for(const auto& b : scene.bindings) {
        if (!b.enabled) continue;

        CompilationTextureBinding binding;
        binding.slot = b.channelIndex;

        if (b.type == TextureType::TextureCube) binding.type = "TextureCube";
        else if (b.type == TextureType::Texture3D) binding.type = "Texture3D";
        else binding.type = "Texture2D";

        bindings.push_back(binding);
    }

    // Compile
    std::vector<ShaderDiagnostic> compileDiagnostics;
    std::vector<std::string> errors;
    const ShaderCompileResult compileResult = m_compilationService->CompilePreviewShader(
        scene.shaderCode,
        bindings,
        false,
        "main",
        L"scene.hlsl",
        ShaderCompileMode::Live);

    for (const auto& diagnostic : compileResult.diagnostics) {
        compileDiagnostics.push_back(diagnostic);
        errors.push_back(diagnostic.message);
    }

    ComPtr<ID3D12PipelineState> pso;
    if (compileResult.success) {
        pso = m_previewRenderer->CreatePSOFromBytecode(compileResult.bytecode);
        if (!pso) {
            errors.push_back("Failed to create graphics pipeline state from compiled scene shader.");
        }
    }

    bool success = (pso != nullptr);

    // Update Scene state
    if (success) {
        scene.pipelineState = pso;
        scene.compiledShaderBytes = compileResult.bytecode.size();
        scene.isDirty = false;
        m_playbackBlockedByCompileError = false;
    } else {
        scene.compiledShaderBytes = 0;
        m_playbackBlockedByCompileError = true;
        if (m_transport.state == TransportState::Playing) {
            m_transport.state = TransportState::Stopped;
            if (m_audioSystem) {
                m_audioSystem->Stop();
            }
            m_activeMusicIndex = -1;
        }
    }

    // If this is the active scene, update the editor UI state too
    if (sceneIndex == m_activeSceneIndex) {
        m_shaderState.status = success ? CompileStatus::Success : CompileStatus::Error;
        m_shaderState.diagnostics.clear();
        for (const auto& diag : compileDiagnostics) {
            Diagnostic d;
            d.line = static_cast<int>(diag.line);
            d.column = static_cast<int>(diag.column);
            d.message = diag.message;
            m_shaderState.diagnostics.push_back(d);
        }

        if (m_shaderState.diagnostics.empty()) {
            for (const auto& msg : errors) {
                Diagnostic d;
                d.line = 0;
                d.column = 0;
                d.message = msg;
                m_shaderState.diagnostics.push_back(d);
            }
        }

        if (success) {
            m_shaderState.lastCompiledText = scene.shaderCode;
        }
    }

    return success;
}

} // namespace ShaderLab
