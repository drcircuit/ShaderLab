#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/Core/CompilationService.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/UI/UISystemAssets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace ShaderLab {

namespace {
using EditorActionWidgets::LabeledActionButton;

std::string FormatByteSize(size_t bytes) {
    if (bytes == 0) return "0 B";
    static const char* units[] = { "B", "KB", "MB", "GB" };
    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 3) {
        value /= 1024.0;
        ++unitIndex;
    }
    char buffer[32] = {};
    if (unitIndex == 0) {
        std::snprintf(buffer, sizeof(buffer), "%zu %s", bytes, units[unitIndex]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unitIndex]);
    }
    return std::string(buffer);
}

std::string TrimLeadingWhitespace(const std::string& line) {
    size_t first = 0;
    while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first])) != 0) {
        ++first;
    }
    return line.substr(first);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    current.reserve(text.size());

    for (char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    lines.push_back(current);
    return lines;
}

std::string IndentShaderSource(const std::string& source, int tabSize) {
    const std::vector<std::string> lines = SplitLines(source);
    const int indentWidth = (std::max)(1, tabSize);
    int indentLevel = 0;
    std::string out;
    out.reserve(source.size() + lines.size() * 2);

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string trimmed = TrimLeadingWhitespace(lines[i]);

        if (!trimmed.empty()) {
            const bool startsWithCloseBrace = trimmed.front() == '}';
            const bool isPreprocessorLine = trimmed.front() == '#';

            if (startsWithCloseBrace) {
                indentLevel = (std::max)(0, indentLevel - 1);
            }

            const int lineIndentLevel = isPreprocessorLine ? 0 : indentLevel;
            out.append(static_cast<size_t>(lineIndentLevel * indentWidth), ' ');
            out += trimmed;

            bool inSingleLineComment = false;
            bool inString = false;
            bool inChar = false;
            bool escaped = false;
            for (size_t j = startsWithCloseBrace ? 1 : 0; j < trimmed.size(); ++j) {
                const char c = trimmed[j];
                const char n = (j + 1 < trimmed.size()) ? trimmed[j + 1] : '\0';

                if (!inString && !inChar) {
                    if (!inSingleLineComment && c == '/' && n == '/') {
                        inSingleLineComment = true;
                        break;
                    }
                    if (c == '"') {
                        inString = true;
                        escaped = false;
                        continue;
                    }
                    if (c == '\'') {
                        inChar = true;
                        escaped = false;
                        continue;
                    }
                    if (c == '{') {
                        ++indentLevel;
                    } else if (c == '}') {
                        indentLevel = (std::max)(0, indentLevel - 1);
                    }
                    continue;
                }

                if (escaped) {
                    escaped = false;
                    continue;
                }

                if (c == '\\') {
                    escaped = true;
                    continue;
                }

                if (inString && c == '"') {
                    inString = false;
                } else if (inChar && c == '\'') {
                    inChar = false;
                }
            }
        }

        if (i + 1 < lines.size()) {
            out.push_back('\n');
        }
    }

    return out;
}

}

ImFont* ShaderLabIDE::ResolveCodeEditorFont(int indexOffset) const {
    int codeFontIndex = static_cast<int>(m_shaderState.codeFontSize) + indexOffset;
    codeFontIndex = (std::max)(0, (std::min)(4, codeFontIndex));
    return m_fontCodeSizes[codeFontIndex] ? m_fontCodeSizes[codeFontIndex] : m_fontCode;
}

void ShaderLabIDE::SyncEditorCommentFont(TextEditor& editor, ImFont* font) {
    if (font) {
        editor.SetCommentFont(font, font->LegacySize);
    }
}

