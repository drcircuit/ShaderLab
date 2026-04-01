; ============================================================================
;  main.asm — ASM MicroPlayer entry point
;  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
;
;  x86 COM ABI: __stdcall, this on stack, callee cleans, vtable ptrs are 4B.
;
;  Assembled with:  nasmw -f win32 main.asm -o main.obj
;  Linked with Crinkler: crinkler /ENTRY:_WinMainCRTStartup /SUBSYSTEM:WINDOWS ...
; ============================================================================

bits 32

%include "constants.inc"
%include "dx12.inc"

; ── Debug logging macros (expand to nothing in release) ─────────────────────
%ifdef DEBUG
  %macro LOG 1
    push    %1
    call    LogMsg
  %endmacro
  %macro LOGHR 1
    push    %1
    call    LogHR
  %endmacro
%else
  %macro LOG 1
  %endmacro
  %macro LOGHR 1
  %endmacro
%endif

; ── External Win32 imports (x86 stdcall: __imp__Name@ParamBytes) ────────────
extern __imp__GetModuleHandleA@4
extern __imp__RegisterClassExA@4
extern __imp__CreateWindowExA@48
extern __imp__ShowCursor@4
extern __imp__PeekMessageA@20
extern __imp__TranslateMessage@4
extern __imp__DispatchMessageA@4
extern __imp__DefWindowProcA@16
extern __imp__PostQuitMessage@4
extern __imp__GetAsyncKeyState@4
extern __imp__QueryPerformanceFrequency@4
extern __imp__QueryPerformanceCounter@4
extern __imp__CreateEventA@16
extern __imp__WaitForSingleObject@8
extern __imp__ExitProcess@4
%ifdef DEBUG
extern __imp__MessageBoxA@16
extern __imp__CreateFileA@28
extern __imp__WriteFile@20
extern __imp__CloseHandle@4
extern __imp__lstrlenA@4
%endif

; ── External DX12/DXGI imports ──────────────────────────────────────────────
extern __imp__CreateDXGIFactory1@8
extern __imp__D3D12CreateDevice@16
extern __imp__D3D12SerializeRootSignature@16

; ── External ASM modules (x86 cdecl: _Name) ────────────────────────────────
extern _orch_init
extern _orch_update

; ── Exports ─────────────────────────────────────────────────────────────────
global _WinMainCRTStartup
global _g_constants

; ── Precompiled shader bytecode (from shader_data.asm) ──────────────────────
extern _g_vsBlob
extern _g_vsBlobSize
extern _g_psBlob
extern _g_psBlobSize

; ============================================================================
;  Read-only data
; ============================================================================
section .rdata align=16

wndClassName:   db "SL_ASM", 0

; ── IID GUIDs ───────────────────────────────────────────────────────────────
IID_IDXGIFactory1:
    dd 0x770aae78
    dw 0xf26f, 0x4dba
    db 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87

IID_ID3D12Device:
    dd 0x189819f1
    dw 0x1db6, 0x4b57
    db 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7

IID_ID3D12Fence:
    dd 0x0a753dcf
    dw 0xc4d8, 0x4b91
    db 0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76

IID_ID3D12CmdList:
    dd 0x5b160d0f
    dw 0xac1b, 0x4185
    db 0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55

IID_ID3D12Resource:
    dd 0x696442be
    dw 0xa72e, 0x4059
    db 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad

IID_IDXGISwapChain3:
    dd 0x94d99bdb
    dw 0xf1f8, 0x4ab0
    db 0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1

IID_ID3D12CommandQueue:
    dd 0x0ec870a6
    dw 0x5d7e, 0x4c22
    db 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed

IID_ID3D12CommandAllocator:
    dd 0x6102DEE4
    dw 0xAF59, 0x4B09
    db 0xB9, 0x99, 0xB4, 0x4D, 0x73, 0xF0, 0x9B, 0x24

IID_ID3D12DescriptorHeap:
    dd 0x8EFB471D
    dw 0x616C, 0x4F49
    db 0x90, 0xF7, 0x12, 0x7B, 0xB7, 0x63, 0xFA, 0x51

IID_ID3D12RootSignature:
    dd 0xC54A6B66
    dw 0x72DF, 0x4EE8
    db 0x8B, 0xE5, 0xA9, 0x46, 0xA1, 0x42, 0x92, 0x14

IID_ID3D12PipelineState:
    dd 0x765a30f3
    dw 0xf624, 0x4c6f
    db 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45

