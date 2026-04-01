; ============================================================================
;  orchestrator.asm — Beat-driven scene scheduler
;  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
;
;  Implements the same beat-tracking logic as DemoPlayer::Update():
;    1. Convert wall-clock time → exact beat number
;    2. Scan tracker rows for events at each beat since last trigger
;    3. Write computed timing values into the shared constant buffer
;
;  The orchestrator is intentionally minimal for a single-scene demo:
;  it computes iTime, iBeat, iBar, fBeat, fBarBeat, fBarBeat16 and
;  writes them to the g_constants buffer that the main loop uploads
;  to the GPU each frame.
;
;  x86 cdecl: params on stack, caller cleans.
;  Assembled with:  nasmw -f win32 orchestrator.asm -o orchestrator.obj
; ============================================================================

bits 32

%include "constants.inc"

; ── Imports from sync_decoder.asm (x86: underscore prefix) ──────────────────
extern _decoded_bpmQ8
extern _decoded_bpm_float
extern _decoded_lenBeats
extern _decoded_rowCount
extern _decoded_rowsPtr

; ── Imports from main.asm ───────────────────────────────────────────────────
extern _g_constants

; ── Exports ─────────────────────────────────────────────────────────────────
global _orch_init
global _orch_update

; ── Orchestrator state (BSS) ────────────────────────────────────────────────
section .bss align=16

; Current active scene index (int32)
_orch_activeScene:    resd 1

; Last triggered beat (int32) — we scan rows for beats > lastBeat && <= currentBeat
_orch_lastBeat:       resd 1

; Transport state: 0 = stopped, 1 = playing
_orch_playing:        resd 1

; Looping flag
_orch_looping:        resd 1

; ── Float constants in rodata ───────────────────────────────────────────────
section .rdata align=16

; 60.0f for BPM→BPS conversion
align 4
f_60:           dd 0x42700000       ; 60.0f
f_4:            dd 0x40800000       ; 4.0f (beats per bar)
f_16:           dd 0x41800000       ; 16.0f (sixteenths per bar)
f_1:            dd 0x3F800000       ; 1.0f
f_0:            dd 0x00000000       ; 0.0f
f_inv256:       dd 0x3B800000       ; 1.0/256.0 = 0.00390625f  for Q8 decode

section .text align=16

; ============================================================================
;  _orch_init — Initialise orchestrator state.
;  Called once after sync_decode_track.
; ============================================================================
_orch_init:
    ; Start on scene 0, beat -1 (so beat 0 triggers), playing, looping
    mov     dword [_orch_activeScene], 0
    mov     dword [_orch_lastBeat], -1
    mov     dword [_orch_playing], 1
    mov     dword [_orch_looping], 1
    ret


; ============================================================================
;  _orch_update(float iTime)
;    [esp+4] = iTime (float, seconds since start)
;
;  x86 cdecl — caller cleans stack.
;  Computes beat-synchronised timing values and writes them to _g_constants.
;  Also scans tracker rows for scene changes at the current beat.
;
;  _g_constants layout:
;    [0]  iTime          (float) — already written by main.asm
;    [4]  iResolution.x  (float) — already written by main.asm
;    [8]  iResolution.y  (float) — already written by main.asm
;    [12] iBeat          (float) — exact beat (fractional)
;    [16] iBar           (float) — floor(beat / 4)
;    [20] fBeat          (float) — floor(beat)
;    [24] fBarBeat       (float) — beat mod 4
;    [28] fBarBeat16     (float) — (beat mod 4) * 4  (sixteenth note position)
; ============================================================================
_orch_update:
    push    ebx
    push    esi
    push    edi
    sub     esp, 16                 ; local scratch space

    ; ── Check if playing ────────────────────────────────────────────────────
    cmp     dword [_orch_playing], 0
    je      .writeZeroBeat

    ; ── Compute exact beat ──────────────────────────────────────────────────
    ; exactBeat = iTime * (bpm / 60.0)
    ; Use x87 FPU for all float math
    fld     dword [esp+32]          ; st0 = iTime  (esp+16+12+4 = 32)
    fld     dword [_decoded_bpm_float]  ; st0 = bpm, st1 = iTime
    fdiv    dword [f_60]            ; st0 = bpm/60
    fmulp   st1, st0               ; st0 = exactBeat = iTime * bps

    ; ── Check loop / end ────────────────────────────────────────────────────
    movzx   eax, word [_decoded_lenBeats]
    mov     [esp+0], eax
    fild    dword [esp+0]           ; st0 = lenBeats, st1 = exactBeat
    fcom    st1                     ; compare lenBeats with exactBeat
    fnstsw  ax
    sahf
    ja      .beatInRange_pop        ; lenBeats > exactBeat → in range

    ; Past end — loop or stop
    cmp     dword [_orch_looping], 0
    je      .stop_pop

    ; Wrap: exactBeat = fmod(exactBeat, lenBeats)
    ; fprem: st0 = st0 mod st1 → st1=exactBeat, st0=lenBeats
    fxch    st1                     ; st0=exactBeat, st1=lenBeats
