cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(SHADERLAB_METADATA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/metadata/app_metadata.json")
if(NOT EXISTS "${SHADERLAB_METADATA_FILE}")
    message(FATAL_ERROR "Missing required metadata source: ${SHADERLAB_METADATA_FILE}")
endif()

file(READ "${SHADERLAB_METADATA_FILE}" SHADERLAB_METADATA_JSON)

function(shaderlab_metadata_get out_var key)
    string(JSON _value ERROR_VARIABLE _json_error GET "${SHADERLAB_METADATA_JSON}" "${key}")
    if(_json_error)
        message(FATAL_ERROR "Failed reading key '${key}' from ${SHADERLAB_METADATA_FILE}: ${_json_error}")
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

shaderlab_metadata_get(SHADERLAB_APP_VERSION version)
shaderlab_metadata_get(SHADERLAB_PRODUCT_NAME productName)
shaderlab_metadata_get(SHADERLAB_PUBLISHER publisher)
shaderlab_metadata_get(SHADERLAB_COMPANY_NAME companyName)
shaderlab_metadata_get(SHADERLAB_AUTHOR author)
shaderlab_metadata_get(SHADERLAB_DESCRIPTION description)
shaderlab_metadata_get(SHADERLAB_COPYRIGHT copyright)
shaderlab_metadata_get(SHADERLAB_WEBSITE website)

string(REPLACE "." ";" SHADERLAB_VERSION_COMPONENTS "${SHADERLAB_APP_VERSION}")
list(LENGTH SHADERLAB_VERSION_COMPONENTS SHADERLAB_VERSION_COMPONENT_COUNT)
if(NOT SHADERLAB_VERSION_COMPONENT_COUNT EQUAL 3)
    message(FATAL_ERROR "Expected semantic version X.Y.Z in metadata version field, got: ${SHADERLAB_APP_VERSION}")
endif()
list(GET SHADERLAB_VERSION_COMPONENTS 0 SHADERLAB_VER_MAJOR)
list(GET SHADERLAB_VERSION_COMPONENTS 1 SHADERLAB_VER_MINOR)
list(GET SHADERLAB_VERSION_COMPONENTS 2 SHADERLAB_VER_PATCH)
set(SHADERLAB_VER_BUILD 0)
set(SHADERLAB_APP_VERSION_DOTTED "${SHADERLAB_VER_MAJOR}.${SHADERLAB_VER_MINOR}.${SHADERLAB_VER_PATCH}.${SHADERLAB_VER_BUILD}")

set(SHADERLAB_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${SHADERLAB_GENERATED_INCLUDE_DIR}/ShaderLab/Generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppMetadata.h.in"
    "${SHADERLAB_GENERATED_INCLUDE_DIR}/ShaderLab/Generated/AppMetadata.h"
    @ONLY
)

project(ShaderLab VERSION ${SHADERLAB_APP_VERSION} LANGUAGES CXX)

function(shaderlab_add_version_resource target file_description internal_name original_filename)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET ${target})
        return()
    endif()

    set(SHADERLAB_FILE_DESCRIPTION "${file_description}")
    set(SHADERLAB_INTERNAL_NAME "${internal_name}")
    set(SHADERLAB_ORIGINAL_FILENAME "${original_filename}")
    set(_shaderlab_version_rc "${CMAKE_BINARY_DIR}/generated/rc/${target}.version.rc")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WinVersionInfo.rc.in"
        "${_shaderlab_version_rc}"
        @ONLY
    )
    target_sources(${target} PRIVATE "${_shaderlab_version_rc}")
