#pragma once
// The authoritative game state. Everything the player can change goes through
// here, and everything here is serialized into a save.
#include "sim/Shelter.hpp"
#include "sim/Resident.hpp"
#include "sim/Events.hpp"
#include "gameplay/Research.hpp"
#include "gameplay/Surface.hpp"
#include "gameplay/Expedition.hpp"
#include "gameplay/Quests.hpp"
#include "core/Random.hpp"
#include <deque>
#include <string>
#include <vector>

namespace hv::sim {

/// One in-game day is this many real seconds at 1x speed.
constexpr f32 kSecondsPerGameDay = 1200.0f;
constexpr f32 kGameHoursPerSecond = 24.0f / kSecondsPerGameDay;

struct Statistics {
    f64 totalPlaySeconds = 0.0;
    i32 daysSurvived = 0;
    i32 residentsBorn = 0;
    i32 residentsRecruited = 0;
    i32 residentsLost = 0;
    i32 enemiesDefeated = 0;
    i32 expeditionsCompleted = 0;
    i32 expeditionsLost = 0;
    i32 roomsBuilt = 0;
    i32 roomsUpgraded = 0;
    i32 firesExtinguished = 0;
    i32 breakdownsRepaired = 0;
    i32 questsCompleted = 0;
    i32 techUnlocked = 0;
    std::array<f64, kResourceCount> produced{};
    std::array<f64, kResourceCount> consumed{};
};

/// Per-tick production/consumption figures, for the resource bar's trend arrows.
struct FlowRates {
    std::array<f32, kResourceCount> production{};
    std::array<f32, kResourceCount> consumption{};
    f32 net(Resource r) const {
        const int i = static_cast<int>(r);
        return production[i] - consumption[i];
    }
};

class World {
public:
    World();

    /// Fresh game: starter shelter, a handful of residents, a generated surface.
    void newGame(u64 seed);
    void reset();

    // ---- time --------------------------------------------------------------
    void tick(f32 realDt);
    f32  gameTimeSeconds() const { return gameTime_; }
    f32  gameTimeHours() const { return gameTime_ * kGameHoursPerSecond; }
    i32  day() const { return static_cast<i32>(gameTime_ / kSecondsPerGameDay) + 1; }
    f32  hourOfDay() const {
        return std::fmod(gameTimeHours(), 24.0f);
    }
    /// 0 at midnight, 1 at noon — drives the surface light and the shift clock.
    f32  daylight() const;
    bool isNightShift() const { const f32 h = hourOfDay(); return h < 6.0f || h >= 22.0f; }
    f32  speed() const { return speed_; }
    void setSpeed(f32 s) { speed_ = clampf(s, 0.0f, 8.0f); }
    bool paused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }

    // ---- subsystems --------------------------------------------------------
    Shelter& shelter() { return shelter_; }
    const Shelter& shelter() const { return shelter_; }
    ResourcePool& resources() { return resources_; }
    const ResourcePool& resources() const { return resources_; }
    gameplay::TechTree& tech() { return tech_; }
    const gameplay::TechTree& tech() const { return tech_; }
    gameplay::SurfaceMap& surface() { return surface_; }
    const gameplay::SurfaceMap& surface() const { return surface_; }
    gameplay::ExpeditionManager& expeditions() { return expeditions_; }
    const gameplay::ExpeditionManager& expeditions() const { return expeditions_; }
    gameplay::QuestLog& quests() { return quests_; }
    const gameplay::QuestLog& quests() const { return quests_; }
    EventSystem& events() { return events_; }
    const EventSystem& events() const { return events_; }
    Rng& rng() { return rng_; }
    const Statistics& stats() const { return stats_; }
    Statistics& statsMutable() { return stats_; }
    const FlowRates& flows() const { return flows_; }

    // ---- residents ---------------------------------------------------------
    std::vector<Resident>& residents() { return residents_; }
    const std::vector<Resident>& residents() const { return residents_; }
    Resident* resident(ResidentId id);
    const Resident* resident(ResidentId id) const;
    i32 population() const;
    i32 populationCapacity() const;
    f32 averageHappiness() const;
    f32 averageHealth() const;
    ResidentId addResident(Resident r);
    void removeResident(ResidentId id);
    /// Newcomers arrive at the airlock; capacity and morale gate the rate.
    ResidentId spawnNewcomer(i32 tier = 0);