; ── D3D12_COMMAND_QUEUE_DESC ────────────────────────────────────────────────
align 4
cmdQueueDesc:
    dd D3D12_COMMAND_LIST_TYPE_DIRECT
    dd 0                               ; Priority
    dd D3D12_COMMAND_QUEUE_FLAG_NONE
    dd 0                               ; NodeMask

; ── D3D12_DESCRIPTOR_HEAP_DESC ──────────────────────────────────────────────
rtvHeapDesc:
    dd D3D12_DESCRIPTOR_HEAP_TYPE_RTV
    dd SWAPCHAIN_BUFFER_COUNT
    dd D3D12_DESCRIPTOR_HEAP_FLAG_NONE
    dd 0

; ── DXGI_SWAP_CHAIN_DESC1 ──────────────────────────────────────────────────
align 4
swapChainDesc:
    dd SCREEN_W
    dd SCREEN_H
    dd DXGI_FORMAT_R8G8B8A8_UNORM
    dd 0                               ; Stereo
    dd 1                               ; SampleDesc.Count
    dd 0                               ; SampleDesc.Quality
    dd DXGI_USAGE_RENDER_TARGET_OUTPUT
    dd SWAPCHAIN_BUFFER_COUNT
    dd 0                               ; Scaling
    dd DXGI_SWAP_EFFECT_FLIP_DISCARD
    dd 0                               ; AlphaMode
    dd 0                               ; Flags

; ── Clear color ─────────────────────────────────────────────────────────────
align 16
clearColor:
    dd 0x00000000, 0x00000000, 0x00000000, 0x3F800000

%ifdef DEBUG
; ── Debug strings (file-based logging) ──────────────────────────────────────
dbg_logpath:    db "asm_debug.log", 0
dbg_crlf:       db 13, 10, 0
dbg_s_entry:    db "[ENTRY] WinMainCRTStartup reached", 0
dbg_s_wnd:      db "[WNDCR] CreateWindowExA returned hwnd=", 0
dbg_s_pre_init: db "[PREIN] About to call InitDX12", 0
dbg_s_init_ok:  db "[INITOK] InitDX12 succeeded", 0
dbg_s_init_fail:db "[INITFAIL] InitDX12 returned 0", 0
dbg_s_loop:     db "[LOOP] Entering main loop", 0
dbg_s00:        db "[DX-00] Calling CreateDXGIFactory1...", 0
dbg_s00_ok:     db "[DX-00] CreateDXGIFactory1 OK", 0
dbg_s01:        db "[DX-01] Calling D3D12CreateDevice...", 0
dbg_s01_ok:     db "[DX-01] D3D12CreateDevice OK", 0
dbg_s02:        db "[DX-02] Calling CreateCommandQueue...", 0
dbg_s02_ok:     db "[DX-02] CreateCommandQueue OK", 0
dbg_s03:        db "[DX-03] Calling CreateSwapChainForHwnd...", 0
dbg_s03_ok:     db "[DX-03] CreateSwapChainForHwnd OK", 0
dbg_s04:        db "[DX-04] Calling CreateCommandAllocator...", 0
dbg_s04_ok:     db "[DX-04] CreateCommandAllocator OK", 0
dbg_s05:        db "[DX-05] Calling CreateCommandList...", 0
dbg_s05_ok:     db "[DX-05] CreateCommandList OK", 0
dbg_s05c:       db "[DX-05c] Calling CmdList Close...", 0
dbg_s05c_ok:    db "[DX-05c] CmdList Close OK", 0
dbg_s06:        db "[DX-06] Calling CreateFence...", 0
dbg_s06_ok:     db "[DX-06] CreateFence OK", 0
dbg_s07:        db "[DX-07] Calling CreateDescriptorHeap...", 0
dbg_s07_ok:     db "[DX-07] CreateDescriptorHeap OK", 0
dbg_s08:        db "[DX-08] GetBuffer/CreateRTV loop...", 0
dbg_s08a:       db "[DX-08a] GetCPUDescriptorHandleForHeapStart...", 0
dbg_s08b:       db "[DX-08b] Handle obtained, entering loop", 0
dbg_s08c:       db "[DX-08c] GetBuffer OK", 0
dbg_s08d:       db "[DX-08d] CreateRTV OK, next iter", 0
dbg_s08_ok:     db "[DX-08] RTVs created OK", 0
dbg_s09:        db "[DX-09] Calling CreateRootSignature...", 0
dbg_s09_ok:     db "[DX-09] CreateRootSignature OK", 0
dbg_s10:        db "[DX-10] Calling CreatePSO...", 0
dbg_s10_ok:     db "[DX-10] CreatePSO OK", 0
dbg_s_fail_hr:  db "[FAIL] HRESULT=0x", 0
dbg_hex:        db "0123456789ABCDEF"
dbg_title:      db "ShaderLab ASM Debug", 0
%endif

