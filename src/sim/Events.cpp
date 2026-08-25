#include "sim/Events.hpp"
#include "sim/World.hpp"
#include "sim/Names.hpp"
#include "core/Log.hpp"
#include <algorithm>

namespace hv::sim {

const char* eventKindName(EventKind k) {
    switch (k) {
        case EventKind::Fire:          return "Fire";
        case EventKind::Breakdown:     return "Equipment Failure";
        case EventKind::Infestation:   return "Infestation";
        case EventKind::Intrusion:     return "Intrusion";
        case EventKind::PowerSurge:    return "Power Surge";
        case EventKind::Contamination: return "Contamination";
        case EventKind::Illness:       return "Illness";
        case EventKind::Newcomer:      return "Newcomer";
        case EventKind::Trader:        return "Trader";
        case EventKind::Discovery:     return "Discovery";
        default: return "?";
    }
}

bool eventIsEmergency(EventKind k) {
    switch (k) {
        case EventKind::Fire:
        case EventKind::Breakdown:
        case EventKind::Infestation:
        case EventKind::Intrusion:
        case EventKind::PowerSurge:
        case EventKind::Contamination:
            return true;
        default:
            return false;
    }
}

void EventSystem::reset() {
    emergencies_.clear();
    trader_ = TraderOffer{};
    nextId_ = 1;
    nextEventTimer_ = 90.0f;
    sinceStart_ = 0.0f;
}

Emergency* EventSystem::find(u32 id) {
    for (Emergency& e : emergencies_) if (e.id == id) return &e;
    return nullptr;
}
Emergency* EventSystem::forRoom(RoomId room) {
    for (Emergency& e : emergencies_) if (e.room == room && !e.resolved) return &e;
    return nullptr;
}
size_t EventSystem::activeCount() const {
    size_t n = 0;
    for (const Emergency& e : emergencies_) if (!e.resolved) ++n;
    return n;
}

EventKind EventSystem::rollEventKind(World& world) {
    // Weighted pool; weights shift with how deep/large the shelter has grown.
    struct W { EventKind kind; f32 weight; };
    const i32 pop = world.population();
    const i32 floors = world.shelter().deepestFloor();
    std::vector<W> pool = {
        {EventKind::Breakdown, 3.0f},
        {EventKind::Fire, 1.6f},
        {EventKind::Newcomer, 2.4f},
        {EventKind::Trader, 1.2f},
        {EventKind::Discovery, 1.0f},
    };
    if (pop >= 10) pool.push_back({EventKind::Intrusion, 1.0f + 0.15f * static_cast<f32>(pop)});
    if (floors >= 2) pool.push_back({EventKind::Infestation, 0.8f + 0.2f * static_cast<f32>(floors)});
    if (pop >= 8) pool.push_back({EventKind::Illness, 0.7f});
    if (pop >= 6) pool.push_back({EventKind::Contamination, 0.6f});
    if (pop >= 12) pool.push_back({EventKind::PowerSurge, 0.6f});

    f32 total = 0.0f;
    for (const W& w : pool) total += w.weight;
    f32 roll = world.rng().unit() * total;
    for (const W& w : pool) {
        if (roll < w.weight) return w.kind;
        roll -= w.weight;
    }
    return EventKind::Breakdown;
}

u32 EventSystem::trigger(EventKind kind, World& world, RoomId room, f32 severity) {
    switch (kind) {
        case EventKind::Newcomer: {
            const ResidentId id = world.spawnNewcomer(world.rng().rangeI(0, 1));
            if (id != kNoResident) {
                if (const Resident* r = world.resident(id))
                    world.notify(r->name + " is at the airlock, asking to join.",
                                NotifySeverity::Info);
            }
            return 0;
        }
        case EventKind::Trader: {
            if (trader_.active) return 0;
            static const Resource sellable[] = {Resource::Materials, Resource::Medicine, Resource::Water, Resource::Food};
            static const Resource buyable[]   = {Resource::Water, Resource::Food, Resource::Materials};
            trader_.active = true;
            trader_.timeLeft = 600.0f;
            trader_.sells = sellable[world.rng().rangeI(0, 3)];
            trader_.sellAmount = std::round(world.rng().range(60.0f, 160.0f));
            trader_.sellPrice = trader_.sellAmount * world.rng().range(0.55f, 0.85f);
            trader_.buys = buyable[world.rng().rangeI(0, 2)];
            trader_.buyAmount = std::round(world.rng().range(60.0f, 160.0f));
            trader_.buyPrice = trader_.buyAmount * world.rng().range(0.5f, 0.75f);
            if (world.rng().chance(0.4f)) {
                trader_.itemForSale = rollLoot(world.rng(), 1);
                trader_.itemPrice = itemDef(trader_.itemForSale).value * world.rng().range(0.7f, 1.1f);
            }
            world.notify("A trader is at the airlock with goods to move.", NotifySeverity::Info);
            return 0;
        }
        case EventKind::Discovery: {
            const u32 siteId = world.surface().discoverNext(world.rng());
            if (siteId != 0) {
                if (const gameplay::Site* s = world.surface().find(siteId))
                    world.notify("Surface scan located " + s->name + ".", NotifySeverity::Info);
            }
            return 0;
        }
        default: break;
    }

    // Emergencies need a room to happen in.
    RoomId target = room;
    if (target == kNoRoom) {
        std::vector<Room*> candidates;
        for (Room& r : world.shelter().rooms()) {
            if (r.buildProgress < 1.0f) continue;
            if (kind == EventKind::Intrusion && r.type != RoomType::Entrance) continue;
            if (kind != EventKind::Intrusion && r.type == RoomType::Entrance) continue;
            candidates.push_back(&r);
        }
        if (candidates.empty()) return 0;
        target = candidates[static_cast<size_t>(world.rng().rangeI(0, static_cast<i32>(candidates.size()) - 1))]->id;
    }
    if (forRoom(target)) return 0;   // already busy

    Emergency e;
    e.id = nextId_++;
    e.kind = kind;
    e.room = target;
    e.severity = severity;
    emergencies_.push_back(e);

    if (Room* r = world.shelter().room(target)) {
        if (kind == EventKind::Fire) r->fire = std::max(r->fire, 0.35f);
        if (kind == EventKind::Breakdown) r->broken = true;
        world.notify(std::string(eventKindName(kind)) + " in the " + roomTypeName(r->type) + "!",
                    NotifySeverity::Critical, target);
    }
    return e.id;
}

void EventSystem::tickEmergency(Emergency& e, f32 dt, World& world) {
    Room* room = world.shelter().room(e.room);
    if (!room) { e.resolved = true; return; }

    switch (e.kind) {
        case EventKind::Fire: {
            e.spreadTimer += dt;
            room->fire = std::min(1.0f, room->fire + dt * 0.01f * e.severity);
            // Firefighters extinguish it; unattended fire can spread to a neighbour.
            i32 fighters = 0;
            for (ResidentId w : e.responders)
                if (const Resident* r = world.resident(w); r && r->alive()) ++fighters;
            if (fighters > 0) room->fire = std::max(0.0f, room->fire - dt * 0.05f * static_cast<f32>(fighters));
            if (room->fire <= 0.0f) {
                e.resolved = true;
                world.statsMutable().firesExtinguished += 1;
                world.notify("Fire in the " + std::string(roomTypeName(room->type)) + " is out.",
                            NotifySeverity::Good, room->id);
            } else if (e.spreadTimer > 45.0f && fighters == 0) {
                e.spreadTimer = 0.0f;
                room->condition = std::max(0.0f, room->condition - 0.15f);
                // Left unattended long enough, the fire can actually jump to
                // an adjacent room — this used to just be a comment.
                if (room->fire > 0.5f && world.rng().chance(0.4f)) {
                    const Cell left{room->floor, room->colStart - 1};
                    const Cell right{room->floor, room->colEnd() + 1};
                    std::vector<Room*> spreadTargets;
                    for (const Cell& c : {left, right}) {
                        Room* neighbour = world.shelter().roomAt(c);
                        if (neighbour && neighbour->buildProgress >= 1.0f &&
                            neighbour->fire < 0.05f && !forRoom(neighbour->id))
                            spreadTargets.push_back(neighbour);
                    }
                    if (!spreadTargets.empty()) {
                        Room* target = spreadTargets[static_cast<size_t>(
                            world.rng().rangeI(0, static_cast<i32>(spreadTargets.size()) - 1))];
                        pendingFireSpread_.emplace_back(target->id, e.severity * 0.8f);
                    }
                }
            }
            break;
        }
        case EventKind::Breakdown: {
            i32 techs = 0;
            for (ResidentId w : e.responders)
                if (const Resident* r = world.resident(w); r && r->alive()) ++techs;
            if (techs > 0) {
                room->condition = std::min(1.0f, room->condition + dt * 0.02f * static_cast<f32>(techs));
                if (room->condition > 0.4f) {
                    room->broken = false;
                    e.resolved = true;
                    world.statsMutable().breakdownsRepaired += 1;
                    world.notify(std::string(roomTypeName(room->type)) + " repaired.",
                                NotifySeverity::Good, room->id);
                }
            }
            break;
        }
        case EventKind::Intrusion:
        case EventKind::Infestation: {
            if (e.fight.combatants().empty()) {
                const i32 count = 2 + static_cast<i32>(e.severity) + world.rng().rangeI(0, 2);
                const gameplay::EnemyKind kind = e.kind == EventKind::Intrusion
                    ? (world.rng().chance(0.7f) ? gameplay::EnemyKind::Raider : gameplay::EnemyKind::Scavenger)
                    : (world.rng().chance(0.6f) ? gameplay::EnemyKind::Crawler : gameplay::EnemyKind::StingSwarm);
                for (i32 i = 0; i < count; ++i)
                    e.fight.addEnemy(kind, static_cast<u32>(i + 1), e.severity);
                for (ResidentId w : e.responders)
                    if (const Resident* r = world.resident(w))
                        e.fight.addResident(*r, 0.35f, world.tech().bonuses().defence);
            } else {
                for (ResidentId w : e.responders) {
                    bool present = false;
                    for (const auto& c : e.fight.combatants())
                        if (c.isResident && c.sourceId == w) present = true;
                    if (!present)
                        if (const Resident* r = world.resident(w))
                            e.fight.addResident(*r, 0.35f, world.tech().bonuses().defence);
                }
            }
            e.fight.tick(dt, world.rng());
            for (auto& c : e.fight.combatants())
                if (c.isResident)
                    if (Resident* r = world.resident(c.sourceId))
                        r->health = std::min(r->health, c.health);

            for (const auto& kv : e.fight.residentKills()) {
                u32 already = 0;
                for (auto& rp : e.reportedKills) if (rp.first == kv.first) { already = rp.second; break; }
                const u32 delta = kv.second > already ? kv.second - already : 0;
                if (delta > 0) {
                    world.statsMutable().enemiesDefeated += static_cast<i32>(delta);
                    world.quests().notify(gameplay::ObjectiveKind::DefeatEnemies, 0, static_cast<f32>(delta));
                    if (Resident* r = world.resident(kv.first)) r->grantExperience(18.0f * static_cast<f32>(delta));
                }
                bool tracked = false;
                for (auto& rp : e.reportedKills) if (rp.first == kv.first) { rp.second = kv.second; tracked = true; break; }
                if (!tracked) e.reportedKills.emplace_back(kv.first, kv.second);
            }
            e.fight.clearEvents();

            if (e.fight.state() == gameplay::CombatState::ShelterVictory) {
                e.resolved = true;
                world.resources().add(Resource::Scrip, e.fight.scripReward());
                world.resources().add(Resource::Materials, e.fight.materialReward());
                world.notify("The " + std::string(roomTypeName(room->type)) + " is secure.",
                            NotifySeverity::Good, room->id);
            } else if (e.fight.state() == gameplay::CombatState::ShelterDefeat ||
                      e.fight.state() == gameplay::CombatState::Stalemate) {
                // Hostiles are driven off rather than the shelter falling —
                // this is a management game, not a loss state on one room.
                e.resolved = true;
                room->condition = std::max(0.1f, room->condition - 0.3f);
                world.notify("Defenders were overwhelmed in the " + std::string(roomTypeName(room->type)) +
                            "; the room needs repairs.", NotifySeverity::Critical, room->id);
            }
            break;
        }
        case EventKind::PowerSurge: {
            room->condition = std::max(0.0f, room->condition - dt * 0.3f);
            e.elapsed += dt;
            if (e.elapsed > 8.0f) e.resolved = true;
            break;
        }
        case EventKind::Contamination: {
            world.resources().drain(Resource::Water, dt * 0.4f);
            for (auto id : e.responders) (void)id;
            e.elapsed += dt;
            if (e.elapsed > 30.0f) e.resolved = true;
            break;
        }
        default:
            e.resolved = true;
            break;
    }
    e.elapsed += 0.0f;   // kept for symmetry with resolved handling above
}

void EventSystem::tick(f32 dtSeconds, World& world) {
    sinceStart_ += dtSeconds;
    pendingFireSpread_.clear();
    for (Emergency& e : emergencies_) if (!e.resolved) tickEmergency(e, dtSeconds, world);
    emergencies_.erase(std::remove_if(emergencies_.begin(), emergencies_.end(),
                                      [](const Emergency& e) { return e.resolved; }),
                       emergencies_.end());

    // Applied here, after the loop above is done touching emergencies_, since
    // trigger() can append to it and that would invalidate the loop's
    // reference if done mid-iteration.
    for (const auto& [roomId, severity] : pendingFireSpread_) {
        if (forRoom(roomId)) continue;   // something else claimed it meanwhile
        const u32 id = trigger(EventKind::Fire, world, roomId, severity);
        if (id != 0) {
            if (const Room* target = world.shelter().room(roomId))
                world.notify("Fire is spreading into the " + std::string(roomTypeName(target->type)) + "!",
                            NotifySeverity::Critical, roomId);
        }
    }

    if (trader_.active) {
        trader_.timeLeft -= dtSeconds;
        if (trader_.timeLeft <= 0.0f) trader_.active = false;
    }

    nextEventTimer_ -= dtSeconds;
    if (nextEventTimer_ <= 0.0f) {
        const f32 base = 150.0f / difficulty_;
        nextEventTimer_ = base * world.rng().range(0.6f, 1.5f);
        // Do not pile emergencies on top of each other early on.
        if (activeCount() < 3 || world.rng().chance(0.2f))
            trigger(rollEventKind(world), world);
    }
}

void EventSystem::serialize(BlobWriter& w) const {
    w.u32v(static_cast<u32>(emergencies_.size()));
    for (const Emergency& e : emergencies_) {
        w.u32v(e.id);
        w.u8v(static_cast<u8>(e.kind));
        w.u32v(e.room);
        w.f32v(e.severity);
        w.f32v(e.elapsed);
        w.f32v(e.spreadTimer);
        w.boolv(e.resolved);
        w.u32v(static_cast<u32>(e.responders.size()));
        for (ResidentId id : e.responders) w.u32v(id);
    }
    w.boolv(trader_.active);
    w.f32v(trader_.timeLeft);
    w.u8v(static_cast<u8>(trader_.sells));
    w.f32v(trader_.sellAmount);
    w.f32v(trader_.sellPrice);
    w.u8v(static_cast<u8>(trader_.buys));
    w.f32v(trader_.buyAmount);
    w.f32v(trader_.buyPrice);
    w.u16v(trader_.itemForSale);
    w.f32v(trader_.itemPrice);
    w.u32v(nextId_);
    w.f32v(nextEventTimer_);
}

bool EventSystem::deserialize(BlobReader& r, u32 version) {
    (void)version;
    emergencies_.clear();
    const u32 n = r.u32v();
    if (r.failed() || n > 256) return false;
    for (u32 i = 0; i < n; ++i) {
        Emergency e;
        e.id = r.u32v();
        const u8 kind = r.u8v();
        e.kind = kind < static_cast<u8>(EventKind::Count) ? static_cast<EventKind>(kind) : EventKind::Breakdown;
        e.room = r.u32v();
        e.severity = r.f32v();
        e.elapsed = r.f32v();
        e.spreadTimer = r.f32v();
        e.resolved = r.boolv();
        const u32 resp = r.u32v();
        if (r.failed() || resp > 32) return false;
        for (u32 k = 0; k < resp; ++k) e.responders.push_back(r.u32v());
        if (r.failed()) return false;
        emergencies_.push_back(std::move(e));
    }
    trader_.active = r.boolv();
    trader_.timeLeft = r.f32v();
    trader_.sells = static_cast<Resource>(std::min<u8>(r.u8v(), kResourceCount - 1));
    trader_.sellAmount = r.f32v();
    trader_.sellPrice = r.f32v();
    trader_.buys = static_cast<Resource>(std::min<u8>(r.u8v(), kResourceCount - 1));
    trader_.buyAmount = r.f32v();
    trader_.buyPrice = r.f32v();
    trader_.itemForSale = r.u16v();
    trader_.itemPrice = r.f32v();
    nextId_ = std::max(1u, r.u32v());
    nextEventTimer_ = r.f32v();
    return !r.failed();
}

} // namespace hv::sim
