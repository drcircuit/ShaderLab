# ============================================================================
#  CMakeLists.txt — ASM MicroPlayer build
#  ShaderLab Experiment: Ultra-minimal DX12 player in x86 NASM + Crinkler
#
#  Build requirements:
#    - NASMW (third_party/nasm/nasmw.exe)
#    - DXC / FXC for shader pre-compilation
#    - Crinkler 3.0 for compressed linking (optional; falls back to x86 MSVC link)
#
#  Enable with: -DSHADERLAB_BUILD_ASM_MICRO=ON
#
#  NOTE: The host build is x64, but this target produces an x86 PE.
#  All compilation and linking is done via custom commands (NASM + Crinkler/link).
# ============================================================================

if(NOT SHADERLAB_BUILD_ASM_MICRO)
    return()
endif()

# ── Locate NASMW ──────────────────────────────────────────────────────────
set(NASMW_PATH "${CMAKE_SOURCE_DIR}/third_party/nasm/nasmw.exe")
if(NOT EXISTS "${NASMW_PATH}")
    find_program(NASMW_PATH NAMES nasmw nasm)
endif()
if(NOT NASMW_PATH)
    message(FATAL_ERROR "NASMW assembler not found. Place nasmw.exe in third_party/nasm/")
endif()
message(STATUS "ASM MicroPlayer: Using NASMW at ${NASMW_PATH}")

# ── Locate shader compiler (prefer dxc, fall back to fxc) ─────────────────
find_program(DXC_COMPILER dxc HINTS "$ENV{VULKAN_SDK}/Bin" "${CMAKE_SOURCE_DIR}/third_party/dxc")
find_program(FXC_COMPILER fxc)

if(DXC_COMPILER)
    set(SHADER_COMPILER "${DXC_COMPILER}")
    set(SHADER_COMPILER_IS_DXC TRUE)
    message(STATUS "ASM MicroPlayer: Using DXC at ${DXC_COMPILER}")
elseif(FXC_COMPILER)
    set(SHADER_COMPILER "${FXC_COMPILER}")
    set(SHADER_COMPILER_IS_DXC FALSE)
    message(STATUS "ASM MicroPlayer: Using FXC at ${FXC_COMPILER}")
else()
    message(FATAL_ERROR "Neither dxc nor fxc found. Cannot compile shaders for ASM MicroPlayer.")
endif()

# ── Output directories ────────────────────────────────────────────────────
set(ASM_MICRO_GEN_DIR "${CMAKE_BINARY_DIR}/generated/asm_micro")
file(MAKE_DIRECTORY "${ASM_MICRO_GEN_DIR}")

