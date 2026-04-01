; ============================================================================
;  dx12.inc — DX12/DXGI COM vtable offsets for x86 (Win32) calling convention
;  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
;
;  COM vtable layout: Each entry is a 4-byte pointer (x86).
;  Offset = method_index * 4.
;  IUnknown: QueryInterface(0), AddRef(1), Release(2)  → first user method at index 3.
;
;  x86 COM ABI (__stdcall):
;    - All parameters pushed on stack right-to-left
;    - `this` is first parameter (pushed last)
;    - Callee cleans the stack
;    - Return value in eax (or eax:edx for 64-bit)
;    - Struct returns: hidden out-pointer is second parameter (after this)
;
;  Usage:  push arg1
;          push [pObj]             ; this
;          mov  eax, [pObj]
;          mov  eax, [eax]         ; vtable
;          call [eax + OFFSET]
; ============================================================================

; ── IUnknown ────────────────────────────────────────────────────────────────
%define IUnknown_QueryInterface             (0*4)
%define IUnknown_AddRef                     (1*4)
%define IUnknown_Release                    (2*4)

; ── IDXGIFactory1/IDXGIFactory2 ────────────────────────────────────────────
;  Inherits: IUnknown (3 methods)
;  IDXGIObject: SetPrivateData(3), SetPrivateDataInterface(4),
;               GetPrivateData(5), GetParent(6)
;  IDXGIFactory: EnumAdapters(7), MakeWindowAssociation(8),
;                GetWindowAssociation(9), CreateSwapChain(10),
;                CreateSoftwareAdapter(11)
;  IDXGIFactory1: EnumAdapters1(12), IsCurrent(13)
;  IDXGIFactory2: IsWindowedStereoEnabled(14), CreateSwapChainForHwnd(15)
%define IDXGIFactory_CreateSwapChain        (10*4)
%define IDXGIFactory1_EnumAdapters1         (12*4)
%define IDXGIFactory2_CreateSwapChainForHwnd (15*4)

; ── IDXGISwapChain / IDXGISwapChain3 ──────────────────────────────────────
;  Inherits: IDXGIObject (7 methods)
;  IDXGIDeviceSubObject: GetDevice(7)
;  IDXGISwapChain: Present(8), GetBuffer(9), SetFullscreenState(10),
;                  GetFullscreenState(11), GetDesc(12), ResizeBuffers(13),
;                  ResizeTarget(14), GetContainingOutput(15),
;                  GetFrameStatistics(16), GetLastPresentCount(17)
;  IDXGISwapChain1 extends at index 18
;  IDXGISwapChain2 extends at index 29 (7 methods: SetSourceSize..GetMatrixTransform)
;  IDXGISwapChain3: GetCurrentBackBufferIndex(36), CheckColorSpaceSupport(37),
;                   SetColorSpace1(38)
%define IDXGISwapChain_Present              (8*4)
%define IDXGISwapChain_GetBuffer            (9*4)
%define IDXGISwapChain_ResizeBuffers        (13*4)
%define IDXGISwapChain3_GetCurrentBackBufferIndex (36*4)

; ── ID3D12Device ───────────────────────────────────────────────────────────
;  Inherits: IUnknown (3 methods)
;  ID3D12Object: GetPrivateData(3), SetPrivateData(4),
;                SetPrivateDataInterface(5), SetName(6)
;  ID3D12Device methods starting at index 7:
;    GetNodeCount(7), CreateCommandQueue(8), CreateCommandAllocator(9),
;    CreateGraphicsPipelineState(10), CreateComputePipelineState(11),
;    CreateCommandList(12), CheckFeatureSupport(13),
;    CreateDescriptorHeap(14), GetDescriptorHandleIncrementSize(15),
;    CreateRootSignature(16), CreateConstantBufferView(17),
;    CreateShaderResourceView(18), CreateUnorderedAccessView(19),
;    CreateRenderTargetView(20), CreateDepthStencilView(21),
;    CreateSampler(22), CopyDescriptors(23), CopyDescriptorsSimple(24),
;    GetResourceAllocationInfo(25), GetCustomHeapProperties(26),
;    CreateCommittedResource(27), CreateHeap(28), CreatePlacedResource(29),
;    CreateReservedResource(30), CreateSharedHandle(31),
;    OpenSharedHandle(32), OpenSharedHandleByName(33),
;    MakeResident(34), Evict(35), CreateFence(36),
;    GetDeviceRemovedReason(37), GetCopyableFootprints(38),
;    CreateQueryHeap(39), SetStablePowerState(40),
;    CreateCommandSignature(41), GetResourceTiling(42),
;    GetAdapterLuid(43)
%define ID3D12Device_CreateCommandQueue          (8*4)
%define ID3D12Device_CreateCommandAllocator      (9*4)
%define ID3D12Device_CreateGraphicsPipelineState (10*4)
%define ID3D12Device_CreateCommandList           (12*4)
%define ID3D12Device_CreateDescriptorHeap        (14*4)
%define ID3D12Device_GetDescriptorHandleIncrementSize (15*4)
%define ID3D12Device_CreateRootSignature         (16*4)
%define ID3D12Device_CreateRenderTargetView      (20*4)
%define ID3D12Device_CreateCommittedResource     (27*4)
%define ID3D12Device_CreateFence                 (36*4)

