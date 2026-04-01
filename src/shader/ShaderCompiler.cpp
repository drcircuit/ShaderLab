#include "ShaderLab/Shader/ShaderCompiler.h"
#include <exception>
#include <sstream>

namespace ShaderLab {

namespace {
std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        utf8.data(),
        required,
        nullptr,
        nullptr);
    if (written <= 0) {
        return {};
    }
    return utf8;
}
}

ShaderCompiler::ShaderCompiler() = default;
ShaderCompiler::~ShaderCompiler() = default;

bool ShaderCompiler::Initialize() {
#if !defined(SHADERLAB_ENABLE_DXC)
#define SHADERLAB_ENABLE_DXC 1
#endif
#if SHADERLAB_ENABLE_DXC
    Shutdown();

    m_dxcModule = LoadLibraryA("dxcompiler.dll");
    if (!m_dxcModule) {
        return false;
    }

    m_dxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(m_dxcModule, "DxcCreateInstance"));
    if (!m_dxcCreateInstance) {
        FreeLibrary(m_dxcModule);
        m_dxcModule = nullptr;
        return false;
    }

    // Create DXC utils
    HRESULT hr = m_dxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils));
    if (FAILED(hr)) {
        return false;
    }

    // Create DXC compiler
    hr = m_dxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler));
    if (FAILED(hr)) {
        return false;
    }

    // Create include handler
    hr = m_utils->CreateDefaultIncludeHandler(&m_includeHandler);
    if (FAILED(hr)) {
        return false;
    }

    return true;
#else
    return false;
#endif
}

void ShaderCompiler::Shutdown() {
    m_includeHandler.Reset();
    m_compiler.Reset();
    m_utils.Reset();
    m_dxcCreateInstance = nullptr;
    if (m_dxcModule) {
        FreeLibrary(m_dxcModule);
        m_dxcModule = nullptr;
    }
}

ShaderCompileResult ShaderCompiler::CompileFromFile(const std::wstring& filepath,
                                                     const std::string& entryPoint,
                                                     const std::string& target,
                                                     ShaderCompileMode mode) {
    ShaderCompileResult result;
#if !defined(SHADERLAB_ENABLE_DXC)
#define SHADERLAB_ENABLE_DXC 1
#endif
#if !SHADERLAB_ENABLE_DXC
    ShaderDiagnostic diag;
    diag.message = "DXC is disabled in this build";
    diag.filename = WideToUtf8(filepath);
    diag.isError = true;
    result.diagnostics.push_back(diag);
    return result;
#else
    if (!m_utils || !m_compiler || !m_includeHandler) {
        ShaderDiagnostic diag;
        diag.message = "Shader compiler is not initialized";
        diag.filename = WideToUtf8(filepath);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }
    try {

    // Load source file
    ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->LoadFile(filepath.c_str(), nullptr, &sourceBlob);
    if (FAILED(hr)) {
        ShaderDiagnostic diag;
        diag.message = "Failed to load shader file";
        diag.filename = WideToUtf8(filepath);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Convert entry point and target to wide strings
    std::wstring wEntryPoint(entryPoint.begin(), entryPoint.end());
    std::wstring wTarget(target.begin(), target.end());

    // Prepare arguments
    std::vector<std::wstring> args = GetCompileArguments(mode);
    
    // Replace entry point and target in args
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == L"-E" && i + 1 < args.size()) {
            args[i + 1] = wEntryPoint;
        }
        if (args[i] == L"-T" && i + 1 < args.size()) {
            args[i + 1] = wTarget;
        }
    }
    
    std::vector<LPCWSTR> argPtrs;
    for (const auto& arg : args) {
        argPtrs.push_back(arg.c_str());
    }

    // Compile
    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP;

    ComPtr<IDxcResult> compileResult;
    hr = m_compiler->Compile(
        &sourceBuffer,
        argPtrs.data(),
        static_cast<UINT32>(argPtrs.size()),
        m_includeHandler.Get(),
        IID_PPV_ARGS(&compileResult)
    );

    if (FAILED(hr)) {
        ShaderDiagnostic diag;
        diag.message = "Shader compilation failed";
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Check compilation status
    HRESULT compileStatus;
    compileResult->GetStatus(&compileStatus);

    // Get error messages if any
    ComPtr<IDxcBlobUtf8> errors;
    compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0) {
        ParseDiagnostics(errors.Get(), result.diagnostics);
    }

    if (SUCCEEDED(compileStatus)) {
        // Get compiled bytecode
        ComPtr<IDxcBlob> bytecode;
        hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode), nullptr);
        if (SUCCEEDED(hr) && bytecode) {
            const uint8_t* data = static_cast<const uint8_t*>(bytecode->GetBufferPointer());
            size_t size = bytecode->GetBufferSize();
            result.bytecode.assign(data, data + size);
            result.success = true;
        }
    }

    return result;
    } catch (const std::exception& ex) {
        ShaderDiagnostic diag;
        diag.message = std::string("Shader compilation exception: ") + ex.what();
        diag.filename = WideToUtf8(filepath);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    } catch (...) {
        ShaderDiagnostic diag;
        diag.message = "Shader compilation exception: unknown";
        diag.filename = WideToUtf8(filepath);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }
