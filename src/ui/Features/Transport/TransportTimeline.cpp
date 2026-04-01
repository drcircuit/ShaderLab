#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UISystemAssets.h"
#include "ShaderLab/UI/UISystemDemoUtils.h"
#include "ShaderLab/Audio/AudioSystem.h"
#include "ShaderLab/Core/PlaybackService.h"

#include <sstream>
#include <vector>

namespace ShaderLab {

void ShaderLabIDE::ResetTransitionState(bool clearActiveScene) {
    m_transitionActive = false;
    m_pendingActiveScene = -2;
    m_transitionFromIndex = -1;
    m_transitionToIndex = -1;
    m_transitionFromOffset = 0.0f;
    m_transitionToOffset = 0.0f;
    m_transitionStartBeat = 0.0;
    m_transitionDurationBeats = 1.0;
    m_currentTransitionStem.clear();
    m_transitionFromStartBeat = 0.0;
    m_transitionToStartBeat = 0.0;
    m_transitionJustCompletedBeat = -1;

    if (clearActiveScene) {
        m_activeSceneIndex = -1;
        m_activeSceneOffset = 0.0f;
        m_activeSceneStartBeat = 0.0;
    }
}

void ShaderLabIDE::ResetTransportTimelineState() {
    m_transport.timeSeconds = 0.0;
    m_transport.lastFrameWallSeconds = 0.0;
    m_track.currentBeat = 0;
    m_track.lastTriggeredBeat = -1;
}

void ShaderLabIDE::StopAudioAndClearMusicState() {
    if (m_audioSystem) {
        m_audioSystem->Stop();
    }
    m_activeMusicIndex = -1;
}

void ShaderLabIDE::ApplyPlaybackActiveScene(int index) {
    if (m_currentMode == UIMode::Demo) {
        SetActiveScene(index);
        return;
    }

    if (m_currentMode == UIMode::Scene) {
        return;
    }

    if (index >= 0 && index < static_cast<int>(m_scenes.size())) {
        m_activeSceneIndex = index;
    } else {
        m_activeSceneIndex = -1;
    }
}

void ShaderLabIDE::BeginSceneTransition(int beat,
                                    double durationBeats,
                                    int targetSceneIndex,
                                    float targetOffset,
                                    double targetStartBeat,
                                    const std::string& transitionPresetStem) {
    m_transitionActive = true;
    m_transitionFromIndex = m_activeSceneIndex;
    m_transitionFromOffset = m_activeSceneOffset;
    m_transitionFromStartBeat = m_activeSceneStartBeat;

    m_transitionToStartBeat = targetStartBeat;
    m_transitionToIndex = targetSceneIndex;
    m_transitionToOffset = targetOffset;
    m_transitionStartBeat = static_cast<double>(beat);
    m_transitionDurationBeats = durationBeats;
    m_currentTransitionStem = transitionPresetStem;

    m_pendingActiveScene = m_transitionToIndex;
}

void ShaderLabIDE::UpdateTransport(double wallNowSeconds, float dtSeconds) {
    PlaybackService playback;
    if (m_transport.state == TransportState::Playing && !m_transport.freezeTime) {
        // Sync with Audio if available
        if (m_audioSystem && m_audioSystem->IsPlaying()) {
            m_transport.timeSeconds = (double)m_audioSystem->GetPlaybackTime();
        } else {
            playback.AdvanceClock(m_transport, wallNowSeconds, dtSeconds);
        }

        // Demo Track Logic
        // Check triggers
        // Note: m_track is now the single source of truth
        auto& track = m_track;

        if (m_activeMusicIndex >= 0) {
            if (m_activeMusicIndex >= (int)m_audioLibrary.size() || !playback.HasMusicIndexReference(track, m_activeMusicIndex)) {
                StopAudioAndClearMusicState();
            }
        }

        float targetBpm = track.bpm;
        if (m_activeMusicIndex >= 0 && m_activeMusicIndex < (int)m_audioLibrary.size()) {
            const auto& clip = m_audioLibrary[m_activeMusicIndex];
            if (clip.bpm > 0.0f && m_audioSystem && m_audioSystem->IsPlaying()) {
                targetBpm = clip.bpm;
            }
        }
        m_transport.bpm = targetBpm;

        // Update current beat
        const int currentBeat = playback.ComputeCurrentBeat(m_transport, track.bpm);
        track.currentBeat = currentBeat;
        const float beatsPerSec = m_transport.bpm / 60.0f;
        const float exactBeat = static_cast<float>(m_transport.timeSeconds * beatsPerSec);

        if (m_transitionActive) {
            double transitionEndBeat = m_transitionStartBeat + m_transitionDurationBeats;
            if (exactBeat >= transitionEndBeat) {
                m_transitionActive = false;
                if (m_pendingActiveScene != -2) {
                    std::ostringstream msg;
                    msg << "[beat " << track.currentBeat << "] Transition complete -> scene " << m_pendingActiveScene;
                    AppendDemoLog(msg.str());
                    ApplyPlaybackActiveScene(m_pendingActiveScene);
                    m_activeSceneStartBeat = m_transitionToStartBeat;
                    m_activeSceneOffset = (m_pendingActiveScene >= 0) ? m_transitionToOffset : 0.0f;
                    m_pendingActiveScene = -2;
                    m_transitionJustCompletedBeat = track.currentBeat;
                }
            }
        }

        // End-of-track behavior is mode-specific.
        // Scene/PostFX: keep running continuously.
        // Demo: stop only when a STOP row exists; otherwise loop.
        if (m_currentMode == UIMode::Demo && track.lengthBeats > 0 && track.currentBeat >= track.lengthBeats) {
            bool hasStopRow = false;
            for (const auto& row : track.rows) {
                if (row.stop) {
                    hasStopRow = true;
                    break;
                }
            }

            if (hasStopRow) {
                m_transport.state = TransportState::Stopped;
                StopAudioAndClearMusicState();
                return;
            }

            playback.SeekToBeat(m_transport, track, 0);
            ResetTransitionState(false);
            m_pendingActiveScene = -2;
            m_transitionJustCompletedBeat = -1;
            m_transport.lastFrameWallSeconds = wallNowSeconds;
            return;
        }

        // Check for events if we crossed a beat boundary (or multiple)
        if (track.currentBeat > track.lastTriggeredBeat) {
            std::vector<PlaybackEvent> events;
            playback.BuildPlaybackEvents(track, track.lastTriggeredBeat, track.currentBeat, events);

            for (const auto& event : events) {
                const int b = event.beat;
                if (event.type == PlaybackEventType::SceneCommand) {
                        if (m_currentMode == UIMode::Scene) {
                            continue;
                        }
                        // Scene Change
                        if (m_transitionJustCompletedBeat == event.rowId &&
                            event.transitionPresetStem.empty() &&
                            event.sceneIndex >= 0 &&
                            event.sceneIndex == m_activeSceneIndex) {
                            continue;
                        }
                        if (m_transitionActive &&
                            event.transitionPresetStem.empty() &&
                            event.sceneIndex >= 0 &&
                            event.sceneIndex == m_pendingActiveScene) {
                            continue;
                        }
                        if (!event.transitionPresetStem.empty() && event.transitionDuration > 0.0f) {
                            const SceneTransitionResolution target = playback.ResolveSceneTransitionTarget(
                                track,
                                event,
                                m_activeSceneIndex,
                                m_activeSceneOffset,
                                m_activeSceneStartBeat);

                            BeginSceneTransition(
                                b,
                                static_cast<double>(event.transitionDuration),
                                target.targetSceneIndex,
                                target.targetOffset,
                                target.targetStartBeat,
                                event.transitionPresetStem);

                            std::ostringstream msg;
                            const std::string transitionLabel = GetTransitionDisplayNameByStem(event.transitionPresetStem);
                            msg << "[beat " << b << "] Transition " << transitionLabel
                                << " from " << m_transitionFromIndex << " to " << m_transitionToIndex
                                << " dur " << event.transitionDuration;
                            AppendDemoLog(msg.str());
                        } else if (event.sceneIndex >= 0) {
                            // Validate scene index before switching
                            if (event.sceneIndex < (int)m_scenes.size()) {
                                m_transitionActive = false;
                                ApplyPlaybackActiveScene(event.sceneIndex);
                                m_activeSceneStartBeat = static_cast<double>(b);
                                m_activeSceneOffset = event.timeOffset;
                                std::ostringstream msg;
                                msg << "[beat " << b << "] Scene set to " << event.sceneIndex;
                                AppendDemoLog(msg.str());
                            }
                        }
                }

                if (event.type == PlaybackEventType::MusicChange) {
                        // Music Change
                        if (event.musicIndex >= 0 && event.musicIndex < (int)m_audioLibrary.size() && m_audioSystem) {
                             auto& clip = m_audioLibrary[event.musicIndex];
                             m_audioSystem->LoadAudio(clip.path);
                             if (m_transport.state == TransportState::Playing) {
                                 m_audioSystem->Play();
                             }
                             m_activeMusicIndex = event.musicIndex;
                             // Propagate BPM
                             if (clip.bpm > 0.0f) {
                                 m_transport.bpm = clip.bpm;
                                 // Recalculate beatsPerSec immediately?
                                 // This might cause a jump in beat calculation for the *next* frame
                                 // or even this frame if we relied on it.
                                 // For now, let it take effect next frame.
                             }

                                std::ostringstream msg;
                                msg << "[beat " << b << "] Music " << event.musicIndex;
                                AppendDemoLog(msg.str());
                        }
                }

                if (event.type == PlaybackEventType::OneShot) {
                        // One Shot
                        if (event.oneShotIndex >= 0 && event.oneShotIndex < (int)m_audioLibrary.size() && m_audioSystem) {
                            m_audioSystem->PlayOneShot(m_audioLibrary[event.oneShotIndex].path);
                            std::ostringstream msg;
                            msg << "[beat " << b << "] OneShot " << event.oneShotIndex;
                            AppendDemoLog(msg.str());
                        }
                }

                if (event.type == PlaybackEventType::Stop) {
                        // Stop Command
                         m_transport.state = TransportState::Stopped;
                     StopAudioAndClearMusicState();
                            AppendDemoLog("[stop] Track stopped");
                         // Do NOT reset time/beat, so it freezes exactly here.
                         // But we must stop processing further.
                         return;
                }
            }
            track.lastTriggeredBeat = track.currentBeat;
        }
        m_transitionJustCompletedBeat = -1;
    } else {
         auto& track = m_track;
         float targetBpm = track.bpm;
         if (m_activeMusicIndex >= 0 && m_activeMusicIndex < (int)m_audioLibrary.size()) {
             const auto& clip = m_audioLibrary[m_activeMusicIndex];
             if (clip.bpm > 0.0f && m_audioSystem && m_audioSystem->IsPlaying()) {
                 targetBpm = clip.bpm;
             }
         }
         m_transport.bpm = targetBpm;

         // Sync Transport BPM to active track during pause too
         track.currentBeat = playback.ComputeCurrentBeat(m_transport, track.bpm);

         // Reset trigger tracking on rewind
         if (track.currentBeat < track.lastTriggeredBeat) {
              track.lastTriggeredBeat = track.currentBeat - 1;
         }
    }
    m_transport.lastFrameWallSeconds = wallNowSeconds;
}

void ShaderLabIDE::SeekToBeat(int beat) {
    PlaybackService playback;
    auto& track = m_track;
    if (track.lengthBeats <= 0) return;

    playback.SeekToBeat(m_transport, track, beat);
    const int seekBeat = track.currentBeat;

    ResetTransitionState(true);
    int ignoreSceneBeat = -1;

    for (int b = 0; b <= seekBeat; ++b) {
        for (const auto& row : track.rows) {
            if (row.rowId != b) continue;

            if (!row.transitionPresetStem.empty() && row.transitionDuration > 0.0f) {
                PlaybackEvent event;
                event.type = PlaybackEventType::SceneCommand;
                event.beat = b;
                event.rowId = row.rowId;
                event.sceneIndex = row.sceneIndex;
                event.transitionPresetStem = row.transitionPresetStem;
                event.transitionDuration = row.transitionDuration;
                event.timeOffset = row.timeOffset;
                const SceneTransitionResolution target = playback.ResolveSceneTransitionTarget(
                    track,
                    event,
                    m_activeSceneIndex,
                    m_activeSceneOffset,
                    m_activeSceneStartBeat);

                BeginSceneTransition(
                    b,
                    static_cast<double>(row.transitionDuration),
                    target.targetSceneIndex,
                    target.targetOffset,
                    target.targetStartBeat,
                    row.transitionPresetStem);

                const double transitionEndBeat = m_transitionStartBeat + m_transitionDurationBeats;
                if (seekBeat > transitionEndBeat && m_pendingActiveScene != -2) {
                    m_transitionActive = false;
                    ApplyPlaybackActiveScene(m_pendingActiveScene);
                    m_activeSceneStartBeat = m_transitionToStartBeat;
                    m_activeSceneOffset = (m_pendingActiveScene >= 0) ? m_transitionToOffset : 0.0f;
                    m_pendingActiveScene = -2;
                    ignoreSceneBeat = static_cast<int>(transitionEndBeat);
                }
            } else if (row.sceneIndex >= 0) {
                if (ignoreSceneBeat == row.rowId && row.sceneIndex == m_activeSceneIndex) {
                    continue;
                }
                if (m_transitionActive && row.sceneIndex == m_pendingActiveScene) {
                    continue;
                }
                m_transitionActive = false;
                m_pendingActiveScene = -2;
                m_activeSceneIndex = row.sceneIndex;
                m_activeSceneStartBeat = static_cast<double>(b);
                m_activeSceneOffset = row.timeOffset;
            }
        }
    }

    ApplyPlaybackActiveScene(m_activeSceneIndex);
}

} // namespace ShaderLab
