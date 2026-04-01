#pragma once

#include "ShaderLab/Core/CompilationService.h"

#include <memory>

namespace ShaderLab {

class ShaderCompiler;

class DxcCompilationService final : public ICompilationService {
public:
    DxcCompilationService();
    ~DxcCompilationService() override;

    ShaderCompileResult CompileFromSource(const std::string& source,
                                          const std::string& entryPoint,
                                          const std::string& target,
                                          const std::wstring& sourceName,
                                          ShaderCompileMode mode,
                                          const std::vector<CompilationTextureBinding>& bindings) override;

    ShaderCompileResult CompilePreviewShader(const std::string& shaderSource,
                                             const std::vector<CompilationTextureBinding>& textureBindings,
                                             bool flipFragCoord,
                                             const std::string& shaderEntryPoint,
                                             const std::wstring& sourceName,
                                             ShaderCompileMode mode) override;

private:
    std::unique_ptr<ShaderCompiler> m_compiler;
    bool m_initialized = false;
};

} // namespace ShaderLab
