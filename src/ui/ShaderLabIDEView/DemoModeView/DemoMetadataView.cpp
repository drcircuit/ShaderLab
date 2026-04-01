#include "ShaderLab/UI/ShaderLabIDE.h"

#include <imgui.h>

#include <cstdio>

namespace ShaderLab {

void ShaderLabIDE::ShowDemoMetadata() {
    if (!ImGui::Begin("Demo: Metadata")) {
        ImGui::End();
        return;
    }

    char demoTitleBuffer[256];
    std::snprintf(demoTitleBuffer, sizeof(demoTitleBuffer), "%s", m_demoTitle.c_str());
    if (ImGui::InputText("Title", demoTitleBuffer, sizeof(demoTitleBuffer))) {
        m_demoTitle = demoTitleBuffer;
    }

    char demoAuthorBuffer[256];
    std::snprintf(demoAuthorBuffer, sizeof(demoAuthorBuffer), "%s", m_demoAuthor.c_str());
    if (ImGui::InputText("Author", demoAuthorBuffer, sizeof(demoAuthorBuffer))) {
        m_demoAuthor = demoAuthorBuffer;
    }

    char demoDescriptionBuffer[2048];
    std::snprintf(demoDescriptionBuffer, sizeof(demoDescriptionBuffer), "%s", m_demoDescription.c_str());
    const ImVec2 descriptionSize(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f);
    if (ImGui::InputTextMultiline("##DemoDescription", demoDescriptionBuffer, sizeof(demoDescriptionBuffer), descriptionSize)) {
        m_demoDescription = demoDescriptionBuffer;
    }
    if (demoDescriptionBuffer[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 hintPos = ImVec2(
            ImGui::GetItemRectMin().x + ImGui::GetStyle().FramePadding.x,
            ImGui::GetItemRectMin().y + ImGui::GetStyle().FramePadding.y);
        ImGui::GetWindowDrawList()->AddText(hintPos, ImGui::GetColorU32(m_uiThemeColors.StatusFontColor), "Description");
    }

    ImGui::End();
}

} // namespace ShaderLab
