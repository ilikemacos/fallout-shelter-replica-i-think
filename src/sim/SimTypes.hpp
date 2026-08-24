#pragma once
#include "core/Types.hpp"
#include "core/Math.hpp"
#include <array>
#include <string>

namespace hv::sim {

// ---------------------------------------------------------------------------
//  Shelter grid. The facility is a set of horizontal floors dug downward from
//  the surface entry; every room occupies whole cells on exactly one floor.
// ---------------------------------------------------------------------------
constexpr i32 kGridWidth   = 16;   ///< cells across one floor
constexpr i32 kMaxFloors   = 12;   ///< how deep the shelter can go
constexpr f32 kCellWidth   = 6.0f; ///< world units per cell (X)
constexpr f32 kFloorHeight = 4.6f; ///< world units per floor (Y)
constexpr f32 kRoomDepth   = 7.0f; ///< world units into the screen (Z)

struct Cell {
    i32 floor = 0;
    i32 col   = 0;
    constexpr bool operator==(const Cell& o) const { return floor == o.floor && col == o.col; }
    constexpr bool operator!=(const Cell& o) const { return !(*this == o); }
    constexpr bool inBounds() const {
        return floor >= 0 && floor < kMaxFloors && col >= 0 && col < kGridWidth;
    }
    constexpr i32 linear() const { return floor * kGridWidth + col; }
};

/// Centre of a cell in world space. X grows right, Y grows up (floor 0 is the
/// topmost dug level), Z is the room's depth axis.
inline Vec3 cellToWorld(const Cell& c) {
    return Vec3{ (static_cast<f32>(c.col) - (kGridWidth - 1) * 0.5f) * kCellWidth,
                 -static_cast<f32>(c.floor) * kFloorHeight,
                 0.0f };
}

inline Cell worldToCell(const Vec3& p) {
    Cell c;
    c.col = static_cast<i32>(std::lround(p.x / kCellWidth + (kGridWidth - 1) * 0.5f));
    c.floor = static_cast<i32>(std::lround(-p.y / kFloorHeight));
    return c;
}

// ---------------------------------------------------------------------------
//  Resources
// ---------------------------------------------------------------------------
enum class Resource : u8 {
    Power = 0, Water, Food, Medicine, Materials, Research, Scrip, Count
};
constexpr int kResourceCount = static_cast<int>(Resource::Count);

const char* resourceName(Resource r);
const char* resourceShortName(Resource r);

struct ResourcePool {
    std::array<f32, kResourceCount> amount{};
    std::array<f32, kResourceCount> capacity{};

    f32  get(Resource r) const { return amount[static_cast<int>(r)]; }
    f32  cap(Resource r) const { return capacity[static_cast<int>(r)]; }
    f32  fraction(Resource r) const {
        const f32 c = cap(r);
        return c > 0.0f ? saturate(get(r) / c) : 0.0f;
    }
    /// Adds and clips to capacity; returns how much was actually stored.
    f32  add(Resource r, f32 v) {
        const int i = static_cast<int>(r);
        const f32 before = amount[i];
        amount[i] = clampf(amount[i] + v, 0.0f, capacity[i]);
        return amount[i] - before;
    }
    bool has(Resource r, f32 v) const { return get(r) >= v - 1e-4f; }
    /// Consumes when affordable; leaves the pool untouched otherwise.
    bool spend(Resource r, f32 v) {
        if (!has(r, v)) return false;
        amount[static_cast<int>(r)] -= v;
        return true;
    }
    /// Consumes up to `v`, returning the shortfall (0 when fully satisfied).
    f32 drain(Resource r, f32 v) {
        const int i = static_cast<int>(r);
        const f32 taken = std::min(amount[i], v);
        amount[i] -= taken;
        return v - taken;
    }
    void setCapacity(Resource r, f32 v) {
        const int i = static_cast<int>(r);
        capacity[i] = v;
        amount[i] = std::min(amount[i], v);
    }
};

/// A resource cost, e.g. the price of building or upgrading a room.
struct Cost {
    f32 materials = 0;
    f32 scrip = 0;
    f32 power = 0;      ///< one-off draw, not a running cost
    bool affordable(const ResourcePool& p) const {
        return p.has(Resource::Materials, materials) && p.has(Resource::Scrip, scrip);
    }
    void pay(ResourcePool& p) const {
        p.spend(Resource::Materials, materials);
        p.spend(Resource::Scrip, scrip);
    }
    Cost scaled(f32 k) const { return Cost{materials * k, scrip * k, power}; }
};

// ---------------------------------------------------------------------------
//  Skills — original set, no franchise acronym.
// ---------------------------------------------------------------------------
enum class Skill : u8 {
    Engineering = 0,  ///< generators, workshops, repairs
    Hydrology,        ///< water purification
    Agronomy,         ///< hydroponics, cafeteria
    Medicine,         ///< medbay, healing, radiation
    Security,         ///< combat, defence, intruders
    Science,          ///< labs, research
    Logistics,        ///< storage, recycling, throughput
    Presence,         ///< morale, command, communications, trading
    Count
};
constexpr int kSkillCount = static_cast<int>(Skill::Count);
const char* skillName(Skill s);
const char* skillAbbrev(Skill s);

struct SkillSet {
    std::array<u8, kSkillCount> value{};   ///< 1..10
    u8 get(Skill s) const { return value[static_cast<int>(s)]; }
    void set(Skill s, u8 v) { value[static_cast<int>(s)] = static_cast<u8>(std::min<int>(v, 10)); }
    int total() const {
        int t = 0;
        for (u8 v : value) t += v;
        return t;
    }
};

} // namespace hv::sim
