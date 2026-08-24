#include "gameplay/Combat.hpp"
#include <algorithm>

namespace hv::gameplay {
namespace {

std::vector<EnemyDef> buildEnemies() {
    std::vector<EnemyDef> v(static_cast<size_t>(EnemyKind::Count));
    auto set = [&](EnemyKind k, const char* n, const char* pl, f32 hp, f32 dmg, f32 rate,
                   f32 acc, f32 armor, f32 speed, f32 threat, f32 scrip, f32 mat,
                   u32 tint, f32 scale) {
        EnemyDef& d = v[static_cast<size_t>(k)];
        d.kind = k; d.name = n; d.plural = pl; d.health = hp; d.damage = dmg;
        d.fireRate = rate; d.accuracy = acc; d.armor = armor; d.speed = speed;
        d.threat = threat; d.scripReward = scrip; d.materialReward = mat;
        d.tint = colorFromHex(tint); d.scale = scale;
    };
    set(EnemyKind::Scavenger,  "Scavenger",   "Scavengers",   38,  6.0f, 1.0f, 0.52f, 0.02f, 1.7f, 1.0f, 10,  7, 0x8A7F6A, 0.98f);
    set(EnemyKind::Raider,     "Raider",      "Raiders",      62, 10.0f, 1.1f, 0.58f, 0.08f, 1.8f, 1.8f, 22, 14, 0x7A5A46, 1.02f);
    set(EnemyKind::Marauder,   "Marauder",    "Marauders",   110, 16.0f, 0.9f, 0.60f, 0.22f, 1.5f, 3.2f, 45, 28, 0x5E4B3C, 1.12f);
    set(EnemyKind::Crawler,    "Crawler",     "Crawlers",     34, 11.0f, 1.6f, 0.62f, 0.00f, 2.9f, 1.5f,  6, 10, 0x6B7355, 0.85f);
    set(EnemyKind::Ravager,    "Ravager",     "Ravagers",    210, 26.0f, 0.55f, 0.55f, 0.28f, 1.3f, 5.5f, 70, 55, 0x4F5B43, 1.55f);
    set(EnemyKind::StingSwarm, "Sting Swarm", "Sting Swarms", 18,  4.5f, 2.4f, 0.48f, 0.00f, 3.2f, 0.9f,  3,  4, 0x8C7A3E, 0.62f);
    set(EnemyKind::SentryDrone,"Sentry Drone","Sentry Drones",90, 14.0f, 1.3f, 0.72f, 0.30f, 1.9f, 3.8f, 55, 40, 0x59636B, 1.05f);
    return v;
}

} // namespace

const EnemyDef& enemyDef(EnemyKind k) {
    static const std::vector<EnemyDef> table = buildEnemies();
    const size_t i = static_cast<size_t>(k);
    return table[i < table.size() ? i : 0];
}

const char* enemyName(EnemyKind k) { return enemyDef(k).name; }

void Encounter::reset() {
    combatants_.clear();
    events_.clear();
    residentDamage_.clear();
    residentKills_.clear();
    state_ = CombatState::Active;
    elapsed_ = 0.0f;
    scripReward_ = materialReward_ = 0.0f;
}

int Encounter::addResident(const Resident& r, f32 cover, f32 techDefenceBonus) {
    Combatant c;
    c.sourceId = r.id;
    c.isResident = true;
    c.name = r.name;
    c.health = r.health;
    c.maxHealth = r.effectiveMaxHealth();
    c.damage = r.attackDamage();
    c.fireRate = r.attackRate();
    c.accuracy = r.hitChance();
    c.armorReduction = clampf(r.damageReduction() + techDefenceBonus, 0.0f, 0.85f);
    c.cover = saturate(cover);
    c.team = 0;
    c.position = r.position;
    // Stagger the first swing so everyone does not fire on the same tick.
    c.attackTimer = 0.0f;
    combatants_.push_back(std::move(c));
    return static_cast<int>(combatants_.size()) - 1;
}

int Encounter::addEnemy(EnemyKind kind, u32 id, f32 difficultyScale) {
    const EnemyDef& d = enemyDef(kind);
    Combatant c;
    c.sourceId = id;
    c.isResident = false;
    c.kind = kind;
    c.name = d.name;
    c.maxHealth = d.health * difficultyScale;
    c.health = c.maxHealth;
    c.damage = d.damage * difficultyScale;
    c.fireRate = d.fireRate;
    c.accuracy = d.accuracy;
    c.armorReduction = d.armor;
    c.team = 1;
    combatants_.push_back(std::move(c));
    scripReward_ += d.scripReward * difficultyScale;
    materialReward_ += d.materialReward * difficultyScale;
    return static_cast<int>(combatants_.size()) - 1;
}

int Encounter::aliveOn(int team) const {
    int n = 0;
    for (const Combatant& c : combatants_) if (c.team == team && c.alive()) ++n;
    return n;
}

f32 Encounter::teamHealthFraction(int team) const {
    f32 cur = 0.0f, max = 0.0f;
    for (const Combatant& c : combatants_) {
        if (c.team != team) continue;
        cur += std::max(0.0f, c.health);
        max += c.maxHealth;
    }
    return max > 0.0f ? cur / max : 0.0f;
}

int Encounter::pickTarget(const Combatant& attacker, Rng& rng) const {
    // Teams focus fire on whoever is closest to going down, with a little
    // noise so it does not look robotic.
    int best = -1;
    f32 bestScore = 1e30f;
    for (size_t i = 0; i < combatants_.size(); ++i) {
        const Combatant& c = combatants_[i];
        if (c.team == attacker.team || !c.alive()) continue;
        f32 score = c.health * (1.0f + c.cover) * rng.range(0.85f, 1.25f);
        if (score < bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}

void Encounter::log(const std::string& text, bool friendlyCasualty) {
    events_.push_back(CombatEvent{text, elapsed_, friendlyCasualty});
    if (events_.size() > 64) events_.erase(events_.begin());
}

void Encounter::recordDamage(ResidentId id, f32 amount) {
    for (auto& p : residentDamage_) if (p.first == id) { p.second += amount; return; }
    residentDamage_.emplace_back(id, amount);
}

void Encounter::recordKill(ResidentId id) {
    for (auto& p : residentKills_) if (p.first == id) { ++p.second; return; }
    residentKills_.emplace_back(id, 1u);
}

bool Encounter::tick(f32 dt, Rng& rng) {
    if (state_ != CombatState::Active) return false;
    elapsed_ += dt;

    for (size_t i = 0; i < combatants_.size(); ++i) {
        Combatant& a = combatants_[i];
        if (!a.alive()) continue;
        a.attackTimer += dt * a.fireRate;
        if (a.attackTimer < 1.0f) continue;
        a.attackTimer -= 1.0f;

        if (a.target < 0 || a.target >= static_cast<int>(combatants_.size()) ||
            !combatants_[static_cast<size_t>(a.target)].alive() ||
            combatants_[static_cast<size_t>(a.target)].team == a.team) {
            a.target = pickTarget(a, rng);
        }
        if (a.target < 0) continue;

        Combatant& t = combatants_[static_cast<size_t>(a.target)];
        const f32 chance = clampf(a.accuracy - t.cover * 0.35f, 0.05f, 0.95f);
        if (!rng.chance(chance)) {
            if (rng.chance(0.12f))
                log(a.name + " misses " + t.name + ".");
            continue;
        }
        f32 dmg = a.damage * rng.range(0.82f, 1.18f);
        dmg *= (1.0f - t.armorReduction);
        t.health -= dmg;
        if (t.isResident) recordDamage(t.sourceId, dmg);

        if (t.health <= 0.0f) {
            t.health = 0.0f;
            if (t.isResident) {
                // "Down", not gore: residents are incapacitated, not butchered.
                log(t.name + " is down and needs the infirmary.", true);
            } else {
                log(a.name + " puts down the " + t.name + ".");
                if (a.isResident) recordKill(a.sourceId);
            }
        }
    }

    const int shelterAlive = aliveOn(0);
    const int hostileAlive = aliveOn(1);
    if (hostileAlive == 0) state_ = CombatState::ShelterVictory;
    else if (shelterAlive == 0) state_ = CombatState::ShelterDefeat;
    else if (elapsed_ > 240.0f) state_ = CombatState::Stalemate;
    return state_ == CombatState::Active;
}

} // namespace hv::gameplay