void ShaderLabIDE::ShowShaderEditorSearchReplaceBar() {
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_F)) {
        m_shaderState.showSearchReplace = !m_shaderState.showSearchReplace;
    }

    if (!m_shaderState.showSearchReplace) {
        return;
    }

    ImVec4 searchBarBg = m_uiThemeColors.ControlBackground;
    searchBarBg.w = (std::clamp)(m_uiThemeColors.ControlOpacity, 0.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, searchBarBg);
    ImGui::BeginChild("SearchBar", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 3.5f), true);

    ImGui::Text("Find:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-130);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText("##Search", m_shaderState.searchBuffer, sizeof(m_shaderState.searchBuffer));
    ImGui::SameLine();
    if (LabeledActionButton("FindNext", OpenFontIcons::kSearch, "Next", "Find next", ImVec2(120.0f, 0.0f))) {
        std::string searchStr = m_shaderState.searchBuffer;
        if (!searchStr.empty()) {
            std::string text = m_textEditor.GetText();
            auto cursor = m_textEditor.GetCursorPosition();

            auto lines = m_textEditor.GetTextLines();
            size_t textPos = 0;
            for (int i = 0; i < cursor.mLine && i < (int)lines.size(); i++) {
                textPos += lines[i].length() + 1;
            }
            textPos += cursor.mColumn;
            textPos += 1;

            size_t found = text.find(searchStr, textPos);
            if (found == std::string::npos) {
                found = text.find(searchStr, 0);
            }

            if (found != std::string::npos) {
                TextEditor::Coordinates newPos;
                size_t pos = 0;
                for (int line = 0; line < (int)lines.size(); line++) {
                    if (pos + lines[line].length() >= found) {
                        newPos.mLine = line;
                        newPos.mColumn = (int)(found - pos);
                        break;
                    }
                    pos += lines[line].length() + 1;
                }
                m_textEditor.SetCursorPosition(newPos);
                TextEditor::Coordinates endPos = newPos;
                endPos.mColumn += (int)searchStr.length();
                m_textEditor.SetSelection(newPos, endPos);
            }
        }
    }

    ImGui::Text("Replace:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-130);
    ImGui::InputText("##Replace", m_shaderState.replaceBuffer, sizeof(m_shaderState.replaceBuffer));
    ImGui::SameLine();
    if (LabeledActionButton("ReplaceAll", OpenFontIcons::kRefresh, "Replace All", "Replace all", ImVec2(120.0f, 0.0f))) {
        std::string search = m_shaderState.searchBuffer;
        std::string replace = m_shaderState.replaceBuffer;
        if (!search.empty()) {
            std::string text = m_textEditor.GetText();
            size_t pos = 0;
            int count = 0;
            while ((pos = text.find(search, pos)) != std::string::npos) {
                text.replace(pos, search.length(), replace);
                pos += replace.length();
                count++;
            }
            if (count > 0) {
                m_textEditor.SetText(text);
                m_shaderState.text = text;
                if (m_currentMode == UIMode::PostFX) {
                    if (m_postFxSelectedIndex >= 0 && m_postFxSelectedIndex < (int)m_postFxDraftChain.size()) {
                        auto& effect = m_postFxDraftChain[m_postFxSelectedIndex];
                        effect.shaderCode = text;
                        effect.isDirty = true;
                    } else if (m_computeEffectSelectedIndex >= 0 && m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) {
                        auto& effect = m_computeEffectDraftChain[m_computeEffectSelectedIndex];
                        effect.shaderCode = text;
                        effect.isDirty = true;
                    }
                } else if (m_editingSceneIndex >= 0 && m_editingSceneIndex < (int)m_scenes.size() &&
                           m_activeSceneIndex == m_editingSceneIndex) {
                    m_scenes[m_editingSceneIndex].shaderCode = text;
                }
                m_shaderState.status = CompileStatus::Dirty;
            }
        }
    }

    if (LabeledActionButton("CloseSearch", OpenFontIcons::kX, "Close", "Close search", ImVec2(120.0f, 0.0f))) {
        m_shaderState.showSearchReplace = false;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ShaderLabIDE::ShowShaderEditorStatusBar() {
    ImGui::Separator();
    auto cursorPos = m_textEditor.GetCursorPosition();
    ImGui::TextUnformatted("Lines:");
    ImGui::SameLine();
    PushNumericFont();
    ImGui::Text("%d", m_textEditor.GetTotalLines());
    PopNumericFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("| Ln");
    ImGui::SameLine();
    PushNumericFont();
    ImGui::Text("%d", cursorPos.mLine + 1);
    PopNumericFont();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(", Col");
    ImGui::SameLine();
    PushNumericFont();
    ImGui::Text("%d", cursorPos.mColumn + 1);
    PopNumericFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_textEditor.IsOverwrite() ? "Ovr" : "Ins");
}

void ShaderLabIDE::GetShaderEditorCompileStatusDisplay(const char*& statusText, ImVec4& statusColor) const {
    statusText = "Clean";
    statusColor = m_uiThemeColors.StatusFontColor;

    switch (m_shaderState.status) {
        case CompileStatus::Dirty:
            statusText = "Dirty";
            statusColor = m_uiThemeColors.TrackerAccentBeatFontColor;
            break;
        case CompileStatus::Compiling:
            statusText = "Compiling...";
            statusColor = m_uiThemeColors.IconColor;
            break;
        case CompileStatus::Success:
            statusText = "OK";
            statusColor = m_uiThemeColors.ButtonIconColor;
            break;
        case CompileStatus::Error:
            statusText = "Error";
            statusColor = m_uiThemeColors.ActivePanelTitleColor;
            break;
    }
}

size_t ShaderLabIDE::GetShaderEditorCompiledByteSize() const {
    if (m_currentMode == UIMode::PostFX) {
        if (m_postFxSelectedIndex >= 0 && m_postFxSelectedIndex < (int)m_postFxDraftChain.size()) {
            return m_postFxDraftChain[m_postFxSelectedIndex].compiledShaderBytes;
        }
        if (m_computeEffectSelectedIndex >= 0 && m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) {
            return m_computeEffectDraftChain[m_computeEffectSelectedIndex].compiledShaderBytes;
        }
        return 0;
    }

    if (m_editingSceneIndex >= 0 && m_editingSceneIndex < (int)m_scenes.size()) {
        return m_scenes[m_editingSceneIndex].compiledShaderBytes;
    }
    return 0;
}

void ShaderLabIDE::ShowShaderEditorCompiledByteSize() const {
    if (m_shaderState.status != CompileStatus::Success) {
        return;
    }

    const size_t compiledBytes = GetShaderEditorCompiledByteSize();
    if (compiledBytes > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", FormatByteSize(compiledBytes).c_str());
    }
}

void ShaderLabIDE::ApplyShaderEditorTheme(EditorTheme theme) {
    auto ApplyPalette = [&](const TextEditor::Palette& palette) {
        m_textEditor.SetPalette(palette);
        m_snippetTextEditor.SetPalette(palette);
        m_tutCodeTextEditor.SetPalette(palette);
        m_ubershaderTextEditor.SetPalette(palette);
    };

    TextEditor::Palette palette;
    switch (theme) {
        case EditorTheme::Dark:
            palette = TextEditor::GetDarkPalette();
            palette[(int)TextEditor::PaletteIndex::Keyword] = 0xffd69c56;
            palette[(int)TextEditor::PaletteIndex::KnownIdentifier] = 0xffb0c94e;
            palette[(int)TextEditor::PaletteIndex::Number] = 0xffa8ceb5;
            palette[(int)TextEditor::PaletteIndex::String] = 0xff7891ce;
            palette[(int)TextEditor::PaletteIndex::Comment] = 0xff55996a;
            palette[(int)TextEditor::PaletteIndex::MultiLineComment] = 0xff55996a;
            palette[(int)TextEditor::PaletteIndex::Identifier] = 0xffdcdcdc;
            palette[(int)TextEditor::PaletteIndex::Punctuation] = 0xffdcdcdc;
            palette[(int)TextEditor::PaletteIndex::Preprocessor] = 0xff9b9b9b;
            ApplyPalette(palette);
            break;
        case EditorTheme::DarkOriginal:
            ApplyPalette(TextEditor::GetDarkPalette());
            break;
        case EditorTheme::Light:
            ApplyPalette(TextEditor::GetLightPalette());
            break;
        case EditorTheme::RetroBlue:
            ApplyPalette(TextEditor::GetRetroBluePalette());
            break;
    }

    ApplyCodeEditorControlOpacity();
}

void ShaderLabIDE::ShowShaderEditorThemeAndFontControls() {
    const float themeWidth = 340.0f;
    const float fontSizeWidth = 110.0f;
    const float rightPad = 10.0f;
    const float totalControlsWidth = themeWidth + fontSizeWidth + ImGui::GetStyle().ItemSpacing.x;
    const float desiredX = ImGui::GetWindowWidth() - totalControlsWidth - rightPad;
    if (ImGui::GetCursorPosX() > desiredX) {
        ImGui::NewLine();
    } else {
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), desiredX));

    ImGui::SetNextItemWidth(themeWidth);
    const char* themeNames[] = { "Dark (Enhanced)", "Dark", "Light", "Retro Blue" };
    int currentTheme = (int)m_shaderState.theme;
    if (ImGui::Combo("##Theme", &currentTheme, themeNames, 4)) {
        m_shaderState.theme = (EditorTheme)currentTheme;
        ApplyShaderEditorTheme(m_shaderState.theme);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(fontSizeWidth);
    const char* codeSizeNames[] = { "XS", "S", "M", "L", "XL" };
    int currentCodeFontSize = (int)m_shaderState.codeFontSize;
    if (ImGui::Combo("##CodeFontSize", &currentCodeFontSize, codeSizeNames, 5)) {
        currentCodeFontSize = (std::max)(0, (std::min)(4, currentCodeFontSize));
        m_shaderState.codeFontSize = (CodeFontSize)currentCodeFontSize;
    }
}

void ShaderLabIDE::CompileShaderEditorSelection() {
    if (!m_compilationService) {
        return;
    }

    m_shaderState.status = CompileStatus::Compiling;
    m_shaderState.diagnostics.clear();

    if (m_currentMode == UIMode::PostFX) {
        if (m_postFxSelectedIndex >= 0 && m_postFxSelectedIndex < (int)m_postFxDraftChain.size()) {
            auto& selected = m_postFxDraftChain[m_postFxSelectedIndex];
            selected.shaderCode = m_shaderState.text;

            bool anyErrors = false;
            if (m_postFxSourceSceneIndex >= 0 && m_postFxSourceSceneIndex < (int)m_scenes.size()) {
                if (m_scenes[m_postFxSourceSceneIndex].isDirty ||
                    !m_scenes[m_postFxSourceSceneIndex].pipelineState) {
                    if (!CompileScene(m_postFxSourceSceneIndex)) {
                        anyErrors = true;
                        Diagnostic diag;
                        diag.message = "Source Scene: compilation failed.";
                        m_shaderState.diagnostics.push_back(diag);
                    }
                }
            }

            for (auto& effect : m_postFxDraftChain) {
                if (!effect.pipelineState || effect.isDirty) {
                    std::vector<std::string> fxErrors;
                    if (!CompilePostFxEffect(effect, fxErrors)) {
                        anyErrors = true;
                        for (const auto& error : fxErrors) {
                            Diagnostic diag;
                            diag.message = effect.name + ": " + error;
                            m_shaderState.diagnostics.push_back(diag);
                        }
                    }
                }
            }

            if (anyErrors) {
                m_shaderState.status = CompileStatus::Error;
                m_playbackBlockedByCompileError = true;
            } else {
                m_shaderState.status = CompileStatus::Success;
                m_shaderState.lastCompiledText = m_shaderState.text;
                m_playbackBlockedByCompileError = false;
            }
        } else if (m_computeEffectSelectedIndex >= 0 && m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) {
            auto& selected = m_computeEffectDraftChain[m_computeEffectSelectedIndex];
            selected.shaderCode = m_shaderState.text;

            std::vector<Diagnostic> computeDiagnostics;
            const bool success = CompileComputeEffect(selected, computeDiagnostics);
            m_shaderState.diagnostics = std::move(computeDiagnostics);

            if (success) {
                m_shaderState.status = CompileStatus::Success;
                m_shaderState.lastCompiledText = m_shaderState.text;
                m_playbackBlockedByCompileError = false;
            } else {
                m_shaderState.status = CompileStatus::Error;
                if (m_shaderState.diagnostics.empty()) {
                    Diagnostic diag;
                    diag.message = "Compute effect: compilation failed.";
                    m_shaderState.diagnostics.push_back(diag);
                }
                m_playbackBlockedByCompileError = true;
            }
        } else {
            m_shaderState.status = CompileStatus::Error;
            Diagnostic diag;
            diag.message = "No effect selected.";
            m_shaderState.diagnostics.push_back(diag);
        }
    } else {
        if (m_editingSceneIndex < 0 || m_editingSceneIndex >= (int)m_scenes.size()) {
            m_shaderState.status = CompileStatus::Error;
            Diagnostic diag;
            diag.message = "No edited scene selected.";
            m_shaderState.diagnostics.push_back(diag);
            m_playbackBlockedByCompileError = true;
            RefreshPresetService();
            return;
        }

        if (m_currentMode == UIMode::Scene && m_activeSceneIndex != m_editingSceneIndex) {
            m_shaderState.status = CompileStatus::Error;
            Diagnostic diag;
            diag.message = "Active scene changed while editing. Re-select the scene to continue.";
            m_shaderState.diagnostics.push_back(diag);
            m_playbackBlockedByCompileError = true;
            RefreshPresetService();
            return;
        }

        m_scenes[m_editingSceneIndex].shaderCode = m_shaderState.text;
        m_scenes[m_editingSceneIndex].isDirty = true;

        if (CompileScene(m_editingSceneIndex)) {
            m_shaderState.status = CompileStatus::Success;
            m_shaderState.lastCompiledText = m_shaderState.text;
            m_playbackBlockedByCompileError = false;
        } else {
            if (m_editingSceneIndex >= 0 && m_editingSceneIndex < (int)m_scenes.size()) {
                m_scenes[m_editingSceneIndex].compiledShaderBytes = 0;
            } else {
                Diagnostic diag;
                diag.message = "No active scene selected.";
                m_shaderState.diagnostics.push_back(diag);
            }
            m_shaderState.status = CompileStatus::Error;
            m_playbackBlockedByCompileError = true;
        }
    }

    RefreshPresetService();
}

bool ShaderLabIDE::CompileComputeEffect(Scene::ComputeEffect& effect, std::vector<Diagnostic>& outDiagnostics) {
    outDiagnostics.clear();
    if (!m_compilationService) {
        Diagnostic diag;
        diag.message = "Compilation service unavailable.";
        outDiagnostics.push_back(diag);
        return false;
    }

    const std::string entryPoint = effect.entryPoint.empty() ? "main" : effect.entryPoint;
    const ShaderCompileResult compileResult = m_compilationService->CompileFromSource(
        effect.shaderCode,
        entryPoint,
        "cs_6_0",
        L"compute.hlsl",
        ShaderCompileMode::Build,
        {});

    for (const auto& diagnostic : compileResult.diagnostics) {
        Diagnostic diag;
        diag.line = static_cast<int>(diagnostic.line);
        diag.column = static_cast<int>(diagnostic.column);
        diag.message = diagnostic.message;
        outDiagnostics.push_back(diag);
    }

    if (compileResult.success) {
        effect.compiledShaderBytes = compileResult.bytecode.size();
        effect.lastCompiledCode = effect.shaderCode;
        if (!CompileComputePipeline(effect)) {
            Diagnostic diag;
            diag.message = "Compute shader compiled, but pipeline creation failed.";
            outDiagnostics.push_back(diag);
            return false;
        }
        return true;
    }

    effect.compiledShaderBytes = 0;
    return false;
}

void ShaderLabIDE::ShowShaderEditorHeader(bool editorFocused, bool ctrlEnterPressed) {
    const char* statusText = nullptr;
    ImVec4 statusColor{};
    GetShaderEditorCompileStatusDisplay(statusText, statusColor);

    ImFont* headerFont = m_fontMenuSmall
        ? m_fontMenuSmall
        : (m_fontOrbitronText ? m_fontOrbitronText : ImGui::GetFont());
    if (headerFont) {
        const float headerFontSize = (std::max)(9.0f, headerFont->LegacySize - 1.0f);
        ImGui::PushFont(headerFont, headerFontSize);
    }

    if (LabeledActionButton("CompileShader", OpenFontIcons::kPlay, "Compile", "Compile (Ctrl+Enter / F7) | Auto-indent (Alt+Shift+F)", ImVec2(150.0f, 0.0f)) ||
        ctrlEnterPressed ||
        (editorFocused && ImGui::IsKeyPressed(ImGuiKey_F7))) {
        CompileShaderEditorSelection();
        GetShaderEditorCompileStatusDisplay(statusText, statusColor);
    }
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(statusColor, "%s", statusText);
    ShowShaderEditorCompiledByteSize();
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("Auto-indent: Alt+Shift+F");
    ShowShaderEditorThemeAndFontControls();

    if (headerFont) {
        ImGui::PopFont();
    }

    ImGui::Separator();
}

void ShaderLabIDE::ShowShaderEditorBody(bool editorFocused, bool ctrlEnterPressed) {
    float statusBarHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const bool textChangedThisFrame = m_textEditor.IsTextChanged();
    std::string latestEditorText;

    const bool editorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    static UIMode s_lastMode = UIMode::Demo;
    static int s_lastEditingSceneIndex = -1;
    static int s_lastPostFxSelectedIndex = -1;
    static int s_lastComputeEffectSelectedIndex = -1;
    const bool editorContextChanged =
        (s_lastMode != m_currentMode) ||
        (s_lastEditingSceneIndex != m_editingSceneIndex) ||
        (s_lastPostFxSelectedIndex != m_postFxSelectedIndex) ||
        (s_lastComputeEffectSelectedIndex != m_computeEffectSelectedIndex);

    s_lastMode = m_currentMode;
    s_lastEditingSceneIndex = m_editingSceneIndex;
    s_lastPostFxSelectedIndex = m_postFxSelectedIndex;
    s_lastComputeEffectSelectedIndex = m_computeEffectSelectedIndex;

    if (!editorFocused && !editorHovered && editorContextChanged) {
        m_textEditor.SetText(m_shaderState.text);
    }

    const bool suppressKeyboardInputs = ctrlEnterPressed;
    m_textEditor.SetHandleKeyboardInputs(!suppressKeyboardInputs);
    m_textEditor.SetHandleMouseInputs(true);
    ImFont* activeCodeFont = ResolveCodeEditorFont();
    SyncEditorCommentFont(m_textEditor, activeCodeFont);
    if (activeCodeFont) {
        ImGui::PushFont(activeCodeFont);
    }
    m_textEditor.Render("##ShaderCode", ImVec2(-1, -statusBarHeight), true);
    if (activeCodeFont) {
        ImGui::PopFont();
    }
    if (suppressKeyboardInputs) {
        m_textEditor.SetHandleKeyboardInputs(true);
    }

    if (textChangedThisFrame) {
        if (latestEditorText.empty()) {
            latestEditorText = m_textEditor.GetText();
        }
        m_shaderState.text = latestEditorText;
        if (m_currentMode == UIMode::PostFX) {
            if (m_postFxSelectedIndex >= 0 && m_postFxSelectedIndex < (int)m_postFxDraftChain.size()) {
                auto& effect = m_postFxDraftChain[m_postFxSelectedIndex];
                effect.shaderCode = m_shaderState.text;
                effect.isDirty = true;
            } else if (m_computeEffectSelectedIndex >= 0 && m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) {
                auto& effect = m_computeEffectDraftChain[m_computeEffectSelectedIndex];
                effect.shaderCode = m_shaderState.text;
                effect.isDirty = true;
            }
        } else {
            if (m_editingSceneIndex >= 0 && m_editingSceneIndex < (int)m_scenes.size() &&
                (m_currentMode != UIMode::Scene || m_activeSceneIndex == m_editingSceneIndex)) {
                m_scenes[m_editingSceneIndex].shaderCode = m_shaderState.text;
                m_scenes[m_editingSceneIndex].isDirty = true;
            }
        }
        if (m_shaderState.text != m_shaderState.lastCompiledText) {
            m_shaderState.status = CompileStatus::Dirty;
        }
    }
}

void ShaderLabIDE::ShowShaderEditor() {
    if (ImGui::Begin("Shader Editor")) {
        const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool ctrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        const bool shiftDown = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        const bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        const bool ctrlEnterPressed = editorFocused && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_Enter);
        const bool autoFormatPressed = editorFocused && altDown && shiftDown && !ctrlDown && ImGui::IsKeyPressed(ImGuiKey_F, false);

        if (autoFormatPressed) {
            const std::string formatted = IndentShaderSource(m_shaderState.text, m_textEditor.GetTabSize());
            if (formatted != m_shaderState.text) {
                m_shaderState.text = formatted;
                m_textEditor.SetText(formatted);

                if (m_currentMode == UIMode::PostFX) {
                    if (m_postFxSelectedIndex >= 0 && m_postFxSelectedIndex < (int)m_postFxDraftChain.size()) {
                        auto& effect = m_postFxDraftChain[m_postFxSelectedIndex];
                        effect.shaderCode = formatted;
                        effect.isDirty = true;
                    } else if (m_computeEffectSelectedIndex >= 0 && m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) {
                        auto& effect = m_computeEffectDraftChain[m_computeEffectSelectedIndex];
                        effect.shaderCode = formatted;
                        effect.isDirty = true;
                    }
                } else if (m_editingSceneIndex >= 0 && m_editingSceneIndex < (int)m_scenes.size() &&
                           (m_currentMode != UIMode::Scene || m_activeSceneIndex == m_editingSceneIndex)) {
                    m_scenes[m_editingSceneIndex].shaderCode = formatted;
                    m_scenes[m_editingSceneIndex].isDirty = true;
                }

                if (m_shaderState.text != m_shaderState.lastCompiledText) {
                    m_shaderState.status = CompileStatus::Dirty;
                }
            }
        }

        ShowShaderEditorHeader(editorFocused, ctrlEnterPressed);

        ImFont* compactFont = m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)];
        if (compactFont) {
            ImGui::PushFont(compactFont);
        }

        const bool editingCompute =
            (m_currentMode == UIMode::PostFX) &&
            (m_computeEffectSelectedIndex >= 0) &&
            (m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) &&
            (m_postFxSelectedIndex < 0 || m_postFxSelectedIndex >= (int)m_postFxDraftChain.size());

        static bool showAvailableUniforms = false;
        ImGui::SetNextItemOpen(showAvailableUniforms, ImGuiCond_Always);
        const bool uniformsOpen = ImGui::CollapsingHeader("Available Uniforms", ImGuiTreeNodeFlags_None);
        showAvailableUniforms = uniformsOpen;

        if (uniformsOpen) {
            if (editingCompute) {
                ImGui::TextUnformatted("Compute uniforms / resources:");
                ImGui::BulletText("param0, param1, param2, param3");
                ImGui::BulletText("time, invWidth, invHeight, frame");
                ImGui::BulletText("inputTexture (t0)");
                ImGui::BulletText("historyTextureN (t1..t8, if history is used)");
                ImGui::BulletText("outputTexture (u0)");
            } else {
                ImGui::TextUnformatted("Scene/PostFX uniforms:");
                ImGui::BulletText("iResolution : float2");
                ImGui::BulletText("iTime : float");
                ImGui::TextDisabled("Note: iResolution/iTime are engine constants and are also forwarded to main(fragCoord, iResolution, iTime) for compatibility.");
                ImGui::BulletText("iBeat, iBar : float");
                ImGui::BulletText("fBeat, fBarBeat, fBarBeat16 : float");
                ImGui::BulletText("iChannel0..iChannel7 : texture inputs");
                ImGui::BulletText("iSampler0 : sampler state");
            }
            ImGui::Separator();
        }

        if (compactFont) {
            ImGui::PopFont();
        }

        ShowShaderEditorSearchReplaceBar();
        ShowShaderEditorBody(editorFocused, ctrlEnterPressed);

        ShowShaderEditorStatusBar();

    }
    ImGui::End();
}

}