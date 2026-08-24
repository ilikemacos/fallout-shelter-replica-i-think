#pragma once
// The wasteland above: a set of discoverable locations expeditions travel to.
#include "sim/SimTypes.hpp"
#include "core/Random.hpp"
#include "core/Serialization.hpp"
#include <string>
#include <vector>

namespace hv::gameplay {

using namespace hv::sim;

enum class SiteType : u8 {
    RuinedBlock = 0,   ///< collapsed housing, light salvage
    Warehouse,         ///< materials
    Waterworks,        ///< water and medicine
    Farmstead,         ///< food
    Substation,        ///< power components
    Factory,           ///< heavy materials, dangerous
    Sanatorium,        ///< medicine, very dangerous
    Bunker,            ///< pre-collapse facility, best loot
    Settlement,        ///< trade and recruits, safe
    Nest,              ///< creature lair, combat
    RaiderCamp,        ///< hostile humans
    Count
};

const char* siteTypeName(SiteType t);

struct Site {
    u32 id = 0;
    std::string name;
    SiteType type = SiteType::RuinedBlock;
    Vec2 mapPos;              ///< position on the surface map, in km
    f32  distanceKm = 4.0f;   ///< travel time driver
    i32  danger = 1;          ///< 0..5
    bool discovered = false;
    bool cleared = false;     ///< hostiles wiped; yields drop but travel is safe
    f32  depletion = 0.0f;    ///< 0..1, rises as it is looted, recovers slowly
    f32  respawnTimer = 0.0f;
    u32  questId = 0;         ///< non-zero when a quest points here

    /// Which resources this site tends to give up.
    void expectedYield(f32 out[kResourceCount]) const;
};

/// The generated surface map. Sites are procedurally named and placed but the
/// distribution is authored so early play is survivable.
class SurfaceMap {
public:
    void generate(Rng& rng);
    void reset();
    const std::vector<Site>& sites() const { return sites_; }
    std::vector<Site>& sites() { return sites_; }
    Site* find(u32 id);
    const Site* find(u32 id) const;
    /// Reveals one undiscovered site, preferring nearby ones. Returns its id.
    u32 discoverNext(Rng& rng);
    void tick(f32 dtSeconds);
    i32 discoveredCount() const;

    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);

private:
    std::vector<Site> sites_;
    u32 nextId_ = 1;
};

} // namespace hv::gameplay
