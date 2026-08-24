#include "sim/Shelter.hpp"
#include "core/Serialization.hpp"
#include "core/Log.hpp"
#include <algorithm>

namespace hv::sim {

const char* buildErrorText(BuildError e) {
    switch (e) {
        case BuildError::None:             return "";
        case BuildError::OutOfBounds:      return "Outside the excavation limits";
        case BuildError::Occupied:         return "Something is already built here";
        case BuildError::NotAdjacent:      return "Must connect to the existing shelter";
        case BuildError::TooWide:          return "That room cannot be made any wider";
        case BuildError::Unaffordable:     return "Not enough materials or scrip";
        case BuildError::PopulationLocked: return "Needs a larger population";
        case BuildError::TechLocked:       return "Requires research";
        case BuildError::NeedsElevator:    return "No elevator reaches this floor";
        case BuildError::RoomTypeInvalid:  return "Cannot build that";
    }
    return "";
}

void Shelter::reset() {
    rooms_.clear();
    grid_.fill(kNoRoom);
    nextId_ = 1;
    deepestFloor_ = 0;
}

void Shelter::createStarter() {
    reset();
    // Airlock on the top floor, an elevator beside it, and a short corridor.
    Room airlock;
    airlock.id = nextId_++;
    airlock.type = RoomType::Entrance;
    airlock.floor = 0;
    airlock.colStart = kGridWidth / 2 - 1;
    airlock.width = 2;
    rooms_.push_back(airlock);

    Room lift;
    lift.id = nextId_++;
    lift.type = RoomType::Elevator;
    lift.floor = 0;
    lift.colStart = airlock.colStart + 2;
    lift.width = 1;
    rooms_.push_back(lift);

    rebuildIndex();
    updateDeepest();
}

RoomId Shelter::roomIdAt(const Cell& c) const {
    if (!c.inBounds()) return kNoRoom;
    return grid_[static_cast<size_t>(c.linear())];
}

Room* Shelter::roomAt(const Cell& c) { return room(roomIdAt(c)); }
const Room* Shelter::roomAt(const Cell& c) const { return room(roomIdAt(c)); }

Room* Shelter::room(RoomId id) {
    if (id == kNoRoom) return nullptr;
    for (Room& r : rooms_) if (r.id == id) return &r;
    return nullptr;
}
const Room* Shelter::room(RoomId id) const {
    return const_cast<Shelter*>(this)->room(id);
}

bool Shelter::cellWalkable(const Cell& c) const {
    const Room* r = roomAt(c);
    return r != nullptr && r->buildProgress >= 1.0f;
}

bool Shelter::hasElevatorAt(const Cell& c) const {
    const Room* r = roomAt(c);
    return r && r->type == RoomType::Elevator;
}

std::optional<i32> Shelter::elevatorColumnBetween(i32 floorA, i32 floorB) const {
    const i32 lo = std::min(floorA, floorB), hi = std::max(floorA, floorB);
    for (i32 col = 0; col < kGridWidth; ++col) {
        bool all = true;
        for (i32 f = lo; f <= hi && all; ++f) all = hasElevatorAt(Cell{f, col});
        if (all) return col;
    }
    return std::nullopt;
}

std::vector<RoomId> Shelter::roomsOfType(RoomType t) const {
    std::vector<RoomId> out;
    for (const Room& r : rooms_) if (r.type == t) out.push_back(r.id);
    return out;
}

i32 Shelter::countOfType(RoomType t) const {
    i32 n = 0;
    for (const Room& r : rooms_) if (r.type == t) ++n;
    return n;
}

namespace {

bool techUnlocked(const std::vector<std::string>& unlocked, const char* required) {
    if (!required || required[0] == '\0') return true;
    for (const std::string& s : unlocked) if (s == required) return true;
    return false;
}

} // namespace

BuildPlan Shelter::planBuild(RoomType type, const Cell& origin, i32 width,
                             const ResourcePool& res, i32 population,
                             const std::vector<std::string>& unlockedTech) const {
    BuildPlan plan;
    plan.type = type;
    plan.origin = origin;
    plan.width = std::max(1, width);

    if (type == RoomType::None || type == RoomType::Count || type == RoomType::Entrance) {
        plan.error = BuildError::RoomTypeInvalid;
        return plan;
    }
    const RoomDef& def = roomDef(type);
    if (plan.width != def.width && type != RoomType::Elevator && type != RoomType::Corridor) {
        plan.width = def.width;
    }
    if (!origin.inBounds() || !Cell{origin.floor, origin.col + plan.width - 1}.inBounds()) {
        plan.error = BuildError::OutOfBounds;
        return plan;
    }
    if (population < def.requiredPopulation) {
        plan.error = BuildError::PopulationLocked;
        return plan;
    }
    if (!techUnlocked(unlockedTech, def.techRequired)) {
        plan.error = BuildError::TechLocked;
        return plan;
    }

    // Every target cell must be empty rock.
    for (i32 i = 0; i < plan.width; ++i) {
        if (cellOccupied(Cell{origin.floor, origin.col + i})) {
            plan.error = BuildError::Occupied;
            return plan;
        }
    }

    // The placement must touch existing structure — horizontally on the same
    // floor, or vertically through the floor above/below.
    bool adjacent = rooms_.empty();
    RoomId mergeCandidate = kNoRoom;
    for (i32 i = 0; i < plan.width && !adjacent; ++i) {
        const i32 col = origin.col + i;
        const Cell neighbours[4] = {
            {origin.floor, col - 1}, {origin.floor, col + 1},
            {origin.floor - 1, col}, {origin.floor + 1, col}
        };
        for (const Cell& n : neighbours) if (cellOccupied(n)) { adjacent = true; break; }
    }
    if (!adjacent) {
        plan.error = BuildError::NotAdjacent;
        return plan;
    }

    // Merge with a same-type room touching either end on this floor.
    if (type != RoomType::Elevator) {
        const Cell left{origin.floor, origin.col - 1};
        const Cell right{origin.floor, origin.col + plan.width};
        for (const Cell& c : {left, right}) {
            const Room* r = roomAt(c);
            if (r && r->type == type && r->level == 1) { mergeCandidate = r->id; break; }
        }
        if (mergeCandidate != kNoRoom) {
            const Room* r = room(mergeCandidate);
            if (r->width + plan.width > kMaxMergedWidth) {
                plan.error = BuildError::TooWide;
                return plan;
            }
        }
    }
    plan.mergeInto = mergeCandidate;

    // A new floor is only useful once an elevator reaches it; allow the
    // elevator itself and a corridor beside it so the player can dig down.
    if (origin.floor > 0 && type != RoomType::Elevator) {
        bool reachable = false;
        for (i32 col = 0; col < kGridWidth && !reachable; ++col)
            if (hasElevatorAt(Cell{origin.floor, col})) reachable = true;
        if (!reachable) {
            plan.error = BuildError::NeedsElevator;
            return plan;
        }
    }

    plan.cost = roomBuildCost(def, plan.width, origin.floor);
    if (!plan.cost.affordable(res)) {
        plan.error = BuildError::Unaffordable;
        return plan;
    }
    return plan;
}

RoomId Shelter::build(const BuildPlan& plan, ResourcePool& res) {
    if (!plan.ok()) return kNoRoom;
    plan.cost.pay(res);

    RoomId result;
    if (plan.mergeInto != kNoRoom) {
        Room* target = room(plan.mergeInto);
        // Extending left moves the origin; extending right only grows width.
        if (plan.origin.col < target->colStart) target->colStart = plan.origin.col;
        target->width += plan.width;
        target->buildProgress = 0.0f;
        result = target->id;
    } else {
        Room r;
        r.id = nextId_++;
        r.type = plan.type;
        r.floor = plan.origin.floor;
        r.colStart = plan.origin.col;
        r.width = plan.width;
        r.buildProgress = 0.0f;
        rooms_.push_back(r);
        result = r.id;
    }
    rebuildIndex();
    updateDeepest();
    applyStorageCapacity(res);
    return result;
}

bool Shelter::demolish(RoomId id, ResourcePool& res) {
    Room* r = room(id);
    if (!r || r->type == RoomType::Entrance) return false;
    const Cost c = roomBuildCost(r->def(), r->width, r->floor);
    res.add(Resource::Materials, c.materials * 0.5f);
    res.add(Resource::Scrip, c.scrip * 0.35f);
    rooms_.erase(std::remove_if(rooms_.begin(), rooms_.end(),
                                [id](const Room& x) { return x.id == id; }),
                 rooms_.end());
    rebuildIndex();
    updateDeepest();
    applyStorageCapacity(res);
    return true;
}

bool Shelter::upgrade(RoomId id, ResourcePool& res) {
    Room* r = room(id);
    if (!r || !r->canUpgrade()) return false;
    const Cost c = r->nextUpgradeCost();
    if (!c.affordable(res)) return false;
    c.pay(res);
    ++r->level;
    r->condition = std::min(1.0f, r->condition + 0.35f);
    r->broken = false;
    applyStorageCapacity(res);
    return true;
}

void Shelter::applyStorageCapacity(ResourcePool& res) const {
    // Baseline capacity plus everything the store rooms add.
    f32 general = 320.0f;
    for (const Room& r : rooms_) {
        if (r.def().storageBonus <= 0.0f || r.buildProgress < 1.0f) continue;
        const f32 lvl = 1.0f + 0.6f * static_cast<f32>(r.level - 1);
        const f32 cells = static_cast<f32>(r.width) / static_cast<f32>(std::max(1, r.def().width));
        general += r.def().storageBonus * lvl * cells;
    }
    res.setCapacity(Resource::Water,     general);
    res.setCapacity(Resource::Food,      general);
    res.setCapacity(Resource::Materials, general * 1.25f);
    res.setCapacity(Resource::Medicine,  general * 0.35f);
    res.setCapacity(Resource::Scrip,     std::max(1500.0f, general * 4.0f));
    res.setCapacity(Resource::Research,  std::max(400.0f, general * 0.6f));
    // Power is a flow, not a stock: capacity is whatever the generators make,
    // held in the buffer batteries, so it scales with the shelter's size.
    res.setCapacity(Resource::Power, std::max(120.0f, general * 0.5f));
}

void Shelter::rebuildIndex() {
    grid_.fill(kNoRoom);
    for (const Room& r : rooms_) {
        for (i32 i = 0; i < r.width; ++i) {
            const Cell c{r.floor, r.colStart + i};
            if (c.inBounds()) grid_[static_cast<size_t>(c.linear())] = r.id;
        }
    }
}

void Shelter::updateDeepest() {
    deepestFloor_ = 0;
    for (const Room& r : rooms_) deepestFloor_ = std::max(deepestFloor_, r.floor);
}

void Shelter::serialize(BlobWriter& w) const {
    w.u32v(static_cast<u32>(rooms_.size()));
    for (const Room& r : rooms_) {
        w.u32v(r.id);
        w.u8v(static_cast<u8>(r.type));
        w.i32v(r.floor);
        w.i32v(r.colStart);
        w.i32v(r.width);
        w.i32v(r.level);
        w.f32v(r.condition);
        w.boolv(r.broken);
        w.f32v(r.fire);
        w.f32v(r.powerSatisfaction);
        w.f32v(r.storedOutput);
        w.f32v(r.rushCooldown);
        w.f32v(r.buildProgress);
        w.u32v(r.emergency);
        w.u32v(static_cast<u32>(r.workers.size()));
        for (ResidentId id : r.workers) w.u32v(id);
    }
    w.u32v(nextId_);
}

bool Shelter::deserialize(BlobReader& r, u32 version) {
    (void)version;
    reset();
    const u32 count = r.u32v();
    if (r.failed() || count > static_cast<u32>(kMaxFloors * kGridWidth)) return false;
    rooms_.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        Room room_;
        room_.id = r.u32v();
        const u8 type = r.u8v();
        room_.type = type < kRoomTypeCount ? static_cast<RoomType>(type) : RoomType::Corridor;
        room_.floor = r.i32v();
        room_.colStart = r.i32v();
        room_.width = r.i32v();
        room_.level = r.i32v();
        room_.condition = r.f32v();
        room_.broken = r.boolv();
        room_.fire = r.f32v();
        room_.powerSatisfaction = r.f32v();
        room_.storedOutput = r.f32v();
        room_.rushCooldown = r.f32v();
        room_.buildProgress = r.f32v();
        room_.emergency = r.u32v();
        const u32 workers = r.u32v();
        if (r.failed() || workers > 64) return false;
        for (u32 k = 0; k < workers; ++k) room_.workers.push_back(r.u32v());
        // Clamp anything a corrupted or older file could have put out of range.
        room_.floor = std::clamp(room_.floor, 0, kMaxFloors - 1);
        room_.colStart = std::clamp(room_.colStart, 0, kGridWidth - 1);
        room_.width = std::clamp(room_.width, 1, kMaxMergedWidth);
        room_.level = std::clamp(room_.level, 1, room_.def().maxLevel);
        room_.condition = saturate(room_.condition);
        room_.buildProgress = saturate(room_.buildProgress);
        if (r.failed()) return false;
        rooms_.push_back(std::move(room_));
    }
    nextId_ = r.u32v();
    if (nextId_ == 0) nextId_ = 1;
    rebuildIndex();
    updateDeepest();
    return !r.failed();
}

} // namespace hv::sim
