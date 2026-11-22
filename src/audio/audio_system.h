#pragma once

#include <psp2/types.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include "../bsa/bsa_reader.h"

// Sound effect IDs for fast playback
enum class SoundID {
    GunFire10mm,
    GunFireLaser,
    GunFireShotgun,
    GunFireRifle,
    GunReloadIn,
    GunReloadOut,
    GunDryFire,
    PipBoyOpen,
    PipBoyClose,
    PipBoyTab,
    PipBoySelect,
    PipBoyLightOn,
    PipBoyLightOff,
    FootstepWood,
    QuestNew,
    DocWoahThere,
    Count
};

struct DecodedAudio {
    std::vector<int16_t> samples; // 48000 Hz, stereo interleaved (L, R, L, R...)
    uint32_t sampleFrames = 0;   // number of stereo pairs
};

struct AudioVoice {
    const int16_t* samples = nullptr;
    uint32_t totalFrames = 0;
    uint32_t currentFrame = 0;
    float volumeLeft = 1.0f;
    float volumeRight = 1.0f;
    bool active = false;
    bool loop = false;
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    // Initialize audio port and background mixing thread
    bool init(BSAReader* soundBsa = nullptr);
    void shutdown();

    // Load or preload audio from Sound BSA
    bool loadWav(SoundID id, const char* folder, const char* file);

    // Play a preloaded sound by ID
    void play(SoundID id, float volume = 1.0f, float pan = 0.0f);

    // Play raw decoded audio
    void playRaw(const DecodedAudio& audio, float volume = 1.0f, float pan = 0.0f, bool loop = false);

    // Stop all playing voices
    void stopAll();

    // Check if sound BSA is available
    bool hasSoundBsa() const { return m_soundBsa != nullptr && m_soundBsa->isOpen(); }

private:
    static const int SAMPLE_RATE = 48000;
    static const int BUFFER_FRAMES = 512;
    static const int MAX_VOICES = 16;

    int m_audioPort = -1;
    SceUID m_threadId = -1;
    std::atomic<bool> m_running{false};
    SceUID m_voiceMutex = -1;

    BSAReader* m_soundBsa = nullptr;

    AudioVoice m_voices[MAX_VOICES];
    std::unordered_map<int, DecodedAudio> m_soundCache;

    // Background mixing thread entry
    static int audioThreadFunc(SceSize args, void* argp);
    void mixAudio(int16_t* outputBuffer, int framesToMix);

    // Parse and resample RIFF WAV data
    bool parseAndResampleWav(const uint8_t* wavBytes, size_t size, DecodedAudio& outAudio);

    // Generate fallback sound when BSA sound is missing
    void generateFallbackSound(SoundID id, DecodedAudio& outAudio);
};
