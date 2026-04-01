#pragma once

#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <wrl/client.h>

namespace ShaderLab {

using Microsoft::WRL::ComPtr;

class Device;

class CommandQueue {
public:
    CommandQueue();
    ~CommandQueue();

    bool Initialize(Device* device, D3D12_COMMAND_LIST_TYPE type);
    void Shutdown();

    ID3D12CommandQueue* GetQueue() const { return m_queue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }

    void ResetCommandList();
    void ExecuteCommandList();
    void WaitForGPU();
    uint64_t SignalFence();

private:
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;

    uint64_t m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;
};

} // namespace ShaderLab