; ============================================================================
;  BSS — mutable data
; ============================================================================
section .bss align=16

g_hwnd:         resd 1
g_hInstance:    resd 1

g_factory:      resd 1
g_device:       resd 1
g_cmdQueue:     resd 1
g_cmdAlloc:     resd 1
g_cmdList:      resd 1
g_swapChain:    resd 1
g_fence:        resd 1
g_fenceEvent:   resd 1
g_fenceValue:   resq 1      ; UINT64

g_rtvHeap:      resd 1
g_rtvSize:      resd 1
g_backBuffers:  resd SWAPCHAIN_BUFFER_COUNT

g_rootSig:      resd 1
g_pso:          resd 1

g_perfFreq:     resq 1
g_startTime:    resq 1
g_currentTime:  resq 1

alignb 16
_g_constants:   resd CONSTANTS_SIZE_DWORDS

alignb 4
g_msg:          resb SIZEOF_MSG
g_barrier:      resb SIZEOF_BARRIER
g_viewport:     resb 24
g_scissorRect:  resb 16
g_wndClass:     resb SIZEOF_WNDCLASSEXA
g_rootParam:    resb SIZEOF_ROOT_PARAMETER
g_rootSigDesc:  resb SIZEOF_ROOT_SIGNATURE_DESC
g_scratchHandle: resd 1    ; For COM struct returns
%ifdef DEBUG
g_dbgStep:       resd 1    ; Debug: last init step before failure
g_logHandle:     resd 1    ; Debug: log file handle
g_logWritten:    resd 1    ; Debug: bytes written scratch
g_hexBuf:        resb 12   ; Debug: hex conversion buffer
%endif


; ============================================================================
;  Code
; ============================================================================
section .text align=16

; ── Window procedure ────────────────────────────────────────────────────────
; LRESULT __stdcall WndProc(HWND, UINT, WPARAM, LPARAM)
; [esp+4]=hwnd [esp+8]=uMsg [esp+12]=wParam [esp+16]=lParam
global _WndProc@16
_WndProc@16:
    mov     eax, [esp+8]
    cmp     eax, WM_DESTROY
    je      .destroy
    cmp     eax, WM_KEYDOWN
    jne     .default
    cmp     dword [esp+12], VK_ESCAPE
    je      .destroy
.default:
    jmp     [__imp__DefWindowProcA@16]
.destroy:
    push    0
    call    [__imp__PostQuitMessage@4]
    xor     eax, eax
    ret     16


%ifdef DEBUG
; ── Debug: Log a message string to the log file ────────────────────────────
; Input: [esp+4] = pointer to null-terminated string
; Preserves: ebx, esi, edi, ebp
LogMsg:
    push    ebx
    mov     ebx, [esp+8]           ; pStr

    ; Get string length
    push    ebx
    call    [__imp__lstrlenA@4]     ; eax = length

    ; WriteFile(hFile, pBuf, nBytes, pWritten, pOverlapped)
    push    0
    push    g_logWritten
    push    eax
    push    ebx
    push    dword [g_logHandle]
    call    [__imp__WriteFile@20]

    ; Write CRLF
    push    0
    push    g_logWritten
    push    2
    push    dbg_crlf
    push    dword [g_logHandle]
    call    [__imp__WriteFile@20]

    pop     ebx
    ret     4

; ── Debug: Log HRESULT in hex ───────────────────────────────────────────────
; Input: [esp+4] = HRESULT value
; Writes "[FAIL] HRESULT=0xXXXXXXXX" to log
LogHR:
    push    ebx
    push    esi
    mov     eax, [esp+12]          ; HRESULT value

    ; Convert to hex string in g_hexBuf (8 chars + \0)
    lea     ebx, [g_hexBuf]
    mov     ecx, 8
.hexLoop:
    dec     ecx
    mov     edx, eax
    and     edx, 0x0F
    mov     dl, [dbg_hex + edx]
    mov     [ebx + ecx], dl
    shr     eax, 4
    test    ecx, ecx
    jnz     .hexLoop
    mov     byte [ebx+8], 0

    ; Write prefix
    push    dbg_s_fail_hr
    call    LogMsg

    ; Write hex value
    push    g_hexBuf
    call    LogMsg

    pop     esi
    pop     ebx
    ret     4
%endif ; DEBUG


; ── Entry point ─────────────────────────────────────────────────────────────
_WinMainCRTStartup:
    sub     esp, 256

