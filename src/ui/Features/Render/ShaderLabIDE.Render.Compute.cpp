#include "ShaderLab/UI/ShaderLabIDE.h"

#include "ShaderLab/Core/CompilationService.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Dx12ResourceService.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include <d3dcompiler.h>

namespace ShaderLab {

namespace {

constexpr uint32_t kComputeHistorySlots = 8;
constexpr uint32_t kComputeDescriptorCount = 11;

struct ComputeDispatchParams {
    float param0;
    float param1;
    float param2;
    float param3;
    float time;
    float invWidth;
    float invHeight;
    uint32_t frame;
};

struct UiComputeSceneResources {
    ComPtr<ID3D12Resource> textureA;
    ComPtr<ID3D12Resource> textureB;
    uint32_t width = 0;
    uint32_t height = 0;
    ID3D12Device* ownerDevice = nullptr;
};

ComPtr<ID3D12RootSignature> g_uiComputeRootSignature;
ComPtr<ID3D12DescriptorHeap> g_uiComputeDescriptorHeap;
ComPtr<ID3D12Resource> g_uiComputeParamsBuffer;
uint8_t* g_uiComputeParamsMapped = nullptr;
ID3D12Device* g_uiComputeDevice = nullptr;
std::unordered_map<int, UiComputeSceneResources> g_uiComputeSceneResources;
std::unordered_map<Scene::ComputeEffect*, ID3D12Device*> g_uiComputePipelineDeviceMap;

void ResetUiComputeDeviceState() {
    if (g_uiComputeParamsBuffer && g_uiComputeParamsMapped) {
        g_uiComputeParamsBuffer->Unmap(0, nullptr);
    }

    g_uiComputeParamsMapped = nullptr;
    g_uiComputeParamsBuffer.Reset();
    g_uiComputeDescriptorHeap.Reset();
    g_uiComputeRootSignature.Reset();
    g_uiComputeSceneResources.clear();
    g_uiComputePipelineDeviceMap.clear();
    g_uiComputeDevice = nullptr;
}

UINT DescriptorStep(ID3D12Device* device) {
    return device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

uint32_t Align256(uint32_t value) {
    return (value + 255u) & ~255u;
}

bool EnsureUiComputeRootSignature(Device* deviceRef) {
    if (!deviceRef) return false;
    ID3D12Device* device = deviceRef->GetDevice();
    if (g_uiComputeDevice && g_uiComputeDevice != device) {
        ResetUiComputeDeviceState();
    }
    if (!g_uiComputeDevice) {
        g_uiComputeDevice = device;
    }
    if (g_uiComputeRootSignature) return true;

    D3D12_DESCRIPTOR_RANGE inputRange = {};
    inputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    inputRange.NumDescriptors = 1;
    inputRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE historyRange = {};
    historyRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    historyRange.NumDescriptors = kComputeHistorySlots;
    historyRange.BaseShaderRegister = 1;

    D3D12_DESCRIPTOR_RANGE outputRange = {};
    outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    outputRange.NumDescriptors = 1;
    outputRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE cbvRange = {};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER rootParams[4] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &inputRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &historyRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &outputRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &cbvRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParams);
    rootDesc.pParameters = rootParams;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           serialized.GetAddressOf(), errors.GetAddressOf()))) {
        return false;
    }

    return SUCCEEDED(deviceRef->GetDevice()->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(g_uiComputeRootSignature.ReleaseAndGetAddressOf())));
}