#endif
}

ShaderCompileResult ShaderCompiler::CompileFromSource(const std::string& source,
                                                       const std::string& entryPoint,
                                                       const std::string& target,
                                                       const std::wstring& sourceName,
                                                       ShaderCompileMode mode) {
    ShaderCompileResult result;
#if !defined(SHADERLAB_ENABLE_DXC)
#define SHADERLAB_ENABLE_DXC 1
#endif
#if !SHADERLAB_ENABLE_DXC
    ShaderDiagnostic diag;
    diag.message = "DXC is disabled in this build";
    diag.filename = WideToUtf8(sourceName);
    diag.isError = true;
    result.diagnostics.push_back(diag);
    return result;
#else
    if (!m_utils || !m_compiler || !m_includeHandler) {
        ShaderDiagnostic diag;
        diag.message = "Shader compiler is not initialized";
        diag.filename = WideToUtf8(sourceName);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }
    try {

    // Create source blob
    ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->CreateBlob(source.c_str(), static_cast<UINT32>(source.size()),
                                     DXC_CP_ACP, &sourceBlob);
    if (FAILED(hr)) {
        ShaderDiagnostic diag;
        diag.message = "Failed to create source blob";
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Convert entry point and target to wide strings
    std::wstring wEntryPoint(entryPoint.begin(), entryPoint.end());
    std::wstring wTarget(target.begin(), target.end());

    // Prepare arguments
    std::vector<std::wstring> args = GetCompileArguments(mode);
    
    // Replace entry point and target in args
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == L"-E" && i + 1 < args.size()) {
            args[i + 1] = wEntryPoint;
        }
        if (args[i] == L"-T" && i + 1 < args.size()) {
            args[i + 1] = wTarget;
        }
    }
    
    std::vector<LPCWSTR> argPtrs;
    for (const auto& arg : args) {
        argPtrs.push_back(arg.c_str());
    }

    // Compile
    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP;

    ComPtr<IDxcResult> compileResult;
    hr = m_compiler->Compile(
        &sourceBuffer,
        argPtrs.data(),
        static_cast<UINT32>(argPtrs.size()),
        m_includeHandler.Get(),
        IID_PPV_ARGS(&compileResult)
    );

    if (FAILED(hr)) {
        ShaderDiagnostic diag;
        diag.message = "Shader compilation failed";
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Check compilation status
    HRESULT compileStatus;
    compileResult->GetStatus(&compileStatus);

    // Get error messages
    ComPtr<IDxcBlobUtf8> errors;
    compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0) {
        ParseDiagnostics(errors.Get(), result.diagnostics);
    }

    if (SUCCEEDED(compileStatus)) {
        // Get compiled bytecode
        ComPtr<IDxcBlob> bytecode;
        hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode), nullptr);
        if (SUCCEEDED(hr) && bytecode) {
            const uint8_t* data = static_cast<const uint8_t*>(bytecode->GetBufferPointer());
            size_t size = bytecode->GetBufferSize();
            result.bytecode.assign(data, data + size);
            result.success = true;
        }
    }

    return result;
    } catch (const std::exception& ex) {
        ShaderDiagnostic diag;
        diag.message = std::string("Shader compilation exception: ") + ex.what();
        diag.filename = WideToUtf8(sourceName);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    } catch (...) {
        ShaderDiagnostic diag;
        diag.message = "Shader compilation exception: unknown";
        diag.filename = WideToUtf8(sourceName);
        diag.isError = true;
        result.diagnostics.push_back(diag);
        return result;
    }