%ifdef DEBUG
    ; ── Open debug log file ─────────────────────────────────────────────────
    ; CreateFileA(lpFileName, dwAccess, dwShareMode, lpSA, dwCreation, dwFlags, hTemplate)
    push    0                       ; hTemplateFile
    push    0x80                    ; FILE_ATTRIBUTE_NORMAL
    push    2                       ; CREATE_ALWAYS
    push    0                       ; lpSecurityAttributes
    push    0                       ; dwShareMode
    push    0x40000000              ; GENERIC_WRITE
    push    dbg_logpath
    call    [__imp__CreateFileA@28]
    mov     [g_logHandle], eax

    push    dbg_s_entry
    call    LogMsg
%endif

    ; ── GetModuleHandle ─────────────────────────────────────────────────────
    push    0
    call    [__imp__GetModuleHandleA@4]
    mov     [g_hInstance], eax

    ; ── Hide cursor ─────────────────────────────────────────────────────────
    push    0
    call    [__imp__ShowCursor@4]

    ; ── Register window class ───────────────────────────────────────────────
    lea     edi, [g_wndClass]
    xor     eax, eax
    mov     ecx, SIZEOF_WNDCLASSEXA / 4
    rep stosd

    lea     edi, [g_wndClass]
    mov     dword [edi + WNDCLS_cbSize], SIZEOF_WNDCLASSEXA
    mov     dword [edi + WNDCLS_style], CS_OWNDC
    mov     dword [edi + WNDCLS_lpfnWndProc], _WndProc@16
    mov     eax, [g_hInstance]
    mov     [edi + WNDCLS_hInstance], eax
    mov     dword [edi + WNDCLS_lpszClassName], wndClassName

    push    edi
    call    [__imp__RegisterClassExA@4]

    ; ── Create fullscreen popup window ──────────────────────────────────────
    push    0                       ; lpParam
    push    dword [g_hInstance]     ; hInstance
    push    0                       ; hMenu
    push    0                       ; hWndParent
    push    SCREEN_H
    push    SCREEN_W
    push    0                       ; Y
    push    0                       ; X
    push    WS_POPUP | WS_VISIBLE
    push    0                       ; lpWindowName
    push    wndClassName
%ifdef DEBUG
    push    0                       ; WS_EX_TOPMOST disabled for debug
%else
    push    WS_EX_TOPMOST
%endif
    call    [__imp__CreateWindowExA@48]
    mov     [g_hwnd], eax

    LOG     dbg_s_wnd

    ; ── Init DX12 ──────────────────────────────────────────────────────────
    LOG     dbg_s_pre_init

    call    InitDX12
    test    eax, eax
    jz      .exit

    LOG     dbg_s_init_ok

    ; ── Init timing ────────────────────────────────────────────────────────
    push    g_perfFreq
    call    [__imp__QueryPerformanceFrequency@4]
    push    g_startTime
    call    [__imp__QueryPerformanceCounter@4]
    mov     eax, [g_startTime]
    mov     [g_currentTime], eax
    mov     eax, [g_startTime+4]
    mov     [g_currentTime+4], eax

    ; ── Init orchestrator ──────────────────────────────────────────────────
    call    _orch_init

    LOG     dbg_s_loop

    ; ── Main loop ──────────────────────────────────────────────────────────
.mainLoop:
    push    PM_REMOVE
    push    0
    push    0
    push    0
    push    g_msg
    call    [__imp__PeekMessageA@20]
    test    eax, eax
    jz      .render

    cmp     dword [g_msg + MSG_message], WM_QUIT
    je      .exit

    push    g_msg
    call    [__imp__TranslateMessage@4]
    push    g_msg
    call    [__imp__DispatchMessageA@4]
    jmp     .mainLoop

.render:
    push    VK_ESCAPE
    call    [__imp__GetAsyncKeyState@4]
    test    ax, ax
    jnz     .exit

    ; ── Update time (FPU int64 arithmetic) ─────────────────────────────────
    push    g_currentTime
    call    [__imp__QueryPerformanceCounter@4]

    ; delta = currentTime - startTime (64-bit)
    mov     eax, [g_currentTime]
    mov     edx, [g_currentTime+4]
    sub     eax, [g_startTime]
    sbb     edx, [g_startTime+4]
    mov     [esp+240], eax
    mov     [esp+244], edx
    fild    qword [esp+240]         ; push delta
    fild    qword [g_perfFreq]      ; push freq
    fdivp   st1, st0                ; delta / freq → seconds
    fstp    dword [_g_constants+0]  ; iTime

    ; iResolution
    mov     dword [esp+240], SCREEN_W
    fild    dword [esp+240]
    fstp    dword [_g_constants+4]
    mov     dword [esp+240], SCREEN_H
    fild    dword [esp+240]
    fstp    dword [_g_constants+8]

    ; ── Update orchestrator ────────────────────────────────────────────────
    push    dword [_g_constants+0]  ; iTime (float)
    call    _orch_update
    add     esp, 4

    ; ── Render ─────────────────────────────────────────────────────────────
    call    RenderFrame

    jmp     .mainLoop

