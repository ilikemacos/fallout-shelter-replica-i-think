#include "gameplay/Quests.hpp"
#include "sim/Names.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace hv::gameplay {
namespace {

std::string fmt(const char* f, ...) {
    va_list a;
    va_start(a, f);
    char buf[512];
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return std::string(buf);
}

} // namespace

std::string Objective::describe() const {
    switch (kind) {
        case ObjectiveKind::BuildRoom:
            return fmt("Build %d x %s (%d/%d)", static_cast<int>(target),
                       roomTypeName(static_cast<RoomType>(param)),
                       static_cast<int>(progress), static_cast<int>(target));
        case ObjectiveKind::UpgradeRoom:
            return fmt("Upgrade a %s to level %d", roomTypeName(static_cast<RoomType>(param)),
                       static_cast<int>(target));
        case ObjectiveKind::ReachPopulation:
            return fmt("Reach %d residents (%d)", static_cast<int>(target), static_cast<int>(progress));
        case ObjectiveKind::StockResource:
            return fmt("Hold %d %s (%d)", static_cast<int>(target),
                       resourceName(static_cast<Resource>(param)), static_cast<int>(progress));
        case ObjectiveKind::CompleteExpeditions:
            return fmt("Complete %d expeditions (%d)", static_cast<int>(target), static_cast<int>(progress));
        case ObjectiveKind::DefeatEnemies:
            return fmt("Defeat %d hostiles (%d)", static_cast<int>(target), static_cast<int>(progress));
        case ObjectiveKind::UnlockTech:
            return "Research: " + techId;
        case ObjectiveKind::SurviveDays:
            return fmt("Survive %d days (%d)", static_cast<int>(target), static_cast<int>(progress));
        case ObjectiveKind::TrainSkill:
            return fmt("Train %s to %d in any resident", skillName(static_cast<Skill>(param)),
                       static_cast<int>(target));
        case ObjectiveKind::ExploreSites:
            return fmt("Discover %d surface sites (%d)", static_cast<int>(target), static_cast<int>(progress));
        default: return "?";
    }
}

std::string QuestReward::describe() const {
    std::string s;
    for (int i = 0; i < kResourceCount; ++i) {
        if (resources[i] <= 0.0f) continue;
        if (!s.empty()) s += ", ";
        s += fmt("%d %s", static_cast<int>(resources[i]), resourceName(static_cast<Resource>(i)));
    }
    if (itemId != 0) {
        if (!s.empty()) s += ", ";
        s += itemDef(itemId).name;
    }
    if (experience > 0.0f) {
        if (!s.empty()) s += ", ";
        s += fmt("%d XP each", static_cast<int>(experience));
    }
    if (!unlockTech.empty()) {
        if (!s.empty()) s += ", ";
        s += "research breakthrough";
    }
    return s.empty() ? "No reward" : s;
}

void QuestLog::reset() {
    quests_.clear();
    nextId_ = 1;
    completed_ = 0;
    mainlineStage_ = 0;
}

void QuestLog::add(Quest q) {
    q.id = nextId_++;
    quests_.push_back(std::move(q));
}

