#include "sim/World.hpp"
#include "sim/Names.hpp"
#include "core/Log.hpp"
#include <algorithm>

namespace hv::sim {

World::World() { reset(); }

void World::reset() {
    shelter_.reset();
    residents_.clear();
    storage_.clear();
    trainingChoices_.clear();
    tech_.reset();
    surface_.reset();
    expeditions_.reset();
    quests_.reset();
    events_.reset();
    notifications_.clear();
    resources_ = ResourcePool{};
    stats_ = Statistics{};
    flows_ = FlowRates{};
    flowProducedAcc_.fill(0.0f);
    flowConsumedAcc_.fill(0.0f);
    gameTime_ = 0.0f;
    speed_ = 1.0f;
    paused_ = false;
    nextResidentId_ = 1;
    newcomerTimer_ = 0.0f;
    birthTimer_ = 0.0f;
    lastDay_ = 1;
    flowWindow_ = 0.0f;
    warnedBrownout_ = false;
    starvationWarnTimer_ = 0.0f;
}

void World::newGame(u64 seed) {
    reset();
    seed_ = seed;
    rng_.seed(seed);

    shelter_.createStarter();
    shelter_.applyStorageCapacity(resources_);
    resources_.add(Resource::Power, 90.0f);
    resources_.add(Resource::Water, 180.0f);
    resources_.add(Resource::Food, 180.0f);
    resources_.add(Resource::Materials, 420.0f);
    resources_.add(Resource::Medicine, 25.0f);
    resources_.add(Resource::Scrip, 260.0f);

    for (int i = 0; i < 8; ++i) {
        Resident r = makeResident(rng_, nextResidentId_, gameTimeHours(), 0);
        r.position = shelter_.rooms().front().worldCenter();
        r.cell = Cell{0, kGridWidth / 2};
        addResident(std::move(r));
    }
    surface_.generate(rng_);
    quests_.seedMainline();
    addToStorage(ItemStack{itemIdByName("Pipe Bludgeon"), 2, 1.0f});
    addToStorage(ItemStack{itemIdByName("Field Dressing"), 4, 1.0f});
    addToStorage(ItemStack{itemIdByName("Engineer's Rig"), 1, 1.0f});

    notify("The airlock seals behind you. " + shelterName_ + " is yours to keep alive.",
           NotifySeverity::Info);
    rebuildDerived();
}

// ---------------------------------------------------------------------------
//  Residents
// ---------------------------------------------------------------------------

Resident* World::resident(ResidentId id) {
    if (id == kNoResident) return nullptr;
    for (Resident& r : residents_) if (r.id == id) return &r;
    return nullptr;
}
const Resident* World::resident(ResidentId id) const {
    return const_cast<World*>(this)->resident(id);
}

i32 World::population() const {
    i32 n = 0;
    for (const Resident& r : residents_) if (r.alive()) ++n;
    return n;
}

i32 World::populationCapacity() const {
    i32 cap = 8;   // the airlock and its bunk room hold the founding crew
    for (const Room& r : shelter_.rooms()) {
        if (r.type != RoomType::Dormitory || r.buildProgress < 1.0f) continue;
        const f32 cells = static_cast<f32>(r.width) / static_cast<f32>(std::max(1, r.def().width));
        cap += static_cast<i32>(std::round(6.0f * cells * (1.0f + 0.5f * static_cast<f32>(r.level - 1))));
    }
    return cap;
}

f32 World::averageHappiness() const {
    f32 sum = 0.0f;
    i32 n = 0;
    for (const Resident& r : residents_) if (r.alive()) { sum += r.happiness; ++n; }
    return n ? sum / static_cast<f32>(n) : 0.0f;
}

f32 World::averageHealth() const {
    f32 sum = 0.0f;
    i32 n = 0;
    for (const Resident& r : residents_) if (r.alive()) { sum += r.healthFraction(); ++n; }
    return n ? sum / static_cast<f32>(n) * 100.0f : 0.0f;
}

ResidentId World::addResident(Resident r) {
    if (r.id == kNoResident) r.id = nextResidentId_;
    nextResidentId_ = std::max(nextResidentId_, r.id + 1);
    residents_.push_back(std::move(r));
    return residents_.back().id;
}

void World::removeResident(ResidentId id) {
    unassignResident(id);
    residents_.erase(std::remove_if(residents_.begin(), residents_.end(),
                                    [id](const Resident& r) { return r.id == id; }),
                     residents_.end());
}

ResidentId World::spawnNewcomer(i32 tier) {
    if (population() >= populationCapacity()) return kNoResident;
    Resident r = makeResident(rng_, nextResidentId_, gameTimeHours(), tier);
    const std::vector<RoomId> entrances = shelter_.roomsOfType(RoomType::Entrance);
    if (!entrances.empty()) {
        const Room* e = shelter_.room(entrances.front());
        r.position = e->worldCenter();
        r.cell = Cell{e->floor, e->colStart};
        r.currentRoom = e->id;
    }
    const ResidentId id = addResident(std::move(r));
    ++stats_.residentsRecruited;
    return id;
}

// ---------------------------------------------------------------------------
//  Construction and room actions
// ---------------------------------------------------------------------------

BuildPlan World::previewBuild(RoomType type, const Cell& origin, i32 width) const {
    return shelter_.planBuild(type, origin, width, resources_, population(),
                              tech_.unlockedIds());
}

BuildPlan World::tryBuild(RoomType type, const Cell& origin, i32 width) {
    BuildPlan plan = previewBuild(type, origin, width);
    if (!plan.ok()) return plan;
    const RoomId id = shelter_.build(plan, resources_);
    if (id == kNoRoom) {
        plan.error = BuildError::RoomTypeInvalid;
        return plan;
    }
    ++stats_.roomsBuilt;
    quests_.notify(gameplay::ObjectiveKind::BuildRoom, static_cast<i32>(type), 1.0f);
    notify(std::string(roomTypeName(type)) + " excavation started on level " +
           std::to_string(origin.floor + 1) + ".", NotifySeverity::Info, id);
    return plan;
}

bool World::upgradeRoom(RoomId id) {
    Room* r = shelter_.room(id);
    if (!r) return false;
    const RoomType type = r->type;
    if (!shelter_.upgrade(id, resources_)) return false;
    ++stats_.roomsUpgraded;
    quests_.notifyAbsolute(gameplay::ObjectiveKind::UpgradeRoom, static_cast<i32>(type),
                           static_cast<f32>(shelter_.room(id)->level));
    notify(std::string(roomTypeName(type)) + " upgraded to level " +
           std::to_string(shelter_.room(id)->level) + ".", NotifySeverity::Good, id);
    return true;
}

bool World::demolishRoom(RoomId id) {
    Room* r = shelter_.room(id);
    if (!r) return false;
    // Workers must be released before the room stops existing.
    const std::vector<ResidentId> workers = r->workers;
    for (ResidentId w : workers) unassignResident(w);
    const std::string name = roomTypeName(r->type);
    if (!shelter_.demolish(id, resources_)) return false;
    notify(name + " stripped out; half the materials recovered.", NotifySeverity::Info);
    return true;
}

bool World::assignResident(ResidentId residentId, RoomId roomId) {
    Resident* res = resident(residentId);
    Room* room = shelter_.room(roomId);
    if (!res || !room || !res->available()) return false;
    if (room->workerSlots() == 0) return false;
    if (!room->hasFreeSlot()) return false;
    unassignResident(residentId);
    room->workers.push_back(residentId);
    res->assignedRoom = roomId;
    return true;
}

bool World::unassignResident(ResidentId residentId) {
    Resident* res = resident(residentId);
    bool changed = false;
    for (Room& r : shelter_.rooms()) {
        const size_t before = r.workers.size();
        r.workers.erase(std::remove(r.workers.begin(), r.workers.end(), residentId),
                        r.workers.end());
        changed = changed || r.workers.size() != before;
    }
    if (res) res->assignedRoom = kNoRoom;
    return changed;
}

namespace {

/// How well a resident fits a room's primary skill, 0..1.
f32 jobFitness(const Resident& r, const Room& room) {
    const RoomDef& d = room.def();
    if (d.workerSlots == 0) return 0.0f;
    f32 fit = static_cast<f32>(r.effectiveSkill(d.primarySkill)) / 14.0f;
    // A worker who is well rested and healthy is worth more in any post.
    fit *= 0.6f + 0.4f * r.healthFraction();
    return fit;
}

} // namespace

i32 World::autoAssignAll() {
    i32 assigned = 0;
    // Fill the most valuable posts first: production before comfort.
    std::vector<Room*> rooms;
    for (Room& r : shelter_.rooms()) if (r.workerSlots() > 0) rooms.push_back(&r);
    std::sort(rooms.begin(), rooms.end(), [](const Room* a, const Room* b) {
        const int pa = a->def().function == RoomFunction::Produce ? 0 : 1;
        const int pb = b->def().function == RoomFunction::Produce ? 0 : 1;
        if (pa != pb) return pa < pb;
        return a->baseProduction() > b->baseProduction();
    });

    for (Room* room : rooms) {
        while (room->hasFreeSlot()) {
            Resident* best = nullptr;
            f32 bestFit = 0.0f;
            for (Resident& r : residents_) {
                if (!r.available() || r.assignedRoom != kNoRoom) continue;
                const f32 fit = jobFitness(r, *room);
                if (fit > bestFit) { bestFit = fit; best = &r; }
            }
            if (!best) break;
            if (assignResident(best->id, room->id)) ++assigned;
            else break;
        }
    }
    if (assigned > 0)
        notify(std::to_string(assigned) + " residents posted to open jobs.", NotifySeverity::Good);
    return assigned;
}

bool World::optimiseAssignment(ResidentId id) {
    Resident* res = resident(id);
    if (!res || !res->available()) return false;
    Room* best = nullptr;
    f32 bestFit = res->assignedRoom != kNoRoom
        ? jobFitness(*res, *shelter_.room(res->assignedRoom)) : 0.0f;
    for (Room& r : shelter_.rooms()) {
        if (r.workerSlots() == 0 || !r.hasFreeSlot()) continue;
        const f32 fit = jobFitness(*res, r);
        if (fit > bestFit + 0.05f) { bestFit = fit; best = &r; }
    }
    if (!best) return false;
    return assignResident(id, best->id);
}

f32 World::collectRoom(RoomId id) {
    Room* r = shelter_.room(id);
    if (!r || r->storedOutput <= 0.0f) return 0.0f;
    const Resource res = r->def().produces;
    const f32 stored = r->storedOutput;
    const f32 taken = resources_.add(res, stored);
    r->storedOutput = 0.0f;
    stats_.produced[static_cast<int>(res)] += taken;
    if (taken < stored - 0.5f)
        notify(std::string(resourceName(res)) + " storage is full; the overflow was lost.",
               NotifySeverity::Warning, id);
    return taken;
}

bool World::rushRoom(RoomId id) {
    Room* r = shelter_.room(id);
    if (!r || !r->operational() || r->rushCooldown > 0.0f) return false;
    if (r->def().function != RoomFunction::Produce &&
        r->def().function != RoomFunction::Craft &&
        r->def().function != RoomFunction::Research) return false;

    // Risk climbs with each rush inside the cooldown window and with how
    // worn the machinery already is.
    const f32 risk = clampf(0.18f + (1.0f - r->condition) * 0.5f, 0.05f, 0.85f);
    r->rushCooldown = 90.0f;
    if (rng_.chance(risk)) {
        const bool fire = rng_.chance(0.55f);
        events_.trigger(fire ? EventKind::Fire : EventKind::Breakdown, *this, id,
                        1.0f + rng_.unit());
        notify(std::string(roomTypeName(r->type)) + (fire ? " caught fire during the rush!"
                                                          : " broke down during the rush!"),
               NotifySeverity::Critical, id);
        return false;
    }
    const f32 bonus = r->baseProduction() * rng_.range(1.5f, 3.0f);
    r->storedOutput += bonus;
    r->condition = std::max(0.15f, r->condition - 0.05f);
    for (ResidentId w : r->workers)
        if (Resident* res = resident(w)) res->grantExperience(6.0f);
    notify(std::string(roomTypeName(r->type)) + " pushed hard — extra output banked.",
           NotifySeverity::Good, id);
    return true;
}

bool World::repairRoom(RoomId id) {
    Room* r = shelter_.room(id);
    if (!r) return false;
    const f32 cost = 25.0f + 40.0f * (1.0f - r->condition);
    if (!resources_.spend(Resource::Materials, cost)) return false;
    r->condition = 1.0f;
    r->broken = false;
    ++stats_.breakdownsRepaired;
    notify(std::string(roomTypeName(r->type)) + " serviced and back on line.",
           NotifySeverity::Good, id);
    return true;
}

// ---------------------------------------------------------------------------
//  Equipment, crafting, storage
// ---------------------------------------------------------------------------

void World::addToStorage(const ItemStack& s) {
    if (!s.valid()) return;
    for (ItemStack& x : storage_) {
        if (x.defId == s.defId && std::fabs(x.condition - s.condition) < 0.02f) {
            x.count = static_cast<u16>(std::min<int>(x.count + s.count, 999));
            return;
        }
    }
    storage_.push_back(s);
}

bool World::takeFromStorage(u16 defId, ItemStack& out) {
    for (size_t i = 0; i < storage_.size(); ++i) {
        if (storage_[i].defId != defId) continue;
        out = storage_[i];
        out.count = 1;
        if (--storage_[i].count == 0) storage_.erase(storage_.begin() + static_cast<long>(i));
        return true;
    }
    return false;
}

bool World::equipItem(ResidentId residentId, const ItemStack& item) {
    Resident* r = resident(residentId);
    if (!r || !item.valid()) return false;
    ItemStack taken;
    if (!takeFromStorage(item.defId, taken)) return false;
    const ItemDef& d = itemDef(taken.defId);
    ItemStack* slot = nullptr;
    switch (d.kind) {
        case ItemKind::Weapon:  slot = &r->weapon; break;
        case ItemKind::Outfit:  slot = &r->outfit; break;
        case ItemKind::Utility: slot = &r->utility; break;
        case ItemKind::Consumable:
            r->inventory.push_back(taken);
            return true;
    }
    if (slot->valid()) addToStorage(*slot);
    *slot = taken;
    return true;
}

bool World::unequip(ResidentId residentId, ItemKind kind) {
    Resident* r = resident(residentId);
    if (!r) return false;
    ItemStack* slot = nullptr;
    switch (kind) {
        case ItemKind::Weapon:  slot = &r->weapon; break;
        case ItemKind::Outfit:  slot = &r->outfit; break;
        case ItemKind::Utility: slot = &r->utility; break;
        default: return false;
    }
    if (!slot->valid()) return false;
    addToStorage(*slot);
    *slot = ItemStack{};
    return true;
}

bool World::craftItem(u16 itemId) {
    const ItemDef& d = itemDef(itemId);
    if (d.id == 0 || d.craftMaterials <= 0.0f) return false;
    if (d.techRequired[0] != '\0' && !tech_.unlocked(d.techRequired)) return false;
    // A workshop must exist, be staffed and be running.
    const Room* shop = nullptr;
    for (const Room& r : shelter_.rooms())
        if (r.type == RoomType::Workshop && r.operational() && !r.workers.empty()) { shop = &r; break; }
    if (!shop) return false;
    if (!resources_.spend(Resource::Materials, d.craftMaterials)) return false;
    ItemStack s{d.id, 1, 1.0f};
    addToStorage(s);
    for (ResidentId w : shop->workers)
        if (Resident* res = resident(w)) res->grantExperience(10.0f);
    notify(std::string("Workshop finished a ") + d.name + ".", NotifySeverity::Good, shop->id);
    return true;
}

bool World::useConsumable(ResidentId residentId, u16 itemId) {
    Resident* r = resident(residentId);
    if (!r) return false;
    const ItemDef& d = itemDef(itemId);
    if (d.kind != ItemKind::Consumable) return false;
    ItemStack taken;
    if (!takeFromStorage(itemId, taken)) return false;
    r->health = std::min(r->effectiveMaxHealth(), r->health + d.healAmount);
    r->energy = clampf(r->energy + d.energyAmount, 0.0f, 100.0f);
    r->happiness = clampf(r->happiness + d.moraleAmount, 0.0f, 100.0f);
    if (itemId == 63) r->radiation = std::max(0.0f, r->radiation - 30.0f);
    return true;
}

bool World::setTrainingSkill(ResidentId residentId, Skill s) {
    if (!resident(residentId)) return false;
    for (auto& p : trainingChoices_) if (p.first == residentId) { p.second = s; return true; }
    trainingChoices_.emplace_back(residentId, s);
    return true;
}

Skill World::trainingSkill(ResidentId residentId) const {
    for (const auto& p : trainingChoices_) if (p.first == residentId) return p.second;
    // Default to whatever they are already strongest in.
    const Resident* r = resident(residentId);
    if (!r) return Skill::Engineering;
    Skill best = Skill::Engineering;
    u8 bestVal = 0;
    for (int i = 0; i < kSkillCount; ++i) {
        const Skill s = static_cast<Skill>(i);
        if (r->skills.get(s) > bestVal) { bestVal = r->skills.get(s); best = s; }
    }
    return best;
}

u32 World::launchExpedition(u32 siteId, const std::vector<ResidentId>& squad) {
    const gameplay::Site* site = surface_.find(siteId);
    if (!site || !site->discovered || squad.empty() || squad.size() > 4) return 0;
    for (ResidentId id : squad) {
        const Resident* r = resident(id);
        if (!r || !r->available()) return 0;
    }
    const u32 exp = expeditions_.launch(*site, squad, rng_);
    if (exp == 0) return 0;
    for (ResidentId id : squad) {
        Resident* r = resident(id);
        unassignResident(id);
        r->expeditionId = exp;
        r->activity = Activity::OnExpedition;
    }
    notify("Expedition away to " + site->name + ".", NotifySeverity::Info);
    return exp;
}

void World::recallExpedition(u32 id) { expeditions_.recall(id); }

bool World::acceptTrade(bool buying) {
    TraderOffer& t = events_.trader();
    if (!t.active) return false;
    if (buying) {
        if (!resources_.spend(Resource::Scrip, t.sellPrice)) return false;
        resources_.add(t.sells, t.sellAmount);
        notify("Bought " + std::to_string(static_cast<int>(t.sellAmount)) + " " +
               resourceName(t.sells) + ".", NotifySeverity::Good);
    } else {
        if (!resources_.spend(t.buys, t.buyAmount)) return false;
        resources_.add(Resource::Scrip, t.buyPrice);
        notify("Sold " + std::to_string(static_cast<int>(t.buyAmount)) + " " +
               resourceName(t.buys) + ".", NotifySeverity::Good);
    }
    t.active = false;
    return true;
}

// ---------------------------------------------------------------------------
//  Notifications
// ---------------------------------------------------------------------------

void World::notify(const std::string& text, NotifySeverity sev, RoomId room) {
    Notification n;
    n.text = text;
    n.severity = sev;
    n.gameTime = gameTime_;
    n.roomId = room;
    notifications_.push_back(std::move(n));
    while (notifications_.size() > 128) notifications_.pop_front();
}

} // namespace hv::sim
