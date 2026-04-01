#include "ShaderLab/Graphics/CommandQueue.h"
#include "ShaderLab/Graphics/Device.h"

namespace ShaderLab {

CommandQueue::CommandQueue() = default;

CommandQueue::~CommandQueue() {
    Shutdown();
}

bool CommandQueue::Initialize(Device* device, D3D12_COMMAND_LIST_TYPE type) {
    if (!device || !device->IsValid()) {
        return false;
    }

    ID3D12Device* d3dDevice = device->GetDevice();

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = type;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    
    HRESULT hr = d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
    if (FAILED(hr)) {
        return false;
    }

    // Create command allocator
    hr = d3dDevice->CreateCommandAllocator(type, IID_PPV_ARGS(&m_commandAllocator));
    if (FAILED(hr)) {
        return false;
    }

    // Create command list
    hr = d3dDevice->CreateCommandList(0, type, m_commandAllocator.Get(), 
                                      nullptr, IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr)) {
        return false;
    }

    // Command lists are created in recording state, close it
    m_commandList->Close();

    // Create fence
    hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        return false;
    }

    return true;
}

void CommandQueue::Shutdown() {
    WaitForGPU();

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_fence.Reset();
    m_commandList.Reset();
    m_commandAllocator.Reset();
    m_queue.Reset();
}

void CommandQueue::ResetCommandList() {
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);
}

void CommandQueue::ExecuteCommandList() {
    m_commandList->Close();
    
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_queue->ExecuteCommandLists(1, commandLists);
}

void CommandQueue::WaitForGPU() {
    if (!m_queue || !m_fence) {
        return;
    }

    const uint64_t fenceValue = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), fenceValue);

    if (m_fence->GetCompletedValue() < fenceValue) {
        m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

uint64_t CommandQueue::SignalFence() {
    const uint64_t fenceValue = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), fenceValue);
    return fenceValue;
}

} // namespace ShaderLab