void QuestLog::seedMainline() {
    Quest q;
    q.mainline = true;
    q.title = "Cold Start";
    q.summary = "The airlock holds and the lights are on. Get power, water and food "
                "running before anyone notices how thin the margins are.";
    q.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::Generator), 1, 0, {}});
    q.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::WaterPlant), 1, 0, {}});
    q.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::Hydroponics), 1, 0, {}});
    q.reward.resources[static_cast<int>(Resource::Materials)] = 120;
    q.reward.resources[static_cast<int>(Resource::Scrip)] = 80;
    add(std::move(q));

    Quest q2;
    q2.mainline = true;
    q2.title = "Somewhere To Sleep";
    q2.summary = "Bunks and a mess hall. People work better when they are not "
                 "sleeping on the floor of a generator room.";
    q2.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::Dormitory), 1, 0, {}});
    q2.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::Cafeteria), 1, 0, {}});
    q2.objectives.push_back(Objective{ObjectiveKind::ReachPopulation, 0, 10, 0, {}});
    q2.reward.resources[static_cast<int>(Resource::Materials)] = 160;
    q2.reward.resources[static_cast<int>(Resource::Research)] = 40;
    add(std::move(q2));

    Quest q3;
    q3.mainline = true;
    q3.title = "Eyes Above";
    q3.summary = "Send crews up the shaft. Whatever is left out there is not "
                 "coming down here on its own.";
    q3.objectives.push_back(Objective{ObjectiveKind::CompleteExpeditions, 0, 3, 0, {}});
    q3.objectives.push_back(Objective{ObjectiveKind::ExploreSites, 0, 6, 0, {}});
    q3.reward.resources[static_cast<int>(Resource::Scrip)] = 150;
    q3.reward.itemId = 4;   // Service Revolver
    add(std::move(q3));

    Quest q4;
    q4.mainline = true;
    q4.title = "Hold The Door";
    q4.summary = "Something found the mast. Put trained people and real weapons "
                 "between the airlock and everyone else.";
    q4.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::Security), 1, 0, {}});
    q4.objectives.push_back(Objective{ObjectiveKind::DefeatEnemies, 0, 8, 0, {}});
    q4.objectives.push_back(Objective{ObjectiveKind::TrainSkill, static_cast<i32>(Skill::Security), 7, 0, {}});
    q4.reward.resources[static_cast<int>(Resource::Materials)] = 220;
    q4.reward.itemId = 25;  // Guard Plate
    add(std::move(q4));

    Quest q5;
    q5.mainline = true;
    q5.title = "Deep Work";
    q5.summary = "The lower strata are dry and stable. Dig, and put science in "
                 "the hole you make.";
    q5.objectives.push_back(Objective{ObjectiveKind::BuildRoom, static_cast<i32>(RoomType::Research), 1, 0, {}});
    q5.objectives.push_back(Objective{ObjectiveKind::UnlockTech, 0, 1, 0, "shelter_operations"});
    q5.objectives.push_back(Objective{ObjectiveKind::ReachPopulation, 0, 24, 0, {}});
    q5.reward.resources[static_cast<int>(Resource::Research)] = 120;
    q5.reward.resources[static_cast<int>(Resource::Scrip)] = 250;
    add(std::move(q5));

    Quest q6;
    q6.mainline = true;
    q6.title = "The Long Winter";
    q6.summary = "Self-sufficiency, on paper and in the tanks. Prove the shelter "
                 "can carry itself for a season.";
    q6.objectives.push_back(Objective{ObjectiveKind::StockResource, static_cast<i32>(Resource::Food), 600, 0, {}});
    q6.objectives.push_back(Objective{ObjectiveKind::StockResource, static_cast<i32>(Resource::Water), 600, 0, {}});
    q6.objectives.push_back(Objective{ObjectiveKind::SurviveDays, 0, 30, 0, {}});
    q6.objectives.push_back(Objective{ObjectiveKind::UnlockTech, 0, 1, 0, "arcology_systems"});
    q6.reward.resources[static_cast<int>(Resource::Scrip)] = 800;
    q6.reward.itemId = 28;  // Deepwalker Shell
    add(std::move(q6));
}

