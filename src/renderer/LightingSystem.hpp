#pragma once
// Collects the lights active for one frame: a soft directional fill standing
// in for the buried facility's ambient glow, plus per-room point/spot lights
// (warm industrial fixtures, cold emergency strips, flickering damaged ones).
#include "renderer/RenderTypes.hpp"
#include "core/Random.hpp"
#include <algorithm>
#include <vector>

namespace hv::gfx {

class LightingSystem {
public:
    void beginFrame() { lights_.clear(); }
    void addLight(const Light& l) { if (lights_.size() < kMaxLights) lights_.push_back(l); }
    void setAmbient(const Vec3& colorScale) { ambient_ = colorScale; }
    const Vec3& ambient() const { return ambient_; }
    const std::vector<Light>& lights() const { return lights_; }

    /// Culls to the N most influential lights near `eye` so a large shelter
    /// with hundreds of fixtures still renders a bounded light count.
    std::vector<Light> nearest(const Vec3& eye, size_t maxCount) const {
        std::vector<Light> sorted = lights_;
        std::sort(sorted.begin(), sorted.end(), [&](const Light& a, const Light& b) {
            return distanceSq(a.position, eye) < distanceSq(b.position, eye);
        });
        if (sorted.size() > maxCount) sorted.resize(maxCount);
        return sorted;
    }

    static constexpr size_t kMaxLights = 512;

private:
    std::vector<Light> lights_;
    Vec3 ambient_{0.09f, 0.10f, 0.12f};   ///< cool, dim — this is a bunker, not daylight
};

/// A flickering emergency/industrial light's instantaneous intensity multiplier.
inline f32 flickerIntensity(f32 timeSeconds, f32 speed, f32 amount, u32 seedSalt) {
    if (speed <= 0.0f || amount <= 0.0f) return 1.0f;
    const f32 n = fbm(timeSeconds * speed, static_cast<f32>(seedSalt) * 0.37f, 2, seedSalt);
    return clampf(1.0f - amount * (1.0f - n), 0.05f, 1.2f);
}

} // namespace hv::gfx
