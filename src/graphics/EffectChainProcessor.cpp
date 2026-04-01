#include "ShaderLab/Graphics/EffectChainProcessor.h"
#include "ShaderLab/Core/ShaderLabData.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Shader/ShaderCompiler.h"
#include <d3dcompiler.h>
#include <algorithm>

namespace ShaderLab {

// ============================================================================
// CONSTANTS
// ============================================================================

// Compute effects use groups of 8x8 threads
static constexpr uint32_t COMPUTE_THREAD_GROUP_SIZE = 8;

// Descriptor heap layout for compute effects:
//   Slot 0: Input texture (SRV)
//   Slot 1-N: History textures (SRVs)
//   Slot N+1: Output texture (UAV)
//   Slot N+2: Parameters constant buffer (CBV)
static constexpr uint32_t COMPUTE_DESCRIPTORS_PER_EFFECT = 32;

// Constant buffer for compute effects
struct ComputeEffectParams {
    float param0;
    float param1;
    float param2;
    float param3;
    float time;
    float invWidth;   // 1.0 / width
    float invHeight;  // 1.0 / height
    uint32_t frame;
};

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

EffectChainProcessor::EffectChainProcessor()
    : m_device(nullptr)
{
}

EffectChainProcessor::~EffectChainProcessor()
{
    Shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool EffectChainProcessor::Initialize(Device* device)
{
    if (!device) return false;
    
    m_device = device;
    
    // Create compute root signature
    if (!CreateComputeRootSignature()) {
        // Logging removed
        return false;
    }
    
    // Create descriptor heap for compute effects
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = COMPUTE_DESCRIPTORS_PER_EFFECT * 4;  // Allow 4 concurrent compute effects
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    if (FAILED(m_device->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_computeDescriptorHeap.ReleaseAndGetAddressOf())))) {
        // Logging removed
        return false;
    }
    
    // Logging removed
    return true;
}

void EffectChainProcessor::Shutdown()
{
    m_computeRootSignature.Reset();
    m_computeParamsBuffer.Reset();
    m_computeDescriptorHeap.Reset();
    m_device = nullptr;
}

// ============================================================================
// COMPUTE ROOT SIGNATURE CREATION
// ============================================================================

bool EffectChainProcessor::CreateComputeRootSignature()
{
    if (!m_device) return false;
    
    // Root parameters:
    // 0: Input texture (SRV, t0)
    // 1: History textures (SRV array, t1-t8)
    // 2: Output texture (UAV, u0)
    // 3: Parameters (CBV, b0)
    
    D3D12_ROOT_PARAMETER rootParams[4] = {};
    
    // Param 0: Input texture
    D3D12_DESCRIPTOR_RANGE inputRange = {};
    inputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    inputRange.NumDescriptors = 1;
    inputRange.BaseShaderRegister = 0;
    inputRange.RegisterSpace = 0;
    inputRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &inputRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Param 1: History textures
    D3D12_DESCRIPTOR_RANGE historyRange = {};
    historyRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    historyRange.NumDescriptors = 8;
    historyRange.BaseShaderRegister = 1;
    historyRange.RegisterSpace = 0;
    historyRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &historyRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Param 2: Output UAV
    D3D12_DESCRIPTOR_RANGE outputRange = {};
    outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    outputRange.NumDescriptors = 1;
    outputRange.BaseShaderRegister = 0;
    outputRange.RegisterSpace = 0;
    outputRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &outputRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Param 3: Parameters constant buffer
    D3D12_DESCRIPTOR_RANGE cbvRange = {};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.RegisterSpace = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &cbvRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 4;
    rootDesc.pParameters = rootParams;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf()))) {
        // Serialization failed
        return false;
    }
    
    if (FAILED(m_device->GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                IID_PPV_ARGS(m_computeRootSignature.ReleaseAndGetAddressOf())))) {
        // Logging removed
        return false;
    }
    
    return true;
}

// ============================================================================
// PARAMETERS BUFFER MANAGEMENT
// ============================================================================