.exit:
%ifdef DEBUG
    ; Close log handle before exiting
    push    dword [g_logHandle]
    call    [__imp__CloseHandle@4]
%endif
    push    0
    call    [__imp__ExitProcess@4]


; ============================================================================
;  InitDX12
; ============================================================================
InitDX12:
    push    ebx
    push    esi
    push    edi
    push    ebp
    sub     esp, 64

    ; ── DXGI Factory ────────────────────────────────────────────────────────
    LOG     dbg_s00
    push    g_factory
    push    IID_IDXGIFactory1
    call    [__imp__CreateDXGIFactory1@8]
    test    eax, eax
    js      .fail
    LOG     dbg_s00_ok

    ; ── D3D12 Device ────────────────────────────────────────────────────────
    LOG     dbg_s01
    push    g_device
    push    IID_ID3D12Device
    push    D3D_FEATURE_LEVEL_11_0
    push    0
    call    [__imp__D3D12CreateDevice@16]
    test    eax, eax
    js      .fail
    LOG     dbg_s01_ok

    ; ── Command Queue ───────────────────────────────────────────────────────
    LOG     dbg_s02
    push    g_cmdQueue
    push    IID_ID3D12CommandQueue
    push    cmdQueueDesc
    COM_VCALL g_device, ID3D12Device_CreateCommandQueue
    test    eax, eax
    js      .fail
    LOG     dbg_s02_ok

    ; ── Swap Chain ──────────────────────────────────────────────────────────
    LOG     dbg_s03
    push    g_swapChain
    push    0                       ; pRestrictToOutput
    push    0                       ; pFullscreenDesc
    push    swapChainDesc
    push    dword [g_hwnd]
    push    dword [g_cmdQueue]
    COM_VCALL g_factory, IDXGIFactory2_CreateSwapChainForHwnd
    test    eax, eax
    js      .fail
    LOG     dbg_s03_ok

    ; ── Command Allocator ───────────────────────────────────────────────────
    LOG     dbg_s04
    push    g_cmdAlloc
    push    IID_ID3D12CommandAllocator
    push    D3D12_COMMAND_LIST_TYPE_DIRECT
    COM_VCALL g_device, ID3D12Device_CreateCommandAllocator
    test    eax, eax
    js      .fail
    LOG     dbg_s04_ok

    ; ── Command List ────────────────────────────────────────────────────────
    LOG     dbg_s05
    push    g_cmdList
    push    IID_ID3D12CmdList
    push    0                       ; pInitialState
    push    dword [g_cmdAlloc]
    push    D3D12_COMMAND_LIST_TYPE_DIRECT
    push    0                       ; nodeMask
    COM_VCALL g_device, ID3D12Device_CreateCommandList
    test    eax, eax
    js      .fail
    LOG     dbg_s05_ok

    ; Close it (starts open)
    LOG     dbg_s05c
    COM_VCALL g_cmdList, ID3D12CmdList_Close
    LOG     dbg_s05c_ok

    ; ── Fence ───────────────────────────────────────────────────────────────
    ; CreateFence(this, InitialValue_lo, InitialValue_hi, Flags, riid, ppFence)
    LOG     dbg_s06
    push    g_fence
    push    IID_ID3D12Fence
    push    D3D12_FENCE_FLAG_NONE
    push    dword 0                 ; InitialValue high
    push    dword 0                 ; InitialValue low
    COM_VCALL g_device, ID3D12Device_CreateFence
    test    eax, eax
    js      .fail
    LOG     dbg_s06_ok

    mov     dword [g_fenceValue], 0
    mov     dword [g_fenceValue+4], 0

    push    0
    push    0
    push    0
    push    0
    call    [__imp__CreateEventA@16]
    mov     [g_fenceEvent], eax

    ; ── RTV Descriptor Heap ─────────────────────────────────────────────────
    LOG     dbg_s07
    push    g_rtvHeap
    push    IID_ID3D12DescriptorHeap
    push    rtvHeapDesc
    COM_VCALL g_device, ID3D12Device_CreateDescriptorHeap
    test    eax, eax
    js      .fail
    LOG     dbg_s07_ok

    ; Get RTV descriptor increment size
    push    D3D12_DESCRIPTOR_HEAP_TYPE_RTV
    COM_VCALL g_device, ID3D12Device_GetDescriptorHandleIncrementSize
    mov     [g_rtvSize], eax

    ; ── Get back buffers + create RTVs ──────────────────────────────────────
    LOG     dbg_s08
    ; GetCPUDescriptorHandleForHeapStart — returns struct via hidden out ptr
    LOG     dbg_s08a
    push    g_scratchHandle
    COM_VCALL g_rtvHeap, (9*4)
    mov     ebx, [g_scratchHandle]
    LOG     dbg_s08b

    xor     esi, esi
