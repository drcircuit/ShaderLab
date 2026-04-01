#include "ShaderLab/Audio/BeatClock.h"
#include <cmath>

namespace ShaderLab {

BeatClock::BeatClock() = default;

void BeatClock::SetBPM(float bpm) {
    m_bpm = bpm > 0.0f ? bpm : 120.0f;
}

void BeatClock::SetTimeSignature(int beatsPerBar) {
    m_beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 4;
}

void BeatClock::Update(float audioTimeInSeconds) {
    m_previousAudioTime = m_audioTime;
    m_audioTime = audioTimeInSeconds;

    UpdateBeatCounters(audioTimeInSeconds);
}

void BeatClock::Reset() {
    m_audioTime = 0.0f;
    m_previousAudioTime = 0.0f;
    m_quarterNoteCount = 0;
    m_eighthNoteCount = 0;
    m_sixteenthNoteCount = 0;
    m_barCount = 0;
    m_hitQuarterNote = false;
    m_hitEighthNote = false;
    m_hitSixteenthNote = false;
    m_hitBar = false;
    m_prevQuarterNote = 0;
    m_prevEighthNote = 0;
    m_prevSixteenthNote = 0;
    m_prevBar = 0;
}

float BeatClock::GetBarProgress() const {
    if (m_beatsPerBar <= 0) {
        return 0.0f;
    }

    float secondsPerBeat = 60.0f / m_bpm;
    float secondsPerBar = secondsPerBeat * static_cast<float>(m_beatsPerBar);
    
    if (secondsPerBar <= 0.0f) {
        return 0.0f;
    }

    float timeInBar = std::fmod(m_audioTime, secondsPerBar);
    return timeInBar / secondsPerBar;
}

int BeatClock::GetBeatInBar() const {
    float secondsPerBeat = 60.0f / m_bpm;
    float beatPosition = m_audioTime / secondsPerBeat;
    int beatInBar = static_cast<int>(beatPosition) % m_beatsPerBar;
    return beatInBar;
}

float BeatClock::GetQuarterPhase() const {
    float secondsPerBeat = 60.0f / m_bpm;
    if (secondsPerBeat <= 0.0f) {
        return 0.0f;
    }
    float phase = std::fmod(m_audioTime, secondsPerBeat) / secondsPerBeat;
    return phase;
}

float BeatClock::GetEighthPhase() const {
    float secondsPerEighth = (60.0f / m_bpm) * 0.5f;
    if (secondsPerEighth <= 0.0f) {
        return 0.0f;
    }
    float phase = std::fmod(m_audioTime, secondsPerEighth) / secondsPerEighth;
    return phase;
}

float BeatClock::GetSixteenthPhase() const {
    float secondsPerSixteenth = (60.0f / m_bpm) * 0.25f;
    if (secondsPerSixteenth <= 0.0f) {
        return 0.0f;
    }
    float phase = std::fmod(m_audioTime, secondsPerSixteenth) / secondsPerSixteenth;
    return phase;
}

void BeatClock::UpdateBeatCounters(float newTime) {
    // Calculate time per subdivision
    float secondsPerBeat = 60.0f / m_bpm;
    float secondsPerEighth = secondsPerBeat * 0.5f;
    float secondsPerSixteenth = secondsPerBeat * 0.25f;
    float secondsPerBar = secondsPerBeat * static_cast<float>(m_beatsPerBar);

    // Calculate current beat counts
    m_quarterNoteCount = static_cast<uint32_t>(newTime / secondsPerBeat);
    m_eighthNoteCount = static_cast<uint32_t>(newTime / secondsPerEighth);
    m_sixteenthNoteCount = static_cast<uint32_t>(newTime / secondsPerSixteenth);
    m_barCount = static_cast<uint32_t>(newTime / secondsPerBar);

    // Detect hits (transitions)
    m_hitQuarterNote = (m_quarterNoteCount != m_prevQuarterNote);
    m_hitEighthNote = (m_eighthNoteCount != m_prevEighthNote);
    m_hitSixteenthNote = (m_sixteenthNoteCount != m_prevSixteenthNote);
    m_hitBar = (m_barCount != m_prevBar);

    // Store previous values
    m_prevQuarterNote = m_quarterNoteCount;
    m_prevEighthNote = m_eighthNoteCount;
    m_prevSixteenthNote = m_sixteenthNoteCount;
    m_prevBar = m_barCount;
}

} // namespace ShaderLab