; ── ID3D12CommandQueue ─────────────────────────────────────────────────────
;  Inherits: ID3D12Pageable → ID3D12DeviceChild → ID3D12Object → IUnknown
;  IUnknown(3) + ID3D12Object(4) + ID3D12DeviceChild(1) + ID3D12Pageable(0) = 8
;  ID3D12CommandQueue: UpdateTileMappings(8), CopyTileMappings(9),
;    ExecuteCommandLists(10), SetMarker(11), BeginEvent(12), EndEvent(13),
;    Signal(14), Wait(15), GetTimestampFrequency(16), GetClockCalibration(17),
;    GetDesc(18)
%define ID3D12CommandQueue_ExecuteCommandLists   (10*4)
%define ID3D12CommandQueue_Signal                (14*4)

; ── ID3D12CommandAllocator ─────────────────────────────────────────────────
;  Inherits same pageable chain (8 base methods)
;  Reset(8)
%define ID3D12CommandAllocator_Reset             (8*4)

; ── ID3D12GraphicsCommandList ──────────────────────────────────────────────
;  Inherits: ID3D12CommandList → ID3D12DeviceChild → ID3D12Object → IUnknown
;  IUnknown(3) + ID3D12Object(4) + ID3D12DeviceChild(1) + ID3D12CommandList(1) = 9
;
;  Index 9:  Close
;  Index 10: Reset
;  Index 11: ClearState
;  Index 12: DrawInstanced
;  Index 13: DrawIndexedInstanced
;  Index 14: Dispatch
;  Index 15: CopyBufferRegion
;  Index 16: CopyTextureRegion
;  Index 17: CopyResource
;  Index 18: CopyTiles
;  Index 19: ResolveSubresource
;  Index 20: IASetPrimitiveTopology
;  Index 21: RSSetViewports
;  Index 22: RSSetScissorRects
;  Index 23: OMSetBlendFactor
;  Index 24: OMSetStencilRef
;  Index 25: SetPipelineState
;  Index 26: ResourceBarrier
;  Index 27: ExecuteBundle
;  Index 28: SetDescriptorHeaps
;  Index 29: SetComputeRootSignature
;  Index 30: SetGraphicsRootSignature
;  Index 31: SetComputeRootDescriptorTable
;  Index 32: SetGraphicsRootDescriptorTable
;  Index 33: SetComputeRoot32BitConstant
;  Index 34: SetGraphicsRoot32BitConstant
;  Index 35: SetComputeRoot32BitConstants
;  Index 36: SetGraphicsRoot32BitConstants
;  Index 37: SetComputeRootConstantBufferView
;  Index 38: SetGraphicsRootConstantBufferView
;  Index 39: SetComputeRootShaderResourceView
;  Index 40: SetGraphicsRootShaderResourceView
;  Index 41: SetComputeRootUnorderedAccessView
;  Index 42: SetGraphicsRootUnorderedAccessView
;  Index 43: IASetIndexBuffer
;  Index 44: IASetVertexBuffers
;  Index 45: SOSetTargets
;  Index 46: OMSetRenderTargets
;  Index 47: ClearDepthStencilView
;  Index 48: ClearRenderTargetView
;  Index 49: ClearUnorderedAccessViewInt
;  Index 50: ClearUnorderedAccessViewFloat
;  Index 51: DiscardResource
;  Index 52: BeginQuery
;  Index 53: EndQuery
;  Index 54: ResolveQueryData
;  Index 55: SetPredication
;  Index 56: SetMarker
;  Index 57: BeginEvent
;  Index 58: EndEvent
;  Index 59: ExecuteIndirect
%define ID3D12CmdList_Close                      (9*4)
%define ID3D12CmdList_Reset                      (10*4)
%define ID3D12CmdList_DrawInstanced              (12*4)
%define ID3D12CmdList_IASetPrimitiveTopology     (20*4)
%define ID3D12CmdList_RSSetViewports             (21*4)
%define ID3D12CmdList_RSSetScissorRects          (22*4)
%define ID3D12CmdList_SetPipelineState           (25*4)
%define ID3D12CmdList_ResourceBarrier            (26*4)
%define ID3D12CmdList_SetGraphicsRootSignature   (30*4)
%define ID3D12CmdList_SetGraphicsRoot32BitConstants (36*4)
%define ID3D12CmdList_IASetVertexBuffers         (44*4)
%define ID3D12CmdList_OMSetRenderTargets         (46*4)
%define ID3D12CmdList_ClearRenderTargetView      (48*4)

; ── ID3D12Fence ────────────────────────────────────────────────────────────
;  Inherits pageable chain (8 base methods)
;  GetCompletedValue(8), SetEventOnCompletion(9), Signal(10)
%define ID3D12Fence_GetCompletedValue            (8*4)
%define ID3D12Fence_SetEventOnCompletion         (9*4)

; ── ID3D12Resource ─────────────────────────────────────────────────────────
;  Inherits pageable chain (8 base)
;  Map(8), Unmap(9), GetDesc(10), GetGPUVirtualAddress(11)
%define ID3D12Resource_GetGPUVirtualAddress      (11*4)

; ── Helper macro: x86 COM call through vtable ───────────────────────────────
; Usage: COM_VCALL pObj, VtableOffset
;   pObj = memory address holding the COM interface pointer
;   Must push all args (excluding this) on stack BEFORE this macro.
;   This macro pushes `this` and calls through the vtable.
;   Callee cleans the stack (__stdcall).
%macro COM_VCALL 2
    push    dword [%1]              ; push this
    mov     eax, [%1]              ; load interface ptr
    mov     eax, [eax]              ; dereference to vtable
    call    [eax + %2]              ; call method
%endmacro

; ── IID GUIDs we need ──────────────────────────────────────────────────────
; These are declared as data in the main .asm file that includes this.
