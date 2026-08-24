// World serialization — the authoritative save format.
#include "sim/World.hpp"

namespace hv::sim {
namespace {

void writeResident(BlobWriter& w, const Resident& r) {
    w.u32v(r.id);
    w.str(r.name);
    w.boolv(r.female);
    w.f32v(r.age);
    w.u8v(r.appearance.skinTone); w.u8v(r.appearance.hairStyle);
    w.u8v(r.appearance.hairTone); w.u8v(r.appearance.faceVariant);
    w.u8v(r.appearance.outfitTint);
    w.f32v(r.appearance.height); w.f32v(r.appearance.build);
    w.u8v(static_cast<u8>(r.personality));
    w.u32v(r.traits);
    for (int i = 0; i < kSkillCount; ++i) w.u8v(r.skills.value[static_cast<size_t>(i)]);
    w.i32v(r.level);
    w.f32v(r.experience);
    w.f32v(r.health); w.f32v(r.maxHealth);
    w.f32v(r.happiness); w.f32v(r.energy); w.f32v(r.hunger); w.f32v(r.thirst); w.f32v(r.radiation);
    w.u32v(r.assignedRoom); w.u32v(r.currentRoom);
    w.u8v(static_cast<u8>(r.activity)); w.f32v(r.activityTimer);
    w.vec3(r.position); w.vec3(r.velocity); w.f32v(r.facing);
    auto writeStack = [&](const ItemStack& s) { w.u16v(s.defId); w.u16v(s.count); w.f32v(s.condition); };
    writeStack(r.weapon); writeStack(r.outfit); writeStack(r.utility);
    w.u32v(static_cast<u32>(r.inventory.size()));
    for (const ItemStack& s : r.inventory) writeStack(s);
    w.u32v(static_cast<u32>(r.relationships.size()));
    for (const Relationship& rel : r.relationships) { w.u32v(rel.other); w.f32v(rel.affinity); }
    w.u32v(r.expeditionId);
    w.f32v(r.lastMealTime);
    w.f32v(r.totalWorkedHours);
    w.u32v(r.killCount);
}

bool readResident(BlobReader& r, Resident& out) {
    out.id = r.u32v();
    out.name = r.str();
    out.female = r.boolv();
    out.age = r.f32v();
    out.appearance.skinTone = r.u8v(); out.appearance.hairStyle = r.u8v();
    out.appearance.hairTone = r.u8v(); out.appearance.faceVariant = r.u8v();
    out.appearance.outfitTint = r.u8v();
    out.appearance.height = r.f32v(); out.appearance.build = r.f32v();
    const u8 pers = r.u8v();
    out.personality = pers < static_cast<u8>(Personality::Count)
                          ? static_cast<Personality>(pers) : Personality::Steady;
    out.traits = r.u32v();
    for (int i = 0; i < kSkillCount; ++i) out.skills.value[static_cast<size_t>(i)] = r.u8v();
    out.level = std::max(1, r.i32v());
    out.experience = r.f32v();
    out.health = r.f32v(); out.maxHealth = r.f32v();
    out.happiness = r.f32v(); out.energy = r.f32v(); out.hunger = r.f32v();
    out.thirst = r.f32v(); out.radiation = r.f32v();
    out.assignedRoom = r.u32v(); out.currentRoom = r.u32v();
    const u8 act = r.u8v();
    out.activity = act <= static_cast<u8>(Activity::Dead) ? static_cast<Activity>(act) : Activity::Idle;
    out.activityTimer = r.f32v();
    out.position = r.vec3(); out.velocity = r.vec3(); out.facing = r.f32v();
    auto readStack = [&](ItemStack& s) { s.defId = r.u16v(); s.count = r.u16v(); s.condition = r.f32v(); };
    readStack(out.weapon); readStack(out.outfit); readStack(out.utility);
    const u32 inv = r.u32v();
    if (r.failed() || inv > 64) return false;
    for (u32 i = 0; i < inv; ++i) { ItemStack s; readStack(s); out.inventory.push_back(s); }
    const u32 rel = r.u32v();
    if (r.failed() || rel > 64) return false;
    for (u32 i = 0; i < rel; ++i) {
        Relationship x; x.other = r.u32v(); x.affinity = r.f32v();
        out.relationships.push_back(x);
    }
    out.expeditionId = r.u32v();
    out.lastMealTime = r.f32v();
    out.totalWorkedHours = r.f32v();
    out.killCount = r.u32v();
    return !r.failed();
}

} // namespace

void World::serialize(BlobWriter& w) const {
    w.str("HAVEN");
    w.u32v(3);   // save format version
    w.u64v(seed_);
    w.str(shelterName_);
    w.f32v(gameTime_);
    w.f32v(speed_);
    w.boolv(paused_);
    w.i32v(lastDay_);
    w.u32v(nextResidentId_);
    w.f32v(newcomerTimer_);
    w.f32v(birthTimer_);

    for (int i = 0; i < kResourceCount; ++i) {
        w.f32v(resources_.amount[static_cast<size_t>(i)]);
        w.f32v(resources_.capacity[static_cast<size_t>(i)]);
    }

    shelter_.serialize(w);

    w.u32v(static_cast<u32>(residents_.size()));
    for (const Resident& r : residents_) writeResident(w, r);

    w.u32v(static_cast<u32>(storage_.size()));
    for (const ItemStack& s : storage_) { w.u16v(s.defId); w.u16v(s.count); w.f32v(s.condition); }

    w.u32v(static_cast<u32>(trainingChoices_.size()));
    for (const auto& p : trainingChoices_) { w.u32v(p.first); w.u8v(static_cast<u8>(p.second)); }

    tech_.serialize(w);
    surface_.serialize(w);
    expeditions_.serialize(w);
    quests_.serialize(w);
    events_.serialize(w);

    w.f64v(stats_.totalPlaySeconds);
    w.i32v(stats_.daysSurvived);
    w.i32v(stats_.residentsBorn);
    w.i32v(stats_.residentsRecruited);
    w.i32v(stats_.residentsLost);
    w.i32v(stats_.enemiesDefeated);
    w.i32v(stats_.expeditionsCompleted);
    w.i32v(stats_.expeditionsLost);
    w.i32v(stats_.roomsBuilt);
    w.i32v(stats_.roomsUpgraded);
    w.i32v(stats_.firesExtinguished);
    w.i32v(stats_.breakdownsRepaired);
    w.i32v(stats_.questsCompleted);
    w.i32v(stats_.techUnlocked);
    for (int i = 0; i < kResourceCount; ++i) { w.f64v(stats_.produced[static_cast<size_t>(i)]); w.f64v(stats_.consumed[static_cast<size_t>(i)]); }
}

bool World::deserialize(BlobReader& r, u32 /*outerVersion*/) {
    reset();
    const std::string magic = r.str();
    if (magic != "HAVEN") return false;
    const u32 version = r.u32v();
    if (r.failed() || version == 0 || version > 3) return false;

    seed_ = r.u64v();
    rng_.seed(seed_);
    shelterName_ = r.str();
    gameTime_ = r.f32v();
    speed_ = clampf(r.f32v(), 0.0f, 8.0f);
    paused_ = r.boolv();
    lastDay_ = r.i32v();
    nextResidentId_ = std::max(1u, r.u32v());
    newcomerTimer_ = r.f32v();
    birthTimer_ = r.f32v();

    for (int i = 0; i < kResourceCount; ++i) {
        resources_.amount[static_cast<size_t>(i)] = r.f32v();
        resources_.capacity[static_cast<size_t>(i)] = r.f32v();
    }
    if (r.failed()) return false;

    if (!shelter_.deserialize(r, version)) return false;

    const u32 residentCount = r.u32v();
    if (r.failed() || residentCount > 4096) return false;
    residents_.reserve(residentCount);
    for (u32 i = 0; i < residentCount; ++i) {
        Resident res;
        if (!readResident(r, res)) return false;
        residents_.push_back(std::move(res));
    }

    const u32 storageCount = r.u32v();
    if (r.failed() || storageCount > 4096) return false;
    for (u32 i = 0; i < storageCount; ++i) {
        ItemStack s;
        s.defId = r.u16v(); s.count = r.u16v(); s.condition = r.f32v();
        storage_.push_back(s);
    }

    const u32 trainingCount = r.u32v();
    if (r.failed() || trainingCount > 4096) return false;
    for (u32 i = 0; i < trainingCount; ++i) {
        const ResidentId id = r.u32v();
        const u8 s = r.u8v();
        if (s < kSkillCount) trainingChoices_.emplace_back(id, static_cast<Skill>(s));
    }
    if (r.failed()) return false;

    if (!tech_.deserialize(r, version)) return false;
    if (!surface_.deserialize(r, version)) return false;
    if (!expeditions_.deserialize(r, version)) return false;
    if (!quests_.deserialize(r, version)) return false;
    if (!events_.deserialize(r, version)) return false;

    stats_.totalPlaySeconds = r.f64v();
    stats_.daysSurvived = r.i32v();
    stats_.residentsBorn = r.i32v();
    stats_.residentsRecruited = r.i32v();
    stats_.residentsLost = r.i32v();
    stats_.enemiesDefeated = r.i32v();
    stats_.expeditionsCompleted = r.i32v();
    stats_.expeditionsLost = r.i32v();
    stats_.roomsBuilt = r.i32v();
    stats_.roomsUpgraded = r.i32v();
    stats_.firesExtinguished = r.i32v();
    stats_.breakdownsRepaired = r.i32v();
    stats_.questsCompleted = r.i32v();
    stats_.techUnlocked = r.i32v();
    for (int i = 0; i < kResourceCount; ++i) {
        stats_.produced[static_cast<size_t>(i)] = r.f64v();
        stats_.consumed[static_cast<size_t>(i)] = r.f64v();
    }
    if (r.failed()) return false;

    rebuildDerived();
    return true;
}

} // namespace hv::sim
