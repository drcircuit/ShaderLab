#include <algorithm>

#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/SemanticColors.h"

#include <imgui.h>

namespace ShaderLab {

void ShaderLabIDE::PushNumericFont() {
    if (m_fontErbosDracoNumbers) {
        ImGui::PushFont(m_fontErbosDracoNumbers);
    }
}

void ShaderLabIDE::PopNumericFont() {
    if (m_fontErbosDracoNumbers) {
        ImGui::PopFont();
    }
}

float ShaderLabIDE::GetNumericFieldMinWidth() const {
    ImGuiStyle& style = ImGui::GetStyle();
    float width = 0.0f;
    if (m_fontErbosDracoNumbers) {
        ImGui::PushFont(m_fontErbosDracoNumbers);
    }
    width = ImGui::CalcTextSize("000.0").x + style.FramePadding.x * 2.0f;
    if (m_fontErbosDracoNumbers) {
        ImGui::PopFont();
    }
    return width;
}

void ShaderLabIDE::SetNextNumericFieldWidth(float requestedWidth) {
    const float minWidth = GetNumericFieldMinWidth();
    const float width = (requestedWidth > 0.0f) ? (std::max)(requestedWidth, minWidth) : minWidth;
    ImGui::SetNextItemWidth(width);
}

ImVec4 ShaderLabIDE::GetSemanticSuccessColor() const {
    return EditorCore::SemanticSuccessColor();
}

ImVec4 ShaderLabIDE::GetSemanticWarningColor() const {
    return EditorCore::SemanticWarningColor();
}

ImVec4 ShaderLabIDE::GetSemanticErrorColor() const {
    return EditorCore::SemanticErrorColor();
}

ImVec4 ShaderLabIDE::GetSemanticInfoColor() const {
    return EditorCore::SemanticInfoColor();
}

} // namespace ShaderLab
