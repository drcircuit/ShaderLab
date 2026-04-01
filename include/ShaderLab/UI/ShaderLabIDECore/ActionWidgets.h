#pragma once

#include <cstdint>

#include <imgui.h>

namespace ShaderLab::EditorActionWidgets {

bool LabeledActionButton(const char* id,
                         uint32_t iconCodepoint,
                         const char* label,
                         const char* tooltip,
                         const ImVec2& size = ImVec2(0.0f, 0.0f));

}
