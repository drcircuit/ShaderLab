; ============================================================================
;  sync_decoder.asm — Compact track v3 binary decoder
;  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
;
;  Decodes the TKR3 compact binary track format into an in-memory structure
;  that the orchestrator can scan each frame.
;
;  Track v3 binary layout:
;    Header (14 bytes):
;      [0..1]   u16  magic_lo   ('TK' = 0x4B54)
;      [2..3]   u16  magic_hi   ('R3' = 0x3352)
;      [4..5]   u16  bpmQ8      (BPM * 256, fixed-point 8.8)
;      [6..7]   u16  lenBeats   (total length in beats)
;      [8..9]   u16  rowCount   (number of tracker rows)
;      [10..11] u16  sceneCount (number of scenes)
;      [12]     u8   transSlotCount (always 6)
;      [13]     u8   renderConfig   (resolution/aspect presets)
;
;    Transition Module Map (12 bytes):
;      6 x i16  moduleIndex per transition slot (-1 = unused)
;
;    Per-Scene Module Map (variable):
;      For each scene:
;        i16  sceneModuleIndex
;        u16  postFxCount
;        i16[postFxCount]  postFxModuleIndices
;
;    Row Data (9 bytes each, rowCount entries):
;      [0..1]  i16  rowId       (beat number)
;      [2..3]  i16  sceneIndex  (-1 = no change)
;      [4]     u8   transition  (slot index, 0xFF = none)
;      [5]     u8   flags       (bit 0 = stop)
;      [6]     u8   transDurQ4  (duration / 16 → beats)
;      [7]     i8   timeOffQ4   (time offset / 16 → beats)
;      [8]     i8   musicIndex  (-1 = no change)
;
;  x86 cdecl: all parameters passed on stack, caller cleans.
;  Assembled with:  nasmw -f win32 sync_decoder.asm -o sync_decoder.obj
; ============================================================================

bits 32

%include "constants.inc"

; ── Exports (x86 C symbol decoration: underscore prefix) ────────────────────
global _sync_decode_track

; ── Decoded track structure (in BSS) ────────────────────────────────────────
; This is the output of the decoder, consumed by the orchestrator.
global _decoded_bpmQ8
global _decoded_bpm_float
global _decoded_lenBeats
global _decoded_rowCount
global _decoded_sceneCount
global _decoded_rowsPtr
global _decoded_renderConfig

section .bss align=16

_decoded_bpmQ8:       resw 1       ; BPM as Q8 fixed point
_decoded_bpm_float:   resd 1       ; BPM as float32
_decoded_lenBeats:    resw 1       ; total beat count
_decoded_rowCount:    resw 1       ; number of tracker rows
_decoded_sceneCount:  resw 1       ; number of scenes
_decoded_renderConfig: resb 1      ; render config byte
alignb 4
_decoded_rowsPtr:     resd 1       ; pointer to first row in the raw binary
                                    ; (rows are accessed in-place, no copy needed)

; Transition module map (6 entries of i16)
global _decoded_transMap
_decoded_transMap:    resw 6

; Scene module indices (up to 32 scenes)
%define MAX_SCENES 32
global _decoded_sceneModules
_decoded_sceneModules: resw MAX_SCENES

section .text align=16

; ============================================================================
;  _sync_decode_track(const uint8_t* data, uint32_t size)
;    [esp+4] = pointer to track binary data
;    [esp+8] = size in bytes
;
;  x86 cdecl — caller cleans stack.
;  Returns: eax = 1 on success, 0 on failure (bad magic or too small)
; ============================================================================
_sync_decode_track:
    push    ebx
    push    esi
    push    edi

    mov     esi, [esp+16]           ; esi = data pointer  (esp+4 + 3*4 saved regs)
    mov     ebx, [esp+20]           ; ebx = size

    ; ── Validate minimum size ───────────────────────────────────────────────
    cmp     ebx, TRACK_HEADER_SIZE + TRACK_TRANS_MAP_SIZE
    jb      .fail

    ; ── Validate magic ──────────────────────────────────────────────────────
    movzx   eax, word [esi+0]
    cmp     eax, TRACK_MAGIC_LO
    jne     .fail
    movzx   eax, word [esi+2]
    cmp     eax, TRACK_MAGIC_HI
    jne     .fail

    ; ── Extract header fields ───────────────────────────────────────────────
    movzx   eax, word [esi+4]
    mov     [_decoded_bpmQ8], ax

    ; Convert BPM Q8 to float: bpm = bpmQ8 / 256.0
    ; Use x87 FPU (avoid SSE dependency for smaller code)
    mov     [esp-4], eax            ; scratch space
    fild    dword [esp-4]
    mov     dword [esp-4], 256
    fidiv   dword [esp-4]
    fstp    dword [_decoded_bpm_float]

    movzx   eax, word [esi+6]
    mov     [_decoded_lenBeats], ax

    movzx   eax, word [esi+8]
    mov     [_decoded_rowCount], ax
    mov     edi, eax                ; edi = rowCount for later

    movzx   eax, word [esi+10]
    mov     [_decoded_sceneCount], ax

    movzx   eax, byte [esi+13]
    mov     [_decoded_renderConfig], al

    ; ── Copy transition module map (6 x i16 = 12 bytes) ────────────────────
    lea     ecx, [esi + TRACK_HEADER_SIZE]
    lea     edx, [_decoded_transMap]
    ; Copy 12 bytes (3 dwords)
    mov     eax, [ecx]
    mov     [edx], eax
    mov     eax, [ecx+4]
    mov     [edx+4], eax
    mov     eax, [ecx+8]
    mov     [edx+8], eax

    ; ── Parse per-scene module map ──────────────────────────────────────────
    lea     ecx, [esi + TRACK_HEADER_SIZE + TRACK_TRANS_MAP_SIZE]
    movzx   edx, word [_decoded_sceneCount]
    lea     edi, [_decoded_sceneModules]
    xor     ebx, ebx                ; scene index

.sceneLoop:
    cmp     ebx, edx
    jge     .scenesDone

    ; Read scene module index
    movsx   eax, word [ecx]
    mov     [edi + ebx*2], ax
    add     ecx, 2

    ; Read postFxCount and skip postFx module indices
    movzx   eax, word [ecx]
    add     ecx, 2
    ; Skip eax * 2 bytes (postFx indices)
    lea     ecx, [ecx + eax*2]

    inc     ebx
    jmp     .sceneLoop
.scenesDone:

    ; ── Store pointer to row data ───────────────────────────────────────────
    ; ecx now points to the first row
    mov     [_decoded_rowsPtr], ecx

    ; ── Validate we have enough data for all rows ───────────────────────────
    ; (Optional size check — skip for minimal size)

    mov     eax, 1
    jmp     .done

.fail:
    xor     eax, eax
.done:
    pop     edi
    pop     esi
    pop     ebx
    ret
