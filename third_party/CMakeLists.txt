# Dear ImGui CMake integration

cmake_minimum_required(VERSION 3.20)

# Check if ImGui exists
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.h)
    message(WARNING "ImGui not found in third_party/imgui/. Please download from https://github.com/ocornut/imgui")
    message(WARNING "Build will fail without ImGui.")
endif()

# ImGui library
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_win32.cpp
    imgui/backends/imgui_impl_dx12.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

# Link Windows libraries for ImGui
target_link_libraries(imgui PUBLIC
    d3d12.lib
    dxgi.lib
)
