#include "ShaderLab/Audio/AudioSystem.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <vector>

namespace ShaderLab {

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::Initialize() {
    if (m_initialized) {
        return true;
    }

    m_engine = new ma_engine();
    
    ma_result result = ma_engine_init(nullptr, m_engine);
    if (result != MA_SUCCESS) {
        delete m_engine;
        m_engine = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

void AudioSystem::Shutdown() {
    if (m_sound) {
        ma_sound_uninit(m_sound);
        delete m_sound;
        m_sound = nullptr;
    }

    if (m_decoder) {
        ma_decoder_uninit((ma_decoder*)m_decoder);
        delete (ma_decoder*)m_decoder;
        m_decoder = nullptr;
    }
    m_audioBuffer.clear();

    if (m_engine) {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }

    m_initialized = false;
}

bool AudioSystem::LoadAudio(const std::string& filepath) {
    if (!m_initialized) {
        return false;
    }

    // Unload previous sound
    if (m_sound) {
        ma_sound_uninit(m_sound);
        delete m_sound;
        m_sound = nullptr;
    }
    
    // Clear old memory resources
    if (m_decoder) {
        ma_decoder_uninit((ma_decoder*)m_decoder);
        delete (ma_decoder*)m_decoder;
        m_decoder = nullptr;
    }
    m_audioBuffer.clear();

    m_sound = new ma_sound();
    
    ma_result result = ma_sound_init_from_file(m_engine, filepath.c_str(), 
                                               0, nullptr, nullptr, m_sound);
    if (result != MA_SUCCESS) {
        delete m_sound;
        m_sound = nullptr;
        return false;
    }

    return true;
}

bool AudioSystem::LoadAudioFromMemory(const void* data, size_t size) {
    if (!m_initialized || !data || size == 0) return false;

    if (m_sound) {
        ma_sound_uninit(m_sound);
        delete m_sound;
        m_sound = nullptr;
    }
    if (m_decoder) {
        ma_decoder_uninit((ma_decoder*)m_decoder);
        delete (ma_decoder*)m_decoder;
        m_decoder = nullptr;
    }

    // Keep data alive
    m_audioBuffer.resize(size);
    memcpy(m_audioBuffer.data(), data, size);

    m_decoder = new ma_decoder();
    ma_decoder_config config = ma_decoder_config_init_default();
    
    if (ma_decoder_init_memory(m_audioBuffer.data(), size, &config, (ma_decoder*)m_decoder) != MA_SUCCESS) {
        delete (ma_decoder*)m_decoder;
        m_decoder = nullptr;
        return false;
    }

    m_sound = new ma_sound();
    if (ma_sound_init_from_data_source(m_engine, (ma_decoder*)m_decoder, 0, nullptr, m_sound) != MA_SUCCESS) {
        ma_decoder_uninit((ma_decoder*)m_decoder);
        delete (ma_decoder*)m_decoder;
        m_decoder = nullptr;
        delete m_sound;
        m_sound = nullptr;
        return false;
    }
    return true;
}

void AudioSystem::Play() {
    if (m_sound) {
        ma_sound_start(m_sound);
    }
}

void AudioSystem::Pause() {
    if (m_sound) {
        ma_sound_stop(m_sound);
    }
}

void AudioSystem::Stop() {
    if (m_sound) {
        ma_sound_stop(m_sound);
        ma_sound_seek_to_pcm_frame(m_sound, 0);
    }
}

void AudioSystem::Seek(float timeInSeconds) {
    if (!m_sound) {
        return;
    }

    ma_uint32 sampleRate;
    ma_sound_get_data_format(m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    
    ma_uint64 framePosition = static_cast<ma_uint64>(timeInSeconds * sampleRate);
    ma_sound_seek_to_pcm_frame(m_sound, framePosition);
}

void AudioSystem::SetVolume(float volume) {
    if (m_sound) {
        ma_sound_set_volume(m_sound, volume);
    }
}

void AudioSystem::PlayOneShot(const std::string& filepath) {
    if (!m_initialized) return;
    ma_engine_play_sound(m_engine, filepath.c_str(), nullptr);
}

bool AudioSystem::IsPlaying() const {
    if (!m_sound) {
        return false;
    }
    return ma_sound_is_playing(m_sound);
}

float AudioSystem::GetPlaybackTime() const {
    if (!m_sound) {
        return 0.0f;
    }

    ma_uint64 cursor;
    ma_sound_get_cursor_in_pcm_frames(m_sound, &cursor);

    ma_uint32 sampleRate;
    ma_sound_get_data_format(m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);

    return static_cast<float>(cursor) / static_cast<float>(sampleRate);
}

float AudioSystem::GetDuration() const {
    if (!m_sound) {
        return 0.0f;
    }

    ma_uint64 lengthInFrames;
    ma_sound_get_length_in_pcm_frames(m_sound, &lengthInFrames);

    ma_uint32 sampleRate;
    ma_sound_get_data_format(m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);

    return static_cast<float>(lengthInFrames) / static_cast<float>(sampleRate);
}

} // namespace ShaderLab
