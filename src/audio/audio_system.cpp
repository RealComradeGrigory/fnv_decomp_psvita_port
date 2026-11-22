#include "audio_system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern void logMsg(const char* fmt, ...);

AudioSystem::AudioSystem() {
    for (int i = 0; i < MAX_VOICES; ++i) {
        m_voices[i].active = false;
    }
}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::init(BSAReader* soundBsa) {
    if (m_audioPort >= 0) return true;
    m_soundBsa = soundBsa;

    // Open PS Vita hardware audio port (48kHz, stereo, 512 samples per slice)
    m_audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, BUFFER_FRAMES, SAMPLE_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (m_audioPort < 0) {
        logMsg("AudioSystem: failed to open audio port (0x%08X)", m_audioPort);
        return false;
    }

    int vol[2] = { 32768, 32768 }; // Max volume left and right
    sceAudioOutSetVolume(m_audioPort, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);

    m_voiceMutex = sceKernelCreateMutex("fnv_audio_voices", 0, 0, nullptr);
    if (m_voiceMutex < 0) {
        sceAudioOutReleasePort(m_audioPort);
        m_audioPort = -1;
        return false;
    }
    m_running = true;

    // Create high-priority audio mixing thread (thread priority 0x10000100)
    m_threadId = sceKernelCreateThread("fnv_audio_mixer", audioThreadFunc, 0x10000100, 0x10000, 0, 0, nullptr);
    if (m_threadId < 0) {
        logMsg("AudioSystem: failed to create audio mixer thread (0x%08X)", m_threadId);
        sceAudioOutReleasePort(m_audioPort);
        m_audioPort = -1;
        sceKernelDeleteMutex(m_voiceMutex);
        m_voiceMutex = -1;
        m_running = false;
        return false;
    }

    AudioSystem* self = this;
    if (sceKernelStartThread(m_threadId, sizeof(self), &self) < 0) {
        sceKernelDeleteThread(m_threadId);
        m_threadId = -1;
        shutdown();
        return false;
    }
    logMsg("AudioSystem: audio mixer thread started (48000 Hz, 16-channel PCM)");

    // Preload authentic Fallout New Vegas audio from Fallout - Sound.bsa
    loadWav(SoundID::GunFire10mm,   "sound\\fx\\wpn\\10mm_pistol\\fire_2d",  "wpn_10mmfire_2d_01.wav");
    loadWav(SoundID::GunFireLaser,  "sound\\fx\\wpn\\pistollaser\\fire_2d",  "wpn_lasergun_fire_2d_01.wav");
    loadWav(SoundID::GunFireShotgun,"sound\\fx\\wpn\\shotgunsawed\\fire_2d", "wpn_sawedoffshotgun_fire_2d_01.wav");
    loadWav(SoundID::GunFireRifle,  "sound\\fx\\wpn\\riflevarmint\\fire_2d", "wpn_varmintriflefire2d_01.wav");
    loadWav(SoundID::GunReloadIn,   "sound\\fx\\wpn\\pistol_10mm\\reload",   "wpn_pistol10mm_reloadin.wav");
    loadWav(SoundID::GunReloadOut,  "sound\\fx\\wpn\\pistol_10mm\\reload",  "wpn_pistol10mm_reloadout.wav");
    loadWav(SoundID::GunDryFire,    "sound\\fx\\wpn\\10mm_pistol",          "wpn_10mmpistolfire_jam.wav");
    loadWav(SoundID::PipBoyOpen,    "sound\\fx\\ui\\pipboy",                 "ui_pipboy_mode.wav");
    loadWav(SoundID::PipBoyClose,   "sound\\fx\\ui\\pipboy",                 "ui_pipboy_mode.wav");
    loadWav(SoundID::PipBoyTab,     "sound\\fx\\ui\\pipboy",                 "ui_pipboy_tab.wav");
    loadWav(SoundID::PipBoySelect,  "sound\\fx\\ui\\pipboy",                 "ui_pipboy_select.wav");
    loadWav(SoundID::PipBoyLightOn, "sound\\fx\\ui\\pipboy",                 "ui_pipboy_light_on.wav");
    loadWav(SoundID::PipBoyLightOff,"sound\\fx\\ui\\pipboy",                 "ui_pipboy_light_off.wav");
    loadWav(SoundID::FootstepWood,  "sound\\fx\\fst\\wood\\run\\left",       "fst_wood_run_01.wav");
    loadWav(SoundID::QuestNew,      "sound\\fx\\ui\\popup",                  "ui_popup_questnew.wav");
    loadWav(SoundID::DocWoahThere,  "sound\\fx\\fol\\docmitchell",           "fol_doc_woahthere.wav");

    return true;
}

