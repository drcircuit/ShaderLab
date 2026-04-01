#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/UI/UISystemAssets.h"
#include "ShaderLab/Audio/AudioSystem.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ShaderLab {

namespace {

float GetUiScale() {
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    float scale = viewport ? viewport->DpiScale : ImGui::GetIO().DisplayFramebufferScale.y;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    return scale;
}

bool IconButton(const char* id, uint32_t iconCodepoint, const char* tooltip, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    const std::string icon = OpenFontIcons::ToUtf8(iconCodepoint);
    const std::string buttonId = std::string("##") + id;

    ImVec2 buttonSize = size;
    if (buttonSize.x <= 0.0f || buttonSize.y <= 0.0f) {
        const ImVec2 textSize = ImGui::CalcTextSize(icon.c_str());
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        if (buttonSize.x <= 0.0f) buttonSize.x = textSize.x + pad.x * 2.0f;
        if (buttonSize.y <= 0.0f) buttonSize.y = textSize.y + pad.y * 2.0f;
    }

    const bool pressed = ImGui::InvisibleButton(buttonId.c_str(), buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();

    const ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
    drawList->AddRectFilled(min, max, bg, style.FrameRounding);
    if (style.FrameBorderSize > 0.0f) {
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }

    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const bool isSpinnerStep = (iconCodepoint == OpenFontIcons::kPlus || iconCodepoint == OpenFontIcons::kMinus);
    const float uiScale = GetUiScale();

    if (isSpinnerStep) {
        float textX = min.x;
        float textY = min.y;
        bool usedGlyphBounds = false;
        if (font) {
            if (ImFontBaked* baked = font->GetFontBaked(fontSize)) {
                if (ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(iconCodepoint))) {
                    const float glyphH = glyph->Y1 - glyph->Y0;
                    const float advance = glyph->AdvanceX > 0.0f ? glyph->AdvanceX : (glyph->X1 - glyph->X0);
                    textX = min.x + (buttonSize.x - advance) * 0.5f;
                    textY = min.y + (buttonSize.y - glyphH) * 0.5f - glyph->Y0;
                    textX -= 0.4f * uiScale;
                    textY += 1.2f * uiScale;
                    if (iconCodepoint == OpenFontIcons::kMinus) {
                        textY += 2.4f * uiScale;
                    }
                    usedGlyphBounds = true;
                }
            }
        }
        if (!usedGlyphBounds) {
            const ImVec2 textSize = ImGui::CalcTextSize(icon.c_str());
            textX = min.x + (buttonSize.x - textSize.x) * 0.5f;
            textY = min.y + (buttonSize.y - textSize.y) * 0.5f;
            textX -= 0.4f * uiScale;
            textY += 0.4f * uiScale;
            if (iconCodepoint == OpenFontIcons::kMinus) {
                textY += 1.2f * uiScale;
            }
        }
        drawList->AddText(font, fontSize, ImVec2(std::floor(textX), std::floor(textY)), ImGui::GetColorU32(ImGuiCol_CheckMark), icon.c_str());
    } else {
        float textX = min.x;
        float textY = min.y;
        bool usedGlyphBounds = false;
        if (font) {
            if (ImFontBaked* baked = font->GetFontBaked(fontSize)) {
                if (ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(iconCodepoint))) {
                    const float glyphW = glyph->X1 - glyph->X0;
                    const float glyphH = glyph->Y1 - glyph->Y0;
                    textX = min.x + (buttonSize.x - glyphW) * 0.5f - glyph->X0;
                    textY = min.y + (buttonSize.y - glyphH) * 0.5f - glyph->Y0;
                    usedGlyphBounds = true;
                }
            }
        }
        if (!usedGlyphBounds) {
            const ImVec2 textSize = ImGui::CalcTextSize(icon.c_str());
            textX = min.x + (buttonSize.x - textSize.x) * 0.5f;
            textY = min.y + (buttonSize.y - textSize.y) * 0.5f;
        }
        drawList->AddText(font, fontSize, ImVec2(std::floor(textX), std::floor(textY)), ImGui::GetColorU32(ImGuiCol_CheckMark), icon.c_str());
    }

