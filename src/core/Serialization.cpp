#include "core/Serialization.hpp"

namespace hv {
namespace {
struct Crc32Table {
    u32 v[256];
    constexpr Crc32Table() : v() {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            v[i] = c;
        }
    }
};
constexpr Crc32Table kTable{};
} // namespace

u32 crc32(const u8* data, size_t size) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = kTable.v[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace hv