u32 QuestLog::generateSideQuest(Rng& rng, i32 population, i32 deepestFloor, i32 discoveredSites) {
    (void)deepestFloor;
    Quest q;
    q.title = randomFactionName(rng) + " Contract";
    const int roll = rng.rangeI(0, 4);
    switch (roll) {
        case 0: {
            const Resource res = static_cast<Resource>(rng.rangeI(1, 4));
            const f32 amount = std::round(rng.range(80.0f, 220.0f) * (1.0f + 0.02f * static_cast<f32>(population)));
            q.summary = fmt("A trading party will pay well for %d %s delivered to the airlock.",
                            static_cast<int>(amount), resourceName(res));
            q.objectives.push_back(Objective{ObjectiveKind::StockResource, static_cast<i32>(res), amount, 0, {}});
            q.reward.resources[static_cast<int>(Resource::Scrip)] = amount * 0.9f;
            break;
        }
        case 1: {
            const f32 n = static_cast<f32>(rng.rangeI(4, 10));
            q.summary = "Clear the approaches. The surface crews want the road quiet.";
            q.objectives.push_back(Objective{ObjectiveKind::DefeatEnemies, 0, n, 0, {}});
            q.reward.resources[static_cast<int>(Resource::Scrip)] = n * 22.0f;
            q.reward.resources[static_cast<int>(Resource::Materials)] = n * 12.0f;
            break;
        }
        case 2: {
            const f32 n = static_cast<f32>(rng.rangeI(2, 5));
            q.summary = "Map what is left of the district. Bring back anything with a serial number.";
            q.objectives.push_back(Objective{ObjectiveKind::CompleteExpeditions, 0, n, 0, {}});
            q.reward.resources[static_cast<int>(Resource::Research)] = n * 25.0f;
            q.reward.experience = 40.0f;
            break;
        }
        case 3: {
            const Skill s = static_cast<Skill>(rng.rangeI(0, kSkillCount - 1));
            const f32 lvl = static_cast<f32>(std::min(10, 5 + population / 12));
            q.summary = fmt("Someone competent in %s is wanted for a joint job.", skillName(s));
            q.objectives.push_back(Objective{ObjectiveKind::TrainSkill, static_cast<i32>(s), lvl, 0, {}});
            q.reward.resources[static_cast<int>(Resource::Scrip)] = 180.0f;
            q.reward.itemId = 41;   // Field Toolkit
            break;
        }
        default: {
            const f32 n = static_cast<f32>(std::min(28, discoveredSites + rng.rangeI(2, 5)));
            q.summary = "Extend the survey. There are sites nobody has walked into yet.";
            q.objectives.push_back(Objective{ObjectiveKind::ExploreSites, 0, n, 0, {}});
            q.reward.resources[static_cast<int>(Resource::Scrip)] = 120.0f;
            q.reward.resources[static_cast<int>(Resource::Research)] = 60.0f;
            break;
        }
    }
    const u32 id = nextId_;
    add(std::move(q));
    return id;
}

Quest* QuestLog::find(u32 id) {
    for (Quest& q : quests_) if (q.id == id) return &q;
    return nullptr;
}

i32 QuestLog::activeCount() const {
    i32 n = 0;
    for (const Quest& q : quests_)
        if (q.state == QuestState::Active || q.state == QuestState::Complete) ++n;
    return n;
}

void QuestLog::notify(ObjectiveKind kind, i32 param, f32 amount, const std::string& techId) {
    for (Quest& q : quests_) {
        if (q.state != QuestState::Active) continue;
        for (Objective& o : q.objectives) {
            if (o.kind != kind) continue;
            if (kind == ObjectiveKind::UnlockTech) {
                if (o.techId != techId) continue;
                o.progress = o.target;
            } else {
                if (o.param != param && kind != ObjectiveKind::ReachPopulation &&
                    kind != ObjectiveKind::CompleteExpeditions &&
                    kind != ObjectiveKind::DefeatEnemies &&
                    kind != ObjectiveKind::SurviveDays &&
                    kind != ObjectiveKind::ExploreSites) continue;
                o.progress += amount;
            }
        }
        if (q.allObjectivesComplete()) q.state = QuestState::Complete;
    }
}