#endif
}

void ShaderCompiler::ParseDiagnostics(IDxcBlobEncoding* errorBlob,
                                       std::vector<ShaderDiagnostic>& outDiagnostics) {
    if (!errorBlob || errorBlob->GetBufferSize() == 0) {
        return;
    }

    std::string errorString(static_cast<const char*>(errorBlob->GetBufferPointer()),
                           errorBlob->GetBufferSize());

    // Simple parser for common DXC output format
    // Format: filename(line,col): error/warning: message
    std::istringstream stream(errorString);
    std::string line;

    while (std::getline(stream, line)) {
        ShaderDiagnostic diag;
        diag.message = line;
        
        // Try to parse structured error
        size_t parenPos = line.find('(');
        if (parenPos != std::string::npos) {
            diag.filename = line.substr(0, parenPos);
            
            size_t commaPos = line.find(',', parenPos);
            size_t closeParenPos = line.find(')', parenPos);
            
            if (commaPos != std::string::npos && closeParenPos != std::string::npos) {
                try {
                    std::string lineStr = line.substr(parenPos + 1, commaPos - parenPos - 1);
                    std::string colStr = line.substr(commaPos + 1, closeParenPos - commaPos - 1);
                    diag.line = std::stoi(lineStr);
                    diag.column = std::stoi(colStr);
                } catch (...) {
                    // Parsing failed, keep as unstructured message
                }
            }
        }

        // DXC can also report: filename:line:column: error: message
        if (diag.line == 0) {
            size_t firstColon = line.find(':');
            if (firstColon != std::string::npos && firstColon > 1) {
                size_t secondColon = line.find(':', firstColon + 1);
                if (secondColon != std::string::npos) {
                    size_t thirdColon = line.find(':', secondColon + 1);
                    if (thirdColon != std::string::npos) {
                        const std::string lineStr = line.substr(firstColon + 1, secondColon - firstColon - 1);
                        const std::string colStr = line.substr(secondColon + 1, thirdColon - secondColon - 1);
                        bool validLine = !lineStr.empty();
                        bool validCol = !colStr.empty();
                        for (char c : lineStr) {
                            if (c < '0' || c > '9') {
                                validLine = false;
                                break;
                            }
                        }
                        for (char c : colStr) {
                            if (c < '0' || c > '9') {
                                validCol = false;
                                break;
                            }
                        }

                        if (validLine && validCol) {
                            try {
                                diag.filename = line.substr(0, firstColon);
                                diag.line = static_cast<uint32_t>(std::stoi(lineStr));
                                diag.column = static_cast<uint32_t>(std::stoi(colStr));
                            } catch (...) {
                                // Keep unstructured if conversion fails
                            }
                        }
                    }
                }
            }
        }
        
        // Check if error or warning
        diag.isError = (line.find("error") != std::string::npos);
        
        outDiagnostics.push_back(diag);
    }
}

std::vector<std::wstring> ShaderCompiler::GetCompileArguments(ShaderCompileMode mode) {
    std::vector<std::wstring> args;

    // Entry point and target are handled separately in Compile()
    args.push_back(L"-E");
    args.push_back(L"main");  // Will be replaced by actual entry point
    args.push_back(L"-T");
    args.push_back(L"ps_6_0");  // Will be replaced by actual target

    // HLSL version
    args.push_back(L"-HV");
    args.push_back(L"2021");

    if (mode == ShaderCompileMode::Live) {
        // Debug mode: fast compile, no optimization
        args.push_back(L"-Od");   // Disable optimizations
        args.push_back(L"-Zi");   // Debug info
        args.push_back(L"-Qembed_debug");  // Embed debug info
    } else {
        // Build mode: full optimization
        args.push_back(L"-O3");   // Maximum optimization
        args.push_back(L"-Qstrip_debug");  // Strip debug info
        args.push_back(L"-Qstrip_reflect");  // Strip reflection data
        args.push_back(L"-Qstrip_priv");  // Strip private data
    }

    return args;
}

} // namespace ShaderLab
