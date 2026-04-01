; ============================================================================
;  constants.inc — Shared constants for ASM MicroPlayer
;  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
; ============================================================================

; ── Screen ──────────────────────────────────────────────────────────────────
%define SCREEN_W            1920
%define SCREEN_H            1080

; ── Timing ──────────────────────────────────────────────────────────────────
%define DEFAULT_BPM_Q8      35840           ; 140.0 * 256  (fixed-point 8.8)
%define DEFAULT_LENGTH_BEATS 64

; ── Win32 constants ─────────────────────────────────────────────────────────
%define CS_OWNDC            0x0020
%define WS_POPUP            0x80000000
%define WS_VISIBLE          0x10000000
%define WS_EX_TOPMOST       0x00000008
%define CW_USEDEFAULT       0x80000000

%define WM_DESTROY          0x0002
%define WM_KEYDOWN          0x0100
%define WM_QUIT             0x0012

%define VK_ESCAPE           0x1B

%define PM_REMOVE           0x0001
%define SW_SHOW             5

%define SM_CXSCREEN         0
%define SM_CYSCREEN         1

; ── Win32 struct sizes (x86) ────────────────────────────────────────────────
; WNDCLASSEXA: 48 bytes on x86 (vs 80 on x64)
;   cbSize(4) style(4) lpfnWndProc(4) cbClsExtra(4) cbWndExtra(4)
;   hInstance(4) hIcon(4) hCursor(4) hbrBackground(4) lpszMenuName(4)
;   lpszClassName(4) hIconSm(4)
%define SIZEOF_WNDCLASSEXA  48
; Field offsets within WNDCLASSEXA (x86)
%define WNDCLS_cbSize       0
%define WNDCLS_style        4
%define WNDCLS_lpfnWndProc  8
%define WNDCLS_hInstance    20
%define WNDCLS_lpszClassName 40

; MSG: 28 bytes on x86 (vs 48 on x64)
;   hwnd(4) message(4) wParam(4) lParam(4) time(4) pt.x(4) pt.y(4)
%define SIZEOF_MSG          28
; msg.message offset (for WM_QUIT check)
%define MSG_message         4

; ── DXGI / D3D12 constants ──────────────────────────────────────────────────
%define DXGI_FORMAT_R8G8B8A8_UNORM          28
%define DXGI_FORMAT_D32_FLOAT               40
%define DXGI_USAGE_RENDER_TARGET_OUTPUT     0x00000020
%define DXGI_SWAP_EFFECT_FLIP_DISCARD       4

%define D3D12_COMMAND_LIST_TYPE_DIRECT      0
%define D3D12_COMMAND_QUEUE_FLAG_NONE       0
%define D3D12_FENCE_FLAG_NONE               0
%define D3D12_DESCRIPTOR_HEAP_TYPE_RTV      2
%define D3D12_DESCRIPTOR_HEAP_FLAG_NONE     0
%define D3D12_RESOURCE_STATE_PRESENT        0
%define D3D12_RESOURCE_STATE_RENDER_TARGET  4
%define D3D12_RESOURCE_BARRIER_TYPE_TRANSITION      0
%define D3D12_RESOURCE_BARRIER_FLAG_NONE    0

%define D3D_FEATURE_LEVEL_11_0             0xb000

; ── Swap chain parameters ───────────────────────────────────────────────────
%define SWAPCHAIN_BUFFER_COUNT              2

; ── D3D12_GRAPHICS_PIPELINE_STATE_DESC offsets (x86) ────────────────────────
; All pointer fields are 4 bytes; SIZE_T is 4 bytes on x86.
; sizeof(D3D12_RENDER_TARGET_BLEND_DESC) = 40  (9 DWORDs + UINT8 + padding)
; sizeof(D3D12_BLEND_DESC) = 328  (4+4 + 8*40)
; sizeof(D3D12_RASTERIZER_DESC) = 44
; sizeof(D3D12_DEPTH_STENCIL_DESC) = 52
; sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC) = 572
;
;   0:  pRootSignature       (4)
;   4:  VS.pShaderBytecode   (4)
;   8:  VS.BytecodeLength    (4)   SIZE_T
;  12:  PS.pShaderBytecode   (4)
;  16:  PS.BytecodeLength    (4)
;  20:  DS                   (8)
;  28:  HS                   (8)
;  36:  GS                   (8)
;  44:  StreamOutput         (20) = ptr+uint+ptr+uint+uint
;  64:  BlendState           (328) = 4+4 + 8*40
; 392:  SampleMask           (4)
; 396:  RasterizerState      (44)
; 440:  DepthStencilState    (52)
; 492:  InputLayout          (8) = ptr+uint
; 500:  IBStripCutValue      (4)
; 504:  PrimitiveTopology    (4)
; 508:  NumRenderTargets     (4)
; 512:  RTVFormats[8]        (32)
; 544:  DSVFormat            (4)
; 548:  SampleDesc           (8) = Count+Quality
; Total: 572 bytes (verified with MSVC x86 cl.exe)
%define PSO_pRootSignature      0
%define PSO_VS_pBytecode        4
%define PSO_VS_BytecodeLength   8
%define PSO_PS_pBytecode        12
%define PSO_PS_BytecodeLength   16
%define PSO_BlendState          64
%define PSO_BlendRT0_WriteMask  108     ; BlendState(64) + bools(8) + RT[0].WriteMask(36)
%define PSO_SampleMask          392
%define PSO_Rasterizer_FillMode 396
%define PSO_Rasterizer_CullMode 400
%define PSO_Rasterizer_DepthClip 420
%define PSO_PrimitiveTopology   504
%define PSO_NumRenderTargets    508
%define PSO_RTVFormats          512
%define PSO_SampleDesc_Count    548
%define SIZEOF_PSO_DESC         572

