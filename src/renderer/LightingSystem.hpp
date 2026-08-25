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
    /// Uses partial_sort into a reused buffer: fully sorting hundreds of
    /// lights every frame to then throw all but ~16 away was wasted work,
    /// and the temporary vector was a per-frame heap allocation.
    const std::vector<Light>& nearest(const Vec3& eye, size_t maxCount) const {
        scratch_ = lights_;
        const size_t keep = std::min(maxCount, scratch_.size());
        auto byDistance = [&](const Light& a, const Light& b) {
            // Directional lights have no position; keep them first, they
            // always affect the whole scene.
            const bool ad = a.type == LightType::Directional;
            const bool bd = b.type == LightType::Directional;
            if (ad != bd) return ad;
            return distanceSq(a.position, eye) < distanceSq(b.position, eye);
        };
        if (keep < scratch_.size())
            std::partial_sort(scratch_.begin(), scratch_.begin() + static_cast<long>(keep),
                              scratch_.end(), byDistance);
        else
            std::sort(scratch_.begin(), scratch_.end(), byDistance);
        scratch_.resize(keep);
        return scratch_;
    }

    static constexpr size_t kMaxLights = 512;

private:
    std::vector<Light> lights_;
    mutable std::vector<Light> scratch_;   ///< reused by nearest(), never reallocated after warm-up
    Vec3 ambient_{0.09f, 0.10f, 0.12f};   ///< cool, dim — this is a bunker, not daylight
};

/// A flickering emergency/industrial light's instantaneous intensity multiplier.
inline f32 flickerIntensity(f32 timeSeconds, f32 speed, f32 amount, u32 seedSalt) {
    if (speed <= 0.0f || amount <= 0.0f) return 1.0f;
    const f32 n = fbm(timeSeconds * speed, static_cast<f32>(seedSalt) * 0.37f, 2, seedSalt);
    return clampf(1.0f - amount * (1.0f - n), 0.05f, 1.2f);
}

} // namespace hv::gfx
