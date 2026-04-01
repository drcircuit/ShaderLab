#pragma once

#include "ShaderLab/Core/ShaderLabData.h"
#include <climits>

namespace ShaderLab {

inline const TrackerRow* FindNextSceneRow(const DemoTrack& track, int afterBeat) {
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

inline int FindNextSceneIndex(const DemoTrack& track, int afterBeat) {
    const TrackerRow* row = FindNextSceneRow(track, afterBeat);
    return row ? row->sceneIndex : -1;
}

inline double BeatSeconds(float bpm) {
    if (bpm <= 0.0f) return 0.0;
    return 60.0 / static_cast<double>(bpm);
}

inline double SceneTimeSeconds(double exactBeat, double startBeat, float offsetBeats, float bpm) {
    const double beatSeconds = BeatSeconds(bpm);
    if (beatSeconds <= 0.0) return 0.0;
    const double sceneBeats = exactBeat - startBeat + static_cast<double>(offsetBeats);
    return sceneBeats * beatSeconds;
}

} // namespace ShaderLab
