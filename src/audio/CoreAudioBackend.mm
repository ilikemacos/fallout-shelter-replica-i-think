// Procedural audio backend using AudioToolbox's default output AudioUnit.
// Every sound — ambience, UI, alerts, footsteps — is synthesized sample by
// sample in the render callback; nothing is loaded from disk.
#include "audio/AudioSystem.hpp"
#include "core/Log.hpp"
#include "core/Random.hpp"
#import <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace hv::audio {
namespace {

constexpr f64 kSampleRate = 44100.0;
constexpr int kMaxVoices = 24;

/// One playing sound: a tiny synth recipe evaluated per-sample. `kind`
/// selects the waveform/envelope shape in render(); `t` is elapsed seconds.
struct Voice {
    bool active = false;
    Sfx kind = Sfx::UIClick;
    f32 t = 0.0f;
    f32 duration = 0.2f;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    f32 pan = 0.0f;
    u32 seed = 1;
};

struct AmbienceState {
    Ambience current = Ambience::None;
    Ambience target = Ambience::None;
    f32 crossfade = 0.0f;
    f32 fadeDuration = 2.0f;
    f32 phaseA = 0.0f, phaseB = 0.0f, phaseC = 0.0f;
};

f32 envelopeAD(f32 t, f32 attack, f32 total) {
    if (t < attack) return t / std::max(attack, 1e-4f);
    const f32 rel = (t - attack) / std::max(total - attack, 1e-4f);
    return std::max(0.0f, 1.0f - rel);
}

/// One sample of a given SFX recipe. Deliberately simple — sine bursts,
/// filtered noise, short two-tone sweeps — enough to read clearly as UI
/// and environmental feedback without needing licensed audio assets.
f32 renderVoiceSample(Voice& v, f64 tGlobal) {
    const f32 t = v.t;
    f32 s = 0.0f;
    hv::Rng noiseRng(v.seed + static_cast<u32>(tGlobal * kSampleRate));
    switch (v.kind) {
        case Sfx::UIClick:
            s = std::sin(2.0 * kPi * 1400.0 * v.pitch * t) * envelopeAD(t, 0.001f, 0.05f);
            break;
        case Sfx::UIHover:
            s = std::sin(2.0 * kPi * 900.0 * v.pitch * t) * envelopeAD(t, 0.001f, 0.03f) * 0.5f;
            break;
        case Sfx::UIConfirm:
            s = std::sin(2.0 * kPi * (700.0 + 500.0 * t) * v.pitch * t) * envelopeAD(t, 0.005f, 0.18f);
            break;
        case Sfx::UIError:
            s = std::sin(2.0 * kPi * (280.0 - 80.0 * t) * v.pitch * t) * envelopeAD(t, 0.005f, 0.25f);
            break;
        case Sfx::UIOpenPanel:
            s = std::sin(2.0 * kPi * (500.0 + 700.0 * t) * v.pitch * t) * envelopeAD(t, 0.01f, 0.12f) * 0.6f;
            break;
        case Sfx::UIClosePanel:
            s = std::sin(2.0 * kPi * (500.0 - 300.0 * t) * v.pitch * t) * envelopeAD(t, 0.01f, 0.1f) * 0.6f;
            break;
        case Sfx::Footstep:
            s = (noiseRng.unit() * 2.0f - 1.0f) * envelopeAD(t, 0.001f, 0.06f) * 0.35f;
            break;
        case Sfx::DoorOpen:
        case Sfx::DoorClose:
            s = std::sin(2.0 * kPi * 90.0 * v.pitch * t) * envelopeAD(t, 0.02f, 0.4f) * 0.8f +
                (noiseRng.unit() * 2.0f - 1.0f) * envelopeAD(t, 0.02f, 0.4f) * 0.1f;
            break;
        case Sfx::ElevatorMove:
            s = std::sin(2.0 * kPi * 60.0 * t) * 0.25f + (noiseRng.unit() * 2.0f - 1.0f) * 0.04f;
            break;
        case Sfx::ElevatorArrive:
            s = std::sin(2.0 * kPi * 1200.0 * t) * envelopeAD(t, 0.005f, 0.15f) * 0.4f;
            break;
        case Sfx::AlertWarning:
            s = std::sin(2.0 * kPi * (600.0 + 200.0 * std::sin(t * 6.0)) * t) * 0.5f;
            break;
        case Sfx::AlertCritical:
            s = std::sin(2.0 * kPi * (500.0 + 300.0 * std::sin(t * 12.0)) * t) * 0.6f;
            break;
        case Sfx::Construction:
            s = (noiseRng.unit() * 2.0f - 1.0f) * envelopeAD(t, 0.01f, 0.3f) * 0.4f +
                std::sin(2.0 * kPi * 140.0 * t) * envelopeAD(t, 0.01f, 0.3f) * 0.3f;
            break;
        case Sfx::Collect:
            s = std::sin(2.0 * kPi * (900.0 + 400.0 * t) * t) * envelopeAD(t, 0.005f, 0.12f) * 0.5f;
            break;
        case Sfx::Rush:
            s = std::sin(2.0 * kPi * (300.0 + 900.0 * t) * t) * envelopeAD(t, 0.01f, 0.25f) * 0.6f;
            break;
        case Sfx::CombatHit:
            s = (noiseRng.unit() * 2.0f - 1.0f) * envelopeAD(t, 0.002f, 0.08f) * 0.7f;
            break;
        case Sfx::CombatMiss:
            s = std::sin(2.0 * kPi * 2000.0 * t) * envelopeAD(t, 0.001f, 0.04f) * 0.25f;
            break;
        case Sfx::ExplosionSmall:
            s = (noiseRng.unit() * 2.0f - 1.0f) * envelopeAD(t, 0.005f, 0.5f) * 0.8f +
                std::sin(2.0 * kPi * 60.0 * t) * envelopeAD(t, 0.005f, 0.5f) * 0.4f;
            break;
        case Sfx::LevelUp:
            s = std::sin(2.0 * kPi * (500.0 + 900.0 * t) * t) * envelopeAD(t, 0.01f, 0.35f) * 0.5f;
            break;
        case Sfx::Notification:
            s = std::sin(2.0 * kPi * 1100.0 * t) * envelopeAD(t, 0.003f, 0.09f) * 0.4f;
            break;
    }
    return s * v.volume;
}

class CoreAudioBackend final : public AudioSystem {
public:
    CoreAudioBackend() { setupUnit(); }
    ~CoreAudioBackend() override {
        if (unit_) { AudioOutputUnitStop(unit_); AudioUnitUninitialize(unit_); AudioComponentInstanceDispose(unit_); }
    }