void QuestLog::notifyAbsolute(ObjectiveKind kind, i32 param, f32 value, const std::string& techId) {
    for (Quest& q : quests_) {
        if (q.state != QuestState::Active) continue;
        for (Objective& o : q.objectives) {
            if (o.kind != kind) continue;
            if (kind == ObjectiveKind::UnlockTech && o.techId != techId) continue;
            if (kind == ObjectiveKind::BuildRoom || kind == ObjectiveKind::UpgradeRoom ||
                kind == ObjectiveKind::StockResource || kind == ObjectiveKind::TrainSkill) {
                if (o.param != param) continue;
            }
            // Absolute counters only ever move forward for the player's benefit
            // on progress-style objectives, but stock objectives must be able
            // to fall back when the stockpile is spent.
            if (kind == ObjectiveKind::StockResource) o.progress = value;
            else o.progress = std::max(o.progress, value);
        }
        if (q.allObjectivesComplete()) q.state = QuestState::Complete;
        else if (q.state == QuestState::Complete) q.state = QuestState::Active;
    }
}

void QuestLog::tick(f32 dtSeconds) {
    for (Quest& q : quests_) {
        if (q.state != QuestState::Active) continue;
        q.elapsed += dtSeconds;
        if (q.timeLimitSeconds > 0.0f && q.elapsed > q.timeLimitSeconds)
            q.state = QuestState::Failed;
    }
}

std::vector<Quest> QuestLog::collectFinished() {
    std::vector<Quest> out;
    for (Quest& q : quests_) {
        if (q.state != QuestState::Complete) continue;
        q.state = QuestState::Claimed;
        ++completed_;
        out.push_back(q);
    }
    return out;
}

void QuestLog::serialize(BlobWriter& w) const {
    w.u32v(static_cast<u32>(quests_.size()));
    for (const Quest& q : quests_) {
        w.u32v(q.id);
        w.str(q.title);
        w.str(q.summary);
        w.u8v(static_cast<u8>(q.state));
        w.u32v(q.siteId);
        w.f32v(q.timeLimitSeconds);
        w.f32v(q.elapsed);
        w.boolv(q.mainline);
        w.u32v(static_cast<u32>(q.objectives.size()));
        for (const Objective& o : q.objectives) {
            w.u8v(static_cast<u8>(o.kind));
            w.i32v(o.param);
            w.f32v(o.target);
            w.f32v(o.progress);
            w.str(o.techId);
        }
        for (int i = 0; i < kResourceCount; ++i) w.f32v(q.reward.resources[i]);
        w.u16v(q.reward.itemId);
        w.f32v(q.reward.experience);
        w.str(q.reward.unlockTech);
    }
    w.u32v(nextId_);
    w.i32v(completed_);
    w.i32v(mainlineStage_);
}

bool QuestLog::deserialize(BlobReader& r, u32 version) {
    (void)version;
    quests_.clear();
    const u32 n = r.u32v();
    if (r.failed() || n > 512) return false;
    for (u32 i = 0; i < n; ++i) {
        Quest q;
        q.id = r.u32v();
        q.title = r.str();
        q.summary = r.str();
        const u8 st = r.u8v();
        q.state = st <= static_cast<u8>(QuestState::Failed) ? static_cast<QuestState>(st)
                                                            : QuestState::Active;
        q.siteId = r.u32v();
        q.timeLimitSeconds = r.f32v();
        q.elapsed = r.f32v();
        q.mainline = r.boolv();
        const u32 objectives = r.u32v();
        if (r.failed() || objectives > 32) return false;
        for (u32 k = 0; k < objectives; ++k) {
            Objective o;
            const u8 kind = r.u8v();
            o.kind = kind < static_cast<u8>(ObjectiveKind::Count)
                         ? static_cast<ObjectiveKind>(kind) : ObjectiveKind::BuildRoom;
            o.param = r.i32v();
            o.target = r.f32v();
            o.progress = r.f32v();
            o.techId = r.str();
            q.objectives.push_back(std::move(o));
        }
        for (int k = 0; k < kResourceCount; ++k) q.reward.resources[k] = r.f32v();
        q.reward.itemId = r.u16v();
        q.reward.experience = r.f32v();
        q.reward.unlockTech = r.str();
        if (r.failed()) return false;
        quests_.push_back(std::move(q));
    }
    nextId_ = std::max(1u, r.u32v());
    completed_ = r.i32v();
    mainlineStage_ = r.i32v();
    return !r.failed();
}

} // namespace hv::gameplay