; ── D3D12_RESOURCE_BARRIER offsets (x86) ────────────────────────────────────
; Type(4) Flags(4) pResource(4) Subresource(4) StateBefore(4) StateAfter(4) = 24
%define BARRIER_Type            0
%define BARRIER_Flags           4
%define BARRIER_pResource       8
%define BARRIER_Subresource     12
%define BARRIER_StateBefore     16
%define BARRIER_StateAfter      20
%define SIZEOF_BARRIER          24

; ── D3D12_ROOT_PARAMETER offsets (x86) ──────────────────────────────────────
; ParameterType(4) union{Constants(12)} ShaderVisibility(4)
; D3D12_ROOT_CONSTANTS: ShaderRegister(4) RegisterSpace(4) Num32BitValues(4)
%define ROOTPARAM_ParameterType     0
%define ROOTPARAM_ShaderRegister    4
%define ROOTPARAM_RegisterSpace     8
%define ROOTPARAM_Num32BitValues    12
%define ROOTPARAM_ShaderVisibility  16
%define SIZEOF_ROOT_PARAMETER       20

; ── D3D12_ROOT_SIGNATURE_DESC offsets (x86) ─────────────────────────────────
; NumParameters(4) pParameters(4) NumStaticSamplers(4) pStaticSamplers(4) Flags(4)
%define ROOTSIG_NumParameters       0
%define ROOTSIG_pParameters         4
%define ROOTSIG_NumStaticSamplers   8
%define ROOTSIG_pStaticSamplers     12
%define ROOTSIG_Flags               16
%define SIZEOF_ROOT_SIGNATURE_DESC  20

; ── Compact Track v3 format ─────────────────────────────────────────────────
;  Header: 'TK' 'R3' bpmQ8(u16) lenBeats(u16) rowCount(u16) sceneCount(u16)
;          transSlotCount(u8) renderConfig(u8)
%define TRACK_MAGIC_LO      0x4B54          ; 'TK'
%define TRACK_MAGIC_HI      0x3352          ; 'R3'
%define TRACK_HEADER_SIZE   14
%define TRACK_TRANS_MAP_SIZE 12             ; 6 * sizeof(int16_t)

; Per-row layout (9 bytes each):
;   rowId(i16) sceneIdx(i16) transition(u8) flags(u8) transDurQ4(u8)
;   timeOffQ4(i8) musicIdx(i8)
%define TRACK_ROW_SIZE      9
%define TRACK_ROW_OFF_ID          0
%define TRACK_ROW_OFF_SCENE       2
%define TRACK_ROW_OFF_TRANSITION  4
%define TRACK_ROW_OFF_FLAGS       5
%define TRACK_ROW_OFF_DURATION    6
%define TRACK_ROW_OFF_TIMEOFF     7
%define TRACK_ROW_OFF_MUSIC       8

; Row flag bits
%define TRACK_FLAG_STOP     0x01

; ── Shader constant buffer layout (matches PreviewRenderer::Constants) ──────
; float  iTime         offset  0   (4 bytes)
; float  iResolution.x offset  4   (4 bytes)
; float  iResolution.y offset  8   (4 bytes)
; float  iBeat         offset 12   (4 bytes)
; float  iBar          offset 16   (4 bytes)
; float  fBeat         offset 20   (4 bytes)
; float  fBarBeat      offset 24   (4 bytes)
; float  fBarBeat16    offset 28   (4 bytes)
; Total: 32 bytes = 8 DWORDs
%define CONSTANTS_SIZE_DWORDS   8
%define CONSTANTS_SIZE_BYTES    32