    if (ImGui::IsItemHovered() && tooltip && *tooltip) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

ImVec2 CompactIconSquareSize() {
    const float side = ImGui::GetFrameHeight();
    return ImVec2(side, side);
}

using EditorActionWidgets::LabeledActionButton;

} // namespace

void ShaderLabIDE::MarkPlaylistFocusedRow(int beat, int& focusedBeatThisFrame) {
    if (ImGui::IsItemFocused()) {
        focusedBeatThisFrame = beat;
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(m_uiThemeColors.TrackerAccentBeatBackground));
    }
}

void ShaderLabIDE::RenderPlaylistBeatColumn(int beat,
                                        TrackerRow*& row,
                                        int& focusedBeatThisFrame,
                                        bool& pushedBarStartStyle) {
    auto& track = m_track;

    int bar = (beat / 4) + 1;
    int subBeat = (beat % 4) + 1;

    const bool isCurrent = (beat == track.currentBeat);
    const bool isBarStart = (subBeat == 1);
    const bool isBeatThree = (subBeat == 3);
    pushedBarStartStyle = isBarStart;

    if (isBarStart) {
        ImVec4 beatFrameBg = m_uiThemeColors.ControlBackground;
        beatFrameBg.w = (std::clamp)(m_uiThemeColors.ControlOpacity * 0.5f, 0.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, beatFrameBg);
        ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.TrackerHeadingFontColor);
    }

    if (isCurrent) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(m_uiThemeColors.TrackerAccentBeatBackground));
    } else if (isBarStart) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(m_uiThemeColors.TrackerBeatBackground));
    } else if (isBeatThree) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
    }

    ImGui::TableSetColumnIndex(0);

    char beatLabel[24];
    const bool hasStopMarker = (row && row->stop);
    if (hasStopMarker) {
        std::snprintf(beatLabel, sizeof(beatLabel), "STOP %02d:%02d", bar, subBeat);
    } else {
        std::snprintf(beatLabel, sizeof(beatLabel), "%02d:%02d", bar, subBeat);
    }

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
        hasStopMarker
            ? m_uiThemeColors.TrackerAccentBeatFontColor
            : (isBarStart ? m_uiThemeColors.TrackerAccentBeatFontColor : m_uiThemeColors.TrackerBeatFontColor));
    PushNumericFont();
    const bool beatClicked = ImGui::Selectable(beatLabel, false);
    MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
    PopNumericFont();
    ImGui::PopStyleColor(4);

    if (beatClicked) {
        row = EnsurePlaylistRowByBeat(beat);
        row->stop = !row->stop;
    }
}

void ShaderLabIDE::RenderPlaylistSceneColumn(int beat,
                                         TrackerRow*& row,
                                         const std::vector<const char*>& sceneNames,
                                         const ImVec2& spinnerSize,
                                         int& focusedBeatThisFrame) {
    ImGui::TableSetColumnIndex(1);
    int currentSceneSel = (row) ? row->sceneIndex : -1;
    int comboIdx = currentSceneSel + 1;
    float currentOffset = (row) ? row->timeOffset : 0.0f;

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Scene", &comboIdx, sceneNames.data(), (int)sceneNames.size())) {
        row = EnsurePlaylistRowByBeat(beat);
        row->sceneIndex = comboIdx - 1;
    }
    MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);

    ImGui::TableSetColumnIndex(2);
    if (comboIdx > 0) {
        SetNextNumericFieldWidth(52.0f);
        PushNumericFont();
        bool offsetChanged = false;
        if (ImGui::InputFloat("##Off", &currentOffset, 0.0f, 0.0f, "%.1f")) {
            offsetChanged = true;
        }
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
        PopNumericFont();
        ImGui::SameLine(0.0f, 2.0f);
        if (IconButton("OffDec", OpenFontIcons::kMinus, "Decrease offset", spinnerSize)) {
            currentOffset -= 1.0f;
            offsetChanged = true;
        }
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
        ImGui::SameLine(0.0f, 2.0f);
        if (IconButton("OffInc", OpenFontIcons::kPlus, "Increase offset", spinnerSize)) {
            currentOffset += 1.0f;
            offsetChanged = true;
        }
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
        if (offsetChanged) {
            row = EnsurePlaylistRowByBeat(beat);
            row->timeOffset = currentOffset;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time Offset (Beats)");
    } else {
        ImGui::TextDisabled("-");
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
    }
}

