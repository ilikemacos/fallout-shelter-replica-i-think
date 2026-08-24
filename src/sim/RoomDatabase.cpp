#include "sim/RoomDatabase.hpp"
#include <algorithm>

namespace hv::sim {
namespace {

std::vector<RoomDef> buildTable() {
    std::vector<RoomDef> t(kRoomTypeCount);
    for (int i = 0; i < kRoomTypeCount; ++i) t[i].type = static_cast<RoomType>(i);

    auto& none = t[static_cast<int>(RoomType::None)];
    none.name = "Empty"; none.description = "Unexcavated rock."; none.width = 1;
    none.maxLevel = 1; none.workerSlots = 0;

    auto& entrance = t[static_cast<int>(RoomType::Entrance)];
    entrance.name = "Airlock";
    entrance.description = "Blast door to the surface. Expeditions leave and return here.";
    entrance.width = 2; entrance.maxLevel = 3; entrance.workerSlots = 2;
    entrance.buildCost = {0, 0, 0};
    entrance.upgradeCost = {120, 60, 0};
    entrance.function = RoomFunction::Defend;
    entrance.powerDraw = 2.0f;
    entrance.primarySkill = Skill::Security;

    auto& elevator = t[static_cast<int>(RoomType::Elevator)];
    elevator.name = "Elevator";
    elevator.description = "Freight lift linking floors. Residents ride it between levels.";
    elevator.width = 1; elevator.maxLevel = 2; elevator.workerSlots = 0;
    elevator.buildCost = {45, 10, 0};
    elevator.upgradeCost = {80, 30, 0};
    elevator.function = RoomFunction::Transit;
    elevator.powerDraw = 1.5f;

    auto& corridor = t[static_cast<int>(RoomType::Corridor)];
    corridor.name = "Corridor";
    corridor.description = "Bare service passage. Cheap, walkable, holds cable runs.";
    corridor.width = 1; corridor.maxLevel = 1; corridor.workerSlots = 0;
    corridor.buildCost = {15, 0, 0};
    corridor.function = RoomFunction::Transit;
    corridor.powerDraw = 0.3f;

    auto& gen = t[static_cast<int>(RoomType::Generator)];
    gen.name = "Generator Hall";
    gen.description = "Diesel-electric sets on rubber mounts. Everything downstream needs them.";
    gen.width = 3; gen.workerSlots = 3;
    gen.buildCost = {110, 40, 0};
    gen.upgradeCost = {150, 90, 0};
    gen.function = RoomFunction::Produce;
    gen.produces = Resource::Power;
    gen.productionPerMinute = 26.0f;
    gen.primarySkill = Skill::Engineering;

    auto& water = t[static_cast<int>(RoomType::WaterPlant)];
    water.name = "Water Reclamation";
    water.description = "Sand filters and a chlorination stack. Smells of wet iron.";
    water.width = 3; water.workerSlots = 3;
    water.buildCost = {120, 45, 0};
    water.upgradeCost = {160, 95, 0};
    water.function = RoomFunction::Produce;
    water.produces = Resource::Water;
    water.productionPerMinute = 19.0f;
    water.powerDraw = 7.0f;
    water.primarySkill = Skill::Hydrology;

    auto& hydro = t[static_cast<int>(RoomType::Hydroponics)];
    hydro.name = "Hydroponics";
    hydro.description = "Grow racks under sodium lamps. The only green left.";
    hydro.width = 3; hydro.workerSlots = 3;
    hydro.buildCost = {120, 45, 0};
    hydro.upgradeCost = {160, 95, 0};
    hydro.function = RoomFunction::Produce;
    hydro.produces = Resource::Food;
    hydro.productionPerMinute = 18.0f;
    hydro.powerDraw = 8.0f;
    hydro.primarySkill = Skill::Agronomy;

    auto& cafe = t[static_cast<int>(RoomType::Cafeteria)];
    cafe.name = "Mess Hall";
    cafe.description = "Steel tables, one working urn. Residents eat and gossip here.";
    cafe.width = 3; cafe.workerSlots = 2;
    cafe.buildCost = {90, 30, 0};
    cafe.upgradeCost = {110, 70, 0};
    cafe.function = RoomFunction::Morale;
    cafe.powerDraw = 3.0f;
    cafe.moraleAura = 5.0f;
    cafe.primarySkill = Skill::Agronomy;
    cafe.requiredPopulation = 6;

    auto& dorm = t[static_cast<int>(RoomType::Dormitory)];
    dorm.name = "Dormitory";
    dorm.description = "Stacked bunks and a shared locker. Sleep restores energy.";
    dorm.width = 3; dorm.workerSlots = 0;
    dorm.buildCost = {80, 25, 0};
    dorm.upgradeCost = {100, 60, 0};
    dorm.function = RoomFunction::Rest;
    dorm.powerDraw = 2.5f;
    dorm.moraleAura = 2.0f;

    auto& store = t[static_cast<int>(RoomType::Storage)];
    store.name = "Store Room";
    store.description = "Pallet racking and crates. Raises how much you can hold.";
    store.width = 2; store.workerSlots = 1;
    store.buildCost = {70, 20, 0};
    store.upgradeCost = {90, 55, 0};
    store.function = RoomFunction::Store;
    store.powerDraw = 1.0f;
    store.storageBonus = 260.0f;
    store.primarySkill = Skill::Logistics;

    auto& shop = t[static_cast<int>(RoomType::Workshop)];
    shop.name = "Workshop";
    shop.description = "Lathe, press and a wall of salvage. Turns scrap into materials and gear.";
    shop.width = 3; shop.workerSlots = 3;
    shop.buildCost = {130, 55, 0};
    shop.upgradeCost = {170, 100, 0};
    shop.function = RoomFunction::Craft;
    shop.produces = Resource::Materials;
    shop.productionPerMinute = 12.0f;
    shop.powerDraw = 9.0f;
    shop.primarySkill = Skill::Engineering;
    shop.requiredPopulation = 8;

    auto& med = t[static_cast<int>(RoomType::Medical)];
    med.name = "Infirmary";
    med.description = "Two beds, an autoclave, a locked drug cabinet. Produces and applies medicine.";
    med.width = 2; med.workerSlots = 2;
    med.buildCost = {140, 60, 0};
    med.upgradeCost = {180, 110, 0};
    med.function = RoomFunction::Heal;
    med.produces = Resource::Medicine;
    med.productionPerMinute = 4.5f;
    med.powerDraw = 6.0f;
    med.primarySkill = Skill::Medicine;
    med.requiredPopulation = 8;

    auto& train = t[static_cast<int>(RoomType::Training)];
    train.name = "Training Hall";
    train.description = "Mats, weights and a scarred practice dummy. Raises resident skills.";
    train.width = 3; train.workerSlots = 4;
    train.buildCost = {120, 50, 0};
    train.upgradeCost = {150, 90, 0};
    train.function = RoomFunction::Train;
    train.powerDraw = 4.0f;
    train.primarySkill = Skill::Security;
    train.requiredPopulation = 10;

    auto& sec = t[static_cast<int>(RoomType::Security)];
    sec.name = "Security Post";
    sec.description = "Gun lockers, a camera bank, and someone always awake.";
    sec.width = 2; sec.workerSlots = 3;
    sec.buildCost = {110, 45, 0};
    sec.upgradeCost = {140, 85, 0};
    sec.function = RoomFunction::Defend;
    sec.powerDraw = 4.5f;
    sec.primarySkill = Skill::Security;
    sec.requiredPopulation = 10;

    auto& lab = t[static_cast<int>(RoomType::Laboratory)];
    lab.name = "Laboratory";
    lab.description = "Fume hood, centrifuge, a chalkboard nobody wipes. Refines medicine.";
    lab.width = 3; lab.workerSlots = 3;
    lab.buildCost = {180, 90, 0};
    lab.upgradeCost = {210, 130, 0};
    lab.function = RoomFunction::Produce;
    lab.produces = Resource::Medicine;
    lab.productionPerMinute = 7.0f;
    lab.powerDraw = 11.0f;
    lab.primarySkill = Skill::Science;
    lab.requiredPopulation = 16;
    lab.techRequired = "applied_chemistry";

    auto& armory = t[static_cast<int>(RoomType::Armory)];
    armory.name = "Armoury";
    armory.description = "Racked weapons behind a steel grille. Equipment lives here.";
    armory.width = 2; armory.workerSlots = 2;
    armory.buildCost = {160, 80, 0};
    armory.upgradeCost = {190, 120, 0};
    armory.function = RoomFunction::Store;
    armory.storesResource = Resource::Materials;
    armory.storageBonus = 120.0f;
    armory.powerDraw = 3.0f;
    armory.primarySkill = Skill::Security;
    armory.requiredPopulation = 14;
    armory.techRequired = "small_arms";

    auto& cmd = t[static_cast<int>(RoomType::Command)];
    cmd.name = "Operations";
    cmd.description = "Status boards and a working intercom. Improves everything, slightly.";
    cmd.width = 3; cmd.workerSlots = 2;
    cmd.buildCost = {220, 140, 0};
    cmd.upgradeCost = {240, 180, 0};
    cmd.function = RoomFunction::Morale;
    cmd.powerDraw = 8.0f;
    cmd.moraleAura = 3.0f;
    cmd.primarySkill = Skill::Presence;
    cmd.requiredPopulation = 20;
    cmd.techRequired = "shelter_operations";

    auto& rec = t[static_cast<int>(RoomType::Recreation)];
    rec.name = "Rec Room";
    rec.description = "Card table, a radio, one surviving pool cue. Morale climbs here.";
    rec.width = 2; rec.workerSlots = 1;
    rec.buildCost = {100, 40, 0};
    rec.upgradeCost = {120, 75, 0};
    rec.function = RoomFunction::Morale;
    rec.powerDraw = 3.5f;
    rec.moraleAura = 8.0f;
    rec.primarySkill = Skill::Presence;
    rec.requiredPopulation = 12;

    auto& recy = t[static_cast<int>(RoomType::Recycling)];
    recy.name = "Reclamation";
    recy.description = "Shredders and a smelter. Chews waste back into usable stock.";
    recy.width = 3; recy.workerSlots = 3;
    recy.buildCost = {150, 60, 0};
    recy.upgradeCost = {170, 100, 0};
    recy.function = RoomFunction::Produce;
    recy.produces = Resource::Materials;
    recy.productionPerMinute = 9.0f;
    recy.powerDraw = 10.0f;
    recy.primarySkill = Skill::Logistics;
    recy.requiredPopulation = 18;
    recy.techRequired = "materials_reclamation";

    auto& comms = t[static_cast<int>(RoomType::Communications)];
    comms.name = "Signals";
    comms.description = "A valve transmitter and a mast on the surface. Draws newcomers, finds work.";
    comms.width = 2; comms.workerSlots = 2;
    comms.buildCost = {170, 90, 0};
    comms.upgradeCost = {190, 120, 0};
    comms.function = RoomFunction::Trade;
    comms.produces = Resource::Scrip;
    comms.productionPerMinute = 5.0f;
    comms.powerDraw = 7.0f;
    comms.primarySkill = Skill::Presence;
    comms.requiredPopulation = 14;
    comms.techRequired = "long_range_radio";

    auto& research = t[static_cast<int>(RoomType::Research)];
    research.name = "Research Bay";
    research.description = "Salvaged test rigs and stacks of pre-war paper. Generates research.";
    research.width = 3; research.workerSlots = 3;
    research.buildCost = {200, 110, 0};
    research.upgradeCost = {220, 150, 0};
    research.function = RoomFunction::Research;
    research.produces = Resource::Research;
    research.productionPerMinute = 3.2f;
    research.powerDraw = 12.0f;
    research.primarySkill = Skill::Science;
    research.requiredPopulation = 12;

    return t;
}

} // namespace

const std::vector<RoomDef>& allRoomDefs() {
    static const std::vector<RoomDef> table = buildTable();
    return table;
}

const RoomDef& roomDef(RoomType t) {
    const std::vector<RoomDef>& table = allRoomDefs();
    const int i = static_cast<int>(t);
    return table[(i >= 0 && i < kRoomTypeCount) ? i : 0];
}

const char* roomTypeName(RoomType t) { return roomDef(t).name; }

namespace {
/// Wider rooms are more efficient per cell — the classic reason to merge.
f32 widthEfficiency(const RoomDef& d, i32 widthCells) {
    if (d.width <= 0) return 1.0f;
    const f32 units = static_cast<f32>(widthCells) / static_cast<f32>(d.width);
    return units <= 1.0f ? units : 1.0f + (units - 1.0f) * 1.15f;
}
} // namespace

f32 roomProduction(const RoomDef& d, i32 level, i32 widthCells) {
    const f32 lvl = 1.0f + 0.55f * static_cast<f32>(std::max(0, level - 1));
    return d.productionPerMinute * lvl * widthEfficiency(d, widthCells);
}

f32 roomPowerDraw(const RoomDef& d, i32 level, i32 widthCells) {
    const f32 lvl = 1.0f + 0.35f * static_cast<f32>(std::max(0, level - 1));
    return d.powerDraw * lvl * widthEfficiency(d, widthCells);
}

i32 roomWorkerSlots(const RoomDef& d, i32 level, i32 widthCells) {
    if (d.workerSlots == 0) return 0;
    const f32 units = d.width > 0 ? static_cast<f32>(widthCells) / static_cast<f32>(d.width) : 1.0f;
    const i32 base = static_cast<i32>(std::round(static_cast<f32>(d.workerSlots) * std::max(1.0f, units)));
    return base + (level - 1);
}

Cost roomUpgradeCost(const RoomDef& d, i32 targetLevel, i32 widthCells) {
    const f32 k = static_cast<f32>(targetLevel - 1) *
                  (0.7f + 0.35f * static_cast<f32>(widthCells));
    return d.upgradeCost.scaled(std::max(1.0f, k));
}

Cost roomBuildCost(const RoomDef& d, i32 widthCells, i32 floor) {
    // Digging deeper costs more; each floor down adds 8%.
    const f32 depth = 1.0f + 0.08f * static_cast<f32>(std::max(0, floor));
    const f32 cells = d.width > 0 ? static_cast<f32>(widthCells) / static_cast<f32>(d.width) : 1.0f;
    return d.buildCost.scaled(depth * std::max(1.0f, cells));
}

} // namespace hv::sim
