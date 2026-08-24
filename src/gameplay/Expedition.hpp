#pragma once
// Surface expeditions: a squad walks out, explores, fights, loots and walks
// home. Everything resolves on the simulation tick, no player input required
// once the squad has left.
#include "gameplay/Surface.hpp"
#include "gameplay/Combat.hpp"
#include "sim/Resident.hpp"
#include <string>
#include <vector>

namespace hv::sim { class World; }

namespace hv::gameplay {

enum class ExpeditionPhase : u8 {
    Outbound = 0, Exploring, Fighting, Returning, Finished, Lost
};
const char* expeditionPhaseName(ExpeditionPhase p);

struct ExpeditionLogEntry {
    f32 timeHours = 0.0f;
    std::string text;
    bool bad = false;
};

struct Expedition {
    u32 id = 0;
    u32 siteId = 0;
    std::string siteName;
    std::vector<ResidentId> squad;
    ExpeditionPhase phase = ExpeditionPhase::Outbound;

    f32 elapsed = 0.0f;            ///< seconds since departure
    f32 travelSeconds = 120.0f;    ///< each way
    f32 exploreSeconds = 240.0f;
    f32 phaseTimer = 0.0f;
    f32 nextEventIn = 20.0f;

    std::array<f32, kResourceCount> haul{};
    std::vector<ItemStack> loot;
    std::vector<ExpeditionLogEntry> log;
    Encounter fight;
    i32 danger = 1;
    i32 encountersWon = 0;
    bool recalled = false;
    u32 questId = 0;

    f32 progress() const {
        const f32 total = travelSeconds * 2.0f + exploreSeconds;
        return total > 0.0f ? saturate(elapsed / total) : 0.0f;
    }
    bool active() const { return phase != ExpeditionPhase::Finished && phase != ExpeditionPhase::Lost; }
    void addLog(f32 hours, std::string text, bool bad = false) {
        log.push_back(ExpeditionLogEntry{hours, std::move(text), bad});
        if (log.size() > 40) log.erase(log.begin());
    }
};

/// Everything the caller needs to apply an expedition tick back onto the world.
struct ExpeditionOutcome {
    bool finished = false;
    bool squadLost = false;
    std::vector<std::string> notifications;
};

class ExpeditionManager {
public:
    /// Squad members must be available; returns 0 when the launch is rejected.
    u32 launch(const Site& site, const std::vector<ResidentId>& squad, Rng& rng);
    void recall(u32 expeditionId);
    /// Advances every expedition. `residents` is used for combat strength and
    /// receives damage, experience and loot on return.
    void tick(f32 dtSeconds, Rng& rng, hv::sim::World& world);

    std::vector<Expedition>& all() { return expeditions_; }
    const std::vector<Expedition>& all() const { return expeditions_; }
    Expedition* find(u32 id);
    const Expedition* find(u32 id) const;
    size_t activeCount() const;

    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);
    void reset();

private:
    std::vector<Expedition> expeditions_;
    u32 nextId_ = 1;
};

} // namespace hv::gameplay