    // ---- player actions ----------------------------------------------------
    /// Validates and, when valid, executes the placement. Inspect .ok().
    BuildPlan tryBuild(RoomType type, const Cell& origin, i32 width);
    /// Validation only — used for the build cursor preview.
    BuildPlan previewBuild(RoomType type, const Cell& origin, i32 width) const;
    bool upgradeRoom(RoomId id);
    bool demolishRoom(RoomId id);
    bool assignResident(ResidentId resident, RoomId room);
    bool unassignResident(ResidentId resident);
    /// Fills every empty job slot with the best unassigned candidate.
    i32  autoAssignAll();
    /// Moves a resident to the job they are best suited for.
    bool optimiseAssignment(ResidentId resident);
    /// Collect a room's stored output into the shared stockpile.
    f32  collectRoom(RoomId id);
    /// Gamble: instant output at the risk of starting a fire or a breakdown.
    bool rushRoom(RoomId id);
    bool repairRoom(RoomId id);
    bool equipItem(ResidentId resident, const ItemStack& item);
    bool unequip(ResidentId resident, ItemKind kind);
    bool craftItem(u16 itemId);
    bool useConsumable(ResidentId resident, u16 itemId);
    /// Trains a skill by putting the resident in a training hall.
    bool setTrainingSkill(ResidentId resident, Skill s);
    Skill trainingSkill(ResidentId resident) const;
    u32  launchExpedition(u32 siteId, const std::vector<ResidentId>& squad);
    void recallExpedition(u32 id);
    bool acceptTrade(bool buying);

    // ---- storage inventory --------------------------------------------------
    std::vector<ItemStack>& storage() { return storage_; }
    const std::vector<ItemStack>& storage() const { return storage_; }
    void addToStorage(const ItemStack& s);
    bool takeFromStorage(u16 defId, ItemStack& out);

    // ---- notifications -----------------------------------------------------
    void notify(const std::string& text, NotifySeverity sev = NotifySeverity::Info,
                RoomId room = kNoRoom);
    const std::deque<Notification>& notifications() const { return notifications_; }
    std::deque<Notification>& notificationsMutable() { return notifications_; }

    // ---- save/load ---------------------------------------------------------
    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);

    /// Recomputes derived state after a load (indices, capacities, worker links).
    void rebuildDerived();

    u64 seed() const { return seed_; }
    const std::string& shelterName() const { return shelterName_; }
    void setShelterName(std::string n) { shelterName_ = std::move(n); }

private:
    void tickProduction(f32 dt);
    void tickNeeds(f32 dt);
    void tickMorale(f32 dt);
    void tickPopulation(f32 dt);
    void tickQuests(f32 dt);
    void applyQuestRewards();
    void syncQuestCounters();
    f32  powerBalance() const;

    Rng rng_;
    u64 seed_ = 1;
    std::string shelterName_ = "Haven";

    Shelter shelter_;
    ResourcePool resources_;
    std::vector<Resident> residents_;
    std::vector<ItemStack> storage_;
    std::vector<std::pair<ResidentId, Skill>> trainingChoices_;

    gameplay::TechTree tech_;
    gameplay::SurfaceMap surface_;
    gameplay::ExpeditionManager expeditions_;
    gameplay::QuestLog quests_;
    EventSystem events_;

    Statistics stats_;
    FlowRates flows_;
    std::deque<Notification> notifications_;

    f32 gameTime_ = 0.0f;
    f32 speed_ = 1.0f;
    bool paused_ = false;
    ResidentId nextResidentId_ = 1;
    f32 newcomerTimer_ = 0.0f;
    f32 birthTimer_ = 0.0f;
    i32 lastDay_ = 1;
    f32 flowWindow_ = 0.0f;
    bool warnedBrownout_ = false;
    f32 starvationWarnTimer_ = 0.0f;
    std::array<f32, kResourceCount> flowProducedAcc_{};
    std::array<f32, kResourceCount> flowConsumedAcc_{};
};

} // namespace hv::sim