endfunction()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build options
option(SHADERLAB_BUILD_EDITOR "Build the editor application" ON)
option(SHADERLAB_BUILD_RUNTIME "Build the standalone runtime" ON)
option(SHADERLAB_ENABLE_DXC "Enable DXC shader compiler (requires dxcompiler.dll)" ON)
option(SHADERLAB_STATIC_RUNTIME "Link MSVC runtime statically" OFF)
option(SHADERLAB_USE_CRINKLER "Use Crinkler as the linker (self-contained builds)" OFF)
option(SHADERLAB_CRINKLER_TINYIMPORT "Enable Crinkler /TINYIMPORT optimization" ON)
set(SHADERLAB_CRINKLER_HASH_SIZE_MB "64" CACHE STRING "Crinkler hash table size in MB")
set(SHADERLAB_CRINKLER_HASH_TRIES "1" CACHE STRING "Crinkler hash optimization tries")
set(SHADERLAB_CRINKLER_ORDER_TRIES "1" CACHE STRING "Crinkler code reordering tries")
option(SHADERLAB_RUNTIME_IMGUI "Enable ImGui debug overlay in runtime player" ON)
option(SHADERLAB_COMPACT_TRACK_DEBUG "Enable compact track runtime debug logs" OFF)
option(SHADERLAB_RUNTIME_DEBUG_LOG "Enable general runtime debug logs" OFF)
option(SHADERLAB_TINY_PLAYER "Enable ultra-minimal runtime profile (strips optional subsystems)" OFF)
option(SHADERLAB_BUILD_MICRO_PLAYER "Build ultra-minimal benchmark runtime target" OFF)
option(SHADERLAB_BUILD_ASM_MICRO "Build ASM MicroPlayer (x64 NASM DX12 experiment)" OFF)
option(SHADERLAB_TINY_RUNTIME_COMPILE "Enable runtime shader compilation in tiny player" ON)
option(SHADERLAB_TINY_TRACE "Enable lean tiny-player diagnostics via OutputDebugString" OFF)
option(SHADERLAB_TINY_DEV_OVERLAY "Enable tiny-player on-screen diagnostic overlay" OFF)
set(CRINKLER_PATH "" CACHE FILEPATH "Path to crinkler.exe")

# Platform check
if(NOT WIN32)
    message(FATAL_ERROR "ShaderLab currently supports Windows only")
endif()

if(MSVC AND SHADERLAB_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC AND SHADERLAB_USE_CRINKLER)
    if(CRINKLER_PATH)
        if(EXISTS "${CRINKLER_PATH}")
            set(CMAKE_LINKER "${CRINKLER_PATH}" CACHE FILEPATH "Crinkler linker" FORCE)
        else()
            message(WARNING "CRINKLER_PATH does not exist: ${CRINKLER_PATH}")
        endif()
    endif()
endif()

function(shaderlab_assert_target_has_no_sources target)
    if(NOT TARGET ${target})
        return()
    endif()

    get_target_property(_sources ${target} SOURCES)
    if(NOT _sources)
        return()
    endif()

    foreach(_source IN LISTS _sources)
        file(TO_CMAKE_PATH "${_source}" _source_norm)
        foreach(_forbidden IN LISTS ARGN)
            string(FIND "${_source_norm}" "${_forbidden}" _match)
            if(NOT _match EQUAL -1)
                message(FATAL_ERROR
                    "Layering violation: target '${target}' contains forbidden source '${_source_norm}' (matched '${_forbidden}').")
            endif()
        endforeach()
    endforeach()
endfunction()

# Third-party dependencies
add_subdirectory(third_party)

# Core modules
set(SHADERLAB_CORE_RUNTIME_SOURCES
    src/graphics/Device.cpp
    src/graphics/Swapchain.cpp
    src/graphics/CommandQueue.cpp
    src/graphics/PreviewRenderer.cpp
    src/graphics/EffectChainProcessor.cpp
    src/shader/ShaderCompiler.cpp
    src/audio/BeatClock.cpp
    src/core/Serializer.cpp
    src/core/PackageManager.cpp
    src/core/PlaybackService.cpp
    src/core/DxcCompilationService.cpp
    src/audio/AudioSystem.cpp
    src/graphics/Dx12ResourceService.cpp
)


