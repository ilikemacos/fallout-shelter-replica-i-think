// The simulation step: production, needs, morale, population, progression.
#include "sim/World.hpp"
#include "core/Log.hpp"
#include <algorithm>

namespace hv::sim {
namespace {

/// Rooms are tuned in "units per minute"; the tick works in seconds.
constexpr f32 kPerMinute = 1.0f / 60.0f;
/// Share of a room's output that flows straight into the stockpile. The rest
/// accumulates as a collectible bonus, so attentive players get more.
constexpr f32 kAutoDepositShare = 0.70f;
/// Per resident, per in-game hour.
constexpr f32 kFoodPerHour  = 0.55f;
constexpr f32 kWaterPerHour = 0.65f;
constexpr f32 kBuildSecondsPerCell = 14.0f;

f32 techMultiplierFor(Resource r, const gameplay::TechBonus& b) {
    switch (r) {
        case Resource::Power:     return 1.0f + b.powerOutput;
        case Resource::Water:     return 1.0f + b.waterOutput;
        case Resource::Food:      return 1.0f + b.foodOutput;
        case Resource::Materials: return 1.0f + b.materialOutput;
        case Resource::Medicine:  return 1.0f + b.medicineOutput;
        case Resource::Research:  return 1.0f + b.researchOutput;
        default: return 1.0f;
    }
}

} // namespace

f32 World::daylight() const {
    // Peaks at 13:00, zero between roughly 20:00 and 05:00.
    const f32 h = hourOfDay();
    const f32 t = std::cos((h - 13.0f) / 24.0f * kTwoPi);
    return saturate((t - 0.15f) / 0.85f);
}

f32 World::powerBalance() const {
    f32 supply = 0.0f, demand = 0.0f;
    for (const Room& r : shelter_.rooms()) {
        if (r.buildProgress < 1.0f) continue;
        if (r.def().produces == Resource::Power && r.def().function == RoomFunction::Produce) {
            if (r.operational()) supply += r.baseProduction();
        }
        demand += r.powerDraw();
    }
    return demand > 0.0f ? supply / demand : 1.0f;
}

void World::tick(f32 realDt) {
    if (paused_ || realDt <= 0.0f) return;
    const f32 dt = realDt * speed_;
    gameTime_ += dt;
    stats_.totalPlaySeconds += realDt;

    const i32 today = day();
    if (today != lastDay_) {
        lastDay_ = today;
        stats_.daysSurvived = today - 1;
        quests_.notifyAbsolute(gameplay::ObjectiveKind::SurviveDays, 0,
                               static_cast<f32>(stats_.daysSurvived));
        // Everyone ages, slowly. 1 in-game year per 60 days keeps it gentle.
        for (Resident& r : residents_) r.age += 1.0f / 60.0f;
    }

    tickProduction(dt);
    tickNeeds(dt);
    tickMorale(dt);
    tickPopulation(dt);

    surface_.tick(dt);
    expeditions_.tick(dt, rng_, *this);
    events_.tick(dt, *this);

    const std::string finishedTech = tech_.tick(dt, 1.0f);
    if (!finishedTech.empty()) {
        ++stats_.techUnlocked;
        const gameplay::TechNode* n = tech_.find(finishedTech);
        notify("Research complete: " + (n ? n->name : finishedTech), NotifySeverity::Good);
        quests_.notify(gameplay::ObjectiveKind::UnlockTech, 0, 1.0f, finishedTech);
        shelter_.applyStorageCapacity(resources_);
    }

    tickQuests(dt);

    for (Notification& n : notifications_) n.age += realDt;

    // Rolling production/consumption window for the resource bar.
    flowWindow_ += dt;
    if (flowWindow_ >= 1.0f) {
        for (int i = 0; i < kResourceCount; ++i) {
            flows_.production[i] = flowProducedAcc_[i] * 60.0f / flowWindow_;
            flows_.consumption[i] = flowConsumedAcc_[i] * 60.0f / flowWindow_;
            flowProducedAcc_[i] = flowConsumedAcc_[i] = 0.0f;
        }
        flowWindow_ = 0.0f;
    }
}

void World::tickProduction(f32 dt) {
    const gameplay::TechBonus& bonus = tech_.bonuses();

    // --- construction progress ---------------------------------------------
    for (Room& r : shelter_.rooms()) {
        if (r.buildProgress >= 1.0f) continue;
        const f32 total = kBuildSecondsPerCell * static_cast<f32>(r.width);
        r.buildProgress = saturate(r.buildProgress + dt / total);
        if (r.buildProgress >= 1.0f) {
            shelter_.applyStorageCapacity(resources_);
            notify(std::string(roomTypeName(r.type)) + " is finished.",
                   NotifySeverity::Good, r.id);
        }
    }

    // --- power supply -------------------------------------------------------
    f32 supply = 0.0f;
    for (Room& r : shelter_.rooms()) {
        const RoomDef& d = r.def();
        if (d.function != RoomFunction::Produce || d.produces != Resource::Power) continue;
        if (!r.operational()) continue;
        f32 staff = 0.0f;
        for (ResidentId w : r.workers)
            if (const Resident* res = resident(w))
                if (res->available() && res->currentRoom == r.id) staff += res->workEfficiency(d.primarySkill);
        if (r.workers.empty()) staff = 0.0f;
        const f32 crewFactor = r.workerSlots() > 0
            ? saturate(staff / static_cast<f32>(r.workerSlots())) : 0.0f;
        supply += r.baseProduction() * crewFactor * r.condition *
                  techMultiplierFor(Resource::Power, bonus);
    }
    const f32 supplyThisTick = supply * kPerMinute * dt;
    const f32 stored = resources_.add(Resource::Power, supplyThisTick);
    flowProducedAcc_[static_cast<int>(Resource::Power)] += stored;
    stats_.produced[static_cast<int>(Resource::Power)] += stored;

    // --- power demand -------------------------------------------------------
    f32 demand = 0.0f;
    for (const Room& r : shelter_.rooms())
        if (r.buildProgress >= 1.0f) demand += r.powerDraw();
    const f32 demandThisTick = demand * kPerMinute * dt;
    f32 satisfaction = 1.0f;
    if (demandThisTick > 0.0f) {
        const f32 available = resources_.get(Resource::Power);
        const f32 taken = std::min(available, demandThisTick);
        resources_.spend(Resource::Power, taken);
        satisfaction = saturate(taken / demandThisTick);
        flowConsumedAcc_[static_cast<int>(Resource::Power)] += taken;
        stats_.consumed[static_cast<int>(Resource::Power)] += taken;
    }
    if (satisfaction < 0.6f && !warnedBrownout_) {
        notify("Brown-out: the shelter is drawing more power than it makes.",
               NotifySeverity::Warning);
        warnedBrownout_ = true;
    } else if (satisfaction > 0.9f) {
        warnedBrownout_ = false;
    }

    // --- everything else ----------------------------------------------------
    for (Room& r : shelter_.rooms()) {
        if (r.buildProgress < 1.0f) continue;
        const RoomDef& d = r.def();
        r.powerSatisfaction = d.powerDraw > 0.0f ? satisfaction : 1.0f;
        r.animPhase += dt * (0.6f + 0.9f * r.powerSatisfaction);
        if (r.rushCooldown > 0.0f) r.rushCooldown = std::max(0.0f, r.rushCooldown - dt);

        if (!r.operational()) continue;

        // Crew efficiency, averaged over the slots rather than the heads, so a
        // half-staffed room really does run at half output.
        f32 staff = 0.0f;
        i32 present = 0;
        for (ResidentId w : r.workers) {
            Resident* res = resident(w);
            if (!res || !res->available()) continue;
            if (res->currentRoom != r.id) continue;
            staff += res->workEfficiency(d.primarySkill);
            ++present;
        }
        const f32 crewFactor = r.workerSlots() > 0
            ? saturate(staff / static_cast<f32>(r.workerSlots())) : 0.0f;

        // Machines wear out under load, faster when short of power.
        if (d.powerDraw > 0.0f || d.function == RoomFunction::Produce) {
            const f32 wear = dt * (0.000045f + 0.00006f * (1.0f - r.powerSatisfaction)) *
                             (1.0f + crewFactor);
            r.condition = std::max(0.0f, r.condition - wear *
                                   (1.0f - saturate(bonus.breakdownResistance)));
            if (r.condition < 0.12f && !r.broken && rng_.chance(dt * 0.02f)) {
                events_.trigger(EventKind::Breakdown, *this, r.id, 1.0f);
            }
        }

        if (d.function == RoomFunction::Produce || d.function == RoomFunction::Craft ||
            d.function == RoomFunction::Research || d.function == RoomFunction::Trade) {
            if (d.produces == Resource::Power) continue;   // already handled
            const f32 out = r.baseProduction() * crewFactor * r.condition *
                            r.powerSatisfaction * techMultiplierFor(d.produces, bonus) *
                            kPerMinute * dt;
            if (out > 0.0f) {
                const f32 direct = out * kAutoDepositShare;
                const f32 added = resources_.add(d.produces, direct);
                flowProducedAcc_[static_cast<int>(d.produces)] += added;
                stats_.produced[static_cast<int>(d.produces)] += added;
                // The collectible share is capped so it cannot bank forever.
                const f32 cap = r.baseProduction() * 2.0f;
                r.storedOutput = std::min(cap, r.storedOutput + out * (1.0f - kAutoDepositShare));
            }
            for (ResidentId w : r.workers)
                if (Resident* res = resident(w))
                    if (res->currentRoom == r.id) {
                        res->grantExperience(dt * 0.35f);
                        res->totalWorkedHours += dt * kGameHoursPerSecond;
                    }
        }

        if (d.function == RoomFunction::Train && present > 0) {
            const f32 rate = dt * (1.0f + bonus.trainingSpeed);
            for (ResidentId w : r.workers) {
                Resident* res = resident(w);
                if (!res || res->currentRoom != r.id) continue;
                const Skill s = trainingSkill(w);
                const u8 cur = res->skills.get(s);
                if (cur >= 10) continue;
                // Higher levels take progressively longer to earn.
                const f32 need = 90.0f + 55.0f * static_cast<f32>(cur);
                res->experience += rate * 0.6f;
                if (rng_.chance(rate / need)) {
                    res->skills.set(s, static_cast<u8>(cur + 1));
                    notify(res->name + " reached " + skillName(s) + " " +
                           std::to_string(cur + 1) + ".", NotifySeverity::Good, r.id);
                    quests_.notifyAbsolute(gameplay::ObjectiveKind::TrainSkill,
                                           static_cast<i32>(s), static_cast<f32>(cur + 1));
                }
            }
        }

        if (d.function == RoomFunction::Heal) {
            for (Resident& res : residents_) {
                if (res.currentRoom != r.id || !res.alive()) continue;
                if (res.health >= res.effectiveMaxHealth() && res.radiation <= 0.0f) continue;
                const f32 medicineWanted = dt * 0.02f;
                if (resources_.spend(Resource::Medicine, medicineWanted)) {
                    flowConsumedAcc_[static_cast<int>(Resource::Medicine)] += medicineWanted;
                    res.health = std::min(res.effectiveMaxHealth(),
                                          res.health + dt * 1.6f * (1.0f + 0.1f * static_cast<f32>(r.level)));
                    res.radiation = std::max(0.0f, res.radiation - dt * 0.35f);
                }
            }
        }
    }
}

void World::tickNeeds(f32 dt) {
    const f32 hours = dt * kGameHoursPerSecond;
    const gameplay::TechBonus& bonus = tech_.bonuses();
    (void)bonus;

    f32 foodWanted = 0.0f, waterWanted = 0.0f;
    for (const Resident& r : residents_) {
        if (!r.alive() || r.expeditionId != 0) continue;
        const f32 k = r.hasTrait(Trait_IronStomach) ? 0.75f : 1.0f;
        foodWanted += kFoodPerHour * hours * k;
        waterWanted += kWaterPerHour * hours * k;
    }
    const f32 foodShort = resources_.drain(Resource::Food, foodWanted);
    const f32 waterShort = resources_.drain(Resource::Water, waterWanted);
    flowConsumedAcc_[static_cast<int>(Resource::Food)] += foodWanted - foodShort;
    flowConsumedAcc_[static_cast<int>(Resource::Water)] += waterWanted - waterShort;
    stats_.consumed[static_cast<int>(Resource::Food)] += foodWanted - foodShort;
    stats_.consumed[static_cast<int>(Resource::Water)] += waterWanted - waterShort;

    const f32 foodCoverage = foodWanted > 0.0f ? 1.0f - foodShort / foodWanted : 1.0f;
    const f32 waterCoverage = waterWanted > 0.0f ? 1.0f - waterShort / waterWanted : 1.0f;

    starvationWarnTimer_ = std::max(0.0f, starvationWarnTimer_ - dt);
    if ((foodCoverage < 0.5f || waterCoverage < 0.5f) && starvationWarnTimer_ <= 0.0f) {
        notify(foodCoverage < waterCoverage ? "Food stores are empty. People are going hungry."
                                            : "The tanks are dry. People are going thirsty.",
               NotifySeverity::Critical);
        starvationWarnTimer_ = 60.0f;
    }

    for (Resident& r : residents_) {
        if (!r.alive()) continue;
        if (r.expeditionId != 0) continue;

        r.hunger = clampf(r.hunger + hours * 6.0f * (1.0f - foodCoverage) - hours * 8.0f * foodCoverage,
                          0.0f, 100.0f);
        r.thirst = clampf(r.thirst + hours * 7.0f * (1.0f - waterCoverage) - hours * 9.0f * waterCoverage,
                          0.0f, 100.0f);

        // Energy: drains while active, recovers while asleep.
        if (r.activity == Activity::Sleeping) {
            const f32 rate = r.hasTrait(Trait_Insomniac) ? 7.0f : 12.0f;
            r.energy = clampf(r.energy + hours * rate, 0.0f, 100.0f);
        } else {
            const f32 rate = r.hasTrait(Trait_NightOwl) ? 3.0f : 4.0f;
            r.energy = clampf(r.energy - hours * rate, 0.0f, 100.0f);
        }

        // Starvation, thirst and radiation eat health; otherwise it recovers.
        f32 healthDelta = 0.0f;
        if (r.hunger > 80.0f) healthDelta -= hours * 3.0f;
        if (r.thirst > 80.0f) healthDelta -= hours * 4.0f;
        if (r.radiation > 60.0f) healthDelta -= hours * 1.5f;
        if (healthDelta == 0.0f && r.activity != Activity::Fighting)
            healthDelta += hours * 0.8f;
        r.health = clampf(r.health + healthDelta, 0.0f, r.effectiveMaxHealth());

        if (r.health <= 0.0f) {
            r.health = 0.0f;
            r.activity = Activity::Dead;
            unassignResident(r.id);
            ++stats_.residentsLost;
            notify(r.name + " has died.", NotifySeverity::Critical);
        }
    }

    // Remove the long dead so the population lists stay clean.
    residents_.erase(std::remove_if(residents_.begin(), residents_.end(),
        [](const Resident& r) { return r.activity == Activity::Dead && r.activityTimer > 120.0f; }),
        residents_.end());
    for (Resident& r : residents_) if (!r.alive()) r.activityTimer += dt;
}

void World::tickMorale(f32 dt) {
    const gameplay::TechBonus& bonus = tech_.bonuses();
    const f32 foodOk = resources_.fraction(Resource::Food) > 0.05f ? 1.0f : 0.0f;
    const f32 waterOk = resources_.fraction(Resource::Water) > 0.05f ? 1.0f : 0.0f;
    const bool underAttack = !events_.emergencies().empty();

    // Room auras: a resident standing in a rec room or mess hall feels better.
    for (Resident& r : residents_) {
        if (!r.alive()) continue;
        f32 target = 50.0f + bonus.moraleFloor;
        target += 12.0f * foodOk + 12.0f * waterOk;
        target -= saturate(r.hunger / 100.0f) * 30.0f;
        target -= saturate(r.thirst / 100.0f) * 30.0f;
        target -= (1.0f - r.healthFraction()) * 25.0f;
        target -= saturate((30.0f - r.energy) / 30.0f) * 12.0f;

        if (const Room* room = shelter_.room(r.currentRoom)) {
            target += room->def().moraleAura * (1.0f + 0.25f * static_cast<f32>(room->level - 1));
            if (r.hasTrait(Trait_Claustrophobe)) target -= static_cast<f32>(room->floor) * 1.6f;
        }
        if (r.assignedRoom != kNoRoom) {
            const Room* job = shelter_.room(r.assignedRoom);
            if (job) {
                // Working a post that suits you is worth real morale.
                const i32 skill = r.effectiveSkill(job->def().primarySkill);
                target += static_cast<f32>(skill - 5) * 1.4f;
            }
        } else {
            target -= 6.0f;   // idleness grinds people down
        }
        if (underAttack && !r.hasTrait(Trait_Brave)) target -= 12.0f;
        if (r.personality == Personality::Anxious) target -= 5.0f;
        if (r.personality == Personality::Gregarious && r.currentRoom != kNoRoom) {
            i32 company = 0;
            for (const Resident& o : residents_)
                if (o.id != r.id && o.currentRoom == r.currentRoom && o.alive()) ++company;
            target += std::min(8.0f, static_cast<f32>(company) * 2.5f);
        }
        // Friendships lift, feuds drag.
        f32 social = 0.0f;
        for (const Relationship& rel : r.relationships) social += rel.affinity * 0.05f;
        target += clampf(social, -10.0f, 12.0f);

        target = clampf(target, 0.0f, 100.0f);
        r.happiness = damp(r.happiness, target, 0.12f, dt);
    }
}

void World::tickPopulation(f32 dt) {
    const i32 pop = population();
    const i32 cap = populationCapacity();
    quests_.notifyAbsolute(gameplay::ObjectiveKind::ReachPopulation, 0, static_cast<f32>(pop));

    // --- newcomers ----------------------------------------------------------
    newcomerTimer_ -= dt;
    if (newcomerTimer_ <= 0.0f) {
        f32 interval = 420.0f;
        const i32 signals = shelter_.countOfType(RoomType::Communications);
        interval /= (1.0f + 0.8f * static_cast<f32>(signals));
        interval /= clampf(averageHappiness() / 60.0f, 0.4f, 1.8f);
        newcomerTimer_ = interval * rng_.range(0.7f, 1.4f);
        if (pop < cap && rng_.chance(0.75f)) {
            events_.trigger(EventKind::Newcomer, *this);
        }
    }

    // --- births -------------------------------------------------------------
    birthTimer_ -= dt;
    if (birthTimer_ <= 0.0f) {
        birthTimer_ = 600.0f * rng_.range(0.8f, 1.5f);
        if (pop < cap && pop >= 4 && averageHappiness() > 65.0f) {
            // Find the strongest mutual bond among adults sharing a dormitory.
            Resident* a = nullptr;
            Resident* b = nullptr;
            f32 best = 35.0f;
            for (Resident& x : residents_) {
                if (!x.available() || x.age < 18.0f || x.age > 45.0f) continue;
                for (Resident& y : residents_) {
                    if (y.id <= x.id || !y.available() || y.age < 18.0f || y.age > 45.0f) continue;
                    if (x.female == y.female) continue;
                    const f32 bond = std::min(x.affinityWith(y.id), y.affinityWith(x.id));
                    if (bond > best) { best = bond; a = &x; b = &y; }
                }
            }
            if (a && b) {
                Resident child = makeChild(rng_, nextResidentId_, *a, *b);
                const std::vector<RoomId> dorms = shelter_.roomsOfType(RoomType::Dormitory);
                if (!dorms.empty()) {
                    const Room* d = shelter_.room(dorms.front());
                    child.position = d->worldCenter();
                    child.cell = Cell{d->floor, d->colStart};
                    child.currentRoom = d->id;
                }
                const std::string name = child.name;
                addResident(std::move(child));
                ++stats_.residentsBorn;
                notify(name + " has joined the shelter — born to " + a->name + " and " +
                       b->name + ".", NotifySeverity::Good);
            }
        }
    }
}

void World::tickQuests(f32 dt) {
    quests_.tick(dt);
    syncQuestCounters();
    applyQuestRewards();

    // Keep a couple of side contracts available at all times.
    if (quests_.activeCount() < 4 && rng_.chance(dt * 0.004f)) {
        const u32 id = quests_.generateSideQuest(rng_, population(), shelter_.deepestFloor(),
                                                 surface_.discoveredCount());
        if (const gameplay::Quest* q = quests_.find(id))
            notify("New contract: " + q->title, NotifySeverity::Info);
    }
}

void World::syncQuestCounters() {
    for (int i = 0; i < kResourceCount; ++i)
        quests_.notifyAbsolute(gameplay::ObjectiveKind::StockResource, i,
                               resources_.amount[static_cast<size_t>(i)]);
    quests_.notifyAbsolute(gameplay::ObjectiveKind::ExploreSites, 0,
                           static_cast<f32>(surface_.discoveredCount()));
    // Room counts are recomputed rather than incremented so demolition counts.
    for (int i = 0; i < kRoomTypeCount; ++i) {
        const i32 n = shelter_.countOfType(static_cast<RoomType>(i));
        if (n > 0) quests_.notifyAbsolute(gameplay::ObjectiveKind::BuildRoom, i,
                                          static_cast<f32>(n));
    }
    for (int i = 0; i < kSkillCount; ++i) {
        u8 best = 0;
        for (const Resident& r : residents_)
            if (r.alive()) best = std::max(best, r.skills.get(static_cast<Skill>(i)));
        quests_.notifyAbsolute(gameplay::ObjectiveKind::TrainSkill, i, static_cast<f32>(best));
    }
}

void World::applyQuestRewards() {
    for (const gameplay::Quest& q : quests_.collectFinished()) {
        for (int i = 0; i < kResourceCount; ++i) {
            const f32 v = q.reward.resources[static_cast<size_t>(i)];
            if (v > 0.0f) resources_.add(static_cast<Resource>(i), v);
        }
        if (q.reward.itemId != 0) addToStorage(ItemStack{q.reward.itemId, 1, 1.0f});
        if (q.reward.experience > 0.0f)
            for (Resident& r : residents_) r.grantExperience(q.reward.experience);
        ++stats_.questsCompleted;
        notify("Contract complete: " + q.title + " \xE2\x80\x94 " + q.reward.describe(),
               NotifySeverity::Good);
    }
}

void World::rebuildDerived() {
    shelter_.applyStorageCapacity(resources_);
    // Worker lists and resident job pointers must agree after a load.
    for (Resident& r : residents_) r.assignedRoom = kNoRoom;
    for (Room& room : shelter_.rooms()) {
        std::vector<ResidentId> valid;
        for (ResidentId w : room.workers) {
            Resident* res = resident(w);
            if (!res || !res->alive()) continue;
            if (static_cast<i32>(valid.size()) >= room.workerSlots()) break;
            valid.push_back(w);
            res->assignedRoom = room.id;
        }
        room.workers = std::move(valid);
    }
    for (Resident& r : residents_) {
        if (r.currentRoom != kNoRoom && !shelter_.room(r.currentRoom)) r.currentRoom = kNoRoom;
        r.cell = worldToCell(r.position);
    }
}

} // namespace hv::sim
