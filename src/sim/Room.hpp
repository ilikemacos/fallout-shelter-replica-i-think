#pragma once
#include "sim/RoomDatabase.hpp"
#include "sim/Resident.hpp"

namespace hv::sim {

/// A built room instance. Rooms of one type sitting side by side on a floor
/// are merged into a single wider instance at build time.
struct Room {
    RoomId   id = kNoRoom;
    RoomType type = RoomType::None;
    i32      floor = 0;
    i32      colStart = 0;
    i32      width = 1;
    i32      level = 1;

    f32  condition = 1.0f;      ///< 1 = pristine, 0 = broken down
    bool broken = false;        ///< produces nothing until repaired
    f32  fire = 0.0f;           ///< 0 = none, 1 = fully involved
    f32  powerSatisfaction = 1.0f;  ///< fraction of its draw that was met
    f32  storedOutput = 0.0f;   ///< produced but not yet collected
    f32  rushCooldown = 0.0f;   ///< seconds until production can be rushed again
    f32  animPhase = 0.0f;      ///< drives machine animation in the renderer
    f32  buildProgress = 1.0f;  ///< 0..1 while under construction
    u32  emergency = 0;         ///< id of an active emergency here, 0 = none

    std::vector<ResidentId> workers;

    const RoomDef& def() const { return roomDef(type); }
    i32  colEnd() const { return colStart + width - 1; }
    bool coversCell(const Cell& c) const {
        return c.floor == floor && c.col >= colStart && c.col <= colEnd();
    }
    bool operational() const {
        return !broken && fire < 0.15f && buildProgress >= 1.0f && emergency == 0;
    }
    i32  workerSlots() const { return roomWorkerSlots(def(), level, width); }
    bool hasFreeSlot() const { return static_cast<i32>(workers.size()) < workerSlots(); }
    f32  powerDraw() const { return roomPowerDraw(def(), level, width); }
    f32  baseProduction() const { return roomProduction(def(), level, width); }
    Cost nextUpgradeCost() const { return roomUpgradeCost(def(), level + 1, width); }
    bool canUpgrade() const { return level < def().maxLevel; }
    /// Centre of the room in world space.
    Vec3 worldCenter() const {
        const f32 x = (static_cast<f32>(colStart) + static_cast<f32>(width - 1) * 0.5f -
                       (kGridWidth - 1) * 0.5f) * kCellWidth;
        return Vec3{x, -static_cast<f32>(floor) * kFloorHeight, 0.0f};
    }
    AABB bounds() const {
        const Vec3 c = worldCenter();
        const f32 halfW = static_cast<f32>(width) * kCellWidth * 0.5f;
        return AABB{ Vec3{c.x - halfW, c.y - 0.1f, -kRoomDepth * 0.5f},
                     Vec3{c.x + halfW, c.y + kFloorHeight * 0.9f, kRoomDepth * 0.5f} };
    }
    /// Evenly spaced work positions inside the room.
    Vec3 workstation(int index) const {
        const i32 slots = std::max(1, workerSlots());
        const f32 t = (static_cast<f32>(index % slots) + 0.5f) / static_cast<f32>(slots);
        const Vec3 c = worldCenter();
        const f32 halfW = static_cast<f32>(width) * kCellWidth * 0.5f - 0.8f;
        return Vec3{ c.x - halfW + t * 2.0f * halfW, c.y, -0.9f };
    }
};

} // namespace hv::sim