set(SHADERLAB_COREAPI_HEADER_SOURCES
    include/ShaderLab/Graphics/Device.h
    include/ShaderLab/Graphics/Swapchain.h
    include/ShaderLab/Graphics/CommandQueue.h
    include/ShaderLab/Graphics/PreviewRenderer.h
    include/ShaderLab/Graphics/EffectChainProcessor.h
    include/ShaderLab/Graphics/GraphicsDeviceService.h
    include/ShaderLab/Graphics/ResourceService.h
    include/ShaderLab/Graphics/Dx12ResourceService.h
    include/ShaderLab/Shader/ShaderCompiler.h
    include/ShaderLab/Audio/AudioSystem.h
    include/ShaderLab/Audio/BeatClock.h
    include/ShaderLab/Core/CompilationService.h
    include/ShaderLab/Core/DxcCompilationService.h
    include/ShaderLab/Core/PlaybackService.h
    include/ShaderLab/Core/Serializer.h
    include/ShaderLab/Core/PackageManager.h
    include/ShaderLab/Core/ShaderLabData.h
)

set(SHADERLAB_PLAYER_RUNTIME_SOURCES
    src/app/runtime/DemoPlayer.cpp
    src/app/runtime/DemoPlayer.SceneGraph.cpp
    src/app/runtime/DemoPlayer.Frame.cpp
    src/app/runtime/DemoPlayer.Resources.cpp
    src/app/runtime/DemoPlayer.EffectChains.cpp
    src/app/runtime/PlayerApp.cpp
    src/app/runtime/RuntimeStartupPolicy.cpp
    src/app/runtime/RuntimeWindowPolicy.cpp
    include/ShaderLab/App/DemoPlayer.h
    include/ShaderLab/App/PlayerApp.h
    include/ShaderLab/Runtime/RuntimeStartupPolicy.h
    include/ShaderLab/Runtime/RuntimeWindowPolicy.h
)

set(SHADERLAB_DEVKIT_BUILDTOOLS_SOURCES
    src/core/BuildPipeline.cpp
    src/core/RuntimeExporter.cpp
    include/ShaderLab/DevKit/BuildPipeline.h
    include/ShaderLab/DevKit/RuntimeExporter.h
)