    void setSettings(const AudioSettings& s) override {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = s;
    }

    void playSfx(Sfx sfx, f32 volume, f32 pitch, Vec3 worldPos) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Voice& v : voices_) {
            if (v.active) continue;
            v.active = true; v.kind = sfx; v.t = 0.0f; v.pitch = pitch;
            v.volume = volume * masterSfxVolume();
            v.duration = 0.6f;
            v.pan = clampf(worldPos.x * 0.05f, -1.0f, 1.0f);
            v.seed = static_cast<u32>(reinterpret_cast<uintptr_t>(&v)) ^ 0x9E3779B9u;
            return;
        }
    }

    void setAmbience(Ambience amb, f32 fadeSeconds) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ambience_.target = amb;
        ambience_.fadeDuration = std::max(0.1f, fadeSeconds);
        ambience_.crossfade = 0.0f;
    }

    void setMusicIntensity(f32 intensity) override {
        std::lock_guard<std::mutex> lock(mutex_);
        musicIntensity_ = saturate(intensity);
    }

    void setListener(Vec3 position, Vec3 forward) override {
        (void)position; (void)forward;   // stereo panning uses sfx worldPos.x directly for now
    }

    void update(f32 dt) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ambience_.current != ambience_.target) {
            ambience_.crossfade += dt;
            if (ambience_.crossfade >= ambience_.fadeDuration) {
                ambience_.current = ambience_.target;
                ambience_.crossfade = 0.0f;
            }
        }
    }

    // Called from the CoreAudio render thread — must stay lock-light.
    void render(f32* buffer, UInt32 frames, UInt32 channels) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        const bool haveLock = lock.owns_lock();
        const AudioSettings settings = haveLock ? settings_ : AudioSettings{};

        for (UInt32 i = 0; i < frames; ++i) {
            f32 mix = 0.0f;

            // Ambience: a soft low hum plus filtered noise, deeper/darker the
            // further underground the player currently is (driven by depthT).
            const f64 dt = 1.0 / kSampleRate;
            ambience_.phaseA += static_cast<f32>(dt) * 55.0f;
            ambience_.phaseB += static_cast<f32>(dt) * 82.0f;
            ambience_.phaseC += static_cast<f32>(dt) * 0.7f;
            f32 amb = 0.0f;
            auto ambienceLevel = [](Ambience a) { return a == Ambience::None ? 0.0f : 1.0f; };
            const f32 curLevel = ambienceLevel(ambience_.current);
            const f32 fadeT = ambience_.fadeDuration > 0 ? saturate(ambience_.crossfade / ambience_.fadeDuration) : 1.0f;
            const f32 tgtLevel = ambienceLevel(ambience_.target);
            const f32 level = lerpf(curLevel, tgtLevel, fadeT) * 0.06f;
            amb += std::sin(2.0 * kPi * ambience_.phaseA) * level;
            amb += std::sin(2.0 * kPi * ambience_.phaseB) * level * 0.5f;
            amb += std::sin(2.0 * kPi * ambience_.phaseC * 4.0) * level * 0.15f;
            mix += amb * settings.ambience * (settings.muted ? 0.0f : 1.0f);

            for (Voice& v : voices_) {
                if (!v.active) continue;
                mix += renderVoiceSample(v, v.t) * settings.sfx * (settings.muted ? 0.0f : 1.0f);
                v.t += static_cast<f32>(dt);
                if (v.t >= v.duration) v.active = false;
            }

            mix *= settings.master;
            mix = clampf(mix, -1.0f, 1.0f);
            for (UInt32 c = 0; c < channels; ++c) buffer[i * channels + c] = mix;
        }
    }

