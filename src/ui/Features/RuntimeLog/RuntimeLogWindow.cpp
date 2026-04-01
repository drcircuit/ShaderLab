#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/UISystemAssets.h"
#include "ShaderLab/UI/UISystemDemoUtils.h"
#include "ShaderLab/UI/OpenFontIcons.h"

#include <imgui.h>

namespace ShaderLab {

using EditorActionWidgets::LabeledActionButton;

void ShaderLabIDE::ShowDemoRuntimeLogWindow() {
    if (ImGui::Begin("Demo: Runtime Log")) {
        if (LabeledActionButton("ClearRuntimeLog", OpenFontIcons::kTrash2, "Clear Log", "Clear log")) {
            m_demoLog.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_demoLogAutoScroll);
        ImGui::Separator();

        if (m_transitionActive) {
            const double beatsPerSec = m_transport.bpm / 60.0f;
            const double exactBeat = m_transport.timeSeconds * beatsPerSec;
            const double fromTime = SceneTimeSeconds(exactBeat, m_transitionFromStartBeat, m_transitionFromOffset, m_transport.bpm);
            const double toTime = SceneTimeSeconds(exactBeat, m_transitionToStartBeat, m_transitionToOffset, m_transport.bpm);
            const std::string transitionLabel = GetTransitionDisplayNameByStem(m_currentTransitionStem);
            ImGui::Text("Transition: %s", transitionLabel.c_str());
            ImGui::TextUnformatted("A:");
            ImGui::SameLine();
            PushNumericFont();
            ImGui::Text("%d", m_transitionFromIndex);
            PopNumericFont();
            ImGui::SameLine();
            ImGui::TextUnformatted("time");
            ImGui::SameLine();
            PushNumericFont();
            ImGui::Text("%.2f", fromTime);
            PopNumericFont();

            ImGui::TextUnformatted("B:");
            ImGui::SameLine();
            PushNumericFont();
            ImGui::Text("%d", m_transitionToIndex);
            PopNumericFont();
            ImGui::SameLine();
            ImGui::TextUnformatted("time");
            ImGui::SameLine();
            PushNumericFont();
            ImGui::Text("%.2f", toTime);
            PopNumericFont();
            ImGui::Separator();
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, m_uiThemeColors.ConsoleBackground);
        ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.ConsoleFontColor);
        if (ImGui::BeginChild("RuntimeLog", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& line : m_demoLog) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (m_demoLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
} // namespace ShaderLab

}
