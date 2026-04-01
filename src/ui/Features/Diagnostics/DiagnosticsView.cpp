#include "ShaderLab/UI/ShaderLabIDE.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

namespace ShaderLab {

namespace {
int FindLikelyLineFromDiagnosticMessage(const std::string& message, const std::string& sourceText) {
    if (sourceText.empty() || message.empty()) {
        return -1;
    }

    auto findToken = [&](char quote) -> std::string {
        const size_t begin = message.find(quote);
        if (begin == std::string::npos) {
            return {};
        }
        const size_t end = message.find(quote, begin + 1);
        if (end == std::string::npos || end <= begin + 1) {
            return {};
        }
        return message.substr(begin + 1, end - begin - 1);
    };

    std::string token = findToken('\'');
    if (token.empty()) {
        token = findToken('"');
    }

    if (token.empty()) {
        return -1;
    }

    const size_t pos = sourceText.find(token);
    if (pos == std::string::npos) {
        return -1;
    }

    int line = 0;
    for (size_t i = 0; i < pos; ++i) {
        if (sourceText[i] == '\n') {
            ++line;
        }
    }
    return line;
}
}

void ShaderLabIDE::ShowDiagnostics() {
    // Only show if we have diagnostics or we want a persistent "Output" window
    if (ImGui::Begin("Diagnostics")) {
        if (m_shaderState.diagnostics.empty()) {
            ImGui::TextDisabled("No errors or warnings.");
            if (m_shaderState.status == CompileStatus::Success) {
                 ImGui::TextColored(m_uiThemeColors.ButtonIconColor, "Compilation Successful.");
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.ActivePanelTitleColor);
            PushNumericFont();
            ImGui::Text("%d", (int)m_shaderState.diagnostics.size());
            PopNumericFont();
            ImGui::SameLine();
            ImGui::TextUnformatted("Errors Found:");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##Diagnostics")) {
                std::ostringstream text;
                for (size_t i = 0; i < m_shaderState.diagnostics.size(); ++i) {
                    const auto& diag = m_shaderState.diagnostics[i];
                    text << "Line " << diag.line << ", Col " << diag.column << ": " << diag.message;
                    if (i + 1 < m_shaderState.diagnostics.size()) {
                        text << "\n";
                    }
                }
                ImGui::SetClipboardText(text.str().c_str());
            }
            ImGui::Separator();

            for (int i = 0; i < (int)m_shaderState.diagnostics.size(); ++i) {
                const auto& diag = m_shaderState.diagnostics[i];
                ImGui::PushID(i);

                // Selectable error to jump to line
                 char selectableLabel[64];
                 snprintf(selectableLabel, sizeof(selectableLabel), "##DiagSelect_%d", i);
                 const bool selected = ImGui::Selectable(selectableLabel, false, 0, ImVec2(-1.0f, 0.0f));

                 ImGui::SameLine(0.0f, 0.0f);
                 ImGui::TextUnformatted("Line ");
                 ImGui::SameLine(0.0f, 0.0f);
                 PushNumericFont();
                 ImGui::Text("%d", diag.line);
                 PopNumericFont();
                 ImGui::SameLine(0.0f, 0.0f);
                 ImGui::TextUnformatted(", Col ");
                 ImGui::SameLine(0.0f, 0.0f);
                 PushNumericFont();
                 ImGui::Text("%d", diag.column);
                 PopNumericFont();
                 ImGui::SameLine(0.0f, 0.0f);
                 ImGui::TextUnformatted(": ");
                 ImGui::SameLine(0.0f, 0.0f);
                 ImGui::TextUnformatted(diag.message.c_str());
                 if (selected) {
                     const int maxLine = (std::max)(0, m_textEditor.GetTotalLines() - 1);
                     int targetLine = (diag.line > 0) ? (diag.line - 1) : -1;
                     if (targetLine < 0) {
                         targetLine = FindLikelyLineFromDiagnosticMessage(diag.message, m_shaderState.text);
                     }
                     if (targetLine < 0) {
                         targetLine = 0;
                     }
                     const int clampedLine = (std::min)(targetLine, maxLine);
                     const int targetColumn = (diag.column > 0) ? (diag.column - 1) : 0;
                     TextEditor::Coordinates coord(clampedLine, targetColumn);
                     m_textEditor.SetCursorPosition(coord);
                     m_textEditor.SetSelection(coord, coord);
                 }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();

}

} // namespace ShaderLab
