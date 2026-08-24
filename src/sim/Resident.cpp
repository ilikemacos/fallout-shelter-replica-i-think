#include "sim/Resident.hpp"
#include "sim/Names.hpp"
#include <algorithm>

namespace hv::sim {

const char* activityName(Activity a) {
    switch (a) {
        case Activity::Idle:           return "Idle";
        case Activity::Walking:        return "Walking";
        case Activity::RidingElevator: return "In elevator";
        case Activity::Working:        return "Working";
        case Activity::Eating:         return "Eating";
        case Activity::Drinking:       return "Drinking";
        case Activity::Sleeping:       return "Sleeping";
        case Activity::Socialising:    return "Socialising";
        case Activity::Training:       return "Training";
        case Activity::Relaxing:       return "Relaxing";
        case Activity::Recovering:     return "In the infirmary";
        case Activity::Firefighting:   return "Fighting a fire";
        case Activity::Repairing:      return "Repairing";
        case Activity::Fighting:       return "In combat";
        case Activity::OnExpedition:   return "On the surface";
        case Activity::Dead:           return "Deceased";
    }
    return "?";
}

const char* personalityName(Personality p) {
    switch (p) {
        case Personality::Steady:     return "Steady";
        case Personality::Driven:     return "Driven";
        case Personality::Anxious:    return "Anxious";
        case Personality::Gregarious: return "Gregarious";
        case Personality::Solitary:   return "Solitary";
        case Personality::Reckless:   return "Reckless";
        case Personality::Meticulous: return "Meticulous";
        case Personality::Cynical:    return "Cynical";
        default: return "?";
    }
}

const char* traitName(u32 f) {
    switch (f) {
        case Trait_HardWorker:    return "Hard Worker";
        case Trait_NightOwl:      return "Night Owl";
        case Trait_Fragile:       return "Fragile";
        case Trait_Brave:         return "Brave";
        case Trait_Squeamish:     return "Squeamish";
        case Trait_Sociable:      return "Sociable";
        case Trait_QuickStudy:    return "Quick Study";
        case Trait_Tinkerer:      return "Tinkerer";
        case Trait_GreenThumb:    return "Green Thumb";
        case Trait_IronStomach:   return "Iron Stomach";
        case Trait_Scavenger:     return "Scavenger";
        case Trait_Insomniac:     return "Insomniac";
        case Trait_Claustrophobe: return "Claustrophobic";
        default: return "";
    }
}

const char* traitDescription(u32 f) {
    switch (f) {
        case Trait_HardWorker:    return "Produces 12% more on shift.";
        case Trait_NightOwl:      return "No morale penalty for late shifts; sleeps less.";
        case Trait_Fragile:       return "15% less maximum health.";
        case Trait_Brave:         return "Hits harder against intruders and never panics.";
        case Trait_Squeamish:     return "Loses morale when residents are hurt or die.";
        case Trait_Sociable:      return "Builds relationships twice as fast.";
        case Trait_QuickStudy:    return "Gains experience and trains 35% faster.";
        case Trait_Tinkerer:      return "Repairs broken machinery far faster.";
        case Trait_GreenThumb:    return "20% more output in hydroponics.";
        case Trait_IronStomach:   return "Eats and drinks 25% less.";
        case Trait_Scavenger:     return "Brings back more from the surface.";
        case Trait_Insomniac:     return "Recovers energy slowly while asleep.";
        case Trait_Claustrophobe: return "Morale falls on the deepest floors.";
        default: return "";
    }
}

i32 Resident::effectiveSkill(Skill s) const {
    i32 v = skills.get(s);
    const int i = static_cast<int>(s);
    if (weapon.valid())  v += itemDef(weapon.defId).skillBonus[i];
    if (outfit.valid())  v += itemDef(outfit.defId).skillBonus[i];
    if (utility.valid()) v += itemDef(utility.defId).skillBonus[i];
    return std::clamp(v, 1, 14);
}

f32 Resident::workEfficiency(Skill primary) const {
    // Skill 5 is the reference worker (1.0). Health, mood and fatigue all bite.
    const f32 skillPart = 0.45f + 0.11f * static_cast<f32>(effectiveSkill(primary));
    f32 e = skillPart;
    e *= 0.55f + 0.45f * healthFraction();
    e *= 0.70f + 0.30f * saturate(happiness / 100.0f);
    e *= 0.60f + 0.40f * saturate(energy / 100.0f);
    if (hasTrait(Trait_HardWorker)) e *= 1.12f;
    if (hasTrait(Trait_GreenThumb) && primary == Skill::Agronomy) e *= 1.20f;
    if (personality == Personality::Driven) e *= 1.06f;
    if (personality == Personality::Cynical) e *= 0.95f;
    return clampf(e, 0.05f, 3.0f);
}

f32 Resident::attackDamage() const {
    const f32 base = weapon.valid()
        ? itemDef(weapon.defId).damage * (0.4f + 0.6f * weapon.condition)
        : 4.0f;
    f32 d = base * (0.75f + 0.06f * static_cast<f32>(effectiveSkill(Skill::Security)));
    if (hasTrait(Trait_Brave)) d *= 1.10f;
    d *= 0.6f + 0.4f * healthFraction();
    return d;
}

f32 Resident::attackRate() const {
    const f32 base = weapon.valid() ? itemDef(weapon.defId).fireRate : 1.0f;
    return base * (0.85f + 0.03f * static_cast<f32>(effectiveSkill(Skill::Security)));
}

f32 Resident::hitChance() const {
    f32 acc = 0.55f + 0.025f * static_cast<f32>(effectiveSkill(Skill::Security));
    if (weapon.valid()) acc += itemDef(weapon.defId).accuracy;
    acc *= 0.7f + 0.3f * saturate(energy / 100.0f);
    return clampf(acc, 0.10f, 0.95f);
}

f32 Resident::damageReduction() const {
    f32 armor = outfit.valid() ? itemDef(outfit.defId).armor * (0.4f + 0.6f * outfit.condition) : 0.0f;
    // Diminishing returns so heavy armour is strong but never immunity.
    return armor / (armor + 28.0f);
}

void Resident::grantExperience(f32 xp) {
    if (!alive()) return;
    if (hasTrait(Trait_QuickStudy)) xp *= 1.35f;
    experience += xp;
    while (experience >= experienceToNext() && level < 50) {
        experience -= experienceToNext();
        ++level;
        maxHealth += 6.0f;
        health = std::min(health + 6.0f, effectiveMaxHealth());
    }
}

f32 Resident::affinityWith(ResidentId other) const {
    for (const Relationship& r : relationships) if (r.other == other) return r.affinity;
    return 0.0f;
}

void Resident::adjustAffinity(ResidentId other, f32 delta) {
    if (other == kNoResident || other == id) return;
    if (hasTrait(Trait_Sociable) && delta > 0.0f) delta *= 2.0f;
    if (personality == Personality::Solitary && delta > 0.0f) delta *= 0.6f;
    for (Relationship& r : relationships) {
        if (r.other == other) {
            r.affinity = clampf(r.affinity + delta, -100.0f, 100.0f);
            return;
        }
    }
    // Cap the book-keeping: only the strongest bonds are worth remembering.
    if (relationships.size() >= 24) {
        auto weakest = std::min_element(relationships.begin(), relationships.end(),
            [](const Relationship& a, const Relationship& b) {
                return std::fabs(a.affinity) < std::fabs(b.affinity);
            });
        if (std::fabs(weakest->affinity) < std::fabs(delta))
            *weakest = Relationship{other, clampf(delta, -100.0f, 100.0f)};
        return;
    }
    relationships.push_back(Relationship{other, clampf(delta, -100.0f, 100.0f)});
}

std::string Resident::statusLine() const {
    if (!alive()) return "Deceased";
    std::string s = activityName(activity);
    if (health < effectiveMaxHealth() * 0.35f) s += " \xE2\x80\xA2 badly hurt";
    else if (hunger > 70.0f) s += " \xE2\x80\xA2 hungry";
    else if (thirst > 70.0f) s += " \xE2\x80\xA2 thirsty";
    else if (energy < 20.0f) s += " \xE2\x80\xA2 exhausted";
    else if (happiness < 30.0f) s += " \xE2\x80\xA2 unhappy";
    return s;
}

namespace {

/// Family name = everything after the first space; falls back to the whole name.
std::string familyOf(const Resident& r) {
    const size_t sp = r.name.find(' ');
    return sp == std::string::npos ? r.name : r.name.substr(sp + 1);
}

u32 rollTraits(Rng& rng, int count) {
    static const u32 kAll[] = {
        Trait_HardWorker, Trait_NightOwl, Trait_Fragile, Trait_Brave, Trait_Squeamish,
        Trait_Sociable, Trait_QuickStudy, Trait_Tinkerer, Trait_GreenThumb,
        Trait_IronStomach, Trait_Scavenger, Trait_Insomniac, Trait_Claustrophobe
    };
    constexpr int n = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));
    u32 traits = 0;
    for (int i = 0; i < count; ++i) traits |= kAll[rng.rangeI(0, n - 1)];
    return traits;
}

} // namespace