void AudioSystem::shutdown() {
    m_running = false;

    if (m_threadId >= 0) {
        sceKernelWaitThreadEnd(m_threadId, nullptr, nullptr);
        sceKernelDeleteThread(m_threadId);
        m_threadId = -1;
    }

    if (m_audioPort >= 0) {
        sceAudioOutReleasePort(m_audioPort);
        m_audioPort = -1;
    }

    if (m_voiceMutex >= 0) {
        sceKernelDeleteMutex(m_voiceMutex);
        m_voiceMutex = -1;
    }
    for (auto& voice : m_voices) voice = {};
    m_soundCache.clear();
    logMsg("AudioSystem: shutdown complete");
}

int AudioSystem::audioThreadFunc(SceSize args, void* argp) {
    (void)args;
    AudioSystem* self = *reinterpret_cast<AudioSystem**>(argp);
    if (!self) return 0;

    int16_t outputBuffer[BUFFER_FRAMES * 2];

    while (self->m_running) {
        self->mixAudio(outputBuffer, BUFFER_FRAMES);
        sceAudioOutOutput(self->m_audioPort, outputBuffer);
    }

    return 0;
}

void AudioSystem::mixAudio(int16_t* outputBuffer, int framesToMix) {
    sceKernelLockMutex(m_voiceMutex, 1, nullptr);
    int32_t mixAccum[BUFFER_FRAMES * 2];
    memset(mixAccum, 0, framesToMix * 2 * sizeof(int32_t));

    for (int v = 0; v < MAX_VOICES; ++v) {
        AudioVoice& voice = m_voices[v];
        if (!voice.active || !voice.samples) continue;

        for (int f = 0; f < framesToMix; ++f) {
            if (voice.currentFrame >= voice.totalFrames) {
                if (voice.loop) {
                    voice.currentFrame = 0;
                } else {
                    voice.active = false;
                    break;
                }
            }

            int idx = voice.currentFrame * 2;
            int32_t leftSample  = (int32_t)(voice.samples[idx]     * voice.volumeLeft);
            int32_t rightSample = (int32_t)(voice.samples[idx + 1] * voice.volumeRight);

            mixAccum[f * 2]     += leftSample;
            mixAccum[f * 2 + 1] += rightSample;

            voice.currentFrame++;
        }
    }

    sceKernelUnlockMutex(m_voiceMutex, 1);
    // Master clamp to 16-bit signed integer range
    for (int i = 0; i < framesToMix * 2; ++i) {
        int32_t val = mixAccum[i];
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;
        outputBuffer[i] = (int16_t)val;
    }
}

