#pragma once
// Technology progression. Nodes cost research points and unlock rooms, gear,
// or permanent multipliers applied by the simulation.
#include "sim/SimTypes.hpp"
#include "core/Serialization.hpp"
#include <string>
#include <vector>

namespace hv::gameplay {

using namespace hv::sim;

struct TechBonus {
    f32 powerOutput = 0.0f;      ///< additive multiplier, 0.10 = +10%
    f32 waterOutput = 0.0f;
    f32 foodOutput = 0.0f;
    f32 materialOutput = 0.0f;
    f32 medicineOutput = 0.0f;
    f32 researchOutput = 0.0f;
    f32 storageCapacity = 0.0f;
    f32 defence = 0.0f;          ///< damage reduction for defenders
    f32 breakdownResistance = 0.0f;
    f32 expeditionYield = 0.0f;
    f32 trainingSpeed = 0.0f;
    f32 moraleFloor = 0.0f;      ///< added to every resident's happiness target
    void add(const TechBonus& o) {
        powerOutput += o.powerOutput;   waterOutput += o.waterOutput;
        foodOutput += o.foodOutput;     materialOutput += o.materialOutput;
        medicineOutput += o.medicineOutput; researchOutput += o.researchOutput;
        storageCapacity += o.storageCapacity; defence += o.defence;
        breakdownResistance += o.breakdownResistance;
        expeditionYield += o.expeditionYield; trainingSpeed += o.trainingSpeed;
        moraleFloor += o.moraleFloor;
    }
};

struct TechNode {
    std::string id;
    std::string name;
    std::string description;
    f32 cost = 50.0f;              ///< research points
    f32 researchSeconds = 120.0f;  ///< wall-clock at 1x once started
    std::vector<std::string> prerequisites;
    TechBonus bonus;
    i32 tier = 0;                  ///< used purely for tree layout
    i32 column = 0;
};

class TechTree {
public:
    TechTree();

    const std::vector<TechNode>& nodes() const { return nodes_; }
    const TechNode* find(const std::string& id) const;
    bool unlocked(const std::string& id) const;
    const std::vector<std::string>& unlockedIds() const { return unlocked_; }
    bool available(const std::string& id) const;   ///< prerequisites satisfied, not yet owned

    /// Starts research on a node; returns false when unavailable or unaffordable.
    bool beginResearch(const std::string& id, ResourcePool& res);
    void cancelResearch();
    const std::string& activeId() const { return activeId_; }
    f32 activeProgress() const { return activeTotal_ > 0.0f ? saturate(activeElapsed_ / activeTotal_) : 0.0f; }
    f32 activeRemainingSeconds() const { return std::max(0.0f, activeTotal_ - activeElapsed_); }

    /// Advances the active project. Returns the id just completed, or "".
    std::string tick(f32 dtSeconds, f32 speedMultiplier);

    const TechBonus& bonuses() const { return bonuses_; }
    void recomputeBonuses();

    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);
    void reset();

private:
    std::vector<TechNode>    nodes_;
    std::vector<std::string> unlocked_;
    std::string              activeId_;
    f32                      activeElapsed_ = 0.0f;
    f32                      activeTotal_ = 0.0f;
    TechBonus                bonuses_;
};

} // namespace hv::gameplay