Resident makeResident(Rng& rng, ResidentId id, f32 gameTimeHours, i32 tier) {
    Resident r;
    r.id = id;
    r.female = rng.chance(0.5f);
    r.name = randomFullName(rng, r.female);
    r.age = clampf(rng.gaussian(31.0f, 9.0f), 17.0f, 64.0f);
    r.personality = static_cast<Personality>(rng.rangeI(0, static_cast<i32>(Personality::Count) - 1));
    r.traits = rollTraits(rng, rng.rangeI(1, 2));

    // Everybody is competent at something; tier lifts the whole spread a little.
    const f32 bias = static_cast<f32>(tier) * 0.6f;
    for (int i = 0; i < kSkillCount; ++i) {
        const i32 v = std::clamp(static_cast<i32>(std::round(rng.gaussian(3.4f + bias, 1.4f))), 1, 8);
        r.skills.set(static_cast<Skill>(i), static_cast<u8>(v));
    }
    const Skill specialty = static_cast<Skill>(rng.rangeI(0, kSkillCount - 1));
    r.skills.set(specialty, static_cast<u8>(std::min(10, r.skills.get(specialty) + rng.rangeI(2, 4))));

    r.appearance.skinTone    = static_cast<u8>(rng.rangeI(0, 5));
    r.appearance.hairStyle   = static_cast<u8>(rng.rangeI(0, 5));
    r.appearance.hairTone    = static_cast<u8>(rng.rangeI(0, 5));
    r.appearance.faceVariant = static_cast<u8>(rng.rangeI(0, 7));
    r.appearance.outfitTint  = static_cast<u8>(rng.rangeI(0, 3));
    r.appearance.height = rng.range(0.92f, 1.08f);
    r.appearance.build  = rng.range(0.88f, 1.14f);

    r.maxHealth = 90.0f + static_cast<f32>(r.skills.get(Skill::Security)) * 3.0f;
    if (r.hasTrait(Trait_Fragile)) r.maxHealth *= 0.85f;
    r.health = r.maxHealth;
    r.happiness = rng.range(55.0f, 80.0f);
    r.energy = rng.range(70.0f, 100.0f);
    r.hunger = rng.range(0.0f, 25.0f);
    r.thirst = rng.range(0.0f, 25.0f);
    r.lastMealTime = gameTimeHours;
    r.outfit = ItemStack{itemIdByName("Shelter Coveralls"), 1, 1.0f};
    return r;
}

