#pragma once
// All audio in Haven is procedurally synthesized at runtime — hums, clicks,
// alerts, footsteps, doors — so there are zero licensed or third-party audio
// assets anywhere in the build. The synthesis happens in CoreAudioBackend.mm;
// this header is the platform-independent control surface the game calls.
#include "core/Types.hpp"
#include "core/Math.hpp"
#include "core/Settings.hpp"
#include <memory>
#include <string>

namespace hv::audio {

enum class Sfx : u8 {
    UIClick = 0, UIHover, UIConfirm, UIError, UIOpenPanel, UIClosePanel,
    Footstep, DoorOpen, DoorClose, ElevatorMove, ElevatorArrive,
    AlertWarning, AlertCritical, Construction, Collect, Rush,
    CombatHit, CombatMiss, ExplosionSmall, LevelUp, Notification,
};

enum class Ambience : u8 { None = 0, ShelterHum, ShelterDeep, Surface, Combat };

class AudioSystem {
public:
    static std::unique_ptr<AudioSystem> create();
    virtual ~AudioSystem() = default;

    virtual void setSettings(const AudioSettings& s) = 0;
    virtual void playSfx(Sfx sfx, f32 volume = 1.0f, f32 pitch = 1.0f, Vec3 worldPos = {}) = 0;
    /// Crossfades to a new ambience loop over `fadeSeconds`.
    virtual void setAmbience(Ambience amb, f32 fadeSeconds = 2.0f) = 0;
    /// 0 = silence (deep floor, no music), 1 = full theme presence.
    virtual void setMusicIntensity(f32 intensity) = 0;
    virtual void setListener(Vec3 position, Vec3 forward) = 0;
    virtual void update(f32 dt) = 0;
};

} // namespace hv::audio