bool EffectChainProcessor::UpdateComputeParamsBuffer(uint32_t width, uint32_t height, double time)
{
    if (!m_device) return false;
    
    // Create or update parameters constant buffer if needed
    if (!m_computeParamsBuffer) {
        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = sizeof(ComputeEffectParams);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        
        if (FAILED(m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_computeParamsBuffer.ReleaseAndGetAddressOf())))) {
            // Logging removed
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// COMPILATION
// ============================================================================

bool EffectChainProcessor::CompilePostFXEffect(Scene::PostFXEffect& effect, std::vector<std::string>& outErrors)
{
    // For now, delegate to ShaderCompiler or mark as needing compilation
    // The actual compilation happens elsewhere in the system
    effect.isDirty = true;
    return true;
}

bool EffectChainProcessor::CompileComputeEffect(Scene::ComputeEffect& effect, std::vector<std::string>& outErrors)
{
    if (!m_device) return false;
    
    // Compile compute shader
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    
    std::string code = effect.shaderCode;
    if (code.empty() && !effect.shaderCodePath.empty()) {
        // Load from disk if needed
        // For now, assume code is provided
        return false;
    }
    
    // Compile with compute profile (cs_5_0)
    if (FAILED(D3DCompile(
        code.c_str(), code.size(),
        "compute_shader.hlsl",
        nullptr,
        nullptr,
        effect.entryPoint.c_str(),
        "cs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        bytecode.GetAddressOf(),
        errors.GetAddressOf()))) {
        
        if (errors) {
            std::string errorMsg = (char*)errors->GetBufferPointer();
            outErrors.push_back(errorMsg);
            // Logging removed
        }
        return false;
    }
    
    // Create compute pipeline state
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
    pipelineDesc.pRootSignature = m_computeRootSignature.Get();
    pipelineDesc.CS = { bytecode->GetBufferPointer(), bytecode->GetBufferSize() };
    pipelineDesc.NodeMask = 0;
    pipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    
    if (FAILED(m_device->GetDevice()->CreateComputePipelineState(&pipelineDesc, 
                IID_PPV_ARGS(effect.pipelineState.ReleaseAndGetAddressOf())))) {
        // Logging removed
        return false;
    }
    
    effect.isDirty = false;
    effect.compiledShaderBytes = bytecode->GetBufferSize();
    effect.lastCompiledCode = code;
    
    // Compilation successful
    return true;
}

// ============================================================================
// RESOURCE MANAGEMENT
// ============================================================================

void EffectChainProcessor::EnsurePostFxResources(Scene& scene, uint32_t width, uint32_t height)
{
    // Delegate to existing system - PostFX resources are managed by UISystem/DemoPlayer
    // This is a placeholder for potential future unification
}

void EffectChainProcessor::EnsurePostFxHistory(Scene::PostFXEffect& effect, uint32_t width, uint32_t height)
{
    // Delegate to existing system
}

void EffectChainProcessor::EnsureComputeEffectResources(Scene::ComputeEffect& effect, uint32_t width, uint32_t height)
{
    if (!m_device) return;
    
    // Create output texture if needed
    bool needsOutput = !effect.pipelineState || width == 0 || height == 0;
    
    // For now, output is created per-dispatch
    // In a full implementation, would cache based on resolution
}

void EffectChainProcessor::EnsureComputeEffectHistory(Scene::ComputeEffect& effect, uint32_t width, uint32_t height)
{
    if (!m_device || effect.historyCount <= 0) return;
    
    if (effect.historyTextures.size() == static_cast<size_t>(effect.historyCount)) {
        return;  // Already allocated
    }
    
    effect.historyTextures.clear();
    
    for (int i = 0; i < effect.historyCount; ++i) {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        
        ComPtr<ID3D12Resource> historyTexture;
        if (FAILED(m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(historyTexture.GetAddressOf())))) {
            // Logging removed
            return;
        }
        
        effect.historyTextures.push_back(historyTexture);
    }
    
    effect.historyInitialized = true;
    effect.historyIndex = 0;
}

// ============================================================================
// EFFECT DISPATCH
// ============================================================================

ID3D12Resource* EffectChainProcessor::ApplyPostFXEffect(
    ID3D12GraphicsCommandList* commandList,
    Scene& scene,
    Scene::PostFXEffect& effect,
    ID3D12Resource* inputTexture,
    uint32_t width,
    uint32_t height,
    double timeSeconds)
{
    // For now, this is a stub
    // In a full implementation, this would wrap the existing PostFX rendering logic
    // or delegate to a PreviewRenderer instance
    
    // Placeholder: just return input unchanged
    // The actual PostFX rendering is still handled by UISystem/DemoPlayer
    return inputTexture;
}