void ShaderLabIDE::RenderPlaylistTransitionColumn(int beat,
                                              TrackerRow*& row,
                                              const std::vector<const char*>& transitionNames,
                                              const std::vector<std::string>& transitionStems,
                                              const ImVec2& spinnerSize,
                                              int& focusedBeatThisFrame) {
    ImGui::TableSetColumnIndex(3);
    int currentTrans = 0;
    if (row) {
        const std::string& resolvedStem = row->transitionPresetStem;
        for (int i = 1; i < (int)transitionStems.size(); ++i) {
            if (transitionStems[i] == resolvedStem) {
                currentTrans = i;
                break;
            }
        }
    }
    float currentDur = (row) ? row->transitionDuration : 1.0f;

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Trans", &currentTrans, transitionNames.data(), (int)transitionNames.size())) {
        row = EnsurePlaylistRowByBeat(beat);
        if (currentTrans <= 0 || currentTrans >= (int)transitionStems.size()) {
            row->transitionPresetStem.clear();
        } else {
            row->transitionPresetStem = transitionStems[currentTrans];
        }
    }
    MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);

    ImGui::TableSetColumnIndex(4);
    if (currentTrans != 0) {
        SetNextNumericFieldWidth(52.0f);
        PushNumericFont();
        bool durationChanged = false;
        if (ImGui::InputFloat("##Dur", &currentDur, 0.0f, 0.0f, "%.1f")) {
            durationChanged = true;
        }
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
        PopNumericFont();
        ImGui::SameLine(0.0f, 2.0f);
        if (IconButton("DurDec", OpenFontIcons::kMinus, "Shorter transition", spinnerSize)) {
            currentDur = (std::max)(0.1f, currentDur - 0.5f);
            durationChanged = true;
        }
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
        ImGui::SameLine(0.0f, 2.0f);
        if (IconButton("DurInc", OpenFontIcons::kPlus, "Longer transition", spinnerSize)) {
            currentDur += 0.5f;
            durationChanged = true;
        }
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
        if (durationChanged) {
            row = EnsurePlaylistRowByBeat(beat);
            row->transitionDuration = currentDur;
        }
    } else {
        ImGui::TextDisabled("-");
        MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
    }
}

