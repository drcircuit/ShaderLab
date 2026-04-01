#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"

#include <imgui_internal.h>

#include <cmath>
#include <string>

namespace ShaderLab::EditorActionWidgets {

namespace {

float GetUiScale() {
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    float scale = viewport ? viewport->DpiScale : ImGui::GetIO().DisplayFramebufferScale.y;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    return scale;
}

}

bool LabeledActionButton(const char* id,
                         uint32_t iconCodepoint,
                         const char* label,
                         const char* tooltip,
                         const ImVec2& size) {
    const std::string icon = OpenFontIcons::ToUtf8(iconCodepoint);
    const std::string buttonId = std::string("##") + id;
    const char* safeLabel = (label && *label) ? label : "";

    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImGuiStyle& style = ImGui::GetStyle();

    const ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
    const ImVec2 labelSize = ImGui::CalcTextSize(safeLabel);
    const float gap = safeLabel[0] ? style.ItemInnerSpacing.x : 0.0f;

    float iconAdvance = iconSize.x;
    if (font) {
        if (ImFontBaked* baked = font->GetFontBaked(fontSize)) {
            if (ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(iconCodepoint))) {
                if (glyph->AdvanceX > 0.0f) {
                    iconAdvance = glyph->AdvanceX;
                }
            }
        }
    }

    const float iconSlotWidth = (std::max)(iconAdvance, style.FramePadding.x * 2.0f + iconSize.x);
    const float contentWidth = iconSlotWidth + gap + labelSize.x;
    const float minButtonWidth = contentWidth + style.FramePadding.x * 2.0f;

    ImVec2 buttonSize = size;
    if (buttonSize.x <= 0.0f) {
        buttonSize.x = (std::max)(ImGui::CalcItemWidth(), minButtonWidth);
    } else {
        buttonSize.x = (std::max)(buttonSize.x, minButtonWidth);
    }
    if (buttonSize.y <= 0.0f) {
        buttonSize.y = ImGui::GetFrameHeight();
    }

    const bool pressed = ImGui::InvisibleButton(buttonId.c_str(), buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
    drawList->AddRectFilled(min, max, bg, style.FrameRounding);
    if (style.FrameBorderSize > 0.0f) {
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }
    const float uiScale = GetUiScale();
    const float baseX = min.x + (buttonSize.x - contentWidth) * 0.5f - (6.0f * uiScale);
    float iconX = baseX;
    float iconY = min.y + (buttonSize.y - iconSize.y) * 0.5f;
    
    if (font) {
        if (ImFontBaked* baked = font->GetFontBaked(fontSize)) {
            if (ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(iconCodepoint))) {
                const float glyphW = glyph->X1 - glyph->X0;
                const float glyphH = glyph->Y1 - glyph->Y0;
                iconX = baseX + (iconSlotWidth - glyphW) * 0.5f - glyph->X0;
                iconY = min.y + (buttonSize.y - glyphH) * 0.5f - glyph->Y0;
            }
        }
    }
    const float labelY = min.y + (buttonSize.y - labelSize.y) * 0.5f;

    drawList->AddText(font, fontSize, ImVec2(std::floor(iconX), std::floor(iconY)), ImGui::GetColorU32(ImGuiCol_CheckMark), icon.c_str());
    if (safeLabel[0]) {
        drawList->AddText(ImVec2(std::floor(baseX + iconSlotWidth + gap), std::floor(labelY)), ImGui::GetColorU32(ImGuiCol_TextLink), safeLabel);
    }

    if (ImGui::IsItemHovered() && tooltip && *tooltip) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

}