ID3D12Resource* EffectChainProcessor::ApplyComputeEffect(
    ID3D12GraphicsCommandList* commandList,
    Scene::ComputeEffect& effect,
    ID3D12Resource* inputTexture,
    ID3D12Resource*& outputTexture,
    uint32_t width,
    uint32_t height,
    double timeSeconds)
{
    if (!m_device || !commandList || !inputTexture || !m_computeRootSignature || !effect.pipelineState) {
        return inputTexture;
    }
    
    // Ensure history buffers exist
    EnsureComputeEffectHistory(effect, width, height);
    
    // Create output texture (temporary)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    ComPtr<ID3D12Resource> localOutput;
    if (FAILED(m_device->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(localOutput.GetAddressOf())))) {
        // Logging removed
        return inputTexture;
    }
    
    // Transition input to SRV
    D3D12_RESOURCE_BARRIER inputBarrier = {};
    inputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    inputBarrier.Transition.pResource = inputTexture;
    inputBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    inputBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    inputBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    // Barrier is a no-op if already in correct state
    
    // Set pipeline state
    commandList->SetPipelineState(effect.pipelineState.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    
    // Set descriptors
    if (m_computeDescriptorHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_computeDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
    }
    
    // Dispatch compute shader
    // Thread group X,Y are dimensions; Z is typically 1
    uint32_t groupsX = (width + effect.threadGroupX - 1) / effect.threadGroupX;
    uint32_t groupsY = (height + effect.threadGroupY - 1) / effect.threadGroupY;
    uint32_t groupsZ = (1 + effect.threadGroupZ - 1) / effect.threadGroupZ;
    
    commandList->Dispatch(groupsX, groupsY, groupsZ);
    
    // Memory barrier for UAV write
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = localOutput.Get();
    commandList->ResourceBarrier(1, &uavBarrier);
    
    // Copy output to next history if needed
    if (effect.historyInitialized && !effect.historyTextures.empty()) {
        int writeIndex = (effect.historyIndex + 1) % effect.historyCount;
        
        D3D12_RESOURCE_BARRIER historyBarrier = {};
        historyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        historyBarrier.Transition.pResource = effect.historyTextures[writeIndex].Get();
        historyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        historyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        historyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &historyBarrier);
        
        commandList->CopyResource(effect.historyTextures[writeIndex].Get(), localOutput.Get());
        
        std::swap(historyBarrier.Transition.StateBefore, historyBarrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &historyBarrier);
        
        effect.historyIndex = writeIndex;
    }
    
    outputTexture = localOutput.Get();
    return localOutput.Get();
}

// ============================================================================
// MAIN EFFECT CHAIN APPLICATION
// ============================================================================

ID3D12Resource* EffectChainProcessor::ApplyEffectChain(
    ID3D12GraphicsCommandList* commandList,
    Scene& scene,
    ID3D12Resource* inputTexture,
    uint32_t width,
    uint32_t height,
    double timeSeconds)
{
    if (!commandList || !inputTexture) {
        return inputTexture;
    }
    
    // Check if any effects are enabled
    bool anyPostFxEnabled = std::any_of(scene.postFxChain.begin(), scene.postFxChain.end(),
        [](const Scene::PostFXEffect& fx) { return fx.enabled; });
    
    bool anyComputeEnabled = std::any_of(scene.computeEffectChain.begin(), scene.computeEffectChain.end(),
        [](const Scene::ComputeEffect& fx) { return fx.enabled; });
    
    if (!anyPostFxEnabled && !anyComputeEnabled) {
        return inputTexture;
    }
    
    ID3D12Resource* currentInput = inputTexture;
    ID3D12Resource* currentOutput = inputTexture;
    
    // Process PostFX effects
    for (auto& fx : scene.postFxChain) {
        if (!fx.enabled) continue;
        if (!fx.pipelineState) continue;
        
        // Delegate to PostFX handler
        currentOutput = ApplyPostFXEffect(commandList, scene, fx, currentInput, width, height, timeSeconds);
        currentInput = currentOutput;
    }
    
    // Process Compute effects
    for (auto& fx : scene.computeEffectChain) {
        if (!fx.enabled) continue;
        if (!fx.pipelineState) {
            // Try to compile if not already compiled
            std::vector<std::string> errors;
            if (!CompileComputeEffect(fx, errors)) {
                // Compilation failed, skip this effect
                continue;
            }
        }
        
        currentOutput = ApplyComputeEffect(commandList, fx, currentInput, currentOutput, width, height, timeSeconds);
        currentInput = currentOutput;
    }
    
    return currentInput;
}

} // namespace ShaderLab