void ShaderLabIDE::RenderPlaylistMusicColumn(int beat, TrackerRow*& row, int& focusedBeatThisFrame) {
    ImGui::TableSetColumnIndex(5);
    std::string currentMusicName = "";
    int currentMusicIdx = (row) ? row->musicIndex : -1;
    if (currentMusicIdx >= 0 && currentMusicIdx < (int)m_audioLibrary.size()) {
        auto& clip = m_audioLibrary[currentMusicIdx];
        if (clip.bpm > 0.0f) {
            currentMusicName = clip.name;
        } else {
            currentMusicName = clip.name;
        }
    } else if (currentMusicIdx == -1) {
        currentMusicName = "(Hold)";
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##Music", currentMusicName.c_str())) {
        if (ImGui::Selectable("(Hold)", currentMusicIdx == -1)) {
            row = EnsurePlaylistRowByBeat(beat);
            row->musicIndex = -1;
        }
        for (int n = 0; n < (int)m_audioLibrary.size(); n++) {
            auto& clip = m_audioLibrary[n];
            char label[128];
            if (clip.bpm > 0.0f) {
                snprintf(label, sizeof(label), "%s (%.1f)", clip.name.c_str(), clip.bpm);
            } else {
                strncpy_s(label, clip.name.c_str(), _TRUNCATE);
            }
            if (strlen(label) == 0) strcpy_s(label, "Untitled");

            bool is_selected = (currentMusicIdx == n);
            if (ImGui::Selectable(label, is_selected)) {
                row = EnsurePlaylistRowByBeat(beat);
                row->musicIndex = n;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
}

void ShaderLabIDE::RenderPlaylistOneShotColumn(int beat, TrackerRow*& row, int& focusedBeatThisFrame) {
    ImGui::TableSetColumnIndex(6);

    std::string currentOSName = "(None)";
    int currentOSIdx = (row) ? row->oneShotIndex : -1;
    if (currentOSIdx >= 0 && currentOSIdx < (int)m_audioLibrary.size()) {
        currentOSName = m_audioLibrary[currentOSIdx].name;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##OneShot", currentOSName.c_str())) {
        if (ImGui::Selectable("(None)", currentOSIdx == -1)) {
            row = EnsurePlaylistRowByBeat(beat);
            row->oneShotIndex = -1;
        }
        for (int n = 0; n < (int)m_audioLibrary.size(); ++n) {
            std::string label = m_audioLibrary[n].name;
            if (label.empty()) label = "Untitled";
            bool is_selected = (currentOSIdx == n);
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                row = EnsurePlaylistRowByBeat(beat);
                row->oneShotIndex = n;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    MarkPlaylistFocusedRow(beat, focusedBeatThisFrame);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", currentOSName.c_str());
    }
}

TrackerRow* ShaderLabIDE::FindPlaylistRowByBeat(int targetBeat) {
    for (auto& candidate : m_track.rows) {
        if (candidate.rowId == targetBeat) {
            return &candidate;
        }
    }
    return nullptr;
}

TrackerRow* ShaderLabIDE::EnsurePlaylistRowByBeat(int targetBeat) {
    if (TrackerRow* existing = FindPlaylistRowByBeat(targetBeat)) {
        return existing;
    }

    TrackerRow newRow;
    newRow.rowId = targetBeat;
    m_track.rows.push_back(newRow);
    return &m_track.rows.back();
}

void ShaderLabIDE::ScrubPlaylistToBeat(int targetBeat) {
    if (m_transport.state == TransportState::Playing) {
        m_transport.state = TransportState::Paused;
        if (m_audioSystem) {
            m_audioSystem->Pause();
        }
    }
    SeekToBeat(targetBeat);
}

void ShaderLabIDE::ScrubPlaylistByDeltaBeats(double deltaBeats) {
    auto& track = m_track;
    if (m_transport.state == TransportState::Playing) {
        m_transport.state = TransportState::Paused;
        if (m_audioSystem) {
            m_audioSystem->Pause();
        }
    }

    if (track.lengthBeats <= 0) {
        return;
    }

    const float bpm = (m_transport.bpm > 0.0f)
        ? m_transport.bpm
        : ((track.bpm > 0.0f) ? track.bpm : 120.0f);
    const double beatsPerSec = static_cast<double>(bpm) / 60.0;

    double exactBeat = m_transport.timeSeconds * beatsPerSec;
    double targetExactBeat = exactBeat + deltaBeats;
    targetExactBeat = (std::clamp)(targetExactBeat, 0.0, static_cast<double>(track.lengthBeats - 1));

    const int targetBeat = static_cast<int>(std::floor(targetExactBeat));
    SeekToBeat(targetBeat);

    m_transport.bpm = bpm;
    m_transport.timeSeconds = targetExactBeat * (60.0 / static_cast<double>(bpm));
    track.currentBeat = targetBeat;
    track.lastTriggeredBeat = targetBeat - 1;
}

void ShaderLabIDE::HandlePlaylistFocusScrub(bool playlistWindowFocused, bool editingAnyItem, int focusedBeatThisFrame) {
    auto& track = m_track;
    if (!(playlistWindowFocused && !editingAnyItem && focusedBeatThisFrame >= 0)) {
        return;
    }

    const bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
    if (altDown && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        ScrubPlaylistByDeltaBeats(-0.25);
    } else if (altDown && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        ScrubPlaylistByDeltaBeats(0.25);
    } else if (track.currentBeat != focusedBeatThisFrame) {
        ScrubPlaylistToBeat(focusedBeatThisFrame);
    }
}

void ShaderLabIDE::HandlePlaylistScrollFollow(int focusedBeatThisFrame) {
    auto& track = m_track;
    if (track.lengthBeats > 0 && focusedBeatThisFrame >= 0) {
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float windowHeight = ImGui::GetWindowHeight();
        const float rowMin = rowHeight * static_cast<float>(focusedBeatThisFrame);
        const float rowMax = rowMin + rowHeight;
        const float scrollY = ImGui::GetScrollY();
        const float scrollMax = ImGui::GetScrollMaxY();

        const float deadZoneTop = scrollY + windowHeight * 0.30f;
        const float deadZoneBottom = scrollY + windowHeight * 0.70f;
        const bool withinDeadZone = (rowMin >= deadZoneTop) && (rowMax <= deadZoneBottom);

        if (!withinDeadZone) {
            float targetY = rowMin - (windowHeight - rowHeight) * 0.5f;
            if (targetY < 0.0f) targetY = 0.0f;
            if (targetY > scrollMax) targetY = scrollMax;
            ImGui::SetScrollY(targetY);
        }
        return;
    }

    if (m_transport.state == TransportState::Playing && track.lengthBeats > 0) {
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float windowHeight = ImGui::GetWindowHeight();
        const float rowMin = rowHeight * track.currentBeat;
        const float rowMax = rowMin + rowHeight;
        const float scrollY = ImGui::GetScrollY();
        const float scrollMax = ImGui::GetScrollMaxY();

        if (rowMin < scrollY || rowMax > scrollY + windowHeight - rowHeight) {
            float targetY = rowMin - rowHeight * 2.0f;
            if (targetY < 0.0f) targetY = 0.0f;
            if (targetY > scrollMax) targetY = scrollMax;
            ImGui::SetScrollY(targetY);
        }
    }
}

void ShaderLabIDE::RenderPlaylistTopToolbar(const ImVec2& spinnerSize) {
    auto& track = m_track;
    constexpr float kLabelToField = 12.0f;
    constexpr float kSpinnerGap = 6.0f;
    constexpr float kGroupGap = 18.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("BPM");
    ImGui::SameLine(0.0f, kLabelToField);
    SetNextNumericFieldWidth(60.0f);
    PushNumericFont();
    if (ImGui::InputFloat("##TrackBPM", &track.bpm, 0.0f, 0.0f, "%.1f")) {
        if (track.bpm < 1.0f) track.bpm = 1.0f;
    }
    PopNumericFont();
    ImGui::SameLine(0.0f, kSpinnerGap);
    auto SpinnerIconButton = [&](const char* id, uint32_t icon, const char* tooltip) -> bool {
        return IconButton(id, icon, tooltip, spinnerSize);
    };
    if (SpinnerIconButton("BpmDec", OpenFontIcons::kMinus, "Decrease BPM")) {
        if (track.bpm > 1.0f) track.bpm -= 1.0f;
    }
    ImGui::SameLine(0.0f, kSpinnerGap);
    if (SpinnerIconButton("BpmInc", OpenFontIcons::kPlus, "Increase BPM")) {
        track.bpm += 1.0f;
    }

    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bars");
    ImGui::SameLine(0.0f, kLabelToField);
    SetNextNumericFieldWidth(54.0f);
    int bars = (track.lengthBeats + 3) / 4;
    PushNumericFont();
    bool barsChanged = false;
    if (ImGui::InputInt("##TrackBars", &bars, 0, 0)) {
        barsChanged = true;
    }
    PopNumericFont();
    ImGui::SameLine(0.0f, kSpinnerGap);
    if (SpinnerIconButton("BarsDec", OpenFontIcons::kMinus, "Decrease bars")) {
        bars -= 1;
        barsChanged = true;
    }
    ImGui::SameLine(0.0f, kSpinnerGap);
    if (SpinnerIconButton("BarsInc", OpenFontIcons::kPlus, "Increase bars")) {
        bars += 1;
        barsChanged = true;
    }
    if (barsChanged) {
        if (bars < 1) bars = 1;
        track.lengthBeats = bars * 4;
    }
    ImGui::SameLine(0.0f, kGroupGap * 0.6f);
    PushNumericFont();
    ImGui::TextUnformatted("(");
    ImGui::SameLine(0.0f, 0.0f);
    PushNumericFont();
    ImGui::Text("%d", track.lengthBeats);
    PopNumericFont();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(" beats)");
    PopNumericFont();

    ImGui::Separator();
}

void ShaderLabIDE::SetupPlaylistTrackerTable() const {
    ImGui::TableSetupColumn("Bar:Beat", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 250.0f);
    ImGui::TableSetupColumn("Transition", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Trans Time", ImGuiTableColumnFlags_WidthFixed, 250.0f);
    ImGui::TableSetupColumn("Music", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("OneShot", ImGuiTableColumnFlags_WidthFixed, 250.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, m_uiThemeColors.TrackerHeadingFontColor);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::TableHeadersRow();
    ImGui::PopItemFlag();
    ImGui::PopStyleColor();
}

void ShaderLabIDE::BuildPlaylistSceneNameOptions(std::vector<const char*>& sceneNames) const {
    sceneNames.clear();
    sceneNames.reserve(m_scenes.size() + 1);
    sceneNames.push_back("(Hold)");
    for (const auto& scene : m_scenes) {
        sceneNames.push_back(scene.name.c_str());
    }
}

void ShaderLabIDE::ShowDemoPlaylist() {
    // Only use m_track now
    // Create default if empty/fresh? But m_track is a member, so it exists.
    // Just ensure it has some rows maybe.
    if (m_track.rows.empty()) {
        TrackerRow startRow; startRow.rowId = 0;
        m_track.rows.push_back(startRow);
    }

    if (ImGui::Begin("Demo: Playlist")) {
        // --- Single Track Editor ---
        auto& track = m_track;
        const ImVec2 spinnerSize = CompactIconSquareSize();
        RenderPlaylistTopToolbar(spinnerSize);

        // 2. Tracker Grid
        // We want a table: [Beat] [Scene] [Transition] [Music] [OneShot]
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("TrackerGrid", 7, flags)) {
            SetupPlaylistTrackerTable();

            // Prepare scene names for Combos
            std::vector<const char*> sceneNames;
            BuildPlaylistSceneNameOptions(sceneNames);

            std::vector<const char*> transitionNames;
            std::vector<std::string> transitionStems;
            transitionNames.push_back("None");
            transitionStems.push_back(std::string());
            const auto& transitionPresets = GetTransitionPresets();
            transitionNames.reserve(transitionPresets.size() + 1);
            transitionStems.reserve(transitionPresets.size() + 1);
            for (const auto& preset : transitionPresets) {
                transitionNames.push_back(preset.name.c_str());
                transitionStems.push_back(preset.stem);
            }

            const bool playlistWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const bool editingAnyItem = ImGui::IsAnyItemActive();
            int focusedBeatThisFrame = -1;

            // Iterate through every beat
            ImGuiListClipper clipper;
            clipper.Begin(track.lengthBeats);
            while (clipper.Step()) {
                for (int beat = clipper.DisplayStart; beat < clipper.DisplayEnd; beat++) {
                    ImGui::PushID(beat);

                    ImGui::TableNextRow();
                    TrackerRow* row = FindPlaylistRowByBeat(beat);

                    bool pushedBarStartStyle = false;
                    RenderPlaylistBeatColumn(beat, row, focusedBeatThisFrame, pushedBarStartStyle);

                    RenderPlaylistSceneColumn(beat, row, sceneNames, spinnerSize, focusedBeatThisFrame);
                    RenderPlaylistTransitionColumn(beat, row, transitionNames, transitionStems, spinnerSize, focusedBeatThisFrame);
                    RenderPlaylistMusicColumn(beat, row, focusedBeatThisFrame);
                    RenderPlaylistOneShotColumn(beat, row, focusedBeatThisFrame);

                    if (pushedBarStartStyle) {
                        ImGui::PopStyleColor(2);
                    }

                    ImGui::PopID();
                }
            }

            HandlePlaylistFocusScrub(playlistWindowFocused, editingAnyItem, focusedBeatThisFrame);
            HandlePlaylistScrollFollow(focusedBeatThisFrame);

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace ShaderLab