Resident makeChild(Rng& rng, ResidentId id, const Resident& a, const Resident& b) {
    Resident r = makeResident(rng, id, 0.0f, 0);
    r.age = 18.0f;   // children mature off-screen and join the workforce as adults
    r.name = randomGivenName(rng, r.female) + " " +
             (rng.chance(0.5f) ? familyOf(a) : familyOf(b));
    for (int i = 0; i < kSkillCount; ++i) {
        const Skill s = static_cast<Skill>(i);
        const f32 inherited = 0.5f * (static_cast<f32>(a.skills.get(s)) + static_cast<f32>(b.skills.get(s)));
        const i32 v = std::clamp(static_cast<i32>(std::round(inherited + rng.gaussian(0.4f, 1.1f))), 1, 10);
        r.skills.set(s, static_cast<u8>(v));
    }
    // Appearance draws from both parents so families look related.
    r.appearance.skinTone = rng.chance(0.5f) ? a.appearance.skinTone : b.appearance.skinTone;
    r.appearance.hairTone = rng.chance(0.5f) ? a.appearance.hairTone : b.appearance.hairTone;
    r.appearance.height = clampf(0.5f * (a.appearance.height + b.appearance.height) +
                                 rng.range(-0.04f, 0.04f), 0.9f, 1.1f);
    r.maxHealth = 90.0f + static_cast<f32>(r.skills.get(Skill::Security)) * 3.0f;
    r.health = r.maxHealth;
    r.happiness = 70.0f;
    return r;
}

} // namespace hv::sim
