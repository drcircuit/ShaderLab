#pragma once

#include <windows.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include "ShaderLab/Shader/ShaderBase.h"
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

enum class ShaderCompileMode {
    Live,    // Debug, no optimization, fast compile
    Build    // Release, O3, stripped for final demo
};

struct ShaderDiagnostic {
    std::string message;
    std::string filename;
    uint32_t line = 0;
    uint32_t column = 0;
    bool isError = false;
};

struct ShaderCompileResult {
    std::vector<uint8_t> bytecode;
    std::vector<ShaderDiagnostic> diagnostics;
    bool success = false;
};

class ShaderCompiler {
public:
    struct BindingDecl {
        int slot;
        std::string type;
    };

    static std::string WrapShaderSource(const std::string& fragmentSource, const std::vector<BindingDecl>& bindings) {
        std::vector<ShaderBase::TextureBindingDecl> sharedBindings;
        sharedBindings.reserve(bindings.size());
        for (const auto& binding : bindings) {
            sharedBindings.push_back({binding.slot, binding.type});
        }
        return ShaderBase::BuildFragmentShaderTemplate(fragmentSource, sharedBindings);
    }

    ShaderCompiler();
    ~ShaderCompiler();

    bool Initialize();
    void Shutdown();

    ShaderCompileResult CompileFromFile(const std::wstring& filepath,
                                        const std::string& entryPoint,
                                        const std::string& target,
                                        ShaderCompileMode mode = ShaderCompileMode::Live);

    ShaderCompileResult CompileFromSource(const std::string& source,
                                          const std::string& entryPoint,
                                          const std::string& target,
                                          const std::wstring& sourceName = L"shader.hlsl",
                                          ShaderCompileMode mode = ShaderCompileMode::Live);

private:
    using DxcCreateInstanceProc = HRESULT (WINAPI*)(REFCLSID, REFIID, LPVOID*);

    void ParseDiagnostics(IDxcBlobEncoding* errorBlob, 
                         std::vector<ShaderDiagnostic>& outDiagnostics);
    
    std::vector<std::wstring> GetCompileArguments(ShaderCompileMode mode);

    ComPtr<IDxcUtils> m_utils;
    ComPtr<IDxcCompiler3> m_compiler;
    ComPtr<IDxcIncludeHandler> m_includeHandler;
    HMODULE m_dxcModule = nullptr;
    DxcCreateInstanceProc m_dxcCreateInstance = nullptr;
};

} // namespace ShaderLab
