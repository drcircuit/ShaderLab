#pragma once

#include <string>
#include <memory>
#include <vector>

// Forward declare miniaudio types
typedef struct ma_engine ma_engine;
typedef struct ma_sound ma_sound;

namespace ShaderLab {

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool Initialize();
    void Shutdown();

    bool LoadAudio(const std::string& filepath); // Loads background audio
    bool LoadAudioFromMemory(const void* data, size_t size);

    void Play();
    void Pause();
    void Stop();
    void Seek(float timeInSeconds);
    void SetVolume(float volume); // 0.0 to 1.0

    void PlayOneShot(const std::string& filepath);

    bool IsPlaying() const;
    float GetPlaybackTime() const;  // In seconds
    float GetDuration() const;      // In seconds

private:
    ma_engine* m_engine = nullptr;
    ma_sound* m_sound = nullptr; // Background sound
    
    // For memory playback
    void* m_decoder = nullptr; // ma_decoder opaque
    std::vector<uint8_t> m_audioBuffer;

    bool m_initialized = false;
};

} // namespace ShaderLab