bool EnsureUiComputeDispatchResources(Device* deviceRef) {
    if (!deviceRef) return false;
    ID3D12Device* device = deviceRef->GetDevice();
    if (g_uiComputeDevice && g_uiComputeDevice != device) {
        ResetUiComputeDeviceState();
    }
    if (!g_uiComputeDevice) {
        g_uiComputeDevice = device;
    }

    if (!g_uiComputeDescriptorHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kComputeDescriptorCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_uiComputeDescriptorHeap.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    if (!g_uiComputeParamsBuffer) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = Align256(static_cast<uint32_t>(sizeof(ComputeDispatchParams)));
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(g_uiComputeParamsBuffer.ReleaseAndGetAddressOf())))) {
            return false;
        }

        if (FAILED(g_uiComputeParamsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&g_uiComputeParamsMapped)))) {
            g_uiComputeParamsBuffer.Reset();
            g_uiComputeParamsMapped = nullptr;
            return false;
        }
    }

    return true;
}

} // namespace

bool ShaderLabIDE::CompileComputePipeline(Scene::ComputeEffect& effect) {
    if (!m_compilationService || !m_deviceRef) return false;
    if (!EnsureUiComputeRootSignature(m_deviceRef)) return false;
    ID3D12Device* device = m_deviceRef->GetDevice();

    const std::string entryPoint = effect.entryPoint.empty() ? "main" : effect.entryPoint;
    const ShaderCompileResult compileResult = m_compilationService->CompileFromSource(
        effect.shaderCode,
        entryPoint,
        "cs_6_0",
        L"compute.hlsl",
        ShaderCompileMode::Build,
        {});

    if (!compileResult.success) {
        effect.pipelineState.Reset();
        effect.compiledShaderBytes = 0;
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = g_uiComputeRootSignature.Get();
    desc.CS = { compileResult.bytecode.data(), compileResult.bytecode.size() };

    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pipeline.GetAddressOf())))) {
        effect.pipelineState.Reset();
        effect.compiledShaderBytes = 0;
        return false;
    }

    effect.pipelineState = pipeline;
    effect.compiledShaderBytes = compileResult.bytecode.size();
    effect.isDirty = false;
    effect.lastCompiledCode = effect.shaderCode;
    g_uiComputePipelineDeviceMap[&effect] = device;
    return true;
}

void ShaderLabIDE::EnsureComputeHistory(Scene::ComputeEffect& effect, uint32_t width, uint32_t height) {
    if (!m_deviceRef || width == 0 || height == 0) return;

    const int historyCount = (std::max)(0, (std::min)(effect.historyCount, static_cast<int>(kComputeHistorySlots)));
    if (historyCount <= 0) {
        effect.historyTextures.clear();
        effect.historyInitialized = false;
        effect.historyIndex = 0;
        return;
    }

    bool needsCreate = static_cast<int>(effect.historyTextures.size()) != historyCount;
    if (!needsCreate && !effect.historyTextures.empty()) {
        auto desc = effect.historyTextures.front()->GetDesc();
        needsCreate = desc.Width != width || desc.Height != height;
    }

    if (!needsCreate) return;

    effect.historyTextures.clear();
    effect.historyTextures.resize(static_cast<size_t>(historyCount));
    effect.historyInitialized = false;
    effect.historyIndex = 0;

    Dx12ResourceService resourceService(m_deviceRef->GetDevice());
    TextureAllocationRequest req{};
    req.width = width;
    req.height = height;
    req.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    req.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    req.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    for (int i = 0; i < historyCount; ++i) {
        if (!resourceService.AllocateTexture2D(req, effect.historyTextures[static_cast<size_t>(i)])) {
            effect.historyTextures.clear();
            effect.historyInitialized = false;
            effect.historyIndex = 0;
            return;
        }
    }
}

