#include "ShaderLab/Core/PlaybackService.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>
#include <vector>

namespace ShaderLab {

namespace {
const TrackerRow* FindNextSceneRow(const DemoTrack& track, int afterBeat) {
    const TrackerRow* bestRow = nullptr;
    int bestBeat = INT_MAX;
    for (const auto& row : track.rows) {
        if (row.sceneIndex >= 0 && row.rowId > afterBeat && row.rowId < bestBeat) {
            bestBeat = row.rowId;
            bestRow = &row;
        }
    }
    return bestRow;
}
}

void PlaybackService::AdvanceClock(Transport& transport, double wallNowSeconds, float fallbackDtSeconds) const {
    if (transport.state != TransportState::Playing || transport.freezeTime) {
        transport.lastFrameWallSeconds = wallNowSeconds;
        return;
    }

    float dt = fallbackDtSeconds;
    if (transport.lastFrameWallSeconds > 0.0 && wallNowSeconds >= transport.lastFrameWallSeconds) {
        dt = static_cast<float>(wallNowSeconds - transport.lastFrameWallSeconds);
    }

    transport.timeSeconds += static_cast<double>((std::max)(0.0f, dt));
    transport.lastFrameWallSeconds = wallNowSeconds;
}

int PlaybackService::ComputeCurrentBeat(const Transport& transport, float fallbackBpm) const {
    const float bpm = transport.bpm > 0.0f ? transport.bpm : fallbackBpm;
    const float safeBpm = (std::max)(1.0f, bpm);
    const float beatsPerSec = safeBpm / 60.0f;
    return static_cast<int>(std::floor(static_cast<float>(transport.timeSeconds) * beatsPerSec));
}

double PlaybackService::BeatToSeconds(double beat, float bpm) const {
    const float safeBpm = (std::max)(1.0f, bpm);
    return beat * (60.0 / static_cast<double>(safeBpm));
}

void PlaybackService::SeekToBeat(Transport& transport, DemoTrack& track, int beat) const {
    if (track.lengthBeats <= 0) {
        track.currentBeat = 0;
        track.lastTriggeredBeat = -1;
        return;
    }

    const int clampedBeat = (std::clamp)(beat, 0, track.lengthBeats - 1);
    const float bpm = transport.bpm > 0.0f ? transport.bpm : (track.bpm > 0.0f ? track.bpm : 120.0f);

    transport.bpm = bpm;
    transport.timeSeconds = BeatToSeconds(static_cast<double>(clampedBeat), bpm);

    track.currentBeat = clampedBeat;
    track.lastTriggeredBeat = clampedBeat - 1;
}

bool PlaybackService::HasMusicIndexReference(const DemoTrack& track, int musicIndex) const {
    for (const auto& row : track.rows) {
        if (row.musicIndex == musicIndex) {
            return true;
        }
    }
    return false;
}

void PlaybackService::CollectTriggeredRows(const DemoTrack& track,
                                          int fromBeatExclusive,
                                          int toBeatInclusive,
                                          std::vector<std::pair<int, const TrackerRow*>>& outRows) const {
    outRows.clear();
    if (toBeatInclusive <= fromBeatExclusive) {
        return;
    }

    for (int beat = fromBeatExclusive + 1; beat <= toBeatInclusive; ++beat) {
        for (const auto& row : track.rows) {
            if (row.rowId == beat) {
                outRows.emplace_back(beat, &row);
            }
        }
    }
}

void PlaybackService::BuildPlaybackEvents(const DemoTrack& track,
                                         int fromBeatExclusive,
                                         int toBeatInclusive,
                                         std::vector<PlaybackEvent>& outEvents) const {
    outEvents.clear();

    std::vector<std::pair<int, const TrackerRow*>> triggeredRows;
    CollectTriggeredRows(track, fromBeatExclusive, toBeatInclusive, triggeredRows);

    for (const auto& triggered : triggeredRows) {
        const int beat = triggered.first;
        const TrackerRow& row = *triggered.second;

        if (row.sceneIndex >= 0 || (!row.transitionPresetStem.empty() && row.transitionDuration > 0.0f)) {
            PlaybackEvent sceneEvent;
            sceneEvent.type = PlaybackEventType::SceneCommand;
            sceneEvent.beat = beat;
            sceneEvent.rowId = row.rowId;
            sceneEvent.sceneIndex = row.sceneIndex;
            sceneEvent.transitionPresetStem = row.transitionPresetStem;
            sceneEvent.transitionDuration = row.transitionDuration;
            sceneEvent.timeOffset = row.timeOffset;
            outEvents.push_back(sceneEvent);
        }

        if (row.musicIndex >= 0) {
            PlaybackEvent musicEvent;
            musicEvent.type = PlaybackEventType::MusicChange;
            musicEvent.beat = beat;
            musicEvent.rowId = row.rowId;
            musicEvent.musicIndex = row.musicIndex;
            outEvents.push_back(musicEvent);
        }

        if (row.oneShotIndex >= 0) {
            PlaybackEvent oneShotEvent;
            oneShotEvent.type = PlaybackEventType::OneShot;
            oneShotEvent.beat = beat;
            oneShotEvent.rowId = row.rowId;
            oneShotEvent.oneShotIndex = row.oneShotIndex;
            outEvents.push_back(oneShotEvent);
        }

        if (row.stop) {
            PlaybackEvent stopEvent;
            stopEvent.type = PlaybackEventType::Stop;
            stopEvent.beat = beat;
            stopEvent.rowId = row.rowId;
            outEvents.push_back(stopEvent);
        }
    }
}

SceneTransitionResolution PlaybackService::ResolveSceneTransitionTarget(const DemoTrack& track,
                                                                        const PlaybackEvent& event,
                                                                        int currentSceneIndex,
                                                                        float currentSceneOffset,
                                                                        double currentSceneStartBeat) const {
    SceneTransitionResolution resolution;
    resolution.targetSceneIndex = event.sceneIndex;
    resolution.targetOffset = event.timeOffset;
    resolution.targetStartBeat = static_cast<double>(event.beat);

    if (resolution.targetSceneIndex == -1 && event.transitionPresetStem == "crossfade") {
        const TrackerRow* nextRow = FindNextSceneRow(track, event.beat);
        if (nextRow) {
            resolution.targetSceneIndex = nextRow->sceneIndex;
            resolution.targetOffset = nextRow->timeOffset;
        }
    }

    if (resolution.targetSceneIndex == -1 && event.transitionPresetStem != "fade_out") {
        resolution.targetSceneIndex = currentSceneIndex;
        resolution.targetOffset = currentSceneOffset;
        resolution.targetStartBeat = currentSceneStartBeat;
    } else if (resolution.targetSceneIndex == currentSceneIndex) {
        resolution.targetOffset = currentSceneOffset;
        resolution.targetStartBeat = currentSceneStartBeat;
    }

    return resolution;
}

} // namespace ShaderLab
