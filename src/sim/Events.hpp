#pragma once
// Random events and emergencies. Emergencies live in a room, escalate if
// ignored, and are resolved by residents physically going there.
#include "sim/Shelter.hpp"
#include "gameplay/Combat.hpp"
#include "core/Random.hpp"
#include "core/Serialization.hpp"
#include <string>
#include <vector>

namespace hv::sim {

class World;

enum class EventKind : u8 {
    Fire = 0,          ///< spreads to neighbouring rooms if unattended
    Breakdown,         ///< machinery stops until repaired
    Infestation,       ///< creatures burrow in from the rock
    Intrusion,         ///< humans force the airlock
    PowerSurge,        ///< damages a room, briefly cuts power
    Contamination,     ///< water or food spoils, radiation rises
    Illness,           ///< residents lose health over time
    Newcomer,          ///< someone at the door asking to be let in
    Trader,            ///< temporary buy/sell opportunity
    Discovery,         ///< a new surface site appears on the map
    Count
};
const char* eventKindName(EventKind k);
/// True when the event needs residents to physically respond.
bool eventIsEmergency(EventKind k);

enum class NotifySeverity : u8 { Info = 0, Good, Warning, Critical };

struct Notification {
    std::string text;
    NotifySeverity severity = NotifySeverity::Info;
    f32 gameTime = 0.0f;
    f32 age = 0.0f;
    u32 roomId = kNoRoom;     ///< clicking the toast focuses this room
};

struct Emergency {
    u32 id = 0;
    EventKind kind = EventKind::Fire;
    RoomId room = kNoRoom;
    f32 severity = 1.0f;      ///< 0..3; fires grow, repairs shrink
    f32 elapsed = 0.0f;
    f32 spreadTimer = 0.0f;
    bool resolved = false;
    /// Combat-flavoured emergencies carry a live encounter.
    gameplay::Encounter fight;
    std::vector<ResidentId> responders;
    bool hostile() const {
        return kind == EventKind::Intrusion || kind == EventKind::Infestation;
    }
};

/// Trader offers are simple: a fixed basket at a discount, for a while.
struct TraderOffer {
    bool active = false;
    f32  timeLeft = 0.0f;
    Resource sells = Resource::Materials;
    f32  sellAmount = 100.0f;
    f32  sellPrice = 90.0f;      ///< scrip
    Resource buys = Resource::Water;
    f32  buyAmount = 100.0f;
    f32  buyPrice = 70.0f;
    u16  itemForSale = 0;
    f32  itemPrice = 0.0f;
};

class EventSystem {
public:
    void reset();
    /// Decides whether to fire a new event this tick.
    void tick(f32 dtSeconds, World& world);

    /// Forces an event; used by quests, by the debug menu and by tests.
    u32  trigger(EventKind kind, World& world, RoomId room = kNoRoom, f32 severity = 1.0f);

    std::vector<Emergency>& emergencies() { return emergencies_; }
    const std::vector<Emergency>& emergencies() const { return emergencies_; }
    Emergency* find(u32 id);
    Emergency* forRoom(RoomId room);
    size_t activeCount() const;

    TraderOffer& trader() { return trader_; }
    const TraderOffer& trader() const { return trader_; }

    f32 nextEventIn() const { return nextEventTimer_; }
    void setDifficulty(f32 d) { difficulty_ = clampf(d, 0.25f, 3.0f); }

    void serialize(BlobWriter& w) const;
    bool deserialize(BlobReader& r, u32 version);

private:
    void tickEmergency(Emergency& e, f32 dt, World& world);
    EventKind rollEventKind(World& world);

    std::vector<Emergency> emergencies_;
    TraderOffer trader_;
    u32 nextId_ = 1;
    f32 nextEventTimer_ = 90.0f;
    f32 difficulty_ = 1.0f;
    f32 sinceStart_ = 0.0f;
};

} // namespace hv::sim
