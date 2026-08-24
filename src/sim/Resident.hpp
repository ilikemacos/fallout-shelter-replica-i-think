#pragma once
#include "sim/SimTypes.hpp"
#include "sim/ItemDatabase.hpp"
#include "core/Random.hpp"
#include <string>
#include <vector>

namespace hv::sim {

using ResidentId = u32;
using RoomId     = u32;
constexpr ResidentId kNoResident = 0;
constexpr RoomId     kNoRoom = 0;

enum class Activity : u8 {
    Idle = 0, Walking, RidingElevator, Working, Eating, Drinking, Sleeping,
    Socialising, Training, Relaxing, Recovering, Firefighting, Repairing,
    Fighting, OnExpedition, Dead
};
const char* activityName(Activity a);

/// Personality biases the utility AI's scoring; it is not just flavour.
enum class Personality : u8 {
    Steady = 0, Driven, Anxious, Gregarious, Solitary, Reckless, Meticulous, Cynical, Count
};
const char* personalityName(Personality p);

/// Traits are a bitfield so a resident can hold several.
enum TraitFlag : u32 {
    Trait_None         = 0,
    Trait_HardWorker   = 1u << 0,   ///< +production
    Trait_NightOwl     = 1u << 1,   ///< no penalty working the late shift
    Trait_Fragile      = 1u << 2,   ///< -max health
    Trait_Brave        = 1u << 3,   ///< better in combat, resists panic
    Trait_Squeamish    = 1u << 4,   ///< morale hit from casualties
    Trait_Sociable     = 1u << 5,   ///< builds relationships fast
    Trait_QuickStudy   = 1u << 6,   ///< faster training and XP
    Trait_Tinkerer     = 1u << 7,   ///< repairs faster
    Trait_GreenThumb   = 1u << 8,   ///< better in hydroponics
    Trait_IronStomach  = 1u << 9,   ///< eats less
    Trait_Scavenger    = 1u << 10,  ///< better expedition loot
    Trait_Insomniac    = 1u << 11,  ///< sleeps poorly
    Trait_Claustrophobe= 1u << 12,  ///< morale drops on deep floors
    Trait_Count        = 13
};
const char* traitName(u32 singleFlag);
const char* traitDescription(u32 singleFlag);

struct Appearance {
    u8 skinTone = 0;      ///< index into the palette
    u8 hairStyle = 0;
    u8 hairTone = 0;
    u8 faceVariant = 0;
    u8 outfitTint = 0;
    f32 height = 1.0f;    ///< 0.9 .. 1.1 multiplier
    f32 build = 1.0f;     ///< 0.85 .. 1.15 shoulder/torso width
};

struct Relationship {
    ResidentId other = kNoResident;
    f32 affinity = 0.0f;   ///< -100 .. +100
};

struct Resident {
    ResidentId id = kNoResident;
    std::string name;
    bool female = false;
    f32  age = 20.0f;               ///< years, advances with game time
    Appearance appearance;
    Personality personality = Personality::Steady;
    u32  traits = Trait_None;

    SkillSet skills;
    i32  level = 1;
    f32  experience = 0.0f;

    f32  health = 100.0f;
    f32  maxHealth = 100.0f;
    f32  happiness = 65.0f;         ///< 0..100
    f32  energy = 100.0f;           ///< 0..100, drains while awake
    f32  hunger = 0.0f;             ///< 0..100, 100 = starving
    f32  thirst = 0.0f;
    f32  radiation = 0.0f;          ///< 0..100, caps effective max health

    RoomId assignedRoom = kNoRoom;  ///< job posting
    RoomId currentRoom = kNoRoom;   ///< where the body actually is
    Activity activity = Activity::Idle;
    f32  activityTimer = 0.0f;

    Vec3 position;                  ///< world space, driven by the AI/anim layer
    Vec3 velocity;
    f32  facing = 0.0f;             ///< radians about Y
    Cell cell;                      ///< cell the body is standing in

    ItemStack weapon;
    ItemStack outfit;
    ItemStack utility;
    std::vector<ItemStack> inventory;

    std::vector<Relationship> relationships;

    u32  expeditionId = 0;          ///< non-zero while away on the surface
    f32  lastMealTime = 0.0f;
    f32  totalWorkedHours = 0.0f;
    u32  killCount = 0;
    bool selected = false;

    // ---- derived helpers ---------------------------------------------------
    bool alive() const { return health > 0.0f && activity != Activity::Dead; }
    bool available() const { return alive() && expeditionId == 0; }
    bool hasTrait(TraitFlag f) const { return (traits & f) != 0; }
    f32  effectiveMaxHealth() const { return maxHealth * (1.0f - saturate(radiation / 100.0f) * 0.6f); }
    f32  healthFraction() const {
        const f32 m = effectiveMaxHealth();
        return m > 0.0f ? saturate(health / m) : 0.0f;
    }
    /// Skill value including equipment bonuses, 1..14.
    i32  effectiveSkill(Skill s) const;
    /// 0..1 multiplier applied to a room's output for this worker.
    f32  workEfficiency(Skill primary) const;
    /// Combat numbers, equipment included.
    f32  attackDamage() const;
    f32  attackRate() const;
    f32  hitChance() const;
    f32  damageReduction() const;
    /// XP needed to reach the next level.
    f32  experienceToNext() const { return 100.0f + 55.0f * static_cast<f32>(level - 1); }
    void grantExperience(f32 xp);
    f32  affinityWith(ResidentId other) const;
    void adjustAffinity(ResidentId other, f32 delta);
    /// Short human-readable status for panels and tooltips.
    std::string statusLine() const;
};

/// Generates a plausible new resident. `tier` nudges starting skill quality.
Resident makeResident(Rng& rng, ResidentId id, f32 gameTimeHours, i32 tier = 0);
/// A child of two residents, inheriting skill and appearance tendencies.
Resident makeChild(Rng& rng, ResidentId id, const Resident& a, const Resident& b);

} // namespace hv::sim