set(SHADERLAB_EDITORLIB_SOURCES
    src/ui/ShaderLabIDECore/SemanticColors.cpp
    src/ui/ShaderLabIDECore/ActionWidgets.cpp
    src/ui/ShaderLabIDECore/ShaderEditorView.cpp
    src/ui/Features/Diagnostics/DiagnosticsView.cpp
    src/ui/ShaderLabIDEView/DemoModeView/AudioLibraryView.cpp
    src/ui/ShaderLabIDEView/DemoModeView/DemoMetadataView.cpp
    src/ui/ShaderLabIDEView/DemoModeView/DemoPlaylistView.cpp
    src/ui/ShaderLabIDEView/DemoModeView/DefaultTrack.cpp
    src/ui/Features/RuntimeLog/RuntimeLogWindow.cpp
    src/ui/Features/Transport/TransportControlsView.cpp
    src/ui/Features/Transport/TransportTimeline.cpp
    src/ui/ShaderLabIDEView/SceneModeView/DefaultSceneView.cpp
    src/ui/ShaderLabIDEView/SceneModeView/FullscreenPreviewView.cpp
    src/ui/ShaderLabIDEView/Layout/BuildLayoutView.cpp
    src/ui/ShaderLabIDEView/SceneModeView/PostStackView.cpp
    src/ui/ShaderLabIDEView/SceneModeView/PreviewWindowView.cpp
    src/ui/Features/SnippetBin/SnippetBinView.cpp
    src/ui/Features/SnippetBin/ShaderLabIDE.Snippets.cpp
    src/ui/Features/Palette/PaletteGeneratorPanel.cpp
    src/ui/ShaderLabIDEView/SceneModeView/SceneListView.cpp
    src/ui/ShaderLabIDEView/SceneModeView/TexturesAndChannelsView.cpp
    src/ui/ShaderLabIDEView/PostFXModeView/PostFxLibraryWindow.cpp
    src/ui/ShaderLabIDEView/PostFXModeView/PostFxSourceWindow.cpp
    src/ui/ShaderLabIDEView/PostFXModeView/PostFxChainWindow.cpp
    src/ui/ShaderLabIDEView/PostFXModeView/PostEffectsWindowsView.cpp
    src/ui/Features/MainMenuBar/MainMenuBar.cpp
    src/ui/Features/Tuts/TutsFeature.cpp
    src/ui/Features/Theme/ThemeEditorWindow.cpp
    src/ui/Features/BuildSettings/BuildSettingsWindow.cpp
    src/ui/Features/BuildSettings/ShaderLabIDE.BuildSettingsPersistence.cpp
    src/ui/ShaderLabIDE.cpp
    src/ui/Helpers/ShaderLabIDE.UiHelpers.cpp
    src/ui/Helpers/ShaderLabIDE.OrchestratorState.cpp
    src/ui/Features/Theme/ShaderLabIDE.Theme.cpp
    src/ui/Features/Project/ShaderLabIDE.Project.cpp
    src/ui/Features/Project/ShaderLabIDE.ProjectState.cpp
    src/ui/Features/Render/ShaderLabIDE.Render.cpp
    src/ui/Features/Render/ShaderLabIDE.Render.SceneGraph.cpp
    src/ui/Features/Render/ShaderLabIDE.Render.Bindings.cpp
    src/ui/Features/Render/ShaderLabIDE.Render.PostFx.cpp
    src/ui/Features/Render/ShaderLabIDE.Render.Compute.cpp
    src/ui/Features/Render/ShaderLabIDE.Render.Preview.cpp
    src/ui/Features/Render/ShaderLabIDE.Frame.cpp
    src/ui/Features/Render/ShaderLabIDE.SceneCompile.cpp
    src/ui/Features/Render/ShaderLabIDE.TextureIO.cpp
    src/ui/Features/Render/ShaderLabIDE.TextureResources.cpp
    src/ui/Features/CodeEditor/ShaderLabIDE.CodeEditor.cpp
    src/ui/Features/CodeEditor/CodeThemes/CodeThemeFactory.cpp
    src/ui/Features/CodeEditor/CodeThemes/CodeThemeDarkDefault.cpp
    src/ui/Features/CodeEditor/CodeThemes/CodeThemeShaderPunk.cpp
    src/ui/ShaderLabIDE.Init.cpp
    src/ui/ShaderLabIDE.Editor.cpp
    src/ui/ShaderLabIDE.DemoMode.cpp
    src/ui/ShaderLabIDE.SceneMode.cpp
    src/ui/ShaderLabIDE.PostFXMode.cpp
    src/ui/UISystemAssets.cpp
    src/ui/AboutAssets.cpp
    src/ui/Features/About/AboutWindow.cpp
    include/ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h
    include/ShaderLab/UI/ShaderLabIDECore/SemanticColors.h
    third_party/ImGuiColorTextEdit/TextEditor.cpp
    include/ShaderLab/UI/UISystem.h
    third_party/ImGuiColorTextEdit/TextEditor.h
)

set(SHADERLAB_COMMON_INCLUDE_DIRS
    ${SHADERLAB_GENERATED_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuiColorTextEdit
)

add_library(ShaderLabCoreApi STATIC
    ${SHADERLAB_CORE_RUNTIME_SOURCES}
    ${SHADERLAB_COREAPI_HEADER_SOURCES}
)

target_include_directories(ShaderLabCoreApi PUBLIC
    ${SHADERLAB_COMMON_INCLUDE_DIRS}
)

target_include_directories(ShaderLabCoreApi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)

target_link_libraries(ShaderLabCoreApi PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
)

if(SHADERLAB_BUILD_EDITOR OR (SHADERLAB_BUILD_RUNTIME AND SHADERLAB_RUNTIME_IMGUI))
    target_link_libraries(ShaderLabCoreApi PUBLIC imgui)
endif()

add_library(ShaderLabDevKit STATIC ${SHADERLAB_PLAYER_RUNTIME_SOURCES})

target_include_directories(ShaderLabDevKit PUBLIC
    ${SHADERLAB_COMMON_INCLUDE_DIRS}
)

target_include_directories(ShaderLabDevKit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)

target_link_libraries(ShaderLabDevKit PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
)

if(SHADERLAB_BUILD_EDITOR OR (SHADERLAB_BUILD_RUNTIME AND SHADERLAB_RUNTIME_IMGUI))
    target_link_libraries(ShaderLabDevKit PUBLIC imgui)
endif()

if(SHADERLAB_RUNTIME_IMGUI)
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_RUNTIME_IMGUI=1)
else()
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_RUNTIME_IMGUI=0)
endif()

