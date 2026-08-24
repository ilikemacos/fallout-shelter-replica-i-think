#include "gameplay/Expedition.hpp"
#include "sim/World.hpp"
#include "sim/Names.hpp"
#include <algorithm>

namespace hv::gameplay {

const char* expeditionPhaseName(ExpeditionPhase p) {
    switch (p) {
        case ExpeditionPhase::Outbound:  return "Travelling out";
        case ExpeditionPhase::Exploring: return "Exploring";
        case ExpeditionPhase::Fighting:  return "In combat";
        case ExpeditionPhase::Returning: return "Returning";
        case ExpeditionPhase::Finished:  return "Home";
        case ExpeditionPhase::Lost:      return "Lost";
    }
    return "?";
}

u32 ExpeditionManager::launch(const Site& site, const std::vector<ResidentId>& squad, Rng& rng) {
    Expedition e;
    e.id = nextId_++;
    e.siteId = site.id;
    e.siteName = site.name;
    e.squad = squad;
    e.danger = site.danger;
    e.travelSeconds = 60.0f + site.distanceKm * 9.0f;
    e.exploreSeconds = 120.0f + static_cast<f32>(site.danger) * 40.0f;
    e.phase = ExpeditionPhase::Outbound;
    e.nextEventIn = rng.range(20.0f, 50.0f);
    e.addLog(0.0f, "Squad departs for " + site.name + ".");
    expeditions_.push_back(std::move(e));
    return expeditions_.back().id;
}

void ExpeditionManager::recall(u32 id) {
    if (Expedition* e = find(id)) {
        e->recalled = true;
        if (e->phase == ExpeditionPhase::Exploring || e->phase == ExpeditionPhase::Outbound)
            e->addLog(e->elapsed / 3600.0f, "Recalled — heading home.");
    }
}

Expedition* ExpeditionManager::find(u32 id) {
    for (Expedition& e : expeditions_) if (e.id == id) return &e;
    return nullptr;
}
const Expedition* ExpeditionManager::find(u32 id) const {
    return const_cast<ExpeditionManager*>(this)->find(id);
}

size_t ExpeditionManager::activeCount() const {
    size_t n = 0;
    for (const Expedition& e : expeditions_) if (e.active()) ++n;
    return n;
}

namespace {

f32 squadPower(const Expedition& e, sim::World& world) {
    f32 power = 0.0f;
    for (ResidentId id : e.squad)
        if (const sim::Resident* r = world.resident(id))
            power += static_cast<f32>(r->effectiveSkill(sim::Skill::Security)) *
                     r->healthFraction();
    return power;
}

} // namespace

void ExpeditionManager::tick(f32 dtSeconds, Rng& rng, sim::World& world) {
    using namespace sim;
    for (Expedition& e : expeditions_) {
        if (!e.active()) continue;
        e.elapsed += dtSeconds;
        const f32 hours = world.gameTimeHours();

        switch (e.phase) {
            case ExpeditionPhase::Outbound: {
                e.phaseTimer += dtSeconds;
                if (e.recalled || e.phaseTimer >= e.travelSeconds) {
                    if (e.recalled) {
                        e.phase = ExpeditionPhase::Returning;
                    } else {
                        e.phase = ExpeditionPhase::Exploring;
                        e.addLog(hours, "Arrived at " + e.siteName + ".");
                    }
                    e.phaseTimer = 0.0f;
                }
                break;
            }
            case ExpeditionPhase::Exploring: {
                e.phaseTimer += dtSeconds;
                e.nextEventIn -= dtSeconds;
                if (e.nextEventIn <= 0.0f) {
                    e.nextEventIn = rng.range(35.0f, 70.0f);
                    const f32 roll = rng.unit();
                    if (roll < 0.35f + 0.05f * static_cast<f32>(e.danger)) {
                        // Combat encounter.
                        e.fight.reset();
                        for (ResidentId id : e.squad)
                            if (const Resident* r = world.resident(id))
                                e.fight.addResident(*r, 0.15f, world.tech().bonuses().defence);
                        const i32 count = 1 + rng.rangeI(0, e.danger);
                        static const EnemyKind kinds[] = {
                            EnemyKind::Scavenger, EnemyKind::Raider, EnemyKind::Crawler,
                            EnemyKind::Marauder, EnemyKind::SentryDrone, EnemyKind::Ravager
                        };
                        const EnemyKind kind = kinds[std::min<i32>(e.danger, 5)];
                        for (i32 i = 0; i < count; ++i)
                            e.fight.addEnemy(kind, static_cast<u32>(i + 1), 0.8f + 0.15f * static_cast<f32>(e.danger));
                        e.phase = ExpeditionPhase::Fighting;
                        e.addLog(hours, std::string(enemyDef(kind).plural) + " engage the squad.", true);
                    } else if (roll < 0.75f) {
                        // Loot find.
                        const u16 itemId = rollLoot(rng, e.danger);
                        e.loot.push_back(ItemStack{itemId, 1, rng.range(0.7f, 1.0f)});
                        e.addLog(hours, "Found a " + std::string(itemDef(itemId).name) + ".");
                        for (int i = 0; i < kResourceCount; ++i) {
                            f32 yield[kResourceCount];
                            if (const gameplay::Site* s = world.surface().find(e.siteId)) {
                                s->expectedYield(yield);
                                e.haul[static_cast<size_t>(i)] += yield[i] * 0.12f * rng.range(0.7f, 1.3f);
                            }
                        }
                    } else {
                        e.addLog(hours, "Nothing here but dust.");
                    }
                }
                if (e.phaseTimer >= e.exploreSeconds || e.recalled) {
                    if (gameplay::Site* s = world.surface().find(e.siteId)) {
                        f32 yield[kResourceCount];
                        s->expectedYield(yield);
                        for (int i = 0; i < kResourceCount; ++i)
                            e.haul[static_cast<size_t>(i)] += yield[i] * (0.5f + 0.5f * squadPower(e, world) / 20.0f);
                        s->depletion = std::min(1.0f, s->depletion + 0.35f);
                        if (s->type == SiteType::RaiderCamp || s->type == SiteType::Nest)
                            s->cleared = true;
                    }
                    e.phase = ExpeditionPhase::Returning;
                    e.phaseTimer = 0.0f;
                    e.addLog(hours, "Turning back for the shelter.");
                }
                break;
            }
            case ExpeditionPhase::Fighting: {
                const bool ongoing = e.fight.tick(dtSeconds, rng);
                for (const auto& c : e.fight.combatants())
                    if (c.isResident)
                        if (Resident* r = world.resident(c.sourceId))
                            r->health = std::min(r->health, c.health);
                e.fight.clearEvents();
                if (!ongoing) {
                    ++e.encountersWon;
                    for (const auto& kv : e.fight.residentKills()) {
                        world.statsMutable().enemiesDefeated += static_cast<i32>(kv.second);
                        world.quests().notify(gameplay::ObjectiveKind::DefeatEnemies, 0, static_cast<f32>(kv.second));
                    }
                    if (e.fight.state() == CombatState::ShelterVictory) {
                        e.haul[static_cast<size_t>(Resource::Scrip)] += e.fight.scripReward();
                        e.haul[static_cast<size_t>(Resource::Materials)] += e.fight.materialReward();
                        e.addLog(hours, "The squad holds; hostiles are down.");
                        e.phase = ExpeditionPhase::Exploring;
                    } else {
                        e.addLog(hours, "The squad breaks off and falls back.", true);
                        e.phase = ExpeditionPhase::Returning;
                    }
                    e.phaseTimer = 0.0f;
                }
                break;
            }
            case ExpeditionPhase::Returning: {
                e.phaseTimer += dtSeconds;
                if (e.phaseTimer >= e.travelSeconds) {
                    bool anyoneAlive = false;
                    for (ResidentId id : e.squad)
                        if (const Resident* r = world.resident(id); r && r->alive()) anyoneAlive = true;
                    e.phase = anyoneAlive ? ExpeditionPhase::Finished : ExpeditionPhase::Lost;
                }
                break;
            }
            default: break;
        }

        if (e.phase == ExpeditionPhase::Finished) {
            for (int i = 0; i < kResourceCount; ++i)
                world.resources().add(static_cast<Resource>(i), e.haul[static_cast<size_t>(i)]);
            for (const ItemStack& it : e.loot) world.addToStorage(it);
            for (ResidentId id : e.squad) {
                Resident* r = world.resident(id);
                if (!r) continue;
                r->expeditionId = 0;
                r->grantExperience(30.0f + 8.0f * static_cast<f32>(e.encountersWon));
                if (r->alive()) {
                    r->activity = Activity::Idle;
                    if (const std::vector<RoomId> entrances = world.shelter().roomsOfType(RoomType::Entrance);
                        !entrances.empty())
                        if (const Room* room = world.shelter().room(entrances.front()))
                            r->position = room->worldCenter();
                }
            }
            world.statsMutable().expeditionsCompleted += 1;
            world.quests().notify(gameplay::ObjectiveKind::CompleteExpeditions, 0, 1.0f);
            world.notify("The squad is home from " + e.siteName + ".", NotifySeverity::Good);
        } else if (e.phase == ExpeditionPhase::Lost) {
            for (ResidentId id : e.squad) {
                Resident* r = world.resident(id);
                if (!r) continue;
                r->expeditionId = 0;
                if (r->health <= 0.0f) {
                    r->activity = Activity::Dead;
                    world.statsMutable().residentsLost += 1;
                } else {
                    r->activity = Activity::Idle;
                }
            }
            world.statsMutable().expeditionsLost += 1;
            world.notify("The squad did not make it back from " + e.siteName + ".",
                        NotifySeverity::Critical);
        }
    }

    expeditions_.erase(std::remove_if(expeditions_.begin(), expeditions_.end(),
        [](const Expedition& e) { return !e.active(); }), expeditions_.end());
}

void ExpeditionManager::reset() {
    expeditions_.clear();
    nextId_ = 1;
}

void ExpeditionManager::serialize(BlobWriter& w) const {
    w.u32v(static_cast<u32>(expeditions_.size()));
    for (const Expedition& e : expeditions_) {
        w.u32v(e.id);
        w.u32v(e.siteId);
        w.str(e.siteName);
        w.u32v(static_cast<u32>(e.squad.size()));
        for (ResidentId id : e.squad) w.u32v(id);
        w.u8v(static_cast<u8>(e.phase));
        w.f32v(e.elapsed);
        w.f32v(e.travelSeconds);
        w.f32v(e.exploreSeconds);
        w.f32v(e.phaseTimer);
        w.f32v(e.nextEventIn);
        for (int i = 0; i < kResourceCount; ++i) w.f32v(e.haul[static_cast<size_t>(i)]);
        w.u32v(static_cast<u32>(e.loot.size()));
        for (const ItemStack& it : e.loot) { w.u16v(it.defId); w.u16v(it.count); w.f32v(it.condition); }
        w.i32v(e.danger);
        w.i32v(e.encountersWon);
        w.boolv(e.recalled);
        w.u32v(e.questId);
    }
    w.u32v(nextId_);
}

bool ExpeditionManager::deserialize(BlobReader& r, u32 version) {
    (void)version;
    reset();
    const u32 n = r.u32v();
    if (r.failed() || n > 64) return false;
    for (u32 i = 0; i < n; ++i) {
        Expedition e;
        e.id = r.u32v();
        e.siteId = r.u32v();
        e.siteName = r.str();
        const u32 squad = r.u32v();
        if (r.failed() || squad > 8) return false;
        for (u32 k = 0; k < squad; ++k) e.squad.push_back(r.u32v());
        const u8 phase = r.u8v();
        e.phase = phase <= static_cast<u8>(ExpeditionPhase::Lost)
                      ? static_cast<ExpeditionPhase>(phase) : ExpeditionPhase::Finished;
        e.elapsed = r.f32v();
        e.travelSeconds = r.f32v();
        e.exploreSeconds = r.f32v();
        e.phaseTimer = r.f32v();
        e.nextEventIn = r.f32v();
        for (int k = 0; k < kResourceCount; ++k) e.haul[static_cast<size_t>(k)] = r.f32v();
        const u32 loot = r.u32v();
        if (r.failed() || loot > 64) return false;
        for (u32 k = 0; k < loot; ++k) {
            ItemStack it;
            it.defId = r.u16v(); it.count = r.u16v(); it.condition = r.f32v();
            e.loot.push_back(it);
        }
        e.danger = r.i32v();
        e.encountersWon = r.i32v();
        e.recalled = r.boolv();
        e.questId = r.u32v();
        if (r.failed()) return false;
        expeditions_.push_back(std::move(e));
    }
    nextId_ = std::max(1u, r.u32v());
    return !r.failed();
}

} // namespace hv::gameplay
