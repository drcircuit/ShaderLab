#pragma once

#include "ShaderLab/Shader/ShaderCompiler.h"

#include <string>
#include <vector>

namespace ShaderLab {

struct CompilationTextureBinding {
    int slot = 0;
    std::string type = "Texture2D";
};

class ICompilationService {
public:
    virtual ~ICompilationService() = default;

    virtual ShaderCompileResult CompileFromSource(const std::string& source,
                                                  const std::string& entryPoint,
                                                  const std::string& target,
                                                  const std::wstring& sourceName,
                                                  ShaderCompileMode mode,
                                                  const std::vector<CompilationTextureBinding>& bindings) = 0;

    virtual ShaderCompileResult CompilePreviewShader(const std::string& shaderSource,
                                                     const std::vector<CompilationTextureBinding>& textureBindings,
                                                     bool flipFragCoord,
                                                     const std::string& shaderEntryPoint,
                                                     const std::wstring& sourceName,
                                                     ShaderCompileMode mode) = 0;
};

} // namespace ShaderLab