ID3D12Resource* ShaderLabIDE::ApplyComputeEffectChain(ID3D12GraphicsCommandList* commandList,
                                                      int sceneIndex,
                                                      std::vector<Scene::ComputeEffect>& chain,
                                                      ID3D12Resource* inputTexture,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      double timeSeconds) {
    if (!commandList || !m_deviceRef || !inputTexture || chain.empty()) return inputTexture;
    if (!EnsureUiComputeRootSignature(m_deviceRef) || !EnsureUiComputeDispatchResources(m_deviceRef)) {
        return inputTexture;
    }

    bool anyEnabled = false;
    for (const auto& fx : chain) {
        if (fx.enabled) {
            anyEnabled = true;
            break;
        }
    }
    if (!anyEnabled) return inputTexture;

    ID3D12Device* device = m_deviceRef->GetDevice();
    auto& resources = g_uiComputeSceneResources[sceneIndex];
    const bool recreate = !resources.textureA || !resources.textureB || resources.width != width || resources.height != height || resources.ownerDevice != device;
    if (recreate) {
        resources = {};
        Dx12ResourceService resourceService(m_deviceRef->GetDevice());
        TextureAllocationRequest req{};
        req.width = width;
        req.height = height;
        req.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        req.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        req.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (!resourceService.AllocateTexture2D(req, resources.textureA) ||
            !resourceService.AllocateTexture2D(req, resources.textureB)) {
            resources = {};
            return inputTexture;
        }
        resources.width = width;
        resources.height = height;
        resources.ownerDevice = device;
    }

    const UINT step = DescriptorStep(device);
    const auto heapCpu = g_uiComputeDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const auto heapGpu = g_uiComputeDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    ID3D12Resource* currentInput = inputTexture;
    ID3D12Resource* outputA = resources.textureA.Get();
    ID3D12Resource* outputB = resources.textureB.Get();
    ID3D12Resource* currentOutput = outputA;

    for (auto& fx : chain) {
        if (!fx.enabled) continue;
        auto fxDeviceIt = g_uiComputePipelineDeviceMap.find(&fx);
        const bool pipelineDeviceMismatch = (fxDeviceIt == g_uiComputePipelineDeviceMap.end()) || (fxDeviceIt->second != device);
        if (pipelineDeviceMismatch) {
            fx.pipelineState.Reset();
            fx.isDirty = true;
            fx.historyIndex = 0;
            fx.historyInitialized = false;
            fx.historyTextures.clear();
        }
        if (fx.isDirty || !fx.pipelineState) {
            if (!CompileComputePipeline(fx)) {
                continue;
            }
        }

        EnsureComputeHistory(fx, width, height);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE inputCpu = heapCpu;
        device->CreateShaderResourceView(currentInput, &srvDesc, inputCpu);

        D3D12_CPU_DESCRIPTOR_HANDLE fallbackHistoryCpu = heapCpu;
        fallbackHistoryCpu.ptr += static_cast<SIZE_T>(step) * 1;
        device->CreateShaderResourceView(currentInput, &srvDesc, fallbackHistoryCpu);

        for (uint32_t i = 0; i < kComputeHistorySlots; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE histCpu = heapCpu;
            histCpu.ptr += static_cast<SIZE_T>(step) * (1 + i);

            ID3D12Resource* historyRes = nullptr;
            const int historyCount = static_cast<int>(fx.historyTextures.size());
            if (historyCount > 0) {
                int readIndex = fx.historyIndex - static_cast<int>(i);
                while (readIndex < 0) readIndex += historyCount;
                readIndex %= historyCount;
                historyRes = fx.historyTextures[static_cast<size_t>(readIndex)].Get();
            }
            if (!historyRes) {
                if (i > 0) {
                    device->CopyDescriptorsSimple(1, histCpu, fallbackHistoryCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    continue;
                }
                historyRes = currentInput;
            }
            device->CreateShaderResourceView(historyRes, &srvDesc, histCpu);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE outputCpu = heapCpu;
        outputCpu.ptr += static_cast<SIZE_T>(step) * 9;
        device->CreateUnorderedAccessView(currentOutput, nullptr, &uavDesc, outputCpu);

        if (!g_uiComputeParamsMapped || !g_uiComputeParamsBuffer) {
            return currentInput;
        }

        ComputeDispatchParams params{};
        params.param0 = fx.param0;
        params.param1 = fx.param1;
        params.param2 = fx.param2;
        params.param3 = fx.param3;
        params.time = static_cast<float>(timeSeconds);
        params.invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
        params.invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
        params.frame = static_cast<uint32_t>(m_transport.timeSeconds * 60.0);
        std::memcpy(g_uiComputeParamsMapped, &params, sizeof(params));

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_uiComputeParamsBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = Align256(static_cast<uint32_t>(sizeof(ComputeDispatchParams)));
        D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu = heapCpu;
        cbvCpu.ptr += static_cast<SIZE_T>(step) * 10;
        device->CreateConstantBufferView(&cbvDesc, cbvCpu);

        D3D12_RESOURCE_BARRIER beginBarriers[2] = {};
        beginBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        beginBarriers[0].Transition.pResource = currentInput;
        beginBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        beginBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        beginBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        beginBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        beginBarriers[1].Transition.pResource = currentOutput;
        beginBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        beginBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        beginBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(2, beginBarriers);

        ID3D12DescriptorHeap* heaps[] = { g_uiComputeDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetComputeRootSignature(g_uiComputeRootSignature.Get());
        commandList->SetPipelineState(fx.pipelineState.Get());

        D3D12_GPU_DESCRIPTOR_HANDLE inputGpu = heapGpu;
        D3D12_GPU_DESCRIPTOR_HANDLE historyGpu = heapGpu;
        historyGpu.ptr += static_cast<UINT64>(step) * 1;
        D3D12_GPU_DESCRIPTOR_HANDLE outputGpu = heapGpu;
        outputGpu.ptr += static_cast<UINT64>(step) * 9;
        D3D12_GPU_DESCRIPTOR_HANDLE cbvGpu = heapGpu;
        cbvGpu.ptr += static_cast<UINT64>(step) * 10;

        commandList->SetComputeRootDescriptorTable(0, inputGpu);
        commandList->SetComputeRootDescriptorTable(1, historyGpu);
        commandList->SetComputeRootDescriptorTable(2, outputGpu);
        commandList->SetComputeRootDescriptorTable(3, cbvGpu);

        const uint32_t tgx = (std::max)(1u, fx.threadGroupX);
        const uint32_t tgy = (std::max)(1u, fx.threadGroupY);
        const uint32_t tgz = (std::max)(1u, fx.threadGroupZ);
        const uint32_t groupsX = (width + tgx - 1u) / tgx;
        const uint32_t groupsY = (height + tgy - 1u) / tgy;
        const uint32_t groupsZ = (1u + tgz - 1u) / tgz;
        commandList->Dispatch(groupsX, groupsY, groupsZ);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = currentOutput;
        commandList->ResourceBarrier(1, &uavBarrier);

        D3D12_RESOURCE_BARRIER endBarriers[2] = {};
        endBarriers[0] = beginBarriers[0];
        endBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        endBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        endBarriers[1] = beginBarriers[1];
        endBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        endBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(2, endBarriers);

        if (!fx.historyTextures.empty()) {
            const int historyCount = static_cast<int>(fx.historyTextures.size());
            const int writeIndex = (fx.historyIndex + 1) % historyCount;

            D3D12_RESOURCE_BARRIER preCopy[2] = {};
            preCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopy[0].Transition.pResource = currentOutput;
            preCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            preCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            preCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            preCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopy[1].Transition.pResource = fx.historyTextures[static_cast<size_t>(writeIndex)].Get();
            preCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            preCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            preCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(2, preCopy);

            commandList->CopyResource(fx.historyTextures[static_cast<size_t>(writeIndex)].Get(), currentOutput);

            preCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            preCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            preCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            preCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            commandList->ResourceBarrier(2, preCopy);

            fx.historyIndex = writeIndex;
            fx.historyInitialized = true;
        }

        currentInput = currentOutput;
        currentOutput = (currentOutput == outputA) ? outputB : outputA;
    }

    return currentInput;
}

} // namespace ShaderLab
