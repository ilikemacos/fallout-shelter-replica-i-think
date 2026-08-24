#pragma once
// Deterministic PRNG (xoshiro256**). Every gameplay system draws from a
// seeded stream so a save round-trip reproduces the same future.
#include "core/Types.hpp"
#include "core/Math.hpp"
#include <string>

namespace hv {

class Rng {
public:
    Rng() { seed(0x9E3779B97F4A7C15ull); }
    explicit Rng(u64 s) { seed(s); }

    void seed(u64 s) {
        // SplitMix64 expansion so even a tiny seed fills the state well.
        for (u64& v : s_) {
            s += 0x9E3779B97F4A7C15ull;
            u64 z = s;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            v = z ^ (z >> 31);
        }
    }

    u64 next() {
        const u64 result = rotl(s_[1] * 5, 7) * 9;
        const u64 t = s_[1] << 17;
        s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    /// Uniform in [0,1).
    f32 unit() { return static_cast<f32>((next() >> 40)) * (1.0f / 16777216.0f); }
    f32 range(f32 lo, f32 hi) { return lo + unit() * (hi - lo); }
    /// Uniform integer in [lo, hi] inclusive.
    i32 rangeI(i32 lo, i32 hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<i32>(next() % static_cast<u64>(hi - lo + 1));
    }
    bool chance(f32 p) { return unit() < p; }
    /// Roughly normal via the sum of four uniforms (Bates); good enough for stats.
    f32 gaussian(f32 mean, f32 stddev) {
        const f32 s = (unit() + unit() + unit() + unit() - 2.0f) * 1.7320508f;
        return mean + s * stddev;
    }
    template <typename T>
    const T& pick(const std::vector<T>& v) { return v[static_cast<size_t>(rangeI(0, static_cast<i32>(v.size()) - 1))]; }

    u64 state(int i) const { return s_[i]; }
    void setState(u64 a, u64 b, u64 c, u64 d) { s_[0]=a; s_[1]=b; s_[2]=c; s_[3]=d; }

private:
    static u64 rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }
    u64 s_[4]{};
};

/// Stable hash used for procedural asset variation (FNV-1a 64).
inline u64 hashString(std::string_view s) {
    u64 h = 1469598103934665603ull;
    for (char c : s) { h ^= static_cast<u8>(c); h *= 1099511628211ull; }
    return h;
}

/// Value noise, used by the procedural texture and terrain generators.
inline f32 hash01(i32 x, i32 y, u32 seed) {
    u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<f32>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
}

inline f32 valueNoise(f32 x, f32 y, u32 seed) {
    const i32 xi = static_cast<i32>(std::floor(x)), yi = static_cast<i32>(std::floor(y));
    const f32 fx = smoothstepf(x - static_cast<f32>(xi));
    const f32 fy = smoothstepf(y - static_cast<f32>(yi));
    const f32 a = hash01(xi,     yi,     seed), b = hash01(xi + 1, yi,     seed);
    const f32 c = hash01(xi,     yi + 1, seed), d = hash01(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

inline f32 fbm(f32 x, f32 y, int octaves, u32 seed) {
    f32 sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x * freq, y * freq, seed + static_cast<u32>(i) * 977u) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return norm > 0 ? sum / norm : 0.0f;
}

} // namespace hv
