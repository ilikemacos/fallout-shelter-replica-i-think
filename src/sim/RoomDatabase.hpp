#pragma once
#include "sim/SimTypes.hpp"
#include <vector>

namespace hv::sim {

enum class RoomType : u8 {
    None = 0,
    Entrance,        ///< the surface airlock; always exists, floor 0
    Elevator,        ///< vertical link between floors
    Corridor,        ///< cheap walkable filler
    Generator,
    WaterPlant,
    Hydroponics,
    Cafeteria,
    Dormitory,
    Storage,
    Workshop,
    Medical,
    Training,
    Security,
    Laboratory,
    Armory,
    Command,
    Recreation,
    Recycling,
    Communications,
    Research,
    Count
};
constexpr int kRoomTypeCount = static_cast<int>(RoomType::Count);

/// What a room does each production tick.
enum class RoomFunction : u8 {
    None = 0, Produce, Store, Rest, Heal, Train, Defend, Research, Craft, Morale, Trade, Transit
};

struct RoomDef {
    RoomType    type = RoomType::None;
    const char* name = "";
    const char* description = "";
    i32  width = 2;             ///< cells occupied
    i32  maxLevel = 3;
    i32  workerSlots = 2;       ///< per level-1 room; grows with upgrades
    Cost buildCost;
    Cost upgradeCost;           ///< multiplied by the target level
    RoomFunction function = RoomFunction::None;
    Resource produces = Resource::Power;
    f32  productionPerMinute = 0.0f;   ///< at level 1, fully staffed, skill 5
    f32  powerDraw = 0.0f;             ///< units per minute while powered
    Skill primarySkill = Skill::Engineering;
    Resource storesResource = Resource::Materials;
    f32  storageBonus = 0.0f;          ///< capacity added per level
    f32  moraleAura = 0.0f;            ///< happiness/hour for occupants
    i32  requiredPopulation = 0;       ///< gating: dwellers needed to unlock
    const char* techRequired = "";     ///< empty = available from the start
};

/// Static table of every buildable room. Indexed by RoomType.
const RoomDef& roomDef(RoomType t);
const std::vector<RoomDef>& allRoomDefs();
const char* roomTypeName(RoomType t);
/// Rooms of the same type placed side by side on a floor merge into one wider
/// room, up to this many cells.
constexpr i32 kMaxMergedWidth = 6;

/// Level-scaled numbers.
f32  roomProduction(const RoomDef& d, i32 level, i32 widthCells);
f32  roomPowerDraw(const RoomDef& d, i32 level, i32 widthCells);
i32  roomWorkerSlots(const RoomDef& d, i32 level, i32 widthCells);
Cost roomUpgradeCost(const RoomDef& d, i32 targetLevel, i32 widthCells);
Cost roomBuildCost(const RoomDef& d, i32 widthCells, i32 floor);

} // namespace hv::sim
