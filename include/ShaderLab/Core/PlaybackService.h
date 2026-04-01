#pragma once

#include "ShaderLab/Core/ShaderLabData.h"

#include <utility>
#include <vector>

namespace ShaderLab {

enum class PlaybackEventType {
    SceneCommand,
    MusicChange,
    OneShot,
    Stop
};

struct PlaybackEvent {
    PlaybackEventType type = PlaybackEventType::SceneCommand;
    int beat = 0;
    int rowId = 0;

    int sceneIndex = -1;
    std::string transitionPresetStem;
    float transitionDuration = 0.0f;
    float timeOffset = 0.0f;

    int musicIndex = -1;
    int oneShotIndex = -1;
};

struct SceneTransitionResolution {
    int targetSceneIndex = -1;
    float targetOffset = 0.0f;
    double targetStartBeat = 0.0;
};

class PlaybackService {
public:
    void AdvanceClock(Transport& transport, double wallNowSeconds, float fallbackDtSeconds) const;
    int ComputeCurrentBeat(const Transport& transport, float fallbackBpm) const;
    double BeatToSeconds(double beat, float bpm) const;
    void SeekToBeat(Transport& transport, DemoTrack& track, int beat) const;

    bool HasMusicIndexReference(const DemoTrack& track, int musicIndex) const;
    void CollectTriggeredRows(const DemoTrack& track,
                              int fromBeatExclusive,
                              int toBeatInclusive,
                              std::vector<std::pair<int, const TrackerRow*>>& outRows) const;
    void BuildPlaybackEvents(const DemoTrack& track,
                            int fromBeatExclusive,
                            int toBeatInclusive,
                            std::vector<PlaybackEvent>& outEvents) const;

    SceneTransitionResolution ResolveSceneTransitionTarget(const DemoTrack& track,
                                                           const PlaybackEvent& event,
                                                           int currentSceneIndex,
                                                           float currentSceneOffset,
                                                           double currentSceneStartBeat) const;
};

} // namespace ShaderLab
