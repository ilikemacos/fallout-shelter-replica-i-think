"""Core game state and simulation for Haven.

Everything the save file needs to reconstruct lives on GameState.
"""

from __future__ import annotations
import math
import random
import time
from dataclasses import dataclass, field
from typing import Optional

from . import config as C
from . import data as D
from . import audio as A


# --------------------- Items ---------------------
@dataclass
class Item:
    kind: str          # 'weapon' | 'outfit' | 'pa' | 'consumable' | 'material'
    name: str
    rarity: int = 0
    dmg_min: int = 0
    dmg_max: int = 0
    armor: int = 0
    dr: int = 0
    stat_bonus: dict = field(default_factory=dict)
    durability: int = 0
    max_durability: int = 0
    value: int = 0
    level_req: int = 0
    desc: str = ""

    def to_dict(self):
        return self.__dict__.copy()

    @staticmethod
    def from_dict(d):
        return Item(**d)


def make_weapon(name: str) -> Item:
    for n, mn, mx, r, v in D.WEAPONS:
        if n == name:
            return Item("weapon", n, r, dmg_min=mn, dmg_max=mx, value=v,
                        desc=f"Weapon (dmg {mn}-{mx})")
    raise KeyError(name)


def make_outfit(name: str) -> Item:
    for n, sb, ar, r, v in D.OUTFITS:
        if n == name:
            return Item("outfit", n, r, armor=ar, stat_bonus=dict(sb), value=v,
                        desc=f"Outfit (armor +{ar})")
    raise KeyError(name)


def make_power_armor(name: str) -> Item:
    for n, ar, dr, sb, r, dur, rep, lv, desc in D.POWER_ARMOR:
        if n == name:
            return Item("pa", n, r, armor=ar, dr=dr, stat_bonus=dict(sb),
                        durability=dur, max_durability=dur, value=rep * 10,
                        level_req=lv, desc=desc)
    raise KeyError(name)


def make_consumable(name: str) -> Item:
    tbl = {
        "Stimpack": ("consumable", "Stimpack", 1, "Restores 40 HP.", 40),
        "RadAway":  ("consumable", "RadAway", 1, "Cures ailments and heals 20.", 30),
    }
    if name in tbl:
        k, n, r, d, v = tbl[name]
        return Item(k, n, r, value=v, desc=d)
    return Item("material", name, 0, value=1, desc="Crafting material.")


# --------------------- Resident ---------------------
@dataclass
class Resident:
    id: int
    name: str
    portrait_seed: int
    level: int = 1
    xp: int = 0
    hp: int = 40
    max_hp: int = 40
    happiness: float = 60.0
    stats: dict = field(default_factory=lambda: {k: 1 for k in D.STAT_KEYS})
    # Equipment (indices into inventory list; store item dicts inline for simplicity)
    weapon: Optional[dict] = None
    outfit: Optional[dict] = None
    power_armor: Optional[dict] = None
    # World position
    floor: int = 0
    x: float = 0.0
    dest_floor: int = 0
    dest_x: float = 0.0
    # Movement waypoints: list of (floor, x)
    path: list = field(default_factory=list)
    step: int = 0
    facing: int = 1
    # Assignment
    assigned_room: Optional[int] = None
    activity: str = "idle"           # 'idle'|'work'|'walk'|'train'|'sleep'|'heal'|'fight'|'expedition'
    # Radiation/status
    rad: float = 0.0
    # Expedition
    on_expedition: bool = False

    def stat_total(self, k: str) -> int:
        base = self.stats.get(k, 1)
        b = 0
        for e in (self.outfit, self.power_armor):
            if e:
                b += e.get("stat_bonus", {}).get(k, 0)
        return base + b

    def armor_total(self) -> int:
        a = 0
        if self.outfit: a += self.outfit.get("armor", 0)
        if self.power_armor: a += self.power_armor.get("armor", 0)
        return a

    def dr_total(self) -> int:
        d = 0
        if self.power_armor: d += self.power_armor.get("dr", 0)
        return d

    def weapon_damage(self) -> tuple[int, int]:
        if self.weapon:
            return self.weapon.get("dmg_min", 1), self.weapon.get("dmg_max", 2)
        return 1, 2

    def xp_needed(self):
        return int(60 * (self.level ** 1.2))

    def grant_xp(self, x):
        self.xp += int(x)
        leveled = False
        while self.xp >= self.xp_needed():
            self.xp -= self.xp_needed()
            self.level += 1
            self.max_hp += 4
            self.hp = min(self.max_hp, self.hp + 8)
            leveled = True
        return leveled