private:
    f32 masterSfxVolume() const { return settings_.muted ? 0.0f : settings_.master * settings_.sfx; }

    static OSStatus renderCallback(void* inRefCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                                   UInt32, UInt32 inNumberFrames, AudioBufferList* ioData) {
        auto* self = static_cast<CoreAudioBackend*>(inRefCon);
        if (ioData->mNumberBuffers > 0) {
            auto* out = static_cast<f32*>(ioData->mBuffers[0].mData);
            self->render(out, inNumberFrames, ioData->mBuffers[0].mNumberChannels);
        }
        return noErr;
    }

    void setupUnit() {
        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        if (!comp) { HV_ERROR("CoreAudio: no default output component"); return; }
        if (AudioComponentInstanceNew(comp, &unit_) != noErr) { HV_ERROR("CoreAudio: instance creation failed"); return; }

        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = kSampleRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4 * fmt.mChannelsPerFrame;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerPacket = fmt.mBytesPerFrame;
        AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);

        AURenderCallbackStruct cb{};
        cb.inputProc = renderCallback;
        cb.inputProcRefCon = this;
        AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);

        if (AudioUnitInitialize(unit_) != noErr) { HV_ERROR("CoreAudio: init failed"); return; }
        if (AudioOutputUnitStart(unit_) != noErr) { HV_ERROR("CoreAudio: start failed"); return; }
        HV_INFO("CoreAudio backend running at %.0f Hz", kSampleRate);
    }

    AudioUnit unit_ = nullptr;
    std::mutex mutex_;
    AudioSettings settings_;
    Voice voices_[kMaxVoices];
    AmbienceState ambience_;
    f32 musicIntensity_ = 0.0f;
};

} // namespace

std::unique_ptr<AudioSystem> AudioSystem::create() { return std::make_unique<CoreAudioBackend>(); }

} // namespace hv::audio
