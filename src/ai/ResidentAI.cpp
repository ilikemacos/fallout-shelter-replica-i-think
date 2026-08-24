#include "ai/ResidentAI.hpp"
#include <cmath>

namespace hv::ai {

Vec3 ResidentAI::destinationFor(World& world, Resident& r) const {
    RoomId target = kNoRoom;
    switch (r.activity) {
        case Activity::Working:
            target = r.assignedRoom;
            break;
        case Activity::Sleeping: {
            const std::vector<RoomId> dorms = world.shelter().roomsOfType(RoomType::Dormitory);
            if (!dorms.empty()) target = dorms[static_cast<size_t>(r.id) % dorms.size()];
            break;
        }
        case Activity::Eating: {
            const std::vector<RoomId> cafes = world.shelter().roomsOfType(RoomType::Cafeteria);
            if (!cafes.empty()) target = cafes.front();
            break;
        }
        case Activity::Relaxing:
        case Activity::Socialising: {
            const std::vector<RoomId> recs = world.shelter().roomsOfType(RoomType::Recreation);
            if (!recs.empty()) target = recs.front();
            break;
        }
        case Activity::Training: {
            const std::vector<RoomId> halls = world.shelter().roomsOfType(RoomType::Training);
            if (!halls.empty()) target = halls.front();
            break;
        }
        case Activity::Firefighting:
        case Activity::Repairing:
        case Activity::Fighting: {
            if (const Emergency* e = world.events().forRoom(r.currentRoom)) target = e->room;
            for (const Emergency& e : world.events().emergencies())
                for (ResidentId w : e.responders) if (w == r.id) target = e.room;
            break;
        }
        default: break;
    }
    if (const Room* room = world.shelter().room(target)) {
        return room->workstation(static_cast<int>(r.id));
    }
    return r.position;
}

void ResidentAI::tickDecision(World& world, Resident& r, f32 dt) {
    if (!r.alive() || r.expeditionId != 0) return;
    r.activityTimer += dt;

    // Emergencies pull nearby off-duty residents into service.
    for (Emergency& e : world.events().emergencies()) {
        if (e.resolved || e.responders.size() >= 3) continue;
        const Room* room = world.shelter().room(e.room);
        if (!room) continue;
        bool alreadyResponding = false;
        for (ResidentId w : e.responders) if (w == r.id) alreadyResponding = true;
        if (alreadyResponding) continue;
        const bool suitable = r.available() && r.activity != Activity::Fighting &&
                              r.activity != Activity::Firefighting && r.activity != Activity::Repairing;
        if (suitable && distanceSq(r.position, room->worldCenter()) < 40.0f * 40.0f && world.rng().chance(0.35f)) {
            e.responders.push_back(r.id);
            r.currentRoom = e.room;
            r.activity = e.kind == EventKind::Fire ? Activity::Firefighting
                       : (e.hostile() ? Activity::Fighting : Activity::Repairing);
            r.activityTimer = 0.0f;
            return;
        }
    }
    // Someone already responding stays until the emergency clears.
    for (const Emergency& e : world.events().emergencies()) {
        for (ResidentId w : e.responders) {
            if (w == r.id) {
                r.currentRoom = e.room;
                return;
            }
        }
    }

    const f32 hour = world.hourOfDay();
    const bool sleepHours = hour < 6.0f || hour >= 23.0f;

    if (r.health < r.effectiveMaxHealth() * 0.4f) {
        const std::vector<RoomId> med = world.shelter().roomsOfType(RoomType::Medical);
        if (!med.empty()) { r.activity = Activity::Recovering; r.currentRoom = med.front(); return; }
    }
    if (r.hunger > 60.0f && r.activity != Activity::Eating) {
        r.activity = Activity::Eating; r.activityTimer = 0.0f; return;
    }
    if (r.energy < 25.0f && sleepHours) {
        r.activity = Activity::Sleeping; r.activityTimer = 0.0f; return;
    }
    if (r.activity == Activity::Sleeping && (r.energy > 90.0f || !sleepHours)) {
        r.activity = Activity::Idle;
    }
    if (r.activity == Activity::Eating && r.activityTimer > 90.0f) r.activity = Activity::Idle;
    if (r.activity == Activity::Recovering && r.health >= r.effectiveMaxHealth() * 0.9f) r.activity = Activity::Idle;

    if (r.activity == Activity::Idle || r.activity == Activity::Walking) {
        if (r.assignedRoom != kNoRoom && !sleepHours) {
            r.activity = Activity::Working;
        } else if (world.rng().chance(dt * 0.01f)) {
            r.activity = world.rng().chance(0.5f) ? Activity::Relaxing : Activity::Socialising;
            r.activityTimer = 0.0f;
        }
    }
}

void ResidentAI::tickMovement(World& world, Resident& r, f32 dt) {
    if (!r.alive() || r.expeditionId != 0) return;
    const Vec3 dest = destinationFor(world, r);
    const Vec3 toDest = dest - r.position;
    const f32 dist = length(toDest);
    constexpr f32 kSpeed = 2.1f;
    if (dist > 0.15f) {
        const Vec3 dir = toDest * (1.0f / dist);
        r.velocity = dir * std::min(kSpeed, dist / std::max(dt, 1e-3f));
        r.position += r.velocity * dt;
        r.facing = std::atan2(dir.x, dir.z);
        if (r.activity != Activity::Working && r.activity != Activity::Sleeping)
            r.activity = Activity::Walking;
    } else {
        r.velocity = Vec3{0, 0, 0};
    }
    r.cell = worldToCell(r.position);
}

} // namespace hv::ai