.fmodLoop:
    fprem
    fnstsw  ax
    sahf
    jp      .fmodLoop               ; C2 set = incomplete, retry
    fstp    st1                     ; pop lenBeats, st0 = wrapped exactBeat

    ; Reset last triggered beat so row scanning works after loop
    mov     dword [_orch_lastBeat], -1
    jmp     .beatInRange

.beatInRange_pop:
    fstp    st0                     ; pop lenBeats, st0 = exactBeat
.beatInRange:
    ; st0 = exactBeat

    ; Save exactBeat for later use
    fst     dword [esp+0]          ; [esp+0] = exactBeat as float

    ; ── Scan tracker rows for current beat ──────────────────────────────────
    ; currentBeatInt = (int)floor(exactBeat)
    ; FPU truncation: use fisttp or manual truncation
    fnstcw  word [esp+4]           ; save FPU control word
    mov     ax, [esp+4]
    or      ax, 0x0C00             ; set truncation mode
    mov     [esp+6], ax
    fldcw   word [esp+6]
    fist    dword [esp+8]          ; [esp+8] = currentBeatInt (truncated)
    fldcw   word [esp+4]           ; restore FPU control word
    mov     eax, [esp+8]           ; eax = currentBeatInt
    mov     ecx, [_orch_lastBeat]

    ; Only scan if currentBeatInt > lastBeat
    cmp     eax, ecx
    jle     .noRowScan

    ; Walk rows from lastBeat+1 to currentBeatInt
    mov     edx, eax                ; edx = targetBeat
    mov     esi, [_decoded_rowsPtr] ; esi = rows pointer
    movzx   edi, word [_decoded_rowCount]  ; edi = rowCount

    xor     ebx, ebx                ; ebx = row index
.rowLoop:
    cmp     ebx, edi
    jge     .rowsDone

    ; Read row ID (i16 at offset 0)
    movsx   eax, word [esi + TRACK_ROW_OFF_ID]

    ; Check if this row's beat is in range (lastBeat, currentBeat]
    cmp     eax, ecx               ; row beat > lastBeat?
    jle     .nextRow
    cmp     eax, edx               ; row beat <= currentBeat?
    jg      .nextRow

    ; ── Process this row ────────────────────────────────────────────────────
    ; Check for scene change (sceneIndex != -1)
    movsx   eax, word [esi + TRACK_ROW_OFF_SCENE]
    cmp     eax, -1
    je      .checkStop
    mov     [_orch_activeScene], eax

.checkStop:
    ; Check stop flag
    movzx   eax, byte [esi + TRACK_ROW_OFF_FLAGS]
    test    eax, TRACK_FLAG_STOP
    jz      .nextRow
    mov     dword [_orch_playing], 0

.nextRow:
    add     esi, TRACK_ROW_SIZE
    inc     ebx
    jmp     .rowLoop

.rowsDone:
    mov     [_orch_lastBeat], edx    ; update lastBeat

.noRowScan:
    ; ── Compute and write timing constants ──────────────────────────────────
    ; st0 = exactBeat (still on FPU stack)

    ; iBeat = exactBeat
    fst     dword [_g_constants+12]

    ; fBeat = floor(exactBeat)
    fld     st0                     ; duplicate exactBeat
    frndint                         ; round to integer (uses current rounding mode → nearest)
    ; We need floor, not round-to-nearest. Use truncation approach:
    fst     dword [_g_constants+20]  ; fBeat (approximate — for demo, round-to-nearest is close enough)

    ; iBar = floor(exactBeat / 4.0)
    fld     st1                     ; st0 = exactBeat, st1 = roundedBeat, st2 = exactBeat
    fdiv    dword [f_4]            ; st0 = exactBeat / 4
    frndint                         ; floor (approx)
    fst     dword [_g_constants+16] ; iBar

    ; fBarBeat = exactBeat - floor(beat/4)*4
    fmul    dword [f_4]            ; st0 = floor(beat/4)*4
    fld     st2                     ; st0 = exactBeat
    fsubrp  st1, st0               ; st0 = exactBeat - floor(beat/4)*4 = fBarBeat
    fst     dword [_g_constants+24] ; fBarBeat

    ; fBarBeat16 = fBarBeat * 4.0
    fmul    dword [f_4]
    fstp    dword [_g_constants+28] ; fBarBeat16

    ; Clean up FPU stack (pop roundedBeat and exactBeat)
    fstp    st0                     ; pop roundedBeat
    fstp    st0                     ; pop exactBeat

    jmp     .done

.writeZeroBeat:
    ; Stopped — write zero for all beat fields
    xor     eax, eax
    mov     [_g_constants+12], eax  ; iBeat
    mov     [_g_constants+16], eax  ; iBar
    mov     [_g_constants+20], eax  ; fBeat
    mov     [_g_constants+24], eax  ; fBarBeat
    mov     [_g_constants+28], eax  ; fBarBeat16
    jmp     .done

.stop_pop:
    fstp    st0                     ; pop lenBeats
    fstp    st0                     ; pop exactBeat
    mov     dword [_orch_playing], 0

.done:
    add     esp, 16
    pop     edi
    pop     esi
    pop     ebx
    ret