.rtvLoop:
    cmp     esi, SWAPCHAIN_BUFFER_COUNT
    jge     .rtvDone

    ; GetBuffer(this, index, riid, ppResource)
    lea     eax, [g_backBuffers + esi*4]
    push    eax
    push    IID_ID3D12Resource
    push    esi
    COM_VCALL g_swapChain, IDXGISwapChain_GetBuffer
    test    eax, eax
    js      .fail
    LOG     dbg_s08c

    ; CreateRenderTargetView(this, pResource, pDesc, DestDescriptor)
    ; DestDescriptor = D3D12_CPU_DESCRIPTOR_HANDLE, passed by value (4 bytes on x86)
    push    ebx
    push    0                       ; pDesc = NULL
    push    dword [g_backBuffers + esi*4]
    COM_VCALL g_device, ID3D12Device_CreateRenderTargetView
    LOG     dbg_s08d

    add     ebx, [g_rtvSize]
    inc     esi
    jmp     .rtvLoop
.rtvDone:
    LOG     dbg_s08_ok

    ; ── Root Signature ──────────────────────────────────────────────────────
    LOG     dbg_s09
    call    CreateRootSignature
    test    eax, eax
    jz      .fail
    LOG     dbg_s09_ok

    ; ── Pipeline State Object ───────────────────────────────────────────────
    LOG     dbg_s10
    call    CreatePSO
    test    eax, eax
    jz      .fail
    LOG     dbg_s10_ok

    ; ── Viewport + Scissor ──────────────────────────────────────────────────
    lea     edi, [g_viewport]
    xor     eax, eax
    mov     [edi+0], eax            ; TopLeftX
    mov     [edi+4], eax            ; TopLeftY
    mov     dword [edi+8], __float32__(1920.0)
    mov     dword [edi+12], __float32__(1080.0)
    mov     [edi+16], eax           ; MinDepth
    mov     dword [edi+20], 0x3F800000  ; MaxDepth 1.0

    lea     edi, [g_scissorRect]
    xor     eax, eax
    mov     [edi+0], eax
    mov     [edi+4], eax
    mov     dword [edi+8], SCREEN_W
    mov     dword [edi+12], SCREEN_H

    mov     eax, 1
    jmp     .done
.fail:
    ; Log the failing HRESULT
    LOGHR   eax
    xor     eax, eax
.done:
    add     esp, 64
    pop     ebp
    pop     edi
    pop     esi
    pop     ebx
    ret


; ============================================================================
;  CreateRootSignature
; ============================================================================
CreateRootSignature:
    push    ebx
    push    esi
    sub     esp, 32

    lea     edi, [g_rootParam]
    mov     dword [edi + ROOTPARAM_ParameterType], 1
    mov     dword [edi + ROOTPARAM_ShaderRegister], 0
    mov     dword [edi + ROOTPARAM_RegisterSpace], 0
    mov     dword [edi + ROOTPARAM_Num32BitValues], CONSTANTS_SIZE_DWORDS
    mov     dword [edi + ROOTPARAM_ShaderVisibility], 0

    lea     edi, [g_rootSigDesc]
    mov     dword [edi + ROOTSIG_NumParameters], 1
    mov     dword [edi + ROOTSIG_pParameters], g_rootParam
    mov     dword [edi + ROOTSIG_NumStaticSamplers], 0
    mov     dword [edi + ROOTSIG_pStaticSamplers], 0
    mov     dword [edi + ROOTSIG_Flags], 1          ; ALLOW_IA_INPUT_LAYOUT

    ; SerializeRootSignature(pDesc, Version, ppBlob, ppErrorBlob)
    lea     eax, [esp+4]
    push    eax                     ; ppErrorBlob
    lea     eax, [esp+4]
    push    eax                     ; ppBlob
    push    1                       ; VERSION_1
    push    g_rootSigDesc
    call    [__imp__D3D12SerializeRootSignature@16]
    test    eax, eax
    js      .fail

    mov     ebx, [esp+0]           ; ebx = pBlob

    ; GetBufferPointer(this)
    push    ebx
    mov     eax, [ebx]
    call    [eax + 3*4]
    mov     esi, eax

    ; GetBufferSize(this)
    push    ebx
    mov     eax, [ebx]
    call    [eax + 4*4]

    ; CreateRootSignature(this, nodeMask, pBlobData, BlobLength, riid, pp)
    push    g_rootSig
    push    IID_ID3D12RootSignature
    push    eax                     ; BlobLength
    push    esi                     ; pBlobData
    push    0                       ; nodeMask
    COM_VCALL g_device, ID3D12Device_CreateRootSignature
    test    eax, eax
    js      .fail

    ; Release blob
    push    ebx
    mov     eax, [ebx]
    call    [eax + IUnknown_Release]

    mov     eax, 1
    jmp     .done