# --------------------- Rooms ---------------------
@dataclass
class Room:
    id: int
    key: str
    floor: int
    x: int             # leftmost column
    width: int         # cells
    level: int = 1
    # Simulation
    progress: float = 0.0        # 0..1 to next production cycle
    workers: list = field(default_factory=list)   # resident IDs
    # Damage/state
    hp: float = 100.0
    on_fire: bool = False
    invaders: list = field(default_factory=list)  # list of enemy dicts

    def data(self):
        return D.ROOMS[self.key]

    def capacity(self) -> int:
        return self.data().get("capacity", 0) * self.width_units()

    def width_units(self) -> int:
        # merged rooms boost multiplier by their cell width
        return max(1, self.width // max(1, self.data().get("width", 2)))

    def storage_bonus(self) -> int:
        if self.key == "storage":
            return 100 * self.level * self.width_units()
        return 0

    def housing(self) -> int:
        if self.key == "living":
            return 4 * self.level * self.width_units()
        return 0

    def produces(self):
        return self.data().get("produces", {})

    def consumes(self):
        return self.data().get("consumes", {})


# --------------------- GameState ---------------------
class GameState:
    def __init__(self, seed: int | None = None):
        self.seed = seed if seed is not None else random.randint(1, 10**9)
        self.rng = random.Random(self.seed)
        self.time = 0.0
        self.paused = False
        self.speed = 1.0
        self.caps = C.STARTING_CAPS
        self.resources = {"power": 100, "water": 100, "food": 100,
                          "materials": 15, "stim": 3, "radaway": 1}
        self.storage_cap = {"power": 400, "water": 400, "food": 400,
                            "materials": 200, "stim": 20, "radaway": 20}
        self.rooms: dict[int, Room] = {}
        self.residents: dict[int, Resident] = {}
        self._next_room_id = 1
        self._next_resident_id = 1
        self.inventory: list[dict] = []           # unequipped items
        self.pa_storage: list[dict] = []          # spare power armor suits
        self.objectives_done = set()
        self.objectives_progress: dict[str, int] = {}
        self.kills = 0
        self.expeditions_completed = 0
        self.pa_found = 0
        self.trainings_completed = 0
        self.caps_earned = 0
        # UI-visible notification log
        self.notifications: list[tuple[float, str, str]] = []  # (time, level, text)
        # Active incidents
        self.incidents: list[dict] = []
        # Active expeditions
        self.expeditions: list[dict] = []
        # Random event cooldown
        self.next_incident_at = 90.0
        self.next_wanderer_at = 60.0
        # Elevator column
        self.elevator_col = C.COLUMNS // 2

        self._bootstrap()

    # ---- bootstrap ----
    def _bootstrap(self):
        # Give the player a vault door row + one built row underneath
        # Build the elevator on floors 0..2 automatically
        for f in range(3):
            self._place_room("elevator", floor=f, x=self.elevator_col, width=1, free=True)
        # A starter power generator, water, diner and living quarters
        self._place_room("power",  floor=1, x=self.elevator_col - 4, width=2, free=True)
        self._place_room("water",  floor=1, x=self.elevator_col - 2, width=2, free=True)
        self._place_room("diner",  floor=2, x=self.elevator_col - 4, width=2, free=True)
        self._place_room("living", floor=2, x=self.elevator_col + 1, width=2, free=True)
        # Starting residents
        for _ in range(C.STARTING_POP):
            self.spawn_resident()

    # ---- notifications ----
    def notify(self, text: str, level: str = "info"):
        self.notifications.append((self.time, level, text))
        # keep last 60
        self.notifications = self.notifications[-60:]

    # ---- rooms ----
    def can_place(self, key: str, floor: int, x: int, width: int) -> tuple[bool, str]:
        rdata = D.ROOMS.get(key)
        if not rdata:
            return False, "Unknown room"
        if key == "elevator":
            if width != 1: return False, "Elevator is 1 wide"
            if x != self.elevator_col: return False, "Elevator column only"
            # elevator must connect to another elevator immediately above or floor 0
            if floor > 0 and not any(r.key == "elevator" and r.floor == floor - 1 for r in self.rooms.values()):
                return False, "Extend from an elevator above"
        else:
            if width < rdata["width"]:
                return False, f"Min width {rdata['width']}"
            if width > rdata["width"] * C.ROOM_MAX_MERGE:
                return False, "Cannot merge that wide"
        if floor < 0 or floor >= C.FLOOR_COUNT:
            return False, "Off shelter"
        if x < 0 or x + width > C.COLUMNS:
            return False, "Off shelter"
        # cells must be empty AND no overlap with elevator column (except elevator itself)
        for cx in range(x, x + width):
            if key != "elevator" and cx == self.elevator_col:
                return False, "Blocks elevator"
            for r in self.rooms.values():
                if r.floor != floor: continue
                if cx >= r.x and cx < r.x + r.width:
                    return False, "Overlaps existing room"
        # must have an elevator on this floor (any column) or be floor 0
        has_elev = any(r.key == "elevator" and r.floor == floor for r in self.rooms.values())
        if key != "elevator" and not has_elev:
            return False, "Needs an elevator on this floor"
        # Must be supported (something on floor above OR floor 0)
        if floor > 0:
            supported = False
            for r in self.rooms.values():
                if r.floor == floor - 1 and r.x < x + width and r.x + r.width > x:
                    supported = True
                    break
            if not supported:
                return False, "Must connect below an existing room"
        return True, ""

    def _place_room(self, key: str, floor: int, x: int, width: int, free: bool = False) -> Optional[Room]:
        rdata = D.ROOMS[key]
        if not free:
            cost = rdata["cost"]
            if self.caps < cost:
                self.notify("Not enough caps.", "bad"); return None
            ok, why = self.can_place(key, floor, x, width)
            if not ok:
                self.notify(f"Cannot build: {why}", "bad"); return None
            self.caps -= cost
        r = Room(id=self._next_room_id, key=key, floor=floor, x=x, width=width)
        self._next_room_id += 1
        self.rooms[r.id] = r
        if key == "storage":
            for res in ("power", "water", "food", "materials"):
                self.storage_cap[res] += 100
        self.notify(f"Built {rdata['name']}", "good" if not free else "info")
        if not free: A.play("build")
        self._track_objective("build_room", room=key)
        return r

    def place_room(self, key: str, floor: int, x: int, width: int) -> Optional[Room]:
        return self._place_room(key, floor, x, width, free=False)

    def upgrade_room(self, room_id: int):
        r = self.rooms.get(room_id)
        if not r: return
        cost = r.data().get("upgrade", 0) * r.level * r.width_units()
        if cost <= 0:
            self.notify("This room cannot be upgraded.", "bad"); return
        if r.level >= 3:
            self.notify("Already at max level.", "bad"); return
        if self.caps < cost:
            self.notify("Not enough caps.", "bad"); return
        self.caps -= cost
        r.level += 1
        if r.key == "storage":
            for res in ("power", "water", "food", "materials"):
                self.storage_cap[res] += 100
        self.notify(f"Upgraded {r.data()['name']} to level {r.level}", "good")
        A.play("upgrade")

    def destroy_room(self, room_id: int):
        r = self.rooms.pop(room_id, None)
        if not r: return
        # unassign residents
        for res in self.residents.values():
            if res.assigned_room == room_id:
                res.assigned_room = None
                res.activity = "idle"
        # elevator floor above must go too if this leaves them unsupported? keep simple
        if r.key == "storage":
            for res in ("power", "water", "food", "materials"):
                self.storage_cap[res] = max(200, self.storage_cap[res] - 100)
        self.notify(f"Destroyed {r.data()['name']}", "warn")

    # ---- merging: if a compatible neighbour of same key/level exists on same floor, merge sizes ----
    def try_merge(self, room_id: int):
        r = self.rooms.get(room_id)
        if not r: return False
        if r.key == "elevator": return False
        base_w = r.data()["width"]
        for other in list(self.rooms.values()):
            if other.id == r.id: continue
            if other.key != r.key or other.level != r.level or other.floor != r.floor: continue
            if other.width + r.width > base_w * C.ROOM_MAX_MERGE: continue
            # adjacency
            if other.x == r.x + r.width:
                r.width += other.width
                self.rooms.pop(other.id)
                self.notify(f"Merged {r.data()['name']}", "good")
                return True
            if other.x + other.width == r.x:
                r.x = other.x
                r.width += other.width
                self.rooms.pop(other.id)
                self.notify(f"Merged {r.data()['name']}", "good")
                return True
        return False

    # ---- residents ----
    def spawn_resident(self, name: str | None = None) -> Resident:
        if len(self.residents) >= self.housing_cap():
            self.notify("Not enough living quarters.", "warn"); return None
        first = self.rng.choice(D.FIRST_NAMES)
        last = self.rng.choice(D.LAST_NAMES)
        n = name or f"{first} {last}"
        r = Resident(
            id=self._next_resident_id,
            name=n,
            portrait_seed=self.rng.randint(1, 10**9),
            stats={k: self.rng.randint(1, 3) for k in D.STAT_KEYS},
        )
        r.floor = 0
        r.x = float(self.elevator_col)
        r.dest_x = r.x
        r.dest_floor = r.floor
        # give a jumpsuit
        r.outfit = make_outfit("Jumpsuit").to_dict()
        self._next_resident_id += 1
        self.residents[r.id] = r
        self.notify(f"{r.name} joined the shelter.", "good")
        A.play("cash")
        self._track_objective("population", n=len(self.residents))
        return r

    def housing_cap(self) -> int:
        return C.STARTING_POP + sum(r.housing() for r in self.rooms.values())

    def assign(self, res_id: int, room_id: int | None):
        r = self.residents.get(res_id)
        if not r: return
        # remove from previous
        if r.assigned_room:
            prev = self.rooms.get(r.assigned_room)
            if prev and res_id in prev.workers:
                prev.workers.remove(res_id)
        r.assigned_room = room_id
        if room_id:
            room = self.rooms.get(room_id)
            if not room: return
            if len(room.workers) >= room.capacity():
                self.notify("Room is full.", "bad")
                r.assigned_room = None
                return
            room.workers.append(res_id)
            self.walk_to_room(r, room)
        else:
            r.activity = "idle"

    def walk_to_room(self, res: Resident, room: Room):
        # target: middle of the room
        tx = room.x + room.width / 2
        res.dest_floor = room.floor
        res.dest_x = tx
        # path: current floor -> elevator col; elevator to target floor; walk to tx
        res.path = []
        if res.floor != room.floor:
            res.path.append((res.floor, self.elevator_col))
            res.path.append((room.floor, self.elevator_col))
        res.path.append((room.floor, tx))
        res.activity = "walk"

    def find_room(self, floor: int, x: float) -> Optional[Room]:
        for r in self.rooms.values():
            if r.floor == floor and r.x <= x < r.x + r.width:
                return r
        return None

    # ---- inventory ----
    def add_item(self, item: Item | dict):
        if isinstance(item, Item):
            item = item.to_dict()
        if item["kind"] == "pa":
            self.pa_storage.append(item)
            self.pa_found += 1
            self._track_objective("find_pa", n=self.pa_found)
            self.notify(f"Recovered Power Armor: {item['name']}", "good")
            A.play("upgrade")
        else:
            self.inventory.append(item)

    def sell_item(self, index: int):
        if 0 <= index < len(self.inventory):
            it = self.inventory.pop(index)
            price = max(1, it.get("value", 1) // 2)
            self.caps += price
            self.notify(f"Sold {it['name']} for {price} caps.", "good")
            A.play("cash")

    def scrap_item(self, index: int):
        if 0 <= index < len(self.inventory):
            it = self.inventory.pop(index)
            mats = max(1, (it.get("rarity", 0) + 1) * 2)
            self.resources["materials"] = min(self.storage_cap["materials"],
                                              self.resources.get("materials", 0) + mats)
            self.notify(f"Scrapped {it['name']} for {mats} materials.", "info")

    def equip(self, res_id: int, slot: str, index: int):
        res = self.residents.get(res_id)
        if not res: return
        if slot == "pa":
            if not (0 <= index < len(self.pa_storage)): return
            item = self.pa_storage.pop(index)
            if res.power_armor:
                self.pa_storage.append(res.power_armor)
            res.power_armor = item
            A.play("upgrade")
            return
        if not (0 <= index < len(self.inventory)): return
        it = self.inventory[index]
        if it["kind"] == "weapon" and slot == "weapon":
            self.inventory.pop(index)
            if res.weapon: self.inventory.append(res.weapon)
            res.weapon = it
        elif it["kind"] == "outfit" and slot == "outfit":
            self.inventory.pop(index)
            if res.outfit: self.inventory.append(res.outfit)
            res.outfit = it
        A.play("click")

    def unequip(self, res_id: int, slot: str):
        res = self.residents.get(res_id)
        if not res: return
        item = getattr(res, slot, None)
        if not item: return
        if slot == "power_armor":
            self.pa_storage.append(item)
            res.power_armor = None
        else:
            self.inventory.append(item)
            setattr(res, slot, None)
        A.play("click")

    def use_stimpack(self, res_id: int):
        res = self.residents.get(res_id)
        if not res: return
        if self.resources.get("stim", 0) < 1:
            self.notify("No stimpacks available.", "bad"); return
        self.resources["stim"] -= 1
        res.hp = min(res.max_hp, res.hp + 40)
        A.play("heal")
        self.notify(f"{res.name} used a Stimpack.", "good")

    # ---- crafting ----
    def craft(self, kind: str, rarity: int) -> Optional[Item]:
        cost = {0: 5, 1: 12, 2: 25, 3: 60, 4: 140}.get(rarity, 5)
        caps_cost = {0: 20, 1: 60, 2: 150, 3: 400, 4: 900}.get(rarity, 20)
        if self.resources.get("materials", 0) < cost:
            self.notify("Need more materials.", "bad"); return None
        if self.caps < caps_cost:
            self.notify("Need more caps.", "bad"); return None
        # Rarity gates on rooms
        if rarity >= 2 and not any(r.key == "workshop" for r in self.rooms.values()):
            self.notify("Need a Workshop for rarity 2+.", "bad"); return None
        if rarity >= 3 and not any(r.key == "armory" for r in self.rooms.values()) and kind == "weapon":
            self.notify("Need an Armory for rare weapons.", "bad"); return None
        if rarity >= 3 and not any(r.key == "science" for r in self.rooms.values()) and kind != "weapon":
            self.notify("Need a Science Lab for rare items.", "bad"); return None
        self.resources["materials"] -= cost
        self.caps -= caps_cost
        pool = (D.WEAPONS if kind == "weapon" else D.OUTFITS)
        options = [x for x in pool if x[-2] == rarity]
        if not options:
            options = pool
        chosen = self.rng.choice(options)
        if kind == "weapon":
            it = make_weapon(chosen[0])
        else:
            it = make_outfit(chosen[0])
        self.add_item(it)
        self.notify(f"Crafted {it.name}.", "good")
        A.play("upgrade")
        return it

    # ---- production & sim ----
    def tick(self, dt: float):
        if self.paused: return
        dt *= self.speed
        self.time += dt

        # Move residents
        self._sim_movement(dt)
        # Room production
        self._sim_production(dt)
        # Combat / incidents
        self._sim_incidents(dt)
        # Expeditions
        self._sim_expeditions(dt)
        # Happiness drift + healing
        self._sim_wellbeing(dt)
        # Random events
        self._sim_random_events(dt)

    def _sim_movement(self, dt):
        for res in self.residents.values():
            if res.on_expedition: continue
            if not res.path:
                if res.activity == "walk":
                    # arrived
                    if res.assigned_room:
                        res.activity = "work"
                    else:
                        res.activity = "idle"
                continue
            target_floor, tx = res.path[0]
            # First: match floor (via elevator column)
            if res.floor != target_floor:
                # walk to elevator col first
                if abs(res.x - self.elevator_col) > 0.05:
                    dx = (self.elevator_col - res.x)
                    speed = 3.0
                    step = max(-speed * dt, min(speed * dt, dx))
                    res.x += step
                    res.facing = 1 if step > 0 else -1
                    res.step = int((self.time * 6) % 2)
                else:
                    # take elevator
                    if res.floor < target_floor:
                        res.floor += min(1, (target_floor - res.floor))
                    else:
                        res.floor -= 1
                continue
            # same floor: walk to tx
            if abs(res.x - tx) < 0.05:
                res.path.pop(0)
                if not res.path and res.assigned_room:
                    res.activity = "work"
                elif not res.path:
                    res.activity = "idle"
                continue
            dx = (tx - res.x)
            speed = 3.0
            step = max(-speed * dt, min(speed * dt, dx))
            res.x += step
            res.facing = 1 if step > 0 else -1
            res.step = int((self.time * 6) % 2)

    def _sim_production(self, dt):
        for r in self.rooms.values():
            if r.key == "elevator" or r.key == "storage":
                continue
            if not r.workers:
                continue
            # Some rooms need power to operate
            if r.data().get("requires_power") and self.resources.get("power", 0) <= 0:
                continue
            # Consume inputs — pro-rated per second based on production cycle
            cycle = max(1.0, C.PRODUCTION_INTERVAL / max(1, r.level * r.width_units()))
            r.progress += dt / cycle
            # Training rooms
            if "train_stat" in r.data():
                if r.progress >= 1.0:
                    r.progress = 0
                    for wid in list(r.workers):
                        res = self.residents.get(wid)
                        if not res: continue
                        stat = r.data()["train_stat"]
                        cap = 10
                        if res.stats[stat] < cap and self.rng.random() < 0.35 + 0.1 * r.level:
                            res.stats[stat] += 1
                            self.trainings_completed += 1
                            self._track_objective("train", n=self.trainings_completed)
                            self.notify(f"{res.name} improved {D.STAT_NAMES[stat]}.", "good")
                continue
            # Standard production
            if r.progress >= 1.0:
                r.progress = 0
                # Compute effective staff bonus
                staff_stat = r.data().get("staff_stat")
                bonus_mult = 1.0
                if staff_stat:
                    total = sum(self.residents[w].stat_total(staff_stat) for w in r.workers if w in self.residents)
                    bonus_mult = 1.0 + total * 0.05
                # consume
                affordable = True
                for k, v in r.consumes().items():
                    need = v * r.level * r.width_units()
                    if self.resources.get(k, 0) < need:
                        affordable = False
                        break
                if not affordable:
                    continue
                for k, v in r.consumes().items():
                    need = v * r.level * r.width_units()
                    self.resources[k] = max(0, self.resources[k] - need)
                for k, v in r.produces().items():
                    amt = int(v * r.level * r.width_units() * bonus_mult)
                    if k == "attract":
                        # Radio room draws wanderers
                        self.next_wanderer_at = max(0.0, self.next_wanderer_at - 8.0)
                        continue
                    if k == "happy":
                        for res in self.residents.values():
                            res.happiness = min(100.0, res.happiness + amt * 0.6)
                        continue
                    if k == "caps":
                        self.caps += amt
                        self.caps_earned += amt
                        continue
                    cap = self.storage_cap.get(k, 400)
                    self.resources[k] = min(cap, self.resources.get(k, 0) + amt)
                # XP for workers
                for wid in r.workers:
                    res = self.residents.get(wid)
                    if res: res.grant_xp(2 + r.level)

    def _sim_wellbeing(self, dt):
        # Food/water consumption per resident per minute
        needed_food = max(0.5, len(self.residents) * dt / 8.0)
        needed_water = max(0.5, len(self.residents) * dt / 8.0)
        happy_delta = 0.0
        food_ok = self.resources.get("food", 0) >= needed_food
        water_ok = self.resources.get("water", 0) >= needed_water
        power_ok = self.resources.get("power", 0) > 0
        if food_ok:
            self.resources["food"] -= needed_food
            happy_delta += C.HAPPY_DRIFT_PER_MIN * dt / 60
        else:
            happy_delta -= 3.0 * dt / 60
        if water_ok:
            self.resources["water"] -= needed_water
            happy_delta += C.HAPPY_DRIFT_PER_MIN * dt / 60
        else:
            happy_delta -= 3.0 * dt / 60
        if not power_ok:
            happy_delta -= 2.0 * dt / 60

        for res in self.residents.values():
            if res.on_expedition: continue
            res.happiness = max(0.0, min(100.0, res.happiness + happy_delta))
            # Slow heal in medbay
            room = self.find_room(res.floor, res.x)
            if room and room.key == "medbay":
                res.hp = min(res.max_hp, res.hp + 3 * dt)
            # Very slow passive heal
            res.hp = min(res.max_hp, res.hp + 0.2 * dt)

    def _sim_incidents(self, dt):
        for inc in list(self.incidents):
            inc["timer"] -= dt
            r = self.rooms.get(inc["room_id"])
            if not r:
                self.incidents.remove(inc); continue
            # Defenders (workers or nearest residents) fight
            if inc["kind"] == "invaders":
                # spawn 2-4 enemies if not yet
                if not r.invaders:
                    n = self.rng.randint(2, 4)
                    r.invaders = [dict(enemy=self.rng.choice(D.ENEMIES[:5]),
                                       hp=self.rng.choice(D.ENEMIES[:5])[1],
                                       t=0.0) for _ in range(n)]
                # find defenders in the room
                defenders = [self.residents[wid] for wid in list(r.workers) if wid in self.residents]
                # nearest resident helper
                if len(defenders) < 2:
                    others = sorted(
                        [res for res in self.residents.values()
                         if res.assigned_room is None and not res.on_expedition],
                        key=lambda x: (abs(x.floor - r.floor) * 4 + abs(x.x - (r.x + r.width / 2)))
                    )
                    for o in others[:2]:
                        if o.id not in r.workers:
                            self.walk_to_room(o, r)
                    defenders += others[:2]
                # combat tick
                self._combat_tick(dt, defenders, r.invaders)
                # dead invaders
                r.invaders = [e for e in r.invaders if e["hp"] > 0]
                if not r.invaders:
                    self.notify(f"Intrusion in {r.data()['name']} repelled.", "good")
                    self.incidents.remove(inc)
                    continue
            elif inc["kind"] == "fire":
                r.hp = max(0, r.hp - 3 * dt)
                # workers try to fight
                helpers = [self.residents[w] for w in r.workers if w in self.residents]
                for h in helpers:
                    r.hp = min(100, r.hp + 4 * dt * (1 + 0.05 * h.stat_total("S")))
                if r.hp >= 95:
                    r.on_fire = False
                    self.notify(f"Fire in {r.data()['name']} extinguished.", "good")
                    self.incidents.remove(inc); continue
            elif inc["kind"] == "critters":
                # small hp damage over time until helpers arrive
                r.hp = max(0, r.hp - 2 * dt)
                helpers = [self.residents[w] for w in r.workers if w in self.residents]
                for h in helpers:
                    r.hp = min(100, r.hp + 3 * dt)
                if r.hp >= 95:
                    self.notify(f"Infestation in {r.data()['name']} cleared.", "good")
                    self.incidents.remove(inc); continue
            elif inc["kind"] == "failure":
                r.hp = max(0, r.hp - 1.5 * dt)
                helpers = [self.residents[w] for w in r.workers if w in self.residents]
                for h in helpers:
                    r.hp = min(100, r.hp + 4 * dt * (1 + 0.05 * h.stat_total("I")))
                if r.hp >= 95:
                    self.notify(f"Equipment repaired in {r.data()['name']}.", "good")
                    self.incidents.remove(inc); continue
            if inc["timer"] <= 0 and inc in self.incidents:
                self.incidents.remove(inc)

    def _combat_tick(self, dt, defenders, invaders):
        # Each combatant fires every ~1s scaled by agility
        for d in defenders:
            if d.hp <= 0: continue
            d.step = int((self.time * 8) % 2)
            d.activity = "fight"
            rate = 1.0 + 0.05 * d.stat_total("A")
            d._atk = getattr(d, "_atk", 0.0) + dt * rate
            if d._atk >= 1.0:
                d._atk = 0
                if invaders:
                    e = self.rng.choice(invaders)
                    mn, mx = d.weapon_damage()
                    dmg = self.rng.randint(mn, mx) + d.stat_total("P") // 2
                    crit = self.rng.random() < 0.03 + 0.01 * d.stat_total("L")
                    if crit: dmg = int(dmg * 2)
                    e["hp"] -= dmg
                    A.play("shot")
                    if e["hp"] <= 0:
                        self.kills += 1
                        d.grant_xp(e["enemy"][4])
                        self.caps += e["enemy"][5]
                        self.caps_earned += e["enemy"][5]
                        self._track_objective("kills", n=self.kills)
        for e in invaders:
            if e["hp"] <= 0: continue
            e["t"] += dt
            if e["t"] >= 1.0:
                e["t"] = 0
                targets = [x for x in defenders if x.hp > 0]
                if not targets: return
                t = self.rng.choice(targets)
                mn, mx = e["enemy"][2], e["enemy"][3]
                dmg = self.rng.randint(mn, mx)
                dmg = max(1, dmg - t.armor_total())
                dmg = int(dmg * (1 - t.dr_total() / 100.0))
                t.hp = max(0, t.hp - dmg)
                A.play("hurt")
                if t.hp <= 0:
                    self.notify(f"{t.name} was knocked down!", "bad")
                    A.play("death")
                    t.hp = 1  # revive after fight; no perma-death
                # Damage power armor durability
                if t.power_armor:
                    t.power_armor["durability"] = max(0, t.power_armor.get("durability", 0) - 1)

    def repair_pa(self, res_id: int):
        r = self.residents.get(res_id)
        if not r or not r.power_armor:
            self.notify("No power armor equipped.", "bad"); return
        cost = 5 + (r.power_armor.get("max_durability", 100) - r.power_armor.get("durability", 0)) * 2
        if self.caps < cost:
            self.notify(f"Repair costs {cost} caps.", "bad"); return
        if self.resources.get("materials", 0) < 3:
            self.notify("Repair needs 3 materials.", "bad"); return
        self.caps -= cost
        self.resources["materials"] -= 3
        r.power_armor["durability"] = r.power_armor["max_durability"]
        self.notify(f"Repaired {r.power_armor['name']}.", "good")
        A.play("upgrade")

    def _sim_expeditions(self, dt):
        for e in list(self.expeditions):
            e["elapsed"] += dt
            # tick events every ~10-20s of expedition time
            if e["elapsed"] >= e["next_event"] and e["elapsed"] < e["duration"]:
                e["next_event"] += self.rng.uniform(12, 24)
                self._roll_expedition_event(e)
            if e["elapsed"] >= e["duration"]:
                # return home
                res = self.residents.get(e["resident_id"])
                if res:
                    res.on_expedition = False
                    res.floor = 0
                    res.x = float(self.elevator_col)
                    res.activity = "idle"
                    if res.assigned_room:
                        room = self.rooms.get(res.assigned_room)
                        if room:
                            self.walk_to_room(res, room)
                # Grant rewards
                self.caps += e["caps"]
                self.caps_earned += e["caps"]
                for k, v in e["resources"].items():
                    cap = self.storage_cap.get(k, 400)
                    self.resources[k] = min(cap, self.resources.get(k, 0) + v)
                for it in e["items"]:
                    self.add_item(it)
                self.notify(f"{res.name if res else 'A resident'} returned from expedition "
                            f"with {e['caps']} caps and {len(e['items'])} items.", "good")
                self.expeditions_completed += 1
                self._track_objective("expeditions", n=self.expeditions_completed)
                A.play("expedition")
                self.expeditions.remove(e)

    def _roll_expedition_event(self, e: dict):
        res = self.residents.get(e["resident_id"])
        if not res: return
        ev = self.rng.choice(D.EXPLORATION_EVENTS)
        log = ev["text"]
        if ev["kind"].startswith("loot"):
            reward = ev.get("reward", {})
            if ev["kind"] == "loot":
                for k, span in reward.items():
                    v = self.rng.randint(*span)
                    if k == "caps":
                        e["caps"] += v
                    elif k == "xp":
                        res.grant_xp(v)
                    else:
                        e["resources"][k] = e["resources"].get(k, 0) + v
            elif ev["kind"] == "loot_weapon":
                lo = reward.get("weapon_min_rarity", 0)
                hi = reward.get("weapon_max_rarity", 2)
                pool = [w for w in D.WEAPONS if lo <= w[-2] <= hi]
                if pool:
                    w = self.rng.choice(pool)
                    e["items"].append(make_weapon(w[0]).to_dict())
            elif ev["kind"] == "loot_outfit":
                lo = reward.get("outfit_min_rarity", 0)
                hi = reward.get("outfit_max_rarity", 2)
                pool = [w for w in D.OUTFITS if lo <= w[-2] <= hi]
                if pool:
                    w = self.rng.choice(pool)
                    e["items"].append(make_outfit(w[0]).to_dict())
            elif ev["kind"] == "loot_pa":
                if self.rng.random() < reward.get("pa_chance", 0.35):
                    pa = self.rng.choice(D.POWER_ARMOR)
                    e["items"].append(make_power_armor(pa[0]).to_dict())
                    log += f" It's a {pa[0]}!"
        elif ev["kind"] == "combat":
            # simulate a quick fight
            enemy = next(en for en in D.ENEMIES if en[0] == ev["enemy"])
            hp = enemy[1]
            while hp > 0 and res.hp > 0:
                mn, mx = res.weapon_damage()
                dmg = self.rng.randint(mn, mx) + res.stat_total("P") // 2
                hp -= dmg
                if hp <= 0: break
                edmg = max(1, self.rng.randint(enemy[2], enemy[3]) - res.armor_total())
                edmg = int(edmg * (1 - res.dr_total() / 100.0))
                res.hp = max(0, res.hp - edmg)
            if res.hp <= 0:
                res.hp = 1
                log += " You barely survived."
            else:
                e["caps"] += enemy[5]
                res.grant_xp(enemy[4])
                self.kills += 1
                self._track_objective("kills", n=self.kills)
                log += f" You defeated a {enemy[0]}."
        elif ev["kind"] == "discovery" or ev["kind"] == "story":
            reward = ev.get("reward", {})
            for k, span in reward.items():
                v = self.rng.randint(*span)
                if k == "caps": e["caps"] += v
                elif k == "xp": res.grant_xp(v)
                else: e["resources"][k] = e["resources"].get(k, 0) + v
        e["log"].append(log)

    def start_expedition(self, res_id: int, duration_min: float = 8.0):
        if not any(r.key == "command" for r in self.rooms.values()):
            self.notify("Build a Command Center to send expeditions.", "bad"); return
        res = self.residents.get(res_id)
        if not res: return
        if res.on_expedition:
            self.notify(f"{res.name} is already exploring.", "bad"); return
        res.on_expedition = True
        res.activity = "expedition"
        res.assigned_room = None
        e = dict(
            resident_id=res_id,
            duration=duration_min * 60,
            elapsed=0.0,
            next_event=self.rng.uniform(10, 20),
            caps=0,
            resources={},
            items=[],
            log=[],
        )
        self.expeditions.append(e)
        self.notify(f"{res.name} left on an expedition.", "info")
        A.play("expedition")

    def recall_expedition(self, res_id: int):
        for e in self.expeditions:
            if e["resident_id"] == res_id:
                e["duration"] = e["elapsed"]  # end at next tick
                return

    def _sim_random_events(self, dt):
        # Incidents
        self.next_incident_at -= dt
        if self.next_incident_at <= 0:
            self.next_incident_at = self.rng.uniform(120, 240)
            self._spawn_incident()
        # Wanderers
        self.next_wanderer_at -= dt
        if self.next_wanderer_at <= 0:
            self.next_wanderer_at = self.rng.uniform(120, 300)
            if len(self.residents) < self.housing_cap():
                if self.rng.random() < 0.7:
                    self.spawn_resident()

    def _spawn_incident(self):
        # Pick a room to afflict
        candidates = [r for r in self.rooms.values() if r.key not in ("elevator", "storage")]
        if not candidates: return
        room = self.rng.choice(candidates)
        kind_pool = D.INCIDENTS
        total = sum(k["weight"] for k in kind_pool)
        pick = self.rng.uniform(0, total)
        acc = 0
        chosen = kind_pool[0]
        for k in kind_pool:
            acc += k["weight"]
            if pick <= acc:
                chosen = k; break
        inc = dict(kind=chosen["kind"], name=chosen["name"],
                   room_id=room.id, timer=90.0)
        self.incidents.append(inc)
        if chosen["kind"] == "fire":
            room.on_fire = True
        A.play("alarm")
        self.notify(f"{chosen['name']} in {room.data()['name']}!", "bad")
        # Auto-send nearest available residents
        avail = sorted(
            [r for r in self.residents.values() if r.assigned_room is None and not r.on_expedition],
            key=lambda x: abs(x.floor - room.floor) * 4 + abs(x.x - (room.x + room.width / 2))
        )
        for a in avail[:2]:
            self.walk_to_room(a, room)

    # ---- objectives ----
    def _track_objective(self, kind: str, **kw):
        for oid, obj in D.OBJECTIVES:
            if oid in self.objectives_done: continue
            if obj["kind"] != kind: continue
            if kind == "build_room":
                if kw.get("room") == obj["room"]:
                    cur = self.objectives_progress.get(oid, 0) + 1
                    self.objectives_progress[oid] = cur
                    if cur >= obj["n"]:
                        self._complete_objective(oid, obj)
            elif kind in ("population", "kills", "train", "expeditions", "find_pa"):
                n = kw.get("n", 0)
                self.objectives_progress[oid] = n
                if n >= obj["n"]:
                    self._complete_objective(oid, obj)
        # Passive check
        for oid, obj in D.OBJECTIVES:
            if oid in self.objectives_done: continue
            if obj["kind"] == "caps_total":
                self.objectives_progress[oid] = self.caps_earned
                if self.caps_earned >= obj["n"]:
                    self._complete_objective(oid, obj)

    def _complete_objective(self, oid, obj):
        self.objectives_done.add(oid)
        rw = obj.get("reward", {})
        self.caps += rw.get("caps", 0)
        self.caps_earned += rw.get("caps", 0)
        self.notify(f"Objective complete: {obj['desc']} (+{rw.get('caps',0)} caps)", "good")
        A.play("cash")

    # ---- serialization ----
    def to_dict(self) -> dict:
        return dict(
            version=C.SAVE_VERSION,
            meta=dict(pop=len(self.residents), caps=self.caps, time=self.time,
                      saved_at=time.time(), seed=self.seed),
            seed=self.seed, time=self.time, paused=self.paused, speed=self.speed,
            caps=self.caps, resources=self.resources, storage_cap=self.storage_cap,
            rooms={rid: {k: v for k, v in r.__dict__.items() if not k.startswith("_")}
                   for rid, r in self.rooms.items()},
            residents={rid: {k: v for k, v in r.__dict__.items() if not k.startswith("_")}
                       for rid, r in self.residents.items()},
            _next_room_id=self._next_room_id,
            _next_resident_id=self._next_resident_id,
            inventory=self.inventory,
            pa_storage=self.pa_storage,
            objectives_done=list(self.objectives_done),
            objectives_progress=self.objectives_progress,
            kills=self.kills, expeditions_completed=self.expeditions_completed,
            pa_found=self.pa_found, trainings_completed=self.trainings_completed,
            caps_earned=self.caps_earned,
            incidents=self.incidents, expeditions=self.expeditions,
            next_incident_at=self.next_incident_at,
            next_wanderer_at=self.next_wanderer_at,
            elevator_col=self.elevator_col,
        )

    @staticmethod
    def from_dict(d: dict) -> "GameState":
        g = GameState.__new__(GameState)
        g.seed = d.get("seed", 0)
        g.rng = random.Random(g.seed + int(d.get("time", 0)))
        g.time = d.get("time", 0.0)
        g.paused = d.get("paused", False)
        g.speed = d.get("speed", 1.0)
        g.caps = d.get("caps", 0)
        g.resources = d.get("resources", {})
        g.storage_cap = d.get("storage_cap", {})
        g.rooms = {int(rid): Room(**r) for rid, r in d.get("rooms", {}).items()}
        g.residents = {}
        for rid, r in d.get("residents", {}).items():
            res = Resident(**r)
            g.residents[int(rid)] = res
        g._next_room_id = d.get("_next_room_id", 1)
        g._next_resident_id = d.get("_next_resident_id", 1)
        g.inventory = d.get("inventory", [])
        g.pa_storage = d.get("pa_storage", [])
        g.objectives_done = set(d.get("objectives_done", []))
        g.objectives_progress = d.get("objectives_progress", {})
        g.kills = d.get("kills", 0)
        g.expeditions_completed = d.get("expeditions_completed", 0)
        g.pa_found = d.get("pa_found", 0)
        g.trainings_completed = d.get("trainings_completed", 0)
        g.caps_earned = d.get("caps_earned", 0)
        g.notifications = []
        g.incidents = d.get("incidents", [])
        g.expeditions = d.get("expeditions", [])
        g.next_incident_at = d.get("next_incident_at", 90.0)
        g.next_wanderer_at = d.get("next_wanderer_at", 60.0)
        g.elevator_col = d.get("elevator_col", C.COLUMNS // 2)
        return g
