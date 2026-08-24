#pragma once
// The shelter grid: what is dug out, what is built, and what connects to what.
#include "sim/Room.hpp"
#include "core/Serialization.hpp"
#include <optional>
#include <string>
#include <vector>

namespace hv::sim {

enum class BuildError : u8 {
    None = 0, OutOfBounds, Occupied, NotAdjacent, TooWide, Unaffordable,
    PopulationLocked, TechLocked, NeedsElevator, RoomTypeInvalid
};
const char* buildErrorText(BuildError e);

struct BuildPlan {
    RoomType type = RoomType::None;
    Cell     origin;             ///< leftmost cell
    i32      width = 1;
    Cost     cost;
    RoomId   mergeInto = kNoRoom;  ///< non-zero when this extends an existing room
    BuildError error = BuildError::None;
    bool ok() const { return error == BuildError::None; }
};

class Shelter {
public:
    void reset();
    /// Digs the starting airlock and its first elevator.
    void createStarter();

    // ---- queries -----------------------------------------------------------
    RoomId roomIdAt(const Cell& c) const;
    Room*  roomAt(const Cell& c);
    const Room* roomAt(const Cell& c) const;
    Room*  room(RoomId id);
    const Room* room(RoomId id) const;
    std::vector<Room>& rooms() { return rooms_; }
    const std::vector<Room>& rooms() const { return rooms_; }
    size_t roomCount() const { return rooms_.size(); }
    i32 deepestFloor() const { return deepestFloor_; }
    bool cellOccupied(const Cell& c) const { return roomIdAt(c) != kNoRoom; }
    bool cellWalkable(const Cell& c) const;
    bool hasElevatorAt(const Cell& c) const;
    /// Any elevator column that serves both floors.
    std::optional<i32> elevatorColumnBetween(i32 floorA, i32 floorB) const;
    std::vector<RoomId> roomsOfType(RoomType t) const;
    i32 countOfType(RoomType t) const;

    // ---- construction ------------------------------------------------------
    /// Validates a placement without changing anything. `population` and
    /// `unlockedTech` gate room availability.
    BuildPlan planBuild(RoomType type, const Cell& origin, i32 width,
                        const ResourcePool& res, i32 population,
                        const std::vector<std::string>& unlockedTech) const;
    /// Executes a plan produced by planBuild. Returns the affected room.
    RoomId build(const BuildPlan& plan, ResourcePool& res);
    /// Refunds half the build cost.
    bool demolish(RoomId id, ResourcePool& res);
    bool upgrade(RoomId id, ResourcePool& res);

    /// Total storage capacity contributed by rooms, per resource.
    void applyStorageCapacity(ResourcePool& res) const;

    // ---- serialization -----------------------------------------------------
    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);

    RoomId nextRoomId() const { return nextId_; }
    void setNextRoomId(RoomId v) { nextId_ = v; }

private:
    void rebuildIndex();
    void updateDeepest();

    std::vector<Room> rooms_;
    /// floor-major grid of room ids; 0 = solid rock.
    std::array<RoomId, kMaxFloors * kGridWidth> grid_{};
    RoomId nextId_ = 1;
    i32    deepestFloor_ = 0;
};

} // namespace hv::sim
