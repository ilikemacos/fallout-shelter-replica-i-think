#pragma once
// Readable, non-gory combat resolution shared by shelter incursions and
// surface expeditions. Runs on the fixed simulation tick.
#include "sim/Resident.hpp"
#include "core/Random.hpp"
#include <string>
#include <vector>

namespace hv::gameplay {

using namespace hv::sim;

enum class EnemyKind : u8 {
    Scavenger = 0,   ///< opportunist humans, light gear
    Raider,          ///< organised humans, real weapons
    Marauder,        ///< heavy raiders with armour
    Crawler,         ///< burrowing creature, fast and fragile
    Ravager,         ///< large creature, slow and very tough
    StingSwarm,      ///< many weak attackers at once
    SentryDrone,     ///< pre-collapse machine, accurate, armoured
    Count
};

struct EnemyDef {
    EnemyKind kind = EnemyKind::Scavenger;
    const char* name = "";
    const char* plural = "";
    f32 health = 40.0f;
    f32 damage = 6.0f;
    f32 fireRate = 1.0f;
    f32 accuracy = 0.55f;
    f32 armor = 0.0f;
    f32 speed = 1.6f;        ///< world units per second when moving in the shelter
    f32 threat = 1.0f;       ///< used to size waves
    f32 scripReward = 12.0f;
    f32 materialReward = 8.0f;
    Vec3 tint{0.6f, 0.55f, 0.5f};
    f32 scale = 1.0f;
};

const EnemyDef& enemyDef(EnemyKind k);
const char* enemyName(EnemyKind k);

/// One participant. Residents and enemies share the same shape so the
/// resolver does not branch on which side it is looking at.
struct Combatant {
    u32  sourceId = 0;         ///< ResidentId, or a per-encounter enemy id
    bool isResident = false;
    EnemyKind kind = EnemyKind::Scavenger;
    std::string name;
    f32  health = 0.0f;
    f32  maxHealth = 1.0f;
    f32  damage = 1.0f;
    f32  fireRate = 1.0f;
    f32  accuracy = 0.5f;
    f32  armorReduction = 0.0f;  ///< 0..1
    f32  cover = 0.0f;           ///< 0..1, cuts incoming accuracy
    f32  attackTimer = 0.0f;
    i32  team = 0;               ///< 0 = shelter, 1 = hostile
    i32  target = -1;
    Vec3 position;
    bool alive() const { return health > 0.0f; }
    f32  healthFraction() const { return maxHealth > 0 ? saturate(health / maxHealth) : 0.0f; }
};

struct CombatEvent {
    std::string text;
    f32 time = 0.0f;
    bool friendlyCasualty = false;
};

enum class CombatState : u8 { Active = 0, ShelterVictory, ShelterDefeat, Stalemate };

class Encounter {
public:
    void reset();
    int  addResident(const Resident& r, f32 cover, f32 techDefenceBonus);
    int  addEnemy(EnemyKind kind, u32 id, f32 difficultyScale);
    /// Advances by dt seconds. Returns true while the fight continues.
    bool tick(f32 dt, Rng& rng);
    CombatState state() const { return state_; }
    const std::vector<Combatant>& combatants() const { return combatants_; }
    std::vector<Combatant>& combatants() { return combatants_; }
    const std::vector<CombatEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }
    f32  elapsed() const { return elapsed_; }
    int  aliveOn(int team) const;
    f32  teamHealthFraction(int team) const;
    /// Damage dealt to residents this encounter, keyed by resident id.
    const std::vector<std::pair<ResidentId, f32>>& residentDamage() const { return residentDamage_; }
    const std::vector<std::pair<ResidentId, u32>>& residentKills() const { return residentKills_; }
    f32 scripReward() const { return scripReward_; }
    f32 materialReward() const { return materialReward_; }

private:
    int  pickTarget(const Combatant& attacker, Rng& rng) const;
    void log(const std::string& text, bool friendlyCasualty = false);
    void recordDamage(ResidentId id, f32 amount);
    void recordKill(ResidentId id);

    std::vector<Combatant>  combatants_;
    std::vector<CombatEvent> events_;
    std::vector<std::pair<ResidentId, f32>> residentDamage_;
    std::vector<std::pair<ResidentId, u32>> residentKills_;
    CombatState state_ = CombatState::Active;
    f32 elapsed_ = 0.0f;
    f32 scripReward_ = 0.0f;
    f32 materialReward_ = 0.0f;
};

} // namespace hv::gameplay