if(SHADERLAB_COMPACT_TRACK_DEBUG)
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_COMPACT_TRACK_DEBUG=1)
else()
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_COMPACT_TRACK_DEBUG=0)
endif()

if(SHADERLAB_RUNTIME_DEBUG_LOG)
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_RUNTIME_DEBUG_LOG=1)
else()
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_RUNTIME_DEBUG_LOG=0)
endif()

target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_TINY_PLAYER=0)

target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_TINY_RUNTIME_COMPILE=0)

target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_TINY_TRACE=0)

target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_TINY_DEV_OVERLAY=0)

if(SHADERLAB_ENABLE_DXC)
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_ENABLE_DXC=1)
else()
    target_compile_definitions(ShaderLabDevKit PUBLIC SHADERLAB_ENABLE_DXC=0)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(ShaderLabDevKit PUBLIC
        SHADERLAB_DEBUG=1
        SHADERLAB_VALIDATION=1
    )
endif()

if(MSVC)
    target_compile_options(ShaderLabDevKit PRIVATE
        /W4
        /WX-
        /permissive-
        /Zc:__cplusplus
    )
endif()

add_library(ShaderLabDevKitBuildTools STATIC ${SHADERLAB_DEVKIT_BUILDTOOLS_SOURCES})

target_include_directories(ShaderLabDevKitBuildTools PUBLIC
    ${SHADERLAB_COMMON_INCLUDE_DIRS}
)

target_include_directories(ShaderLabDevKitBuildTools PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)

target_link_libraries(ShaderLabDevKitBuildTools PUBLIC ShaderLabCoreApi)

if(SHADERLAB_BUILD_EDITOR)
    add_library(ShaderLabEditorLib STATIC ${SHADERLAB_EDITORLIB_SOURCES})

    target_include_directories(ShaderLabEditorLib PUBLIC
        ${SHADERLAB_COMMON_INCLUDE_DIRS}
    )

    target_include_directories(ShaderLabEditorLib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
    )

    target_link_libraries(ShaderLabEditorLib PUBLIC
        d3d12.lib
        dxgi.lib
        dxguid.lib
        d3dcompiler.lib
        imgui
    )
endif()

if(MSVC)
    set_source_files_properties(
        third_party/ImGuiColorTextEdit/TextEditor.cpp
        PROPERTIES
        COMPILE_OPTIONS "/wd4101;/wd4244"
    )
endif()

