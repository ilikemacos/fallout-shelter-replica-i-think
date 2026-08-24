#include "gameplay/Research.hpp"
#include "core/Log.hpp"
#include <algorithm>

namespace hv::gameplay {
namespace {

TechNode node(const char* id, const char* name, const char* desc, f32 cost,
              f32 seconds, i32 tier, i32 column, std::vector<std::string> prereq) {
    TechNode n;
    n.id = id; n.name = name; n.description = desc;
    n.cost = cost; n.researchSeconds = seconds; n.tier = tier; n.column = column;
    n.prerequisites = std::move(prereq);
    return n;
}

} // namespace

TechTree::TechTree() { reset(); }

void TechTree::reset() {
    nodes_.clear();
    unlocked_.clear();
    activeId_.clear();
    activeElapsed_ = activeTotal_ = 0.0f;

    // --- Tier 0: the basics you research in the first hour -------------------
    TechNode n;
    n = node("load_balancing", "Load Balancing",
             "Rewire the distribution board so generators waste less.", 40, 90, 0, 0, {});
    n.bonus.powerOutput = 0.12f;
    nodes_.push_back(n);

    n = node("filter_media", "Filter Media",
             "Better sand and charcoal beds lift reclamation throughput.", 40, 90, 0, 1, {});
    n.bonus.waterOutput = 0.12f;
    nodes_.push_back(n);

    n = node("grow_lamps", "Spectrum Lamps",
             "Retune the grow lamps to the wavelengths plants actually use.", 40, 90, 0, 2, {});
    n.bonus.foodOutput = 0.12f;
    nodes_.push_back(n);

    n = node("small_arms", "Small Arms",
             "Machining tolerances good enough for reliable sidearms. Unlocks the Armoury.",
             60, 150, 0, 3, {});
    n.bonus.defence = 0.05f;
    nodes_.push_back(n);

    // --- Tier 1 --------------------------------------------------------------
    n = node("shelter_operations", "Shelter Operations",
             "Shift rosters, status boards, an intercom that works. Unlocks Operations.",
             90, 210, 1, 0, {"load_balancing"});
    n.bonus.moraleFloor = 4.0f;
    nodes_.push_back(n);

    n = node("applied_chemistry", "Applied Chemistry",
             "Synthesis routes for antiseptics and analgesics. Unlocks the Laboratory.",
             100, 240, 1, 1, {"filter_media"});
    n.bonus.medicineOutput = 0.15f;
    nodes_.push_back(n);

    n = node("materials_reclamation", "Materials Reclamation",
             "Sorting, shredding and smelting. Unlocks Reclamation.", 100, 240, 1, 2,
             {"grow_lamps"});
    n.bonus.materialOutput = 0.15f;
    nodes_.push_back(n);

    n = node("reinforced_plate", "Reinforced Plate",
             "Layered composite over quilted backing. Better armour for defenders.",
             110, 240, 1, 3, {"small_arms"});
    n.bonus.defence = 0.10f;
    nodes_.push_back(n);

    // --- Tier 2 --------------------------------------------------------------
    n = node("long_range_radio", "Long-Range Radio",
             "A mast on the surface and a valve set below. Unlocks Signals.",
             150, 300, 2, 0, {"shelter_operations"});
    n.bonus.expeditionYield = 0.10f;
    nodes_.push_back(n);

    n = node("sealed_environments", "Sealed Environments",
             "Positive-pressure suits and airlocks that actually seal.",
             160, 320, 2, 1, {"applied_chemistry"});
    n.bonus.medicineOutput = 0.10f;
    n.bonus.expeditionYield = 0.08f;
    nodes_.push_back(n);

    n = node("field_survey", "Field Survey",
             "Teach expedition crews to read a site before they loot it.",
             150, 300, 2, 2, {"materials_reclamation"});
    n.bonus.expeditionYield = 0.18f;
    nodes_.push_back(n);

    n = node("automatic_arms", "Automatic Arms",
             "Gas systems and box magazines. Unlocks better weapons.",
             170, 330, 2, 3, {"reinforced_plate"});
    n.bonus.defence = 0.06f;
    nodes_.push_back(n);

    n = node("preventive_maintenance", "Preventive Maintenance",
             "Scheduled servicing. Machinery breaks down far less often.",
             160, 320, 2, 4, {"shelter_operations"});
    n.bonus.breakdownResistance = 0.35f;
    nodes_.push_back(n);

    // --- Tier 3 --------------------------------------------------------------
    n = node("cascade_turbines", "Cascade Turbines",
             "Recover exhaust heat back into the generator loop.",
             240, 420, 3, 0, {"long_range_radio", "preventive_maintenance"});
    n.bonus.powerOutput = 0.25f;
    nodes_.push_back(n);

    n = node("closed_loop_farming", "Closed-Loop Farming",
             "Nutrient recovery ties hydroponics to reclamation.",
             240, 420, 3, 1, {"field_survey", "sealed_environments"});
    n.bonus.foodOutput = 0.22f;
    n.bonus.waterOutput = 0.10f;
    nodes_.push_back(n);

    n = node("deep_archives", "Deep Archives",
             "Index what the shelter already knows. Research compounds.",
             260, 450, 3, 2, {"field_survey"});
    n.bonus.researchOutput = 0.30f;
    nodes_.push_back(n);

    n = node("energy_weapons", "Directed Energy",
             "Capacitor banks small enough to carry. Unlocks arc and coil weapons.",
             300, 480, 3, 3, {"automatic_arms"});
    n.bonus.defence = 0.08f;
    nodes_.push_back(n);

    n = node("cadre_training", "Cadre Training",
             "Experienced residents teach the rest. Training is much faster.",
             220, 400, 3, 4, {"preventive_maintenance"});
    n.bonus.trainingSpeed = 0.45f;
    nodes_.push_back(n);

    // --- Tier 4: end-game --------------------------------------------------
    n = node("heavy_ordnance", "Heavy Ordnance",
             "Shoulder-fired launchers assembled from salvaged tube stock.",
             400, 600, 4, 0, {"energy_weapons"});
    n.bonus.defence = 0.10f;
    nodes_.push_back(n);

    n = node("powered_frame", "Powered Frame",
             "Restore an exo-frame from the lower vaults. Unlocks the Deepwalker Shell.",
             500, 720, 4, 1, {"heavy_ordnance", "cascade_turbines"});
    n.bonus.defence = 0.12f;
    nodes_.push_back(n);

    n = node("prototype_arms", "Prototype Arms",
             "The deep lab's unfinished work, finally understood.",
             520, 720, 4, 2, {"energy_weapons", "deep_archives"});
    n.bonus.defence = 0.08f;
    nodes_.push_back(n);

    n = node("arcology_systems", "Arcology Systems",
             "Integrated control of every loop in the shelter. Everything improves.",
             650, 900, 4, 3, {"cascade_turbines", "closed_loop_farming", "deep_archives"});
    n.bonus.powerOutput = 0.15f; n.bonus.waterOutput = 0.15f;
    n.bonus.foodOutput = 0.15f;  n.bonus.materialOutput = 0.15f;
    n.bonus.storageCapacity = 0.25f; n.bonus.moraleFloor = 6.0f;
    nodes_.push_back(n);

    recomputeBonuses();
}

const TechNode* TechTree::find(const std::string& id) const {
    for (const TechNode& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}

bool TechTree::unlocked(const std::string& id) const {
    return std::find(unlocked_.begin(), unlocked_.end(), id) != unlocked_.end();
}

bool TechTree::available(const std::string& id) const {
    const TechNode* n = find(id);
    if (!n || unlocked(id)) return false;
    for (const std::string& p : n->prerequisites) if (!unlocked(p)) return false;
    return true;
}

bool TechTree::beginResearch(const std::string& id, ResourcePool& res) {
    if (!activeId_.empty() || !available(id)) return false;
    const TechNode* n = find(id);
    if (!res.spend(Resource::Research, n->cost)) return false;
    activeId_ = id;
    activeElapsed_ = 0.0f;
    activeTotal_ = n->researchSeconds;
    HV_INFO("Research started: %s", n->name.c_str());
    return true;
}

void TechTree::cancelResearch() {
    // Points already spent are not refunded — committing matters.
    activeId_.clear();
    activeElapsed_ = activeTotal_ = 0.0f;
}

std::string TechTree::tick(f32 dtSeconds, f32 speedMultiplier) {
    if (activeId_.empty()) return {};
    activeElapsed_ += dtSeconds * speedMultiplier;
    if (activeElapsed_ < activeTotal_) return {};
    const std::string done = activeId_;
    unlocked_.push_back(done);
    activeId_.clear();
    activeElapsed_ = activeTotal_ = 0.0f;
    recomputeBonuses();
    return done;
}

void TechTree::recomputeBonuses() {
    bonuses_ = TechBonus{};
    for (const std::string& id : unlocked_)
        if (const TechNode* n = find(id)) bonuses_.add(n->bonus);
}

void TechTree::serialize(BlobWriter& w) const {
    w.u32v(static_cast<u32>(unlocked_.size()));
    for (const std::string& s : unlocked_) w.str(s);
    w.str(activeId_);
    w.f32v(activeElapsed_);
    w.f32v(activeTotal_);
}

bool TechTree::deserialize(BlobReader& r, u32 version) {
    (void)version;
    unlocked_.clear();
    const u32 n = r.u32v();
    if (r.failed() || n > 512) return false;
    for (u32 i = 0; i < n; ++i) {
        const std::string id = r.str();
        // Silently drop ids from an older build that no longer exist.
        if (find(id)) unlocked_.push_back(id);
    }
    activeId_ = r.str();
    if (!find(activeId_)) activeId_.clear();
    activeElapsed_ = r.f32v();
    activeTotal_ = r.f32v();
    recomputeBonuses();
    return !r.failed();
}

} // namespace hv::gameplay
