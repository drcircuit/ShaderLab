#pragma once

#include <cstdint>

namespace ShaderLab {

class BeatClock {
public:
    BeatClock();
    ~BeatClock() = default;

    void SetBPM(float bpm);
    void SetTimeSignature(int beatsPerBar);
    
    void Update(float audioTimeInSeconds);
    void Reset();

    // Time getters
    float GetBPM() const { return m_bpm; }
    float GetAudioTime() const { return m_audioTime; }
    
    // Beat counting (quarter notes = 1 beat)
    uint32_t GetQuarterNoteCount() const { return m_quarterNoteCount; }
    uint32_t GetEighthNoteCount() const { return m_eighthNoteCount; }
    uint32_t GetSixteenthNoteCount() const { return m_sixteenthNoteCount; }
    uint32_t GetBarCount() const { return m_barCount; }

    // Position within bar (0.0 to 1.0)
    float GetBarProgress() const;
    
    // Beat within current bar (0 to beatsPerBar-1)
    int GetBeatInBar() const;

    // Hit flags (true for one frame when beat happens)
    bool HitQuarterNote() const { return m_hitQuarterNote; }
    bool HitEighthNote() const { return m_hitEighthNote; }
    bool HitSixteenthNote() const { return m_hitSixteenthNote; }
    bool HitBar() const { return m_hitBar; }

    // Normalized phase (0.0 to 1.0) within each subdivision
    float GetQuarterPhase() const;
    float GetEighthPhase() const;
    float GetSixteenthPhase() const;

private:
    void UpdateBeatCounters(float newTime);

    float m_bpm = 120.0f;
    int m_beatsPerBar = 4;
    
    float m_audioTime = 0.0f;
    float m_previousAudioTime = 0.0f;

    uint32_t m_quarterNoteCount = 0;
    uint32_t m_eighthNoteCount = 0;
    uint32_t m_sixteenthNoteCount = 0;
    uint32_t m_barCount = 0;

    bool m_hitQuarterNote = false;
    bool m_hitEighthNote = false;
    bool m_hitSixteenthNote = false;
    bool m_hitBar = false;

    uint32_t m_prevQuarterNote = 0;
    uint32_t m_prevEighthNote = 0;
    uint32_t m_prevSixteenthNote = 0;
    uint32_t m_prevBar = 0;
};

} // namespace ShaderLab