# Editor executable
if(SHADERLAB_BUILD_EDITOR)
    add_executable(ShaderLabIIDE WIN32
        src/app/ShaderLabMain/main.cpp
        src/app/ShaderLabMain/ShaderLabApp.cpp
        src/app/ShaderLabMain/ShaderLabApp.rc
        src/app/ShaderLabMain/ShaderLabApp.manifest
        include/ShaderLab/App/ShaderLabApp.h
    )
    
    target_link_libraries(ShaderLabIIDE PRIVATE ShaderLabEditorLib ShaderLabDevKitBuildTools ShaderLabCoreApi)
    
    target_include_directories(ShaderLabIIDE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    if(MSVC)
        set_target_properties(ShaderLabIIDE PROPERTIES
            PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>"
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>"
        )
    endif()

    shaderlab_add_version_resource(
        ShaderLabIIDE
        "ShaderLab Interactive IDE"
        "ShaderLabIIDE"
        "ShaderLabIIDE.exe"
    )
    
    # Show console window in debug builds for error messages
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(ShaderLabIIDE PRIVATE "/SUBSYSTEM:CONSOLE" "/ENTRY:WinMainCRTStartup")
    endif()

    # Deploy Development Kit (Code + CMake) for standalone builds
    add_custom_command(TARGET ShaderLabIIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/editor_assets $<TARGET_FILE_DIR:ShaderLabIIDE>/editor_assets

        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/App $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/App
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/Audio $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/Audio
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/Core $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/Core
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/DevKit $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/DevKit
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/Graphics $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/Graphics
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/Runtime $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/Runtime
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderLab/Shader $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/include/ShaderLab/Shader

        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/app/runtime $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src/app/runtime
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/app/tiny $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src/app/tiny
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/audio $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src/audio
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/core $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src/core
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src/graphics
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/shader $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/src/shader

        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMakeLists.txt $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party/CMakeLists.txt
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party/imgui
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party/json
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party/miniaudio
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party/stb
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Crinkler.exe $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/third_party/Crinkler.exe

        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/templates/Standalone_CMakeLists.txt $<TARGET_FILE_DIR:ShaderLabIIDE>/dev_kit/CMakeLists.txt
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ShaderLabIIDE>/editor_assets/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenFontIcons/OpenFontIcons.ttf
            $<TARGET_FILE_DIR:ShaderLabIIDE>/editor_assets/fonts/OpenFontIcons.ttf
    )

    # Copy DXC DLLs to editor output (required for shader compilation/build flow)
    if(SHADERLAB_ENABLE_DXC)
        set(SHADERLAB_DXC_BIN_DIR "" CACHE PATH "Directory containing dxcompiler.dll and dxil.dll")
        set(_shaderlab_dxc_bin_dir "${SHADERLAB_DXC_BIN_DIR}")

        if(NOT _shaderlab_dxc_bin_dir)
            file(GLOB _shaderlab_windows_sdk_bin_dirs "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
            if(_shaderlab_windows_sdk_bin_dirs)
                list(SORT _shaderlab_windows_sdk_bin_dirs ORDER DESCENDING)
                list(GET _shaderlab_windows_sdk_bin_dirs 0 _shaderlab_dxc_bin_dir)
            endif()
        endif()

        if(_shaderlab_dxc_bin_dir
           AND EXISTS "${_shaderlab_dxc_bin_dir}/dxcompiler.dll"
           AND EXISTS "${_shaderlab_dxc_bin_dir}/dxil.dll")
            add_custom_command(TARGET ShaderLabIIDE POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_shaderlab_dxc_bin_dir}/dxcompiler.dll"
                    $<TARGET_FILE_DIR:ShaderLabIIDE>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_shaderlab_dxc_bin_dir}/dxil.dll"
                    $<TARGET_FILE_DIR:ShaderLabIIDE>
            )
        else()
            message(WARNING "DXC runtime DLLs not found. Set SHADERLAB_DXC_BIN_DIR to a folder containing dxcompiler.dll and dxil.dll.")
        endif()
    endif()

endif()

