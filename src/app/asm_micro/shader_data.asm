; ============================================================================
;  shader_data.asm — Embed precompiled shader bytecode via NASM incbin
;  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
;
;  Replaces shader_data.c — no C compiler needed for x86 build.
;  NASM's incbin directive embeds the raw .cso files at assemble time.
;  The -I flag in the NASM command line points to the build output directory
;  where DXC writes the .cso files.
;
;  Assembled with:  nasmw -f win32 -I<cso_dir>/ shader_data.asm
; ============================================================================

bits 32

; ── Exports (x86 C decoration: underscore prefix) ──────────────────────────
global _g_vsBlob
global _g_vsBlobSize
global _g_psBlob
global _g_psBlobSize

; ============================================================================
;  Read-only data — shader bytecode
; ============================================================================
section .rdata align=16

_g_vsBlob:
    incbin "vs_fullscreen.cso"
_g_vsBlob_end:

align 4
_g_vsBlobSize:
    dd _g_vsBlob_end - _g_vsBlob

align 16
_g_psBlob:
    incbin "ps_cloud_tunnel.cso"
_g_psBlob_end:

align 4
_g_psBlobSize:
    dd _g_psBlob_end - _g_psBlob
