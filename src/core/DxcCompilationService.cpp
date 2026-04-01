#include "ShaderLab/Core/DxcCompilationService.h"

#include "ShaderLab/Shader/ShaderCompiler.h"
#include "ShaderLab/Shader/ShaderBase.h"

namespace ShaderLab {

DxcCompilationService::DxcCompilationService()
    : m_compiler(std::make_unique<ShaderCompiler>()) {
    m_initialized = m_compiler && m_compiler->Initialize();
}

DxcCompilationService::~DxcCompilationService() {
    if (m_compiler && m_initialized) {
        m_compiler->Shutdown();
        m_initialized = false;
    }
}

ShaderCompileResult DxcCompilationService::CompileFromSource(const std::string& source,
                                                             const std::string& entryPoint,
                                                             const std::string& target,
                                                             const std::wstring& sourceName,
                                                             ShaderCompileMode mode,
                                                             const std::vector<CompilationTextureBinding>& bindings) {
    if (!m_initialized || !m_compiler) {
        ShaderCompileResult failed;
        failed.success = false;
        return failed;
    }

    std::vector<ShaderBase::TextureBindingDecl> shaderBindings;
    shaderBindings.reserve(bindings.size());
    for (const auto& binding : bindings) {
        shaderBindings.push_back({binding.slot, binding.type});
    }

    const std::string wrapped = ShaderBase::BuildFragmentShaderTemplate(source, shaderBindings);
    return m_compiler->CompileFromSource(wrapped, entryPoint, target, sourceName, mode);
}

ShaderCompileResult DxcCompilationService::CompilePreviewShader(const std::string& shaderSource,
                                                                const std::vector<CompilationTextureBinding>& textureBindings,
                                                                bool flipFragCoord,
                                                                const std::string& shaderEntryPoint,
                                                                const std::wstring& sourceName,
                                                                ShaderCompileMode mode) {
    if (!m_initialized || !m_compiler) {
        ShaderCompileResult failed;
        failed.success = false;
        return failed;
    }

    std::vector<ShaderBase::TextureBindingDecl> bindings;
    bindings.reserve(textureBindings.size());
    for (const auto& binding : textureBindings) {
        bindings.push_back({binding.slot, binding.type});
    }

    const std::string wrappedSource = ShaderBase::BuildPreviewPixelShaderTemplate(
        shaderSource, bindings, flipFragCoord, shaderEntryPoint);

    return m_compiler->CompileFromSource(wrappedSource, "PSMain", "ps_6_0", sourceName, mode);
}

} // namespace ShaderLab