if(SHADERLAB_BUILD_EDITOR)
    add_executable(ShaderLabBuildCli
        src/app/tools/build_cli.cpp
    )

    target_link_libraries(ShaderLabBuildCli PRIVATE ShaderLabDevKitBuildTools ShaderLabCoreApi)

    target_include_directories(ShaderLabBuildCli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    shaderlab_add_version_resource(
        ShaderLabBuildCli
        "ShaderLab Build CLI"
        "ShaderLabBuildCli"
        "ShaderLabBuildCli.exe"
    )
endif()

# Runtime executable
if(SHADERLAB_BUILD_RUNTIME)
    add_executable(ShaderLabPlayer WIN32
        src/app/runtime/main_player.cpp
        include/ShaderLab/App/PlayerApp.h
    )
    
    target_link_libraries(ShaderLabPlayer PRIVATE ShaderLabDevKit ShaderLabCoreApi)
    
    target_include_directories(ShaderLabPlayer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    shaderlab_add_version_resource(
        ShaderLabPlayer
        "ShaderLab Runtime Player"
        "ShaderLabPlayer"
        "ShaderLabPlayer.exe"
    )

    if(MSVC AND SHADERLAB_TINY_PLAYER)
        if(SHADERLAB_USE_CRINKLER)
            target_link_options(ShaderLabPlayer PRIVATE
                /INCREMENTAL:NO
                /MANIFEST:NO
                /RELEASE
                /COMPMODE:SLOW
                /ORDERTRIES:80
                /UNALIGNCODE
            )
        else()
            target_link_options(ShaderLabPlayer PRIVATE
                /INCREMENTAL:NO
                /OPT:REF
                /OPT:ICF
                /LTCG
                /MANIFEST:NO
                /RELEASE
            )
        endif()
    endif()

    add_executable(ShaderLabScreenSaver WIN32
        src/app/runtime/main_screensaver.cpp
        include/ShaderLab/App/PlayerApp.h
    )

    target_link_libraries(ShaderLabScreenSaver PRIVATE ShaderLabDevKit ShaderLabCoreApi)

    target_include_directories(ShaderLabScreenSaver PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    set_target_properties(ShaderLabScreenSaver PROPERTIES SUFFIX ".scr")

    shaderlab_add_version_resource(
        ShaderLabScreenSaver
        "ShaderLab Screen Saver"
        "ShaderLabScreenSaver"
        "ShaderLabScreenSaver.scr"
    )

    if(MSVC AND SHADERLAB_TINY_PLAYER)
        if(SHADERLAB_USE_CRINKLER)
            target_link_options(ShaderLabScreenSaver PRIVATE
                /INCREMENTAL:NO
                /MANIFEST:NO
                /RELEASE
                /COMPMODE:SLOW
                /ORDERTRIES:80
                /UNALIGNCODE
            )
        else()
            target_link_options(ShaderLabScreenSaver PRIVATE
                /INCREMENTAL:NO
                /OPT:REF
                /OPT:ICF
                /LTCG
                /MANIFEST:NO
                /RELEASE
            )
        endif()
    endif()
endif()

if(SHADERLAB_BUILD_MICRO_PLAYER)
    add_subdirectory(src/app/tiny)
endif()

if(SHADERLAB_BUILD_ASM_MICRO)
    add_subdirectory(src/app/asm_micro)
endif()

shaderlab_assert_target_has_no_sources(ShaderLabDevKit
    "src/core/BuildPipeline.cpp"
    "src/core/RuntimeExporter.cpp"
    "src/ui/")

shaderlab_assert_target_has_no_sources(ShaderLabDevKitBuildTools
    "src/app/runtime/"
    "src/ui/")

shaderlab_assert_target_has_no_sources(ShaderLabEditorLib
    "src/app/runtime/"
    "src/core/BuildPipeline.cpp"
    "src/core/RuntimeExporter.cpp"
    "src/graphics/"
    "src/shader/"
    "src/audio/")

shaderlab_assert_target_has_no_sources(ShaderLabIIDE
    "src/app/runtime/"
    "src/core/BuildPipeline.cpp"
    "src/core/RuntimeExporter.cpp"
    "src/graphics/"
    "src/shader/"
    "src/audio/")

shaderlab_assert_target_has_no_sources(ShaderLabPlayer
    "src/core/BuildPipeline.cpp"
    "src/core/RuntimeExporter.cpp"
    "src/ui/")

shaderlab_assert_target_has_no_sources(ShaderLabScreenSaver
    "src/core/BuildPipeline.cpp"
    "src/core/RuntimeExporter.cpp"
    "src/ui/")

shaderlab_assert_target_has_no_sources(ShaderLabMicroPlayer
    "src/core/BuildPipeline.cpp"
    "src/core/RuntimeExporter.cpp"
    "src/ui/")