.fail:
    xor     eax, eax
.done:
    add     esp, 32
    pop     esi
    pop     ebx
    ret


; ============================================================================
;  CreatePSO
; ============================================================================
CreatePSO:
    push    ebx
    push    esi
    sub     esp, SIZEOF_PSO_DESC + 16

    ; Zero PSO desc
    lea     edi, [esp+8]
    xor     eax, eax
    mov     ecx, SIZEOF_PSO_DESC / 4
    rep stosd

    lea     edi, [esp+8]

    mov     eax, [g_rootSig]
    mov     [edi + PSO_pRootSignature], eax

    mov     dword [edi + PSO_VS_pBytecode], _g_vsBlob
    mov     eax, [_g_vsBlobSize]
    mov     [edi + PSO_VS_BytecodeLength], eax

    mov     dword [edi + PSO_PS_pBytecode], _g_psBlob
    mov     eax, [_g_psBlobSize]
    mov     [edi + PSO_PS_BytecodeLength], eax

    mov     byte [edi + PSO_BlendRT0_WriteMask], 0x0F
    mov     dword [edi + PSO_SampleMask], 0xFFFFFFFF
    mov     dword [edi + PSO_Rasterizer_FillMode], 3
    mov     dword [edi + PSO_Rasterizer_CullMode], 1
    mov     dword [edi + PSO_Rasterizer_DepthClip], 1
    mov     dword [edi + PSO_PrimitiveTopology], 3
    mov     dword [edi + PSO_NumRenderTargets], 1
    mov     dword [edi + PSO_RTVFormats], DXGI_FORMAT_R8G8B8A8_UNORM
    mov     dword [edi + PSO_SampleDesc_Count], 1

    ; CreateGraphicsPipelineState(this, pDesc, riid, ppPSO)
    push    g_pso
    push    IID_ID3D12PipelineState
    push    edi
    COM_VCALL g_device, ID3D12Device_CreateGraphicsPipelineState
    test    eax, eax
    js      .fail

    mov     eax, 1
    jmp     .done
.fail:
    xor     eax, eax
.done:
    add     esp, SIZEOF_PSO_DESC + 16
    pop     esi
    pop     ebx
    ret


