if(NOT SHADERLAB_BUILD_MICRO_PLAYER)
    return()
endif()

set(SHADERLAB_TINY_CRINKLER_X86 OFF)
if(SHADERLAB_USE_CRINKLER AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(SHADERLAB_TINY_CRINKLER_X86 ON)
endif()

set(SHADERLAB_TINY_LIB_TYPE STATIC)
if(SHADERLAB_TINY_CRINKLER_X86)
    set(SHADERLAB_TINY_LIB_TYPE OBJECT)
endif()

set(SHADERLAB_TINY_COMMON_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/imgui
    ${CMAKE_SOURCE_DIR}/third_party/json/include
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

add_library(ShaderLabTinyCore ${SHADERLAB_TINY_LIB_TYPE}
    ${CMAKE_SOURCE_DIR}/src/graphics/Device.cpp
    ${CMAKE_SOURCE_DIR}/src/graphics/Swapchain.cpp
    ${CMAKE_SOURCE_DIR}/src/graphics/CommandQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/graphics/PreviewRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/BeatClock.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PackageManager.cpp
)

if(SHADERLAB_TINY_RUNTIME_COMPILE)
    target_sources(ShaderLabTinyCore PRIVATE
        ${CMAKE_SOURCE_DIR}/src/shader/ShaderCompiler.cpp
    )
endif()

target_include_directories(ShaderLabTinyCore PUBLIC ${SHADERLAB_TINY_COMMON_INCLUDE_DIRS})

target_link_libraries(ShaderLabTinyCore PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
)

if(SHADERLAB_TINY_RUNTIME_COMPILE)
    target_link_libraries(ShaderLabTinyCore PUBLIC d3dcompiler.lib)
endif()

if(SHADERLAB_ENABLE_DXC)
    target_link_libraries(ShaderLabTinyCore PUBLIC dxcompiler.lib)
endif()

target_compile_definitions(ShaderLabTinyCore PUBLIC
    SHADERLAB_TINY_PLAYER=1
    SHADERLAB_RUNTIME_IMGUI=0
    SHADERLAB_RUNTIME_DEBUG_LOG=0
    SHADERLAB_COMPACT_TRACK_DEBUG=0
    SHADERLAB_TINY_TRACE=0
    SHADERLAB_TINY_DEV_OVERLAY=0
)

if(SHADERLAB_TINY_RUNTIME_COMPILE)
    target_compile_definitions(ShaderLabTinyCore PUBLIC SHADERLAB_TINY_RUNTIME_COMPILE=1)
else()
    target_compile_definitions(ShaderLabTinyCore PUBLIC SHADERLAB_TINY_RUNTIME_COMPILE=0)
endif()

if(SHADERLAB_ENABLE_DXC)
    target_compile_definitions(ShaderLabTinyCore PUBLIC SHADERLAB_ENABLE_DXC=1)
else()
    target_compile_definitions(ShaderLabTinyCore PUBLIC SHADERLAB_ENABLE_DXC=0)
endif()

if(MSVC)
    target_compile_options(ShaderLabTinyCore PRIVATE
        /W4
        /WX-
        /Os
        /Oy
        /Oi
        /GS-
        /GR-
        /Gy
        /Gw
        /GF
        /Zc:threadSafeInit-
    )
endif()

add_library(ShaderLabTinyKit ${SHADERLAB_TINY_LIB_TYPE}
    ${CMAKE_CURRENT_SOURCE_DIR}/TinyDemoPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TinyPlayerApp.cpp
)

target_include_directories(ShaderLabTinyKit PUBLIC ${SHADERLAB_TINY_COMMON_INCLUDE_DIRS})
target_link_libraries(ShaderLabTinyKit PUBLIC ShaderLabTinyCore)

if(MSVC AND SHADERLAB_TINY_RUNTIME_COMPILE)
    target_link_libraries(ShaderLabTinyKit PUBLIC d3dcompiler.lib)
endif()

target_compile_definitions(ShaderLabTinyKit PUBLIC
    SHADERLAB_TINY_PLAYER=1
    SHADERLAB_RUNTIME_IMGUI=0
    SHADERLAB_RUNTIME_DEBUG_LOG=0
    SHADERLAB_COMPACT_TRACK_DEBUG=0
    SHADERLAB_TINY_TRACE=0
    SHADERLAB_TINY_DEV_OVERLAY=0
)

if(SHADERLAB_TINY_RUNTIME_COMPILE)
    target_compile_definitions(ShaderLabTinyKit PUBLIC SHADERLAB_TINY_RUNTIME_COMPILE=1)
else()
    target_compile_definitions(ShaderLabTinyKit PUBLIC SHADERLAB_TINY_RUNTIME_COMPILE=0)
endif()

if(MSVC)
    target_compile_options(ShaderLabTinyKit PRIVATE
        /W4
        /WX-
        /Os
        /Oy
        /Oi
        /GS-
        /GR-
        /Gy
        /Gw
        /GF
        /Zc:threadSafeInit-
    )
endif()

add_executable(ShaderLabMicroPlayer WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/main_tiny_player.cpp
    ${CMAKE_SOURCE_DIR}/include/ShaderLab/App/PlayerApp.h
)

if(SHADERLAB_USE_CRINKLER AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_sources(ShaderLabMicroPlayer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/CrinklerX86Compat.cpp
    )

    # Tiny x86 + Crinkler only: keep CRT expectations in legacy import mode.
    target_compile_options(ShaderLabTinyCore PRIVATE /Zl)
    target_compile_options(ShaderLabTinyKit PRIVATE /Zl)
    target_compile_options(ShaderLabMicroPlayer PRIVATE /Zl)
    target_compile_options(ShaderLabTinyCore PRIVATE /EHs-c-)
    target_compile_options(ShaderLabTinyKit PRIVATE /EHs-c-)
    target_compile_options(ShaderLabMicroPlayer PRIVATE /EHs-c-)
    target_compile_options(ShaderLabTinyCore PRIVATE /Zc:throwingNew-)
    target_compile_options(ShaderLabTinyKit PRIVATE /Zc:throwingNew-)
    target_compile_options(ShaderLabMicroPlayer PRIVATE /Zc:throwingNew-)
    target_compile_definitions(ShaderLabTinyCore PRIVATE _HAS_EXCEPTIONS=0)
    target_compile_definitions(ShaderLabTinyKit PRIVATE _HAS_EXCEPTIONS=0)
    target_compile_definitions(ShaderLabMicroPlayer PRIVATE _HAS_EXCEPTIONS=0)
endif()

if(SHADERLAB_TINY_CRINKLER_X86)
    target_sources(ShaderLabMicroPlayer PRIVATE
        $<TARGET_OBJECTS:ShaderLabTinyCore>
        $<TARGET_OBJECTS:ShaderLabTinyKit>
    )
    target_link_libraries(ShaderLabMicroPlayer PRIVATE
        d3d12.lib
        dxgi.lib
        dxguid.lib
    )
    if(SHADERLAB_TINY_RUNTIME_COMPILE)
        target_link_libraries(ShaderLabMicroPlayer PRIVATE d3dcompiler.lib)
    endif()
else()
    target_link_libraries(ShaderLabMicroPlayer PRIVATE ShaderLabTinyKit ShaderLabTinyCore)
endif()
target_include_directories(ShaderLabMicroPlayer PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(ShaderLabMicroPlayer PRIVATE
        /W4
        /WX-
        /Os
        /Oy
        /Oi
        /GS-
        /GR-
        /Gy
        /Gw
        /GF
        /Zc:threadSafeInit-
    )

    if(SHADERLAB_USE_CRINKLER)
        set(SHADERLAB_TINY_CRINKLER_COMPMODE "FAST")
        if(DEFINED SHADERLAB_CRINKLER_COMPMODE AND NOT SHADERLAB_CRINKLER_COMPMODE STREQUAL "")
            set(SHADERLAB_TINY_CRINKLER_COMPMODE "${SHADERLAB_CRINKLER_COMPMODE}")
        endif()

        target_link_options(ShaderLabMicroPlayer PRIVATE
            /ENTRY:WinMain
            /INCREMENTAL:NO
            /MANIFEST:NO
            /RELEASE
            /COMPMODE:${SHADERLAB_TINY_CRINKLER_COMPMODE}
            /HASHSIZE:${SHADERLAB_CRINKLER_HASH_SIZE_MB}
            /HASHTRIES:${SHADERLAB_CRINKLER_HASH_TRIES}
            /ORDERTRIES:${SHADERLAB_CRINKLER_ORDER_TRIES}
            /UNALIGNCODE
        )
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            target_link_libraries(ShaderLabMicroPlayer PRIVATE
                msvcprt
                msvcrt
                legacy_stdio_definitions
                oldnames
            )
        elseif(NOT SHADERLAB_STATIC_RUNTIME)
            target_link_options(ShaderLabMicroPlayer PRIVATE
                /REPLACEDLL:api-ms-win-crt-conio-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-convert-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-environment-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-filesystem-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-heap-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-locale-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-math-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-process-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-runtime-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-stdio-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-string-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-time-l1-1-0=ucrtbase
                /REPLACEDLL:api-ms-win-crt-utility-l1-1-0=ucrtbase
            )
            target_link_libraries(ShaderLabMicroPlayer PRIVATE
                msvcprt
                msvcrt
                vcruntime
                ucrt
                legacy_stdio_definitions
                oldnames
            )
        endif()
        if(SHADERLAB_CRINKLER_TINYIMPORT)
            target_link_options(ShaderLabMicroPlayer PRIVATE /TINYIMPORT)
        endif()
    else()
        target_link_options(ShaderLabMicroPlayer PRIVATE
            /INCREMENTAL:NO
            /OPT:REF
            /OPT:ICF
            /LTCG
            /MANIFEST:NO
            /RELEASE
        )
    endif()
endif()
