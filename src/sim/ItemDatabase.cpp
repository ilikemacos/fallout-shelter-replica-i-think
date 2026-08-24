#include "sim/ItemDatabase.hpp"
#include "core/Random.hpp"
#include <cstring>

namespace hv::sim {

const char* rarityName(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return "Common";
        case ItemRarity::Sturdy:    return "Sturdy";
        case ItemRarity::Rare:      return "Rare";
        case ItemRarity::Prototype: return "Prototype";
    }
    return "?";
}

Vec3 rarityColor(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return colorFromHex(0x9AA0A6);
        case ItemRarity::Sturdy:    return colorFromHex(0x7FB069);
        case ItemRarity::Rare:      return colorFromHex(0x6FA8DC);
        case ItemRarity::Prototype: return colorFromHex(0xE0A458);
    }
    return Vec3{1, 1, 1};
}

namespace {

ItemDef weapon(u16 id, const char* n, const char* flavor, ItemRarity r, f32 dmg,
               f32 rate, f32 acc, f32 value, const char* tech = "", f32 craft = 0.0f) {
    ItemDef d;
    d.id = id; d.name = n; d.flavor = flavor; d.kind = ItemKind::Weapon; d.rarity = r;
    d.damage = dmg; d.fireRate = rate; d.accuracy = acc; d.value = value;
    d.techRequired = tech; d.craftMaterials = craft; d.weight = 3.0f;
    return d;
}

ItemDef outfit(u16 id, const char* n, const char* flavor, ItemRarity r, f32 armor,
               f32 value, Skill s1, i8 b1, Skill s2, i8 b2,
               const char* tech = "", f32 craft = 0.0f) {
    ItemDef d;
    d.id = id; d.name = n; d.flavor = flavor; d.kind = ItemKind::Outfit; d.rarity = r;
    d.armor = armor; d.value = value; d.techRequired = tech; d.craftMaterials = craft;
    d.weight = 2.0f;
    d.skillBonus[static_cast<int>(s1)] = b1;
    d.skillBonus[static_cast<int>(s2)] = b2;
    return d;
}

ItemDef consumable(u16 id, const char* n, const char* flavor, f32 heal, f32 energy,
                   f32 morale, f32 value) {
    ItemDef d;
    d.id = id; d.name = n; d.flavor = flavor; d.kind = ItemKind::Consumable;
    d.healAmount = heal; d.energyAmount = energy; d.moraleAmount = morale;
    d.value = value; d.weight = 0.3f;
    return d;
}

std::vector<ItemDef> buildItems() {
    std::vector<ItemDef> v;
    v.push_back(ItemDef{});   // id 0 = nothing

    // --- Weapons -----------------------------------------------------------
    v.push_back(weapon(1,  "Pipe Bludgeon",  "Threaded pipe, taped grip. Everyone starts here.", ItemRarity::Common, 5.0f, 1.1f, 0.00f, 12, "", 20));
    v.push_back(weapon(2,  "Scrap Cleaver",  "Ground from a sheet of hull plate.",               ItemRarity::Common, 7.0f, 1.0f, -0.03f, 20, "", 30));
    v.push_back(weapon(3,  "Bolt Pistol",    "Single action, machined in the workshop.",         ItemRarity::Common, 9.0f, 1.4f, 0.04f, 45, "small_arms", 55));
    v.push_back(weapon(4,  "Service Revolver","Pre-collapse police issue, still tight.",         ItemRarity::Sturdy, 13.0f, 1.3f, 0.07f, 90, "small_arms", 90));
    v.push_back(weapon(5,  "Riot Shotgun",   "Short barrel, brutal at corridor range.",          ItemRarity::Sturdy, 21.0f, 0.75f, -0.05f, 140, "small_arms", 130));
    v.push_back(weapon(6,  "Salvage Carbine","Semi-auto, held together with love and shim stock.", ItemRarity::Sturdy, 16.0f, 1.8f, 0.05f, 165, "automatic_arms", 150));
    v.push_back(weapon(7,  "Arc Lance",      "Capacitor bank on a pole. Hair stands up nearby.", ItemRarity::Rare, 26.0f, 0.9f, 0.10f, 260, "energy_weapons", 220));
    v.push_back(weapon(8,  "Coil Rifle",     "Magnetic accelerator. Whines before it fires.",    ItemRarity::Rare, 31.0f, 1.1f, 0.12f, 340, "energy_weapons", 290));
    v.push_back(weapon(9,  "Breaker Cannon", "Shoulder-fired, one shot, everyone hears it.",     ItemRarity::Prototype, 55.0f, 0.4f, 0.02f, 620, "heavy_ordnance", 480));
    v.push_back(weapon(10, "Sentinel Repeater","Prototype from the deep lab. Nothing rattles.",  ItemRarity::Prototype, 38.0f, 2.1f, 0.16f, 780, "prototype_arms", 600));

    // --- Outfits -----------------------------------------------------------
    v.push_back(outfit(20, "Shelter Coveralls", "Standard issue. Pockets everywhere.",           ItemRarity::Common, 1.0f, 10,  Skill::Logistics, 1, Skill::Engineering, 0, "", 15));
    v.push_back(outfit(21, "Engineer's Rig",    "Tool harness and burn-proof sleeves.",          ItemRarity::Common, 2.0f, 40,  Skill::Engineering, 2, Skill::Logistics, 1, "", 45));
    v.push_back(outfit(22, "Grower's Apron",    "Stained green at the knees.",                   ItemRarity::Common, 1.0f, 35,  Skill::Agronomy, 2, Skill::Hydrology, 1, "", 40));
    v.push_back(outfit(23, "Medic Whites",      "Clean, mostly. Deep pockets for ampoules.",     ItemRarity::Sturdy, 2.0f, 70,  Skill::Medicine, 3, Skill::Science, 1, "", 70));
    v.push_back(outfit(24, "Lab Coat",          "Burn holes read like a service record.",        ItemRarity::Sturdy, 1.0f, 75,  Skill::Science, 3, Skill::Medicine, 1, "", 70));
    v.push_back(outfit(25, "Guard Plate",       "Riveted steel over quilted padding.",           ItemRarity::Sturdy, 8.0f, 120, Skill::Security, 2, Skill::Presence, 0, "reinforced_plate", 110));
    v.push_back(outfit(26, "Sealed Suit",       "Rubberised, filtered. Hot to work in.",         ItemRarity::Rare, 11.0f, 210, Skill::Hydrology, 2, Skill::Medicine, 2, "sealed_environments", 190));
    v.push_back(outfit(27, "Warden Harness",    "Layered composite. Heavy, and worth it.",       ItemRarity::Rare, 16.0f, 320, Skill::Security, 4, Skill::Presence, 1, "reinforced_plate", 260));
    v.push_back(outfit(28, "Deepwalker Shell",  "Powered exo-frame recovered from the lower vaults.", ItemRarity::Prototype, 28.0f, 900, Skill::Security, 5, Skill::Engineering, 3, "powered_frame", 700));

    // --- Utility -----------------------------------------------------------
    ItemDef lamp;
    lamp.id = 40; lamp.name = "Helmet Lamp"; lamp.flavor = "Wide beam, cracked lens.";
    lamp.kind = ItemKind::Utility; lamp.value = 30; lamp.craftMaterials = 25;
    lamp.skillBonus[static_cast<int>(Skill::Logistics)] = 1;
    v.push_back(lamp);

    ItemDef kit;
    kit.id = 41; kit.name = "Field Toolkit"; kit.flavor = "Rolls out flat, everything labelled.";
    kit.kind = ItemKind::Utility; kit.value = 60; kit.craftMaterials = 50;
    kit.skillBonus[static_cast<int>(Skill::Engineering)] = 2;
    v.push_back(kit);

    ItemDef geiger;
    geiger.id = 42; geiger.name = "Counter"; geiger.flavor = "Clicks. You learn to hate the rhythm.";
    geiger.kind = ItemKind::Utility; geiger.value = 80; geiger.craftMaterials = 60;
    geiger.skillBonus[static_cast<int>(Skill::Science)] = 2;
    geiger.techRequired = "field_survey";
    v.push_back(geiger);

    // --- Consumables -------------------------------------------------------
    v.push_back(consumable(60, "Field Dressing", "Gauze and a clamp. Buys time.",            25.0f, 0.0f, 0.0f, 18));
    v.push_back(consumable(61, "Stim Ampoule",   "Cold going in, warm after.",               55.0f, 20.0f, 3.0f, 40));
    v.push_back(consumable(62, "Ration Tin",     "Protein paste. Nobody asks what kind.",     5.0f, 35.0f, 2.0f, 12));
    v.push_back(consumable(63, "Chelating Agent","Flushes what the surface put in you.",     40.0f, -10.0f, 0.0f, 55));
    v.push_back(consumable(64, "Bootleg Spirits","Distilled in Reclamation. Officially banned.", 0.0f, -5.0f, 14.0f, 22));
    return v;
}

} // namespace