; ============================================================================
;  RenderFrame
; ============================================================================
RenderFrame:
    push    ebx
    push    esi
    push    edi
    sub     esp, 64

    ; ── Reset command allocator ─────────────────────────────────────────────
    COM_VCALL g_cmdAlloc, ID3D12CommandAllocator_Reset

    ; ── Reset command list ──────────────────────────────────────────────────
    push    0
    push    dword [g_cmdAlloc]
    COM_VCALL g_cmdList, ID3D12CmdList_Reset

    ; ── Get current back buffer index ───────────────────────────────────────
    COM_VCALL g_swapChain, IDXGISwapChain3_GetCurrentBackBufferIndex
    mov     esi, eax

    ; ── Transition: PRESENT → RENDER_TARGET ─────────────────────────────────
    lea     edi, [g_barrier]
    mov     dword [edi + BARRIER_Type], D3D12_RESOURCE_BARRIER_TYPE_TRANSITION
    mov     dword [edi + BARRIER_Flags], D3D12_RESOURCE_BARRIER_FLAG_NONE
    mov     eax, [g_backBuffers + esi*4]
    mov     [edi + BARRIER_pResource], eax
    mov     dword [edi + BARRIER_Subresource], 0xFFFFFFFF
    mov     dword [edi + BARRIER_StateBefore], D3D12_RESOURCE_STATE_PRESENT
    mov     dword [edi + BARRIER_StateAfter], D3D12_RESOURCE_STATE_RENDER_TARGET

    push    g_barrier
    push    1
    COM_VCALL g_cmdList, ID3D12CmdList_ResourceBarrier

    ; ── Compute RTV handle ──────────────────────────────────────────────────
    push    g_scratchHandle
    COM_VCALL g_rtvHeap, (9*4)
    mov     ebx, [g_scratchHandle]
    mov     eax, [g_rtvSize]
    imul    eax, esi
    add     ebx, eax

    ; ── Clear render target ─────────────────────────────────────────────────
    push    0                       ; pRects
    push    0                       ; NumRects
    push    clearColor              ; ColorRGBA
    push    ebx                     ; rtvHandle
    COM_VCALL g_cmdList, ID3D12CmdList_ClearRenderTargetView

    ; ── Set render target ───────────────────────────────────────────────────
    mov     [esp+48], ebx           ; store handle on stack
    push    0                       ; pDSV
    push    0                       ; RTsSingleHandle
    lea     eax, [esp+56]
    push    eax                     ; pRTVDescriptors
    push    1
    COM_VCALL g_cmdList, ID3D12CmdList_OMSetRenderTargets

    ; ── Viewport + Scissor ──────────────────────────────────────────────────
    push    g_viewport
    push    1
    COM_VCALL g_cmdList, ID3D12CmdList_RSSetViewports

    push    g_scissorRect
    push    1
    COM_VCALL g_cmdList, ID3D12CmdList_RSSetScissorRects

    ; ── Set PSO + Root Sig ──────────────────────────────────────────────────
    push    dword [g_pso]
    COM_VCALL g_cmdList, ID3D12CmdList_SetPipelineState

    push    dword [g_rootSig]
    COM_VCALL g_cmdList, ID3D12CmdList_SetGraphicsRootSignature

    ; ── Set constants ───────────────────────────────────────────────────────
    push    0
    push    _g_constants
    push    CONSTANTS_SIZE_DWORDS
    push    0
    COM_VCALL g_cmdList, ID3D12CmdList_SetGraphicsRoot32BitConstants

    ; ── Topology + Draw ─────────────────────────────────────────────────────
    push    5                       ; TRIANGLESTRIP
    COM_VCALL g_cmdList, ID3D12CmdList_IASetPrimitiveTopology

    push    0                       ; StartInstance
    push    0                       ; StartVertex
    push    1                       ; InstanceCount
    push    3                       ; VertexCount
    COM_VCALL g_cmdList, ID3D12CmdList_DrawInstanced

    ; ── Transition: RENDER_TARGET → PRESENT ─────────────────────────────────
    lea     edi, [g_barrier]
    mov     dword [edi + BARRIER_StateBefore], D3D12_RESOURCE_STATE_RENDER_TARGET
    mov     dword [edi + BARRIER_StateAfter], D3D12_RESOURCE_STATE_PRESENT
    push    g_barrier
    push    1
    COM_VCALL g_cmdList, ID3D12CmdList_ResourceBarrier

    ; ── Close + Execute ─────────────────────────────────────────────────────
    COM_VCALL g_cmdList, ID3D12CmdList_Close

    push    g_cmdList
    push    1
    COM_VCALL g_cmdQueue, ID3D12CommandQueue_ExecuteCommandLists

    ; ── Signal fence ────────────────────────────────────────────────────────
    add     dword [g_fenceValue], 1
    adc     dword [g_fenceValue+4], 0

    push    dword [g_fenceValue+4]
    push    dword [g_fenceValue]
    push    dword [g_fence]
    COM_VCALL g_cmdQueue, ID3D12CommandQueue_Signal

    ; ── Wait for GPU ────────────────────────────────────────────────────────
    COM_VCALL g_fence, ID3D12Fence_GetCompletedValue
    ; eax:edx = completed value
    cmp     edx, [g_fenceValue+4]
    ja      .fenceDone
    jb      .waitFence
    cmp     eax, [g_fenceValue]
    jae     .fenceDone
.waitFence:
    push    dword [g_fenceEvent]
    push    dword [g_fenceValue+4]
    push    dword [g_fenceValue]
    COM_VCALL g_fence, ID3D12Fence_SetEventOnCompletion

    push    0xFFFFFFFF
    push    dword [g_fenceEvent]
    call    [__imp__WaitForSingleObject@8]
.fenceDone:

    ; ── Present ─────────────────────────────────────────────────────────────
    push    0
    push    1
    COM_VCALL g_swapChain, IDXGISwapChain_Present

    add     esp, 64
    pop     edi
    pop     esi
    pop     ebx
    ret
