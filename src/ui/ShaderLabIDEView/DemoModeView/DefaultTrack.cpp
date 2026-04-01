#include "ShaderLab/UI/ShaderLabIDE.h"

namespace ShaderLab {

void ShaderLabIDE::CreateDefaultTrack() {
    m_track.name = "Main Track";
    m_track.bpm = 120.0f;
    m_track.lengthBeats = 512;

    // Start scene 0 at beat 0
    TrackerRow startRow;
    startRow.rowId = 0;
    startRow.sceneIndex = 0;
    m_track.rows.push_back(startRow);
}

} // namespace ShaderLab
