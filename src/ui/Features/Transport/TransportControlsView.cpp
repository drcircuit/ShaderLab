#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/UI/UIConfig.h"
#include "ShaderLab/Audio/AudioSystem.h"

#include <imgui.h>

#include <algorithm>
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
}

void ShaderLabIDE::ShowTransportControls() {
    ImGui::Begin("Transport", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_fontHackedLogo) {
        ImGui::PushFont(m_fontHackedLogo);
        ImVec2 logoSize = ImGui::CalcTextSize("SHADERLAB");
        ImGui::PopFont();

        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
        ImVec2 logoPos(
            windowPos.x + contentMax.x - logoSize.x,
            windowPos.y + contentMin.y + (std::min)(UIConfig::MenuLogoBaselineOffset, 0.0f)
        );

        ImGui::PushFont(m_fontHackedLogo);
        ImGui::GetWindowDrawList()->AddText(logoPos, ImGui::GetColorU32(m_uiThemeColors.LogoFontColor), "SHADERLAB");
        ImGui::PopFont();
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
    const bool allowTransportHotkeys = !io.WantTextInput;
    const bool hotkeyToggle = allowTransportHotkeys && altDown && ImGui::IsKeyPressed(ImGuiKey_Space, false);
    const bool hotkeyStop = allowTransportHotkeys && altDown && ImGui::IsKeyPressed(ImGuiKey_X, false);

    if (m_playbackBlockedByCompileError &&
        m_activeSceneIndex >= 0 &&
        m_activeSceneIndex < (int)m_scenes.size()) {
        const auto& activeScene = m_scenes[m_activeSceneIndex];
        if (activeScene.pipelineState && !activeScene.isDirty && m_shaderState.status != CompileStatus::Error) {
            m_playbackBlockedByCompileError = false;
        }
    }

    const bool playBlocked = m_playbackBlockedByCompileError;

    // Play/Pause/Stop buttons
    const bool isPlaying = (m_transport.state == TransportState::Playing);
    const uint32_t playPauseIcon = isPlaying ? OpenFontIcons::kMinus : OpenFontIcons::kPlay;
    const char* playPauseLabel = isPlaying ? "Pause" : "Start";
    ImGui::BeginDisabled(playBlocked);
    const bool playButtonPressed = LabeledActionButton(
        "TransportPlayPause",
        playPauseIcon,
        playPauseLabel,
        "Toggle transport (Alt+Space)",
        ImVec2(138.0f, 0.0f));
    const bool playPausePressed = (playButtonPressed || hotkeyToggle) && !playBlocked;
    const bool playHovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    if (playBlocked && playHovered) {
        ImGui::SetTooltip("Playback disabled: compile active shader successfully to re-enable");
    }
    if (playPausePressed) {
        if (m_transport.state == TransportState::Playing) {
            m_transport.state = TransportState::Paused;
            if (m_audioSystem) m_audioSystem->Pause();
            if (m_currentMode == UIMode::Demo) {
                AppendDemoLog("[pause] Transport paused");
            }
        } else {
            bool compilationFailed = false;

            // In Demo mode, ensure all scenes are compiled
            if (m_currentMode == UIMode::Demo) {
                for (int i = 0; i < (int)m_scenes.size(); ++i) {
                    if (m_scenes[i].isDirty || m_scenes[i].pipelineState == nullptr) {
                        if (!CompileScene(i)) {
                            // Compilation failed - switch to scene mode to show error
                            m_currentMode = UIMode::Scene;
                            SetActiveScene(i);

                            // Re-compile (with active index set) to populate UI diagnostics
                            CompileScene(i);

                            compilationFailed = true;
                            break;
                        }
                    }

                    for (auto& fx : m_scenes[i].postFxChain) {
                        if (!fx.pipelineState || fx.isDirty) {
                            std::vector<std::string> errors;
                            if (!CompilePostFxEffect(fx, errors)) {
                                m_currentMode = UIMode::PostFX;
                                m_postFxSourceSceneIndex = i;
                                m_postFxDraftChain = m_scenes[i].postFxChain;
                                m_postFxSelectedIndex = m_postFxDraftChain.empty() ? -1 : 0;
                                SyncPostFxEditorToSelection();
                                m_shaderState.status = CompileStatus::Error;
                                m_shaderState.diagnostics.clear();
                                for (const auto& error : errors) {
                                    Diagnostic diag;
                                    diag.message = error;
                                    m_shaderState.diagnostics.push_back(diag);
                                }
                                compilationFailed = true;
                                break;
                            }
                        }
                    }
                    if (compilationFailed) break;
                }
            }

            if (compilationFailed && m_currentMode == UIMode::Demo) {
                m_hasDemoCompiledSize = false;
                m_lastDemoCompiledSizeBytes = 0;
                m_playbackBlockedByCompileError = true;
                m_transport.state = TransportState::Stopped;
                StopAudioAndClearMusicState();
            }

            if (!compilationFailed) {
                m_playbackBlockedByCompileError = false;
                if (m_currentMode == UIMode::Demo) {
                    std::vector<bool> referencedScenes(m_scenes.size(), false);
                    for (const auto& row : m_track.rows) {
                        if (row.sceneIndex >= 0 && row.sceneIndex < (int)m_scenes.size()) {
                            referencedScenes[row.sceneIndex] = true;
                        }
                    }

                    size_t totalBytes = 0;
                    for (int i = 0; i < (int)m_scenes.size(); ++i) {
                        if (!referencedScenes[i]) continue;
                        const auto& scene = m_scenes[i];
                        if (scene.pipelineState && scene.compiledShaderBytes > 0) {
                            totalBytes += scene.compiledShaderBytes;
                        }
                        for (const auto& fx : scene.postFxChain) {
                            if (fx.enabled && fx.pipelineState && fx.compiledShaderBytes > 0) {
                                totalBytes += fx.compiledShaderBytes;
                            }
                        }
                    }

                    m_lastDemoCompiledSizeBytes = totalBytes;
                    m_hasDemoCompiledSize = (totalBytes > 0);
                }

                if (m_transport.state == TransportState::Stopped) {
                    if (m_currentMode == UIMode::Demo) {
                        // Reset Transport and derive active scene from tracker start in Demo mode.
                        ResetTransportTimelineState();
                        ResetTransitionState(true);

                        StopAudioAndClearMusicState();

                        SeekToBeat(0);
                    } else {
                        // Scene/PostFX: keep current active scene for preview while restarting playback.
                        ResetTransportTimelineState();
                        ResetTransitionState(false);
                        StopAudioAndClearMusicState();
                    }
                }
                m_transport.state = TransportState::Playing;
                m_transport.lastFrameWallSeconds = 0.0;
                if (m_currentMode == UIMode::Demo) {
                    AppendDemoLog("[play] Transport playing");
                }
                // Start Audio
                if (m_activeMusicIndex >= 0 && m_activeMusicIndex < (int)m_audioLibrary.size()) {
                    auto& clip = m_audioLibrary[m_activeMusicIndex];
                    m_audioSystem->LoadAudio(clip.path);
                    m_audioSystem->Play();
                }
            }
        }
    }
    ImGui::SameLine();

    const bool stopPressed = LabeledActionButton("TransportStop", OpenFontIcons::kStop, "Stop", "Stop transport (Alt+X)", ImVec2(138.0f, 0.0f));
    if (stopPressed || hotkeyStop) {
        m_transport.state = TransportState::Stopped;
        ResetTransportTimelineState();
        ResetTransitionState(m_currentMode == UIMode::Demo);

        StopAudioAndClearMusicState();

        if (m_currentMode == UIMode::Demo) {
            AppendDemoLog("[stop] Transport stopped");
        }
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(14.0f, 0.0f));
    ImGui::SameLine(0.0f, 0.0f);

    PushNumericFont();
    ImGui::TextUnformatted("Time:");
    ImGui::SameLine();
    PushNumericFont();
    ImGui::Text("%.2f", m_transport.timeSeconds);
    PopNumericFont();
    PopNumericFont();

    // Show beat counter based on active BPM
    float beatsPerSec = m_transport.bpm / 60.0f;
    float exactBeat = (float)(m_transport.timeSeconds * beatsPerSec);
    int bar = (int)exactBeat / 4 + 1;
    int beat = (int)exactBeat % 4 + 1;
    ImGui::SameLine();
    PushNumericFont();
    ImGui::TextUnformatted("Beat:");
    ImGui::SameLine();
    PushNumericFont();
    ImGui::Text("%02d:%02d", bar, beat);
    PopNumericFont();
    PopNumericFont();

    if (m_track.lengthBeats > 0) {
        ImGui::SameLine();
        PushNumericFont();
        ImGui::TextUnformatted("/");
        ImGui::SameLine(0.0f, 0.0f);
        PushNumericFont();
        ImGui::Text("%d", m_track.lengthBeats);
        PopNumericFont();
        PopNumericFont();
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(10.0f, 0.0f));
    ImGui::SameLine(0.0f, 0.0f);
    PushNumericFont();
    ImGui::TextUnformatted("BPM:");
    ImGui::SameLine();
    float bpm = m_transport.bpm;
    ImGui::Text("%.1f", bpm);
    PopNumericFont();

    if (m_currentMode == UIMode::Demo && m_hasDemoCompiledSize) {
        ImGui::SameLine();
        ImGui::TextDisabled("Compiled: %s", FormatByteSize(m_lastDemoCompiledSizeBytes).c_str());
    }

    ImGui::End();
}

} // namespace ShaderLab
