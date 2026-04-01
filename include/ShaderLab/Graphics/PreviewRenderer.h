#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

class Device;
class ShaderCompiler;
struct ShaderCompileResult;

class PreviewRenderer {
public:
    PreviewRenderer();
    ~PreviewRenderer();

    bool Initialize(Device* device, ShaderCompiler* compiler, DXGI_FORMAT renderTargetFormat, const std::vector<uint8_t>* precompiledVertexShader = nullptr);
    void Shutdown();


    struct TextureDecl {
        int slot;
        std::string type; // "Texture2D", "TextureCube", "Texture3D"
    };

    // Compile and return pipeline state.
    ComPtr<ID3D12PipelineState> CompileShader(const std::string& shaderSource, const std::vector<TextureDecl>& textureDecls, std::vector<std::string>& outErrors);
    ComPtr<ID3D12PipelineState> CompileShader(const std::string& shaderSource, const std::vector<TextureDecl>& textureDecls, std::vector<std::string>& outErrors, bool flipFragCoord);
    ComPtr<ID3D12PipelineState> CompileShader(const std::string& shaderSource,
                                              const std::vector<TextureDecl>& textureDecls,
                                              std::vector<std::string>& outErrors,
                                              bool flipFragCoord,
                                              const std::string& shaderEntryPoint);
    
    // Create pipeline state from pre-compiled bytecode
    ComPtr<ID3D12PipelineState> CreatePSOFromBytecode(const std::vector<uint8_t>& psBytecode);

    // Render to the specified render target using specific PSO
    void Render(ID3D12GraphicsCommandList* commandList,
                ID3D12PipelineState* pipelineState,
                ID3D12Resource* renderTarget,
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                D3D12_GPU_DESCRIPTOR_HANDLE srvHandle, /* Handle to table with 4 textures */
                uint32_t width, uint32_t height,
                float timeSeconds,
                float iBeat = 0.0f,
                float iBar = 0.0f,
                float fBarBeat16 = 0.0f,
                float fBeat = 0.0f,
                float fBarBeat = 0.0f);

    bool IsValid(ID3D12PipelineState* pso) const { return pso != nullptr; }

    float GetLastGPUTimeMs() const { return m_lastGPUTimeMs; }
    size_t GetLastCompiledPixelShaderSize() const { return m_lastCompiledPixelShaderSize; }

private:
    bool CreateRootSignature();
    bool CreatePipelineState(const std::vector<uint8_t>& pixelShaderBytecode, ComPtr<ID3D12PipelineState>& outPso);
    void CreateFullscreenQuadVertices();

    Device* m_device = nullptr;
    ShaderCompiler* m_compiler = nullptr;
    DXGI_FORMAT m_renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    
    // Fullscreen quad
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    
    std::vector<uint8_t> m_vertexShaderBytecode;

    // Timestamp queries
    ComPtr<ID3D12QueryHeap> m_queryHeap;
    ComPtr<ID3D12Resource> m_queryResultBuffer;
    uint64_t m_gpuFrequency = 0;
    float m_lastGPUTimeMs = 0.0f;
    size_t m_lastCompiledPixelShaderSize = 0;
};

} // namespace ShaderLab
