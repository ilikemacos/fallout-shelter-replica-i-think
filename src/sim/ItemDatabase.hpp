#pragma once
#include "sim/SimTypes.hpp"
#include "core/Random.hpp"
#include <vector>

namespace hv::sim {

enum class ItemKind : u8 { Weapon = 0, Outfit, Utility, Consumable };
enum class ItemRarity : u8 { Common = 0, Sturdy, Rare, Prototype };

const char* rarityName(ItemRarity r);
Vec3        rarityColor(ItemRarity r);

struct ItemDef {
    u16         id = 0;
    const char* name = "";
    const char* flavor = "";
    ItemKind    kind = ItemKind::Weapon;
    ItemRarity  rarity = ItemRarity::Common;
    f32  damage = 0.0f;         ///< per attack, weapons only
    f32  fireRate = 1.0f;       ///< attacks per second
    f32  accuracy = 0.0f;       ///< added hit chance, -1..1
    f32  armor = 0.0f;          ///< damage reduction, outfits
    f32  value = 10.0f;         ///< scrip
    f32  weight = 1.0f;
    std::array<i8, kSkillCount> skillBonus{};   ///< outfits/utility
    /// Consumables restore these directly.
    f32  healAmount = 0.0f;
    f32  energyAmount = 0.0f;
    f32  moraleAmount = 0.0f;
    const char* techRequired = "";
    f32  craftMaterials = 0.0f;  ///< 0 = cannot be crafted in a workshop
};

const std::vector<ItemDef>& allItems();
const ItemDef& itemDef(u16 id);
u16 itemIdByName(const char* name);
/// Weighted random loot pick appropriate to a danger level 0..5.
u16 rollLoot(Rng& rng, i32 dangerLevel);

/// An item in an inventory: definition plus per-instance condition.
struct ItemStack {
    u16 defId = 0;
    u16 count = 1;
    f32 condition = 1.0f;   ///< 0..1, scales weapon damage and outfit armor
    bool valid() const { return defId != 0; }
};

} // namespace hv::sim
