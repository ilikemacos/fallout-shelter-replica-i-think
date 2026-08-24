#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace hv {

using u8  = std::uint8_t;   using i8  = std::int8_t;
using u16 = std::uint16_t;  using i16 = std::int16_t;
using u32 = std::uint32_t;  using i32 = std::int32_t;
using u64 = std::uint64_t;  using i64 = std::int64_t;
using f32 = float;          using f64 = double;

/// Opaque, generation-checked handle used by the renderer and asset system.
template <typename Tag>
struct Handle {
    u32 index = 0;
    u32 generation = 0;
    constexpr bool valid() const { return generation != 0; }
    constexpr bool operator==(const Handle& o) const {
        return index == o.index && generation == o.generation;
    }
    constexpr bool operator!=(const Handle& o) const { return !(*this == o); }
};

} // namespace hv
