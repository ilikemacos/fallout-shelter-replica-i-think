#pragma once
#include "core/Types.hpp"

namespace hv::ecs {

/// 32-bit id split into index + generation so stale handles are detectable.
struct Entity {
    u32 id = 0;
    static constexpr u32 kIndexBits = 22;
    static constexpr u32 kIndexMask = (1u << kIndexBits) - 1;

    constexpr u32 index() const { return id & kIndexMask; }
    constexpr u32 generation() const { return id >> kIndexBits; }
    constexpr bool valid() const { return id != 0; }
    constexpr bool operator==(const Entity& o) const { return id == o.id; }
    constexpr bool operator!=(const Entity& o) const { return id != o.id; }

    static constexpr Entity make(u32 index, u32 generation) {
        return Entity{(generation << kIndexBits) | (index & kIndexMask)};
    }
};

constexpr Entity kNullEntity{0};

} // namespace hv::ecs
