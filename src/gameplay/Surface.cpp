#include "gameplay/Surface.hpp"
#include "sim/Names.hpp"
#include <algorithm>

namespace hv::gameplay {

const char* siteTypeName(SiteType t) {
    switch (t) {
        case SiteType::RuinedBlock: return "Ruined Block";
        case SiteType::Warehouse:   return "Warehouse";
        case SiteType::Waterworks:  return "Waterworks";
        case SiteType::Farmstead:   return "Farmstead";
        case SiteType::Substation:  return "Substation";
        case SiteType::Factory:     return "Factory";
        case SiteType::Sanatorium:  return "Sanatorium";
        case SiteType::Bunker:      return "Sealed Bunker";
        case SiteType::Settlement:  return "Settlement";
        case SiteType::Nest:        return "Nest";
        case SiteType::RaiderCamp:  return "Raider Camp";
        default: return "?";
    }
}

void Site::expectedYield(f32 out[kResourceCount]) const {
    for (int i = 0; i < kResourceCount; ++i) out[i] = 0.0f;
    auto set = [&](Resource r, f32 v) { out[static_cast<int>(r)] = v; };
    switch (type) {
        case SiteType::RuinedBlock: set(Resource::Materials, 22); set(Resource::Scrip, 14); break;
        case SiteType::Warehouse:   set(Resource::Materials, 55); set(Resource::Food, 18); break;
        case SiteType::Waterworks:  set(Resource::Water, 60); set(Resource::Medicine, 6); break;
        case SiteType::Farmstead:   set(Resource::Food, 58); set(Resource::Water, 14); break;
        case SiteType::Substation:  set(Resource::Materials, 34); set(Resource::Power, 40); break;
        case SiteType::Factory:     set(Resource::Materials, 90); set(Resource::Scrip, 30); break;
        case SiteType::Sanatorium:  set(Resource::Medicine, 34); set(Resource::Research, 18); break;
        case SiteType::Bunker:      set(Resource::Research, 40); set(Resource::Materials, 60);
                                    set(Resource::Scrip, 70); break;
        case SiteType::Settlement:  set(Resource::Scrip, 55); set(Resource::Food, 22); break;
        case SiteType::Nest:        set(Resource::Materials, 18); set(Resource::Medicine, 10); break;
        case SiteType::RaiderCamp:  set(Resource::Scrip, 60); set(Resource::Materials, 30); break;
        default: break;
    }
    const f32 k = (1.0f - depletion) * (1.0f + 0.18f * static_cast<f32>(danger));
    for (int i = 0; i < kResourceCount; ++i) out[i] *= k;
}

void SurfaceMap::reset() {
    sites_.clear();
    nextId_ = 1;
}

void SurfaceMap::generate(Rng& rng) {
    reset();
    struct Band { f32 minKm, maxKm; i32 count; i32 dangerLo, dangerHi; };
    // Rings of increasing distance and danger around the shelter head.
    const Band bands[] = {
        {2.0f,  6.0f, 6, 0, 1},
        {6.0f, 12.0f, 7, 1, 2},
        {12.0f, 20.0f, 7, 2, 3},
        {20.0f, 30.0f, 6, 3, 4},
        {30.0f, 42.0f, 4, 4, 5},
    };
    const SiteType nearTypes[] = {
        SiteType::RuinedBlock, SiteType::Warehouse, SiteType::Farmstead,
        SiteType::Substation, SiteType::Settlement
    };
    const SiteType midTypes[] = {
        SiteType::Warehouse, SiteType::Waterworks, SiteType::Factory,
        SiteType::RaiderCamp, SiteType::Nest, SiteType::Settlement
    };
    const SiteType farTypes[] = {
        SiteType::Factory, SiteType::Sanatorium, SiteType::Bunker,
        SiteType::Nest, SiteType::RaiderCamp
    };

    int bandIndex = 0;
    for (const Band& b : bands) {
        for (i32 i = 0; i < b.count; ++i) {
            Site s;
            s.id = nextId_++;
            const f32 angle = rng.range(0.0f, kTwoPi);
            s.distanceKm = rng.range(b.minKm, b.maxKm);
            s.mapPos = Vec2{std::cos(angle) * s.distanceKm, std::sin(angle) * s.distanceKm};
            s.danger = rng.rangeI(b.dangerLo, b.dangerHi);
            if (bandIndex == 0)      s.type = nearTypes[rng.rangeI(0, 4)];
            else if (bandIndex <= 2) s.type = midTypes[rng.rangeI(0, 5)];
            else                     s.type = farTypes[rng.rangeI(0, 4)];
            if (s.type == SiteType::Settlement) s.danger = std::max(0, s.danger - 2);
            if (s.type == SiteType::Bunker) s.danger = std::min(5, s.danger + 1);
            s.name = randomPlaceName(rng);
            sites_.push_back(std::move(s));
        }
        ++bandIndex;
    }
    // Sort by distance so "the next place to explore" is a simple scan.
    std::sort(sites_.begin(), sites_.end(),
              [](const Site& a, const Site& b) { return a.distanceKm < b.distanceKm; });
    // The two closest sites start known, so there is somewhere to go on day one.
    for (size_t i = 0; i < sites_.size() && i < 2; ++i) sites_[i].discovered = true;
}

Site* SurfaceMap::find(u32 id) {
    for (Site& s : sites_) if (s.id == id) return &s;
    return nullptr;
}
const Site* SurfaceMap::find(u32 id) const {
    return const_cast<SurfaceMap*>(this)->find(id);
}

u32 SurfaceMap::discoverNext(Rng& rng) {
    std::vector<Site*> candidates;
    for (Site& s : sites_) if (!s.discovered) candidates.push_back(&s);
    if (candidates.empty()) return 0;
    // Bias strongly toward the nearest unknown site; the far ones come later.
    const size_t window = std::min<size_t>(candidates.size(), 4);
    Site* picked = candidates[static_cast<size_t>(rng.rangeI(0, static_cast<i32>(window) - 1))];
    picked->discovered = true;
    return picked->id;
}

void SurfaceMap::tick(f32 dtSeconds) {
    // Looted sites slowly recover so the map does not run dry.
    const f32 recovery = dtSeconds / 1800.0f;
    for (Site& s : sites_) {
        s.depletion = std::max(0.0f, s.depletion - recovery);
        if (s.cleared) {
            s.respawnTimer += dtSeconds;
            if (s.respawnTimer > 2400.0f) { s.cleared = false; s.respawnTimer = 0.0f; }
        }
    }
}

i32 SurfaceMap::discoveredCount() const {
    i32 n = 0;
    for (const Site& s : sites_) if (s.discovered) ++n;
    return n;
}

void SurfaceMap::serialize(BlobWriter& w) const {
    w.u32v(static_cast<u32>(sites_.size()));
    for (const Site& s : sites_) {
        w.u32v(s.id);
        w.str(s.name);
        w.u8v(static_cast<u8>(s.type));
        w.f32v(s.mapPos.x); w.f32v(s.mapPos.y);
        w.f32v(s.distanceKm);
        w.i32v(s.danger);
        w.boolv(s.discovered);
        w.boolv(s.cleared);
        w.f32v(s.depletion);
        w.f32v(s.respawnTimer);
        w.u32v(s.questId);
    }
    w.u32v(nextId_);
}

bool SurfaceMap::deserialize(BlobReader& r, u32 version) {
    (void)version;
    reset();
    const u32 n = r.u32v();
    if (r.failed() || n > 4096) return false;
    sites_.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        Site s;
        s.id = r.u32v();
        s.name = r.str();
        const u8 t = r.u8v();
        s.type = t < static_cast<u8>(SiteType::Count) ? static_cast<SiteType>(t)
                                                      : SiteType::RuinedBlock;
        s.mapPos.x = r.f32v(); s.mapPos.y = r.f32v();
        s.distanceKm = r.f32v();
        s.danger = std::clamp(r.i32v(), 0, 5);
        s.discovered = r.boolv();
        s.cleared = r.boolv();
        s.depletion = saturate(r.f32v());
        s.respawnTimer = r.f32v();
        s.questId = r.u32v();
        if (r.failed()) return false;
        sites_.push_back(std::move(s));
    }
    nextId_ = std::max(1u, r.u32v());
    return !r.failed();
}

} // namespace hv::gameplay