bool AudioSystem::parseAndResampleWav(const uint8_t* data, size_t size, DecodedAudio& outAudio) {
    if (!data || size < 44) return false;

    // Check RIFF and WAVE magic
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t offset = 12;
    uint16_t channels = 0;
    uint32_t srcRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* pcmData = nullptr;
    uint32_t pcmDataSize = 0;

    while (offset + 8 <= size) {
        char chunkId[5] = {0};
        memcpy(chunkId, data + offset, 4);
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(data + offset + 4);
        offset += 8;

        if (offset + chunkSize > size) chunkSize = (uint32_t)(size - offset);

        if (memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            uint16_t audioFormat = *reinterpret_cast<const uint16_t*>(data + offset);
            channels = *reinterpret_cast<const uint16_t*>(data + offset + 2);
            srcRate  = *reinterpret_cast<const uint32_t*>(data + offset + 4);
            bitsPerSample = *reinterpret_cast<const uint16_t*>(data + offset + 14);
            if (audioFormat != 1) return false; // Only PCM supported
        } else if (memcmp(chunkId, "data", 4) == 0) {
            pcmData = data + offset;
            pcmDataSize = chunkSize;
        }

        offset += chunkSize;
        if (chunkSize & 1) offset++; // Word alignment
    }

    if (!pcmData || pcmDataSize == 0 || channels == 0 || srcRate == 0 || bitsPerSample != 16) {
        return false;
    }

    uint32_t srcFrames = pcmDataSize / (channels * sizeof(int16_t));
    if (srcFrames == 0) return false;

    // Resample to 48000 Hz stereo
    float ratio = (float)srcRate / (float)SAMPLE_RATE;
    uint32_t destFrames = (uint32_t)((float)srcFrames / ratio);

    outAudio.samples.resize(destFrames * 2);
    outAudio.sampleFrames = destFrames;

    const int16_t* srcSamples = reinterpret_cast<const int16_t*>(pcmData);

    for (uint32_t df = 0; df < destFrames; ++df) {
        float srcPos = (float)df * ratio;
        uint32_t sf0 = (uint32_t)srcPos;
        uint32_t sf1 = (sf0 + 1 < srcFrames) ? sf0 + 1 : sf0;
        float frac = srcPos - (float)sf0;

        int16_t l0, r0, l1, r1;
        if (channels == 1) {
            l0 = r0 = srcSamples[sf0];
            l1 = r1 = srcSamples[sf1];
        } else {
            l0 = srcSamples[sf0 * 2];
            r0 = srcSamples[sf0 * 2 + 1];
            l1 = srcSamples[sf1 * 2];
            r1 = srcSamples[sf1 * 2 + 1];
        }

        int16_t outL = (int16_t)((1.0f - frac) * l0 + frac * l1);
        int16_t outR = (int16_t)((1.0f - frac) * r0 + frac * r1);

        outAudio.samples[df * 2]     = outL;
        outAudio.samples[df * 2 + 1] = outR;
    }

    return true;
}

void AudioSystem::generateFallbackSound(SoundID id, DecodedAudio& outAudio) {
    // High-quality procedural waveform synthesis when Sound BSA file is not found
    float duration = 0.08f;
    float startFreq = 440.0f;
    float endFreq = 440.0f;
    float noiseLevel = 0.0f;

    switch (id) {
        case SoundID::GunFire10mm:
        case SoundID::GunFireShotgun:
        case SoundID::GunFireRifle:
            duration = 0.28f;
            startFreq = 160.0f; endFreq = 40.0f;
            noiseLevel = 0.85f;
            break;
        case SoundID::GunFireLaser:
            duration = 0.22f;
            startFreq = 880.0f; endFreq = 220.0f;
            noiseLevel = 0.15f;
            break;
        case SoundID::GunReloadIn:
        case SoundID::GunReloadOut:
            duration = 0.14f;
            startFreq = 300.0f; endFreq = 650.0f;
            noiseLevel = 0.40f;
            break;
        case SoundID::GunDryFire:
            duration = 0.05f;
            startFreq = 1200.0f; endFreq = 800.0f;
            noiseLevel = 0.35f;
            break;
        case SoundID::PipBoyTab:
            duration = 0.045f;
            startFreq = 660.0f; endFreq = 880.0f;
            break;
        case SoundID::PipBoySelect:
            duration = 0.065f;
            startFreq = 880.0f; endFreq = 1320.0f;
            break;
        case SoundID::PipBoyLightOn:
        case SoundID::PipBoyLightOff:
            duration = 0.05f;
            startFreq = 520.0f; endFreq = 380.0f;
            noiseLevel = 0.25f;
            break;
        case SoundID::FootstepWood:
            duration = 0.09f;
            startFreq = 120.0f; endFreq = 60.0f;
            noiseLevel = 0.60f;
            break;
        case SoundID::QuestNew:
            duration = 0.55f;
            startFreq = 587.33f; endFreq = 880.0f; // D5 -> A5 fanfare
            break;
        case SoundID::DocWoahThere:
            duration = 0.40f;
            startFreq = 240.0f; endFreq = 220.0f;
            break;
        default:
            duration = 0.05f;
            startFreq = 440.0f; endFreq = 440.0f;
            break;
    }

    uint32_t frames = (uint32_t)(duration * (float)SAMPLE_RATE);
    outAudio.samples.resize(frames * 2);
    outAudio.sampleFrames = frames;

    float phase = 0.0f;
    for (uint32_t f = 0; f < frames; ++f) {
        float t = (float)f / (float)frames;
        float freq = (1.0f - t) * startFreq + t * endFreq;
        phase += 2.0f * 3.14159265f * freq / (float)SAMPLE_RATE;
        float env = 1.0f - t; // Linear decay envelope

        float tone = sinf(phase);
        float noise = ((float)(rand() % 2000) / 1000.0f - 1.0f);
        float sample = ((1.0f - noiseLevel) * tone + noiseLevel * noise) * env;

        int16_t s16 = (int16_t)(sample * 24000.0f);
        outAudio.samples[f * 2]     = s16;
        outAudio.samples[f * 2 + 1] = s16;
    }
}