# ── Shader source paths ──────────────────────────────────────────────────
set(VS_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_fullscreen.hlsl")
set(PS_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ps_cloud_tunnel.hlsl")

set(VS_CSO "${ASM_MICRO_GEN_DIR}/vs_fullscreen.cso")
set(PS_CSO "${ASM_MICRO_GEN_DIR}/ps_cloud_tunnel.cso")

set(CLOUD_TUNNEL_SHADER_DIR "${CMAKE_SOURCE_DIR}/experiments/cloud_tunnel/assets/shaders/hlsl")

# ── Compile shaders to .cso ──────────────────────────────────────────────
if(SHADER_COMPILER_IS_DXC)
    add_custom_command(
        OUTPUT "${VS_CSO}"
        COMMAND "${SHADER_COMPILER}" -T vs_6_0 -E main -Fo "${VS_CSO}" "${VS_HLSL}"
        DEPENDS "${VS_HLSL}"
        COMMENT "DXC: Compiling VS (vs_6_0) → ${VS_CSO}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${PS_CSO}"
        COMMAND "${SHADER_COMPILER}" -T ps_6_0 -E PSMain -I "${CLOUD_TUNNEL_SHADER_DIR}" -Fo "${PS_CSO}" "${PS_HLSL}"
        DEPENDS "${PS_HLSL}"
                "${CMAKE_SOURCE_DIR}/experiments/cloud_tunnel/assets/shaders/hlsl/cloud_tunnel.hlsl"
        COMMENT "DXC: Compiling PS (ps_6_0) → ${PS_CSO}"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${VS_CSO}"
        COMMAND "${SHADER_COMPILER}" /T vs_5_0 /E main /Fo "${VS_CSO}" "${VS_HLSL}"
        DEPENDS "${VS_HLSL}"
        COMMENT "FXC: Compiling VS (vs_5_0) → ${VS_CSO}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${PS_CSO}"
        COMMAND "${SHADER_COMPILER}" /T ps_5_0 /E PSMain /I "${CLOUD_TUNNEL_SHADER_DIR}" /Fo "${PS_CSO}" "${PS_HLSL}"
        DEPENDS "${PS_HLSL}"
                "${CMAKE_SOURCE_DIR}/experiments/cloud_tunnel/assets/shaders/hlsl/cloud_tunnel.hlsl"
        COMMENT "FXC: Compiling PS (ps_5_0) → ${PS_CSO}"
        VERBATIM
    )
endif()

add_custom_target(AsmMicroShaderGen DEPENDS "${VS_CSO}" "${PS_CSO}")

# ── Assemble .asm → .obj with NASMW (x86 / win32) ───────────────────────
set(ASM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ASM_SOURCES
    main.asm
    sync_decoder.asm
    orchestrator.asm
    shader_data.asm
)

# Pass -dDEBUG to NASM for Debug builds (but not when using Crinkler)
set(NASM_GLOBAL_FLAGS "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT SHADERLAB_USE_CRINKLER)
    set(NASM_GLOBAL_FLAGS "-dDEBUG")
endif()

set(ASM_OBJECTS "")
foreach(asm_file ${ASM_SOURCES})
    get_filename_component(asm_name "${asm_file}" NAME_WE)
    set(asm_obj "${ASM_MICRO_GEN_DIR}/${asm_name}.obj")

    set(_nasm_extra_flags "")
    set(_nasm_extra_deps "")
    if(asm_file STREQUAL "shader_data.asm")
        # shader_data.asm uses incbin to embed .cso files
        set(_nasm_extra_flags -i "${ASM_MICRO_GEN_DIR}/")
        set(_nasm_extra_deps "${VS_CSO}" "${PS_CSO}")
    endif()

    add_custom_command(
        OUTPUT "${asm_obj}"
        COMMAND "${NASMW_PATH}"
            -f win32
            -i "${ASM_SRC_DIR}/"
            ${NASM_GLOBAL_FLAGS}
            ${_nasm_extra_flags}
            -o "${asm_obj}"
            "${ASM_SRC_DIR}/${asm_file}"
        DEPENDS
            "${ASM_SRC_DIR}/${asm_file}"
            "${ASM_SRC_DIR}/constants.inc"
            "${ASM_SRC_DIR}/dx12.inc"
            ${_nasm_extra_deps}
        COMMENT "NASMW: Assembling ${asm_file} (x86/win32) → ${asm_obj}"
        VERBATIM
    )

    list(APPEND ASM_OBJECTS "${asm_obj}")
endforeach()

add_custom_target(AsmMicroAsmObjects DEPENDS ${ASM_OBJECTS})
add_dependencies(AsmMicroAsmObjects AsmMicroShaderGen)

# ── Locate x86 link tools and libraries ──────────────────────────────────
# Derive x86 MSVC linker from the host x64 linker path.
# CMAKE_LINKER is e.g. .../Hostx64/x64/link.exe → we want .../Hostx64/x86/link.exe
get_filename_component(_LINKER_DIR "${CMAKE_LINKER}" DIRECTORY)
string(REPLACE "/x64" "/x86" _X86_LINKER_DIR "${_LINKER_DIR}")
string(REPLACE "\\x64" "\\x86" _X86_LINKER_DIR "${_X86_LINKER_DIR}")
set(_X86_LINK_EXE "${_X86_LINKER_DIR}/link.exe")

# Derive x86 MSVC lib path (same replacement)
get_filename_component(_MSVC_BIN_DIR "${CMAKE_LINKER}" DIRECTORY)  # .../bin/Hostx64/x64
get_filename_component(_MSVC_BIN_PARENT "${_MSVC_BIN_DIR}" DIRECTORY)  # .../bin/Hostx64
get_filename_component(_MSVC_BIN_ROOT "${_MSVC_BIN_PARENT}" DIRECTORY)  # .../bin
get_filename_component(_MSVC_TOOLS_DIR "${_MSVC_BIN_ROOT}" DIRECTORY)   # .../MSVC/14.xx.yyyyy
set(_X86_MSVC_LIB "${_MSVC_TOOLS_DIR}/lib/x86")

# Windows SDK x86 libraries
# CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION may be empty with Ninja generator.
# Fall back to probing the SDK directory for the latest version.
set(_WINSDK_ROOT "C:/Program Files (x86)/Windows Kits/10")
set(_WINSDK_VERSION "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
if(NOT _WINSDK_VERSION OR _WINSDK_VERSION STREQUAL "")
    # Probe for the latest SDK version in the Lib directory
    file(GLOB _SDK_VERSIONS RELATIVE "${_WINSDK_ROOT}/Lib" "${_WINSDK_ROOT}/Lib/10.*")
    list(SORT _SDK_VERSIONS)
    list(LENGTH _SDK_VERSIONS _SDK_COUNT)
    if(_SDK_COUNT GREATER 0)
        math(EXPR _SDK_LAST "${_SDK_COUNT} - 1")
        list(GET _SDK_VERSIONS ${_SDK_LAST} _WINSDK_VERSION)
    else()
        message(FATAL_ERROR "Could not find Windows SDK version in ${_WINSDK_ROOT}/Lib")
    endif()
endif()
set(_X86_SDK_UM_LIB "${_WINSDK_ROOT}/Lib/${_WINSDK_VERSION}/um/x86")
set(_X86_SDK_UCRT_LIB "${_WINSDK_ROOT}/Lib/${_WINSDK_VERSION}/ucrt/x86")

message(STATUS "ASM MicroPlayer: x86 linker = ${_X86_LINK_EXE}")
message(STATUS "ASM MicroPlayer: x86 MSVC lib = ${_X86_MSVC_LIB}")
message(STATUS "ASM MicroPlayer: x86 SDK um   = ${_X86_SDK_UM_LIB}")

# ── Link into final executable ────────────────────────────────────────────
if(SHADERLAB_USE_CRINKLER)
    # ── Resolve Crinkler path ────────────────────────────────────────────
    if(CRINKLER_PATH AND EXISTS "${CRINKLER_PATH}")
        set(_CRINKLER_EXE "${CRINKLER_PATH}")
    else()
        find_program(_CRINKLER_EXE NAMES Crinkler crinkler
            HINTS
                "${CMAKE_SOURCE_DIR}/third_party/crinkler30/Win64"
                "${CMAKE_SOURCE_DIR}/third_party/crinkler30"
                "${CMAKE_SOURCE_DIR}/third_party"
        )
    endif()
    if(NOT _CRINKLER_EXE)
        message(FATAL_ERROR "Crinkler requested but not found. Set CRINKLER_PATH or place Crinkler.exe in third_party/")
    endif()
    message(STATUS "ASM MicroPlayer: using Crinkler at ${_CRINKLER_EXE}")

    set(_CRINKLER_OUTPUT "${CMAKE_BINARY_DIR}/bin/ShaderLabAsmMicro.exe")

    set(_asm_crinkler_compmode "FAST")
    if(DEFINED SHADERLAB_CRINKLER_COMPMODE AND NOT SHADERLAB_CRINKLER_COMPMODE STREQUAL "")
        set(_asm_crinkler_compmode "${SHADERLAB_CRINKLER_COMPMODE}")
    endif()

    set(_CRINKLER_FLAGS
        /ENTRY:WinMainCRTStartup
        /SUBSYSTEM:WINDOWS
        /COMPMODE:${_asm_crinkler_compmode}
        /HASHSIZE:${SHADERLAB_CRINKLER_HASH_SIZE_MB}
        /HASHTRIES:${SHADERLAB_CRINKLER_HASH_TRIES}
        /ORDERTRIES:${SHADERLAB_CRINKLER_ORDER_TRIES}
        /UNALIGNCODE
        /UNSAFEIMPORT
        /PRINT:IMPORTS
        /PRINT:LABELS
        "/LIBPATH:${_X86_MSVC_LIB}"
        "/LIBPATH:${_X86_SDK_UM_LIB}"
        "/LIBPATH:${_X86_SDK_UCRT_LIB}"
        /OUT:${_CRINKLER_OUTPUT}
    )
    if(SHADERLAB_CRINKLER_TINYIMPORT)
        list(APPEND _CRINKLER_FLAGS /TINYIMPORT)
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_custom_command(
        OUTPUT "${_CRINKLER_OUTPUT}"
        COMMAND "${_CRINKLER_EXE}"
            ${_CRINKLER_FLAGS}
            ${ASM_OBJECTS}
            d3d12.lib dxgi.lib user32.lib kernel32.lib gdi32.lib
        DEPENDS
            ${ASM_OBJECTS}
        COMMENT "Crinkler: linking ShaderLabAsmMicro.exe (x86)"
        VERBATIM
    )

    add_custom_target(ShaderLabAsmMicro ALL
        DEPENDS "${_CRINKLER_OUTPUT}"
    )
    add_dependencies(ShaderLabAsmMicro AsmMicroAsmObjects)

else()
    # ── Standard x86 MSVC link (no Crinkler) ─────────────────────────────
    # We use the x86 MSVC link.exe directly since the host build is x64.
    set(_MSVC_OUTPUT "${CMAKE_BINARY_DIR}/bin/ShaderLabAsmMicro.exe")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_custom_command(
        OUTPUT "${_MSVC_OUTPUT}"
        COMMAND "${_X86_LINK_EXE}"
            ${ASM_OBJECTS}
            /ENTRY:WinMainCRTStartup
            /SUBSYSTEM:WINDOWS
            /MACHINE:X86
            /INCREMENTAL:NO
            /OPT:REF
            /OPT:ICF
            /MANIFEST:NO
            /NODEFAULTLIB
            /RELEASE
            "/LIBPATH:${_X86_MSVC_LIB}"
            "/LIBPATH:${_X86_SDK_UM_LIB}"
            "/LIBPATH:${_X86_SDK_UCRT_LIB}"
            d3d12.lib dxgi.lib user32.lib kernel32.lib gdi32.lib
            /OUT:${_MSVC_OUTPUT}
        DEPENDS
            ${ASM_OBJECTS}
        COMMENT "MSVC x86 link: linking ShaderLabAsmMicro.exe"
        VERBATIM
    )

    add_custom_target(ShaderLabAsmMicro ALL
        DEPENDS "${_MSVC_OUTPUT}"
    )
    add_dependencies(ShaderLabAsmMicro AsmMicroAsmObjects)
endif()

message(STATUS "ASM MicroPlayer target configured: ShaderLabAsmMicro (x86/win32)")