const std::vector<ItemDef>& allItems() {
    static const std::vector<ItemDef> v = buildItems();
    return v;
}

const ItemDef& itemDef(u16 id) {
    const std::vector<ItemDef>& v = allItems();
    for (const ItemDef& d : v) if (d.id == id) return d;
    return v[0];
}

u16 itemIdByName(const char* name) {
    for (const ItemDef& d : allItems()) if (std::strcmp(d.name, name) == 0) return d.id;
    return 0;
}

u16 rollLoot(Rng& rng, i32 dangerLevel) {
    const std::vector<ItemDef>& v = allItems();
    // Danger raises the rarity ceiling; low tiers stay possible so early
    // expeditions still come back with something.
    const f32 roll = rng.unit() * (0.35f + 0.16f * static_cast<f32>(dangerLevel));
    ItemRarity target = ItemRarity::Common;
    if (roll > 0.85f) target = ItemRarity::Prototype;
    else if (roll > 0.60f) target = ItemRarity::Rare;
    else if (roll > 0.30f) target = ItemRarity::Sturdy;

    std::vector<u16> pool;
    for (const ItemDef& d : v) {
        if (d.id == 0) continue;
        if (d.rarity == target) pool.push_back(d.id);
    }
    if (pool.empty()) return 60;   // fall back to a field dressing
    return pool[static_cast<size_t>(rng.rangeI(0, static_cast<i32>(pool.size()) - 1))];
}

} // namespace hv::sim