bool AudioSystem::loadWav(SoundID id, const char* folder, const char* file) {
    DecodedAudio audio;
    bool loaded = false;

    if (m_soundBsa && m_soundBsa->isOpen()) {
        std::vector<uint8_t> wavBytes;
        if (m_soundBsa->extractFile(folder, file, wavBytes)) {
            if (parseAndResampleWav(wavBytes.data(), wavBytes.size(), audio)) {
                logMsg("AudioSystem: loaded '%s\\%s' (%u frames, %.2fs)",
                       folder, file, audio.sampleFrames, (float)audio.sampleFrames / (float)SAMPLE_RATE);
                loaded = true;
            } else {
                logMsg("AudioSystem: failed to parse WAV '%s\\%s'", folder, file);
            }
        }
    }

    if (!loaded) {
        generateFallbackSound(id, audio);
    }

    m_soundCache[(int)id] = std::move(audio);
    return true;
}

void AudioSystem::play(SoundID id, float volume, float pan) {
    auto it = m_soundCache.find((int)id);
    if (it != m_soundCache.end()) {
        playRaw(it->second, volume, pan, false);
    }
}

void AudioSystem::playRaw(const DecodedAudio& audio, float volume, float pan, bool loop) {
    if (m_voiceMutex < 0 || audio.sampleFrames == 0 || audio.samples.empty()) return;
    sceKernelLockMutex(m_voiceMutex, 1, nullptr);

    // Clamp volume and pan
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;

    float volL = volume * (pan <= 0.0f ? 1.0f : (1.0f - pan));
    float volR = volume * (pan >= 0.0f ? 1.0f : (1.0f + pan));

    // Find next available voice channel (or steal oldest/finished voice)
    int voiceIdx = -1;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!m_voices[i].active) {
            voiceIdx = i;
            break;
        }
    }

    if (voiceIdx < 0) {
        voiceIdx = rand() % MAX_VOICES; // Steal random active voice if all full
    }

    AudioVoice& voice = m_voices[voiceIdx];
    voice.samples = audio.samples.data();
    voice.totalFrames = audio.sampleFrames;
    voice.currentFrame = 0;
    voice.volumeLeft = volL;
    voice.volumeRight = volR;
    voice.loop = loop;
    voice.active = true;
    sceKernelUnlockMutex(m_voiceMutex, 1);
}

void AudioSystem::stopAll() {
    if (m_voiceMutex < 0) return;
    sceKernelLockMutex(m_voiceMutex, 1, nullptr);
    for (int i = 0; i < MAX_VOICES; ++i) {
        m_voices[i].active = false;
        m_voices[i].samples = nullptr;
    }
    sceKernelUnlockMutex(m_voiceMutex, 1);
}
