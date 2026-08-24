#pragma once
// Objective-driven quests. Most are generated from the world's current state
// so the log always has something achievable in it.
#include "sim/SimTypes.hpp"
#include "sim/RoomDatabase.hpp"
#include "sim/ItemDatabase.hpp"
#include "core/Random.hpp"
#include "core/Serialization.hpp"
#include <string>
#include <vector>

namespace hv::gameplay {

using namespace hv::sim;

enum class ObjectiveKind : u8 {
    BuildRoom = 0,     ///< param = RoomType, target = count
    UpgradeRoom,       ///< param = RoomType, target = level
    ReachPopulation,
    StockResource,     ///< param = Resource, target = amount
    CompleteExpeditions,
    DefeatEnemies,
    UnlockTech,        ///< techId
    SurviveDays,
    TrainSkill,        ///< param = Skill, target = level reached by anyone
    ExploreSites,
    Count
};

struct Objective {
    ObjectiveKind kind = ObjectiveKind::BuildRoom;
    i32 param = 0;
    f32 target = 1.0f;
    f32 progress = 0.0f;
    std::string techId;
    bool complete() const { return progress >= target - 1e-3f; }
    std::string describe() const;
};

struct QuestReward {
    std::array<f32, kResourceCount> resources{};
    u16 itemId = 0;
    f32 experience = 0.0f;
    std::string unlockTech;
    std::string describe() const;
};

enum class QuestState : u8 { Offered = 0, Active, Complete, Claimed, Failed };

struct Quest {
    u32 id = 0;
    std::string title;
    std::string summary;
    std::vector<Objective> objectives;
    QuestReward reward;
    QuestState state = QuestState::Active;
    u32 siteId = 0;            ///< optional surface link
    f32 timeLimitSeconds = 0;  ///< 0 = untimed
    f32 elapsed = 0.0f;
    bool mainline = false;

    bool allObjectivesComplete() const {
        for (const Objective& o : objectives) if (!o.complete()) return false;
        return !objectives.empty();
    }
    f32 progressFraction() const {
        if (objectives.empty()) return 0.0f;
        f32 sum = 0.0f;
        for (const Objective& o : objectives)
            sum += o.target > 0.0f ? saturate(o.progress / o.target) : 1.0f;
        return sum / static_cast<f32>(objectives.size());
    }
};

class QuestLog {
public:
    void reset();
    /// Seeds the opening chain of mainline quests.
    void seedMainline();
    /// Adds a generated side quest sized to the shelter's current state.
    u32  generateSideQuest(Rng& rng, i32 population, i32 deepestFloor, i32 discoveredSites);
    void add(Quest q);

    std::vector<Quest>& quests() { return quests_; }
    const std::vector<Quest>& quests() const { return quests_; }
    Quest* find(u32 id);
    i32 activeCount() const;
    i32 completedCount() const { return completed_; }

    /// Bumps every matching objective. Called by the world when things happen.
    void notify(ObjectiveKind kind, i32 param, f32 amount, const std::string& techId = {});
    /// Sets (rather than increments) progress — used for stateful counters.
    void notifyAbsolute(ObjectiveKind kind, i32 param, f32 value, const std::string& techId = {});
    void tick(f32 dtSeconds);
    /// Moves finished quests to Claimed and returns their rewards.
    std::vector<Quest> collectFinished();

    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);

private:
    std::vector<Quest> quests_;
    u32 nextId_ = 1;
    i32 completed_ = 0;
    i32 mainlineStage_ = 0;
};

} // namespace hv::gameplay
