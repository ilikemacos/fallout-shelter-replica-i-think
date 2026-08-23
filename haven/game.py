"""Core game state and simulation for Haven.

Everything the save file needs to reconstruct lives on GameState.
"""

from __future__ import annotations

import math
import random
import time
from dataclasses import dataclass, field, fields
from typing import Optional

from . import audio as A
from . import config as C
from . import data as D


# Baseline storage before any Storage Rooms are built. Kept modest so the HUD
# bars read as meaningful levels rather than sitting near empty all game.
BASE_STORAGE = {"power": 240, "water": 240, "food": 240,
                "materials": 120, "stim": 20, "radaway": 20}


def _filter_kwargs(cls, d: dict) -> dict:
    """Drop keys a dataclass no longer has, so old saves still load."""
    known = {f.name for f in fields(cls)}
    return {k: v for k, v in d.items() if k in known}


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
        return Item(**_filter_kwargs(Item, d))


def make_weapon(name: str) -> Item:
    for n, mn, mx, r, v in D.WEAPONS:
        if n == name:
            return Item("weapon", n, r, dmg_min=mn, dmg_max=mx, value=v,
                        desc=f"Damage {mn}-{mx}")
    raise KeyError(name)


def make_outfit(name: str) -> Item:
    for n, sb, ar, r, v in D.OUTFITS:
        if n == name:
            bonus = " ".join(f"+{v2}{k}" for k, v2 in sb.items()) or "no bonuses"
            return Item("outfit", n, r, armor=ar, stat_bonus=dict(sb), value=v,
                        desc=f"Armor +{ar} · {bonus}")
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
        "Stimpack": ("Restores 40 HP.", 40),
        "RadAway": ("Clears radiation and heals 20.", 30),
    }
    if name in tbl:
        d, v = tbl[name]
        return Item("consumable", name, 1, value=v, desc=d)
    return Item("material", name, 0, value=1, desc="Crafting material.")


# --------------------- Resident ---------------------
@dataclass
class Resident:
    id: int
    name: str
    portrait_seed: int
    level: int = 1
    xp: int = 0
    hp: float = 40.0
    max_hp: int = 40
    happiness: float = 60.0
    stats: dict = field(default_factory=lambda: {k: 1 for k in D.STAT_KEYS})
    weapon: Optional[dict] = None
    outfit: Optional[dict] = None
    power_armor: Optional[dict] = None
    # World position
    floor: int = 0
    x: float = 0.0
    path: list = field(default_factory=list)
    step: int = 0
    facing: int = 1
    # Assignment / behaviour
    assigned_room: Optional[int] = None
    activity: str = "idle"
    # Condition
    rads: float = 0.0
    alive: bool = True
    on_expedition: bool = False
    # Life cycle
    gender: str = "f"
    age: str = "adult"           # 'child' | 'adult'
    grow_timer: float = 0.0
    pregnant: bool = False
    preg_timer: float = 0.0
    romance_timer: float = 0.0
    partner_id: Optional[int] = None
    is_robot: bool = False
    # Idle wander bookkeeping
    idle_timer: float = 0.0

    # -- derived --
    def effective_max_hp(self) -> int:
        """Radiation eats into the usable health pool."""
        return max(1, int(self.max_hp - self.rads))

    def stat_total(self, k: str) -> int:
        base = self.stats.get(k, 1)
        b = 0
        for e in (self.outfit, self.power_armor):
            if e:
                b += e.get("stat_bonus", {}).get(k, 0)
        return base + b

    def armor_total(self) -> int:
        a = 0
        if self.outfit:
            a += self.outfit.get("armor", 0)
        if self.power_armor:
            a += self.power_armor.get("armor", 0)
        return a

    def dr_total(self) -> int:
        return self.power_armor.get("dr", 0) if self.power_armor else 0

    def weapon_damage(self) -> tuple[int, int]:
        if self.weapon:
            return self.weapon.get("dmg_min", 1), self.weapon.get("dmg_max", 2)
        return 1, 2

    def can_work(self) -> bool:
        return self.alive and self.age == "adult" and not self.on_expedition

    def xp_needed(self) -> int:
        return int(60 * (self.level ** 1.2))

    def grant_xp(self, x) -> bool:
        if not self.alive or self.age == "child":
            return False
        self.xp += int(x)
        leveled = False
        while self.xp >= self.xp_needed() and self.level < 50:
            self.xp -= self.xp_needed()
            self.level += 1
            self.max_hp += 4
            self.hp = min(self.effective_max_hp(), self.hp + 8)
            leveled = True
        return leveled

    def revive_cost(self) -> int:
        return max(50, self.level * C.REVIVE_COST_PER_LEVEL)


# --------------------- Rooms ---------------------
@dataclass
class Room:
    id: int
    key: str
    floor: int
    x: int
    width: int
    level: int = 1
    progress: float = 0.0
    workers: list = field(default_factory=list)
    hp: float = 100.0
    on_fire: bool = False
    invaders: list = field(default_factory=list)
    # Output banked and awaiting collection
    stored: dict = field(default_factory=dict)
    # Rushing raises the odds of a mishap until the next cycle lands
    rush_risk: float = 0.0
    flash: float = 0.0          # visual pulse timer

    def data(self):
        return D.ROOMS[self.key]

    def width_units(self) -> int:
        return max(1, self.width // max(1, self.data().get("width", 2)))

    def capacity(self) -> int:
        return self.data().get("capacity", 0) * self.width_units()

    def storage_bonus(self) -> int:
        return 100 * self.level * self.width_units() if self.key == "storage" else 0

    def housing(self) -> int:
        return 4 * self.level * self.width_units() if self.key == "living" else 0

    def produces(self):
        return self.data().get("produces", {})

    def consumes(self):
        return self.data().get("consumes", {})

    def is_producer(self) -> bool:
        return bool(self.produces()) and "train_stat" not in self.data()

    def has_output(self) -> bool:
        return any(v > 0 for v in self.stored.values())

    def can_rush(self) -> bool:
        return (self.is_producer() and bool(self.workers)
                and self.progress < 1.0 and not self.on_fire
                and not self.invaders)

    def rush_failure_chance(self) -> float:
        """Base odds rise with each successive rush and fall with worker Luck."""
        return min(0.85, 0.20 + self.rush_risk)


# --------------------- Floating combat/collection text ---------------------
@dataclass
class Floater:
    text: str
    floor: int
    x: float
    color: tuple
    life: float = 1.6
    vy: float = -26.0
    age: float = 0.0


# --------------------- GameState ---------------------
class GameState:
    def __init__(self, seed: int | None = None, vault_number: int | None = None):
        self.seed = seed if seed is not None else random.randint(1, 10 ** 9)
        self.rng = random.Random(self.seed)
        self.vault_number = vault_number or self.rng.randint(11, 999)
        self.time = 0.0
        self.paused = False
        self.speed = 1.0
        self.caps = C.STARTING_CAPS
        self.resources = {"power": 130, "water": 130, "food": 130,
                          "materials": 20, "stim": 4, "radaway": 2}
        self.storage_cap = dict(BASE_STORAGE)
        self.rooms: dict[int, Room] = {}
        self.residents: dict[int, Resident] = {}
        self._next_room_id = 1
        self._next_resident_id = 1
        self.inventory: list[dict] = []
        self.pa_storage: list[dict] = []
        self.lunchboxes = 1
        self.objectives_done = set()
        self.objectives_progress: dict[str, int] = {}
        self.kills = 0
        self.expeditions_completed = 0
        self.pa_found = 0
        self.trainings_completed = 0
        self.caps_earned = 0
        self.births = 0
        self.deaths = 0
        self.rushes_ok = 0
        self.rushes_failed = 0
        self.notifications: list[tuple[float, str, str]] = []
        self.incidents: list[dict] = []
        self.expeditions: list[dict] = []
        self.floaters: list[Floater] = []
        self.next_incident_at = 150.0
        self.next_wanderer_at = 90.0
        self.next_raid_at = 420.0
        self.elevator_col = C.COLUMNS // 2
        self.auto_collect = False
        self.last_lunchbox_reward: list[str] = []

        self._bootstrap()

    # ---- bootstrap ----
    def _bootstrap(self):
        for f in range(3):
            self._place_room("elevator", floor=f, x=self.elevator_col, width=1, free=True)
        self._place_room("power", floor=1, x=self.elevator_col - 4, width=2, free=True)
        self._place_room("water", floor=1, x=self.elevator_col + 1, width=2, free=True)
        self._place_room("diner", floor=2, x=self.elevator_col - 4, width=2, free=True)
        self._place_room("living", floor=2, x=self.elevator_col + 1, width=2, free=True)
        for _ in range(C.STARTING_POP):
            self.spawn_resident(announce=False)

    # ---- notifications / floaters ----
    def notify(self, text: str, level: str = "info"):
        self.notifications.append((self.time, level, text))
        self.notifications = self.notifications[-80:]

    def add_floater(self, text, floor, x, color):
        self.floaters.append(Floater(text=text, floor=floor, x=x, color=color))
        if len(self.floaters) > 60:
            del self.floaters[:-60]

    # ---- placement rules ----
    def can_place(self, key: str, floor: int, x: int, width: int) -> tuple[bool, str]:
        rdata = D.ROOMS.get(key)
        if not rdata:
            return False, "Unknown room"
        if floor < 0 or floor >= C.FLOOR_COUNT:
            return False, "Outside the shelter"
        if x < 0 or x + width > C.COLUMNS:
            return False, "Outside the shelter"

        if key == "elevator":
            if width != 1:
                return False, "Elevators are one cell wide"
            if x != self.elevator_col:
                return False, "Elevators share one shaft"
            if floor > 0 and not any(r.key == "elevator" and r.floor == floor - 1
                                     for r in self.rooms.values()):
                return False, "Extend down from the shaft above"
        else:
            base = rdata["width"]
            if width < base:
                return False, f"Minimum width {base}"
            if width > base * C.ROOM_MAX_MERGE:
                return False, f"Maximum width {base * C.ROOM_MAX_MERGE}"

        for cx in range(x, x + width):
            if key != "elevator" and cx == self.elevator_col:
                return False, "Blocked by the elevator shaft"
            for r in self.rooms.values():
                if r.floor == floor and r.x <= cx < r.x + r.width:
                    return False, "Space already occupied"

        if key != "elevator":
            if not any(r.key == "elevator" and r.floor == floor for r in self.rooms.values()):
                return False, "Dig an elevator to this floor first"
            # The shelter grows outward along each floor from the lift shaft, so a
            # new room only has to touch something already on its own floor.
            touching = any(r.floor == floor and (r.x + r.width == x or x + width == r.x)
                           for r in self.rooms.values())
            if not touching:
                return False, "Must touch a room or the lift on this floor"
        return True, ""

    def _place_room(self, key, floor, x, width, free=False) -> Optional[Room]:
        rdata = D.ROOMS[key]
        if not free:
            ok, why = self.can_place(key, floor, x, width)
            if not ok:
                self.notify(f"Cannot build: {why}", "bad")
                return None
            cost = self.build_cost(key, width)
            if self.caps < cost:
                self.notify(f"Need {cost} caps.", "bad")
                return None
            self.caps -= cost
        r = Room(id=self._next_room_id, key=key, floor=floor, x=x, width=width)
        self._next_room_id += 1
        self.rooms[r.id] = r
        if key == "storage":
            self._recompute_storage()
        if not free:
            self.notify(f"Built {rdata['name']}.", "good")
            A.play("build")
            self.add_floater(rdata["name"], floor, x + width / 2, C.UI_ACCENT)
        self._track_objective("build_room", room=key)
        return r

    def build_cost(self, key: str, width: int) -> int:
        rdata = D.ROOMS[key]
        base = rdata["cost"]
        units = max(1, width // max(1, rdata.get("width", 2)))
        # Each extra merged section costs more than the last.
        return int(base * sum(1.0 + 0.6 * i for i in range(units)))

    def place_room(self, key, floor, x, width):
        return self._place_room(key, floor, x, width, free=False)

    def _recompute_storage(self):
        bonus = sum(r.storage_bonus() for r in self.rooms.values())
        for k, base in BASE_STORAGE.items():
            extra = bonus if k in ("power", "water", "food", "materials") else bonus // 10
            self.storage_cap[k] = base + extra

    def upgrade_cost(self, room: Room) -> int:
        return int(room.data().get("upgrade", 0) * room.level * room.width_units())

    def upgrade_room(self, room_id: int):
        r = self.rooms.get(room_id)
        if not r:
            return
        if r.level >= 3:
            self.notify("Already fully upgraded.", "bad")
            return
        cost = self.upgrade_cost(r)
        if cost <= 0:
            self.notify("This room cannot be upgraded.", "bad")
            return
        if self.caps < cost:
            self.notify(f"Need {cost} caps.", "bad")
            return
        self.caps -= cost
        r.level += 1
        r.flash = 0.8
        if r.key == "storage":
            self._recompute_storage()
        self.notify(f"{r.data()['name']} upgraded to level {r.level}.", "good")
        self.add_floater(f"LV {r.level}", r.floor, r.x + r.width / 2, C.UI_ACCENT)
        A.play("upgrade")

    def destroy_room(self, room_id: int):
        r = self.rooms.get(room_id)
        if not r:
            return
        if r.key == "elevator":
            below = [o for o in self.rooms.values()
                     if o.floor > r.floor and o.key == "elevator"]
            if below:
                self.notify("Remove the shaft below first.", "bad")
                return
        if r.invaders or r.on_fire:
            self.notify("Cannot demolish during an emergency.", "bad")
            return
        self.rooms.pop(room_id, None)
        for res in self.residents.values():
            if res.assigned_room == room_id:
                res.assigned_room = None
                res.activity = "idle"
                res.path = []
        if r.key == "storage":
            self._recompute_storage()
        self.caps += self.build_cost(r.key, r.width) // 4
        self.notify(f"Demolished {r.data()['name']}.", "warn")

    def try_merge(self, room_id: int) -> bool:
        r = self.rooms.get(room_id)
        if not r or r.key == "elevator":
            return False
        base_w = r.data()["width"]
        for other in list(self.rooms.values()):
            if other.id == r.id or other.key != r.key:
                continue
            if other.level != r.level or other.floor != r.floor:
                continue
            if other.width + r.width > base_w * C.ROOM_MAX_MERGE:
                continue
            if other.x == r.x + r.width or other.x + other.width == r.x:
                r.x = min(r.x, other.x)
                r.width += other.width
                r.workers += other.workers
                for k, v in other.stored.items():
                    r.stored[k] = r.stored.get(k, 0) + v
                self.rooms.pop(other.id)
                r.flash = 0.8
                self.notify(f"Merged into a larger {r.data()['name']}.", "good")
                A.play("upgrade")
                return True
        self.notify("No matching neighbour of the same level.", "bad")
        return False

    # ---- residents ----
    def spawn_resident(self, name=None, announce=True, level=1, rare=False) -> Optional[Resident]:
        if self.population() >= self.housing_cap():
            if announce:
                self.notify("No free bunks — build Living Quarters.", "warn")
            return None
        first = self.rng.choice(D.FIRST_NAMES)
        last = self.rng.choice(D.LAST_NAMES)
        lo, hi = (3, 6) if rare else (1, 3)
        r = Resident(
            id=self._next_resident_id,
            name=name or f"{first} {last}",
            portrait_seed=self.rng.randint(1, 10 ** 9),
            gender=self.rng.choice(["f", "m"]),
            stats={k: self.rng.randint(lo, hi) for k in D.STAT_KEYS},
            level=level,
        )
        r.max_hp = 40 + (level - 1) * 4
        r.hp = r.max_hp
        r.floor = 0
        r.x = float(self.elevator_col)
        r.outfit = make_outfit("Jumpsuit").to_dict()
        self._next_resident_id += 1
        self.residents[r.id] = r
        if announce:
            self.notify(f"{r.name} joined the shelter.", "good")
            A.play("cash")
        self._track_objective("population", n=self.population())
        return r

    ROBOT_COST = 2000

    def buy_robot(self) -> Optional[Resident]:
        """A hovering caretaker that patrols one floor collecting output."""
        if self.caps < self.ROBOT_COST:
            self.notify(f"A caretaker unit costs {self.ROBOT_COST} caps.", "bad")
            return None
        if not any(r.key == "workshop" for r in self.rooms.values()):
            self.notify("A Workshop is needed to assemble a caretaker.", "bad")
            return None
        self.caps -= self.ROBOT_COST
        n = sum(1 for r in self.residents.values() if r.is_robot) + 1
        r = Resident(
            id=self._next_resident_id,
            name=f"Caretaker {n}",
            portrait_seed=self.rng.randint(1, 10 ** 9),
            is_robot=True,
            stats={k: 4 for k in D.STAT_KEYS},
        )
        r.max_hp = 90
        r.hp = 90
        r.happiness = 100.0
        r.floor = 0
        r.x = float(self.elevator_col)
        self._next_resident_id += 1
        self.residents[r.id] = r
        self.notify(f"{r.name} came online.", "good")
        self.add_floater("ONLINE", r.floor, r.x, C.UI_BLUE)
        A.play("upgrade")
        return r

    def _sim_robots(self, dt):
        """Robots walk their floor and bank whatever is ready."""
        for res in self.residents.values():
            if not res.is_robot or not res.alive:
                continue
            res.idle_timer -= dt
            if res.path:
                continue
            # Collect anything ready in the room it is standing in.
            here = self.find_room(res.floor, res.x)
            if here is not None and here.has_output():
                self.collect_room(here.id)
            if res.idle_timer > 0:
                continue
            res.idle_timer = 2.0
            # Head for the nearest room with output, preferring its own floor.
            targets = [r for r in self.rooms.values()
                       if r.has_output() and r.key != "elevator"]
            if not targets:
                return
            targets.sort(key=lambda r: (abs(r.floor - res.floor) * 3
                                        + abs(r.x - res.x)))
            self.walk_to_room(res, targets[0])
            res.activity = "walk"

    def _spawn_child(self, mother: Resident):
        first = self.rng.choice(D.FIRST_NAMES)
        last = mother.name.split()[-1]
        r = Resident(
            id=self._next_resident_id,
            name=f"{first} {last}",
            portrait_seed=self.rng.randint(1, 10 ** 9),
            gender=self.rng.choice(["f", "m"]),
            stats={k: max(1, (mother.stats.get(k, 1) + self.rng.randint(0, 2)) // 2 + 1)
                   for k in D.STAT_KEYS},
            age="child",
            grow_timer=C.CHILD_GROW_TIME,
        )
        r.max_hp = 25
        r.hp = 25
        r.floor = mother.floor
        r.x = mother.x
        r.outfit = make_outfit("Jumpsuit").to_dict()
        self._next_resident_id += 1
        self.residents[r.id] = r
        self.births += 1
        self.notify(f"{r.name} was born in the shelter.", "good")
        self.add_floater("New arrival!", r.floor, r.x, C.UI_GOOD)
        A.play("cash")
        self._track_objective("births", n=self.births)
        self._track_objective("population", n=self.population())

    def population(self) -> int:
        return sum(1 for r in self.residents.values() if r.alive and not r.is_robot)

    def adults(self) -> list[Resident]:
        return [r for r in self.residents.values()
                if r.alive and r.age == "adult" and not r.is_robot]

    def housing_cap(self) -> int:
        return C.STARTING_POP + sum(r.housing() for r in self.rooms.values())

    def assign(self, res_id: int, room_id: Optional[int]) -> bool:
        r = self.residents.get(res_id)
        if not r or not r.alive:
            return False
        if r.age == "child":
            self.notify(f"{r.name} is too young to work.", "bad")
            return False
        if r.on_expedition:
            self.notify(f"{r.name} is out in the wasteland.", "bad")
            return False
        if r.assigned_room:
            prev = self.rooms.get(r.assigned_room)
            if prev and res_id in prev.workers:
                prev.workers.remove(res_id)
        r.assigned_room = None
        if room_id is None:
            r.activity = "idle"
            return True
        room = self.rooms.get(room_id)
        if not room:
            return False
        if room.capacity() <= 0:
            self.notify(f"{room.data()['name']} has no work posts.", "bad")
            return False
        if len(room.workers) >= room.capacity():
            self.notify(f"{room.data()['name']} is fully staffed.", "bad")
            return False
        room.workers.append(res_id)
        r.assigned_room = room_id
        self.walk_to_room(r, room)
        return True

    def best_stat_for(self, room: Room) -> Optional[str]:
        return room.data().get("staff_stat") or room.data().get("train_stat")

    def auto_assign_best(self, room_id: int) -> Optional[Resident]:
        """Fill one post with the most suitable idle resident."""
        room = self.rooms.get(room_id)
        if not room or len(room.workers) >= room.capacity():
            return None
        stat = self.best_stat_for(room)
        pool = [r for r in self.residents.values()
                if r.can_work() and r.assigned_room is None]
        if not pool:
            self.notify("No idle residents available.", "warn")
            return None
        if stat:
            pool.sort(key=lambda r: -r.stat_total(stat))
        best = pool[0]
        return best if self.assign(best.id, room_id) else None

    def walk_to_room(self, res: Resident, room: Room):
        res.path = []
        target = room.x + room.width / 2
        if res.floor != room.floor:
            res.path.append((res.floor, float(self.elevator_col)))
            res.path.append((room.floor, float(self.elevator_col)))
        res.path.append((room.floor, target))
        res.activity = "walk"

    def find_room(self, floor: int, x: float) -> Optional[Room]:
        for r in self.rooms.values():
            if r.floor == floor and r.x <= x < r.x + r.width:
                return r
        return None

    # ---- collection & rushing ----
    def collect_room(self, room_id: int) -> bool:
        r = self.rooms.get(room_id)
        if not r or not r.has_output():
            return False
        got = []
        for k, amt in list(r.stored.items()):
            amt = int(amt)
            if amt <= 0:
                continue
            if k == "caps":
                self.caps += amt
                self.caps_earned += amt
                got.append(f"+{amt} caps")
            else:
                cap = self.storage_cap.get(k, 400)
                before = self.resources.get(k, 0)
                self.resources[k] = min(cap, before + amt)
                gained = int(self.resources[k] - before)
                got.append(f"+{gained} {k}")
                if gained < amt:
                    self.notify(f"{k.title()} storage is full — output wasted.", "warn")
        r.stored.clear()
        r.flash = 0.5
        if got:
            self.add_floater(" ".join(got), r.floor, r.x + r.width / 2, C.UI_GOOD)
            A.play("cash")
        self._track_objective("collect", n=1)
        return True

    def collect_all(self) -> int:
        n = 0
        for rid in list(self.rooms):
            if self.rooms[rid].has_output() and self.collect_room(rid):
                n += 1
        if n:
            self.notify(f"Collected from {n} room{'s' if n != 1 else ''}.", "good")
        return n

    def rush_room(self, room_id: int) -> bool:
        """Force an immediate production cycle, risking an incident."""
        r = self.rooms.get(room_id)
        if not r:
            return False
        if not r.can_rush():
            self.notify("This room cannot be rushed right now.", "bad")
            return False
        luck = sum(self.residents[w].stat_total("L")
                   for w in r.workers if w in self.residents)
        chance = max(0.05, r.rush_failure_chance() - luck * 0.012)
        if self.rng.random() < chance:
            self.rushes_failed += 1
            r.rush_risk = min(0.65, r.rush_risk + 0.10)
            kind = self.rng.choice(["fire", "critters"])
            self._start_incident(kind, r,
                                 "Rushing started a fire!" if kind == "fire"
                                 else "Rushing disturbed a nest of radroaches!")
            self.add_floater("RUSH FAILED", r.floor, r.x + r.width / 2, C.UI_BAD)
            return False
        self.rushes_ok += 1
        r.rush_risk = min(0.65, r.rush_risk + 0.10)
        r.progress = 0.0
        self._produce_cycle(r, rushed=True)
        bonus = 10 * r.level * r.width_units()
        r.stored["caps"] = r.stored.get("caps", 0) + bonus
        self.add_floater("RUSH!", r.floor, r.x + r.width / 2, C.UI_ACCENT)
        A.play("upgrade")
        self._track_objective("rush", n=self.rushes_ok)
        return True

    # ---- inventory ----
    def add_item(self, item):
        if isinstance(item, Item):
            item = item.to_dict()
        if item["kind"] == "pa":
            self.pa_storage.append(item)
            self.pa_found += 1
            self._track_objective("find_pa", n=self.pa_found)
            self.notify(f"Recovered Power Armor: {item['name']}!", "good")
            A.play("upgrade")
        else:
            self.inventory.append(item)
        return item

    def sell_item(self, index: int):
        if 0 <= index < len(self.inventory):
            it = self.inventory.pop(index)
            price = max(1, it.get("value", 1) // 2)
            self.caps += price
            self.caps_earned += price
            self.notify(f"Sold {it['name']} for {price} caps.", "good")
            A.play("cash")

    def scrap_item(self, index: int):
        if 0 <= index < len(self.inventory):
            it = self.inventory.pop(index)
            mats = max(1, (it.get("rarity", 0) + 1) * 2)
            self.resources["materials"] = min(self.storage_cap["materials"],
                                              self.resources.get("materials", 0) + mats)
            self.notify(f"Scrapped {it['name']} for {mats} materials.", "info")

    def sell_pa(self, index: int):
        if 0 <= index < len(self.pa_storage):
            it = self.pa_storage.pop(index)
            price = max(50, it.get("value", 100) // 2)
            self.caps += price
            self.caps_earned += price
            self.notify(f"Sold {it['name']} for {price} caps.", "good")
            A.play("cash")

    def equip(self, res_id: int, slot: str, index: int):
        res = self.residents.get(res_id)
        if not res or not res.alive:
            return
        if slot == "pa":
            if not (0 <= index < len(self.pa_storage)):
                return
            item = self.pa_storage[index]
            if res.level < item.get("level_req", 0):
                self.notify(f"{res.name} must reach level {item['level_req']}.", "bad")
                return
            self.pa_storage.pop(index)
            if res.power_armor:
                self.pa_storage.append(res.power_armor)
            res.power_armor = item
            self.notify(f"{res.name} equipped {item['name']}.", "good")
            A.play("upgrade")
            return
        if not (0 <= index < len(self.inventory)):
            return
        it = self.inventory[index]
        if it["kind"] == "weapon" and slot == "weapon":
            self.inventory.pop(index)
            if res.weapon:
                self.inventory.append(res.weapon)
            res.weapon = it
        elif it["kind"] == "outfit" and slot == "outfit":
            self.inventory.pop(index)
            if res.outfit:
                self.inventory.append(res.outfit)
            res.outfit = it
        else:
            return
        A.play("click")

    def unequip(self, res_id: int, slot: str):
        res = self.residents.get(res_id)
        if not res:
            return
        item = getattr(res, slot, None)
        if not item:
            return
        if slot == "power_armor":
            self.pa_storage.append(item)
            res.power_armor = None
        else:
            self.inventory.append(item)
            setattr(res, slot, None)
        A.play("click")

    def use_stimpack(self, res_id: int):
        res = self.residents.get(res_id)
        if not res or not res.alive:
            return
        if self.resources.get("stim", 0) < 1:
            self.notify("No stimpacks in storage.", "bad")
            return
        if res.hp >= res.effective_max_hp():
            self.notify(f"{res.name} is already at full health.", "warn")
            return
        self.resources["stim"] -= 1
        res.hp = min(res.effective_max_hp(), res.hp + 40)
        self.add_floater("+40 HP", res.floor, res.x, C.UI_GOOD)
        A.play("heal")

    def use_radaway(self, res_id: int):
        res = self.residents.get(res_id)
        if not res or not res.alive:
            return
        if self.resources.get("radaway", 0) < 1:
            self.notify("No RadAway in storage.", "bad")
            return
        if res.rads <= 0:
            self.notify(f"{res.name} has no radiation damage.", "warn")
            return
        self.resources["radaway"] -= 1
        res.rads = max(0.0, res.rads - 25)
        res.hp = min(res.effective_max_hp(), res.hp + 20)
        self.add_floater("-RADS", res.floor, res.x, C.UI_BLUE)
        A.play("heal")

    def revive(self, res_id: int):
        res = self.residents.get(res_id)
        if not res or res.alive:
            return
        cost = res.revive_cost()
        if self.caps < cost:
            self.notify(f"Reviving {res.name} costs {cost} caps.", "bad")
            return
        self.caps -= cost
        res.alive = True
        res.rads = 0.0
        res.hp = res.effective_max_hp()
        res.happiness = 50.0
        res.activity = "idle"
        res.path = []
        self.notify(f"{res.name} was revived.", "good")
        self.add_floater("REVIVED", res.floor, res.x, C.UI_GOOD)
        A.play("heal")

    # ---- lunchboxes ----
    def open_lunchbox(self) -> list[str]:
        if self.lunchboxes <= 0:
            self.notify("No lunchboxes to open.", "bad")
            return []
        self.lunchboxes -= 1
        cards: list[str] = []
        for i in range(4):
            # The last card is weighted toward something worth having.
            roll = self.rng.random()
            good = i == 3
            if roll < (0.12 if good else 0.05):
                pa = self.rng.choice(D.POWER_ARMOR)
                self.add_item(make_power_armor(pa[0]))
                cards.append(f"Power Armor: {pa[0]}")
            elif roll < (0.42 if good else 0.30):
                lo = 2 if good else 0
                pool = [w for w in D.WEAPONS if w[-2] >= lo] or D.WEAPONS
                w = self.rng.choice(pool)
                self.add_item(make_weapon(w[0]))
                cards.append(f"Weapon: {w[0]}")
            elif roll < (0.62 if good else 0.52):
                lo = 2 if good else 0
                pool = [o for o in D.OUTFITS if o[-2] >= lo] or D.OUTFITS
                o = self.rng.choice(pool)
                self.add_item(make_outfit(o[0]))
                cards.append(f"Outfit: {o[0]}")
            elif roll < 0.72 and good:
                r = self.spawn_resident(rare=True, announce=False, level=self.rng.randint(3, 8))
                if r:
                    cards.append(f"Special resident: {r.name}")
                else:
                    self.caps += 300
                    cards.append("+300 caps")
            elif roll < 0.86:
                amt = self.rng.choice([100, 150, 250, 400])
                self.caps += amt
                self.caps_earned += amt
                cards.append(f"+{amt} caps")
            else:
                res = self.rng.choice(["food", "water", "power", "materials"])
                amt = self.rng.randint(40, 120)
                cap = self.storage_cap.get(res, 400)
                self.resources[res] = min(cap, self.resources.get(res, 0) + amt)
                cards.append(f"+{amt} {res}")
        self.last_lunchbox_reward = cards
        self.notify("Lunchbox opened!", "good")
        A.play("upgrade")
        return cards

    # ---- crafting ----
    CRAFT_MATS = {0: 5, 1: 12, 2: 25, 3: 60, 4: 140}
    CRAFT_CAPS = {0: 20, 1: 60, 2: 150, 3: 400, 4: 900}

    def craft(self, kind: str, rarity: int) -> Optional[Item]:
        mats = self.CRAFT_MATS.get(rarity, 5)
        caps = self.CRAFT_CAPS.get(rarity, 20)
        if rarity >= 2 and not any(r.key == "workshop" for r in self.rooms.values()):
            self.notify("A Workshop is required for Rare and above.", "bad")
            return None
        if rarity >= 3:
            need = "armory" if kind == "weapon" else "science"
            label = "an Armory" if kind == "weapon" else "a Science Lab"
            if not any(r.key == need for r in self.rooms.values()):
                self.notify(f"{label.capitalize()} is required for Epic and above.", "bad")
                return None
        if self.resources.get("materials", 0) < mats:
            self.notify(f"Need {mats} materials.", "bad")
            return None
        if self.caps < caps:
            self.notify(f"Need {caps} caps.", "bad")
            return None
        self.resources["materials"] -= mats
        self.caps -= caps
        pool = D.WEAPONS if kind == "weapon" else D.OUTFITS
        options = [x for x in pool if x[-2] == rarity] or pool
        chosen = self.rng.choice(options)
        it = make_weapon(chosen[0]) if kind == "weapon" else make_outfit(chosen[0])
        self.add_item(it)
        self.notify(f"Crafted {it.name}.", "good")
        self._track_objective("craft", n=1)
        A.play("upgrade")
        return it

    def repair_pa(self, res_id: int):
        r = self.residents.get(res_id)
        if not r or not r.power_armor:
            self.notify("No Power Armor equipped.", "bad")
            return
        pa = r.power_armor
        missing = pa.get("max_durability", 100) - pa.get("durability", 0)
        if missing <= 0:
            self.notify("That suit is undamaged.", "warn")
            return
        cost = 5 + missing * 2
        if self.caps < cost:
            self.notify(f"Repair costs {cost} caps.", "bad")
            return
        if self.resources.get("materials", 0) < 3:
            self.notify("Repair needs 3 materials.", "bad")
            return
        self.caps -= cost
        self.resources["materials"] -= 3
        pa["durability"] = pa["max_durability"]
        self.notify(f"Repaired {pa['name']}.", "good")
        A.play("upgrade")

    # =================== simulation ===================
    def tick(self, dt: float):
        if self.paused:
            return
        dt *= self.speed
        self.time += dt
        self._sim_movement(dt)
        self._sim_production(dt)
        self._sim_incidents(dt)
        self._sim_expeditions(dt)
        self._sim_wellbeing(dt)
        self._sim_robots(dt)
        self._sim_lifecycle(dt)
        self._sim_random_events(dt)
        self._sim_floaters(dt)
        for r in self.rooms.values():
            if r.flash > 0:
                r.flash = max(0.0, r.flash - dt)

    def _sim_floaters(self, dt):
        for f in self.floaters:
            f.age += dt
        self.floaters = [f for f in self.floaters if f.age < f.life]

    def _sim_movement(self, dt):
        for res in self.residents.values():
            if res.on_expedition or not res.alive:
                continue
            if not res.path:
                if res.activity == "walk":
                    res.activity = "work" if res.assigned_room else "idle"
                # Unassigned residents wander so the shelter looks alive.
                if res.activity == "idle" and not res.is_robot:
                    res.idle_timer -= dt
                    if res.idle_timer <= 0:
                        res.idle_timer = self.rng.uniform(4.0, 11.0)
                        rooms = [r for r in self.rooms.values() if r.key != "elevator"]
                        if rooms and self.rng.random() < 0.7:
                            dest = self.rng.choice(rooms)
                            self.walk_to_room(res, dest)
                            res.activity = "walk"
                continue

            target_floor, tx = res.path[0]
            speed = 3.4 if res.activity == "emergency" else 2.4
            if res.is_robot:
                speed = 3.0
            if res.floor != target_floor:
                if abs(res.x - self.elevator_col) > 0.06:
                    dx = self.elevator_col - res.x
                    stepv = max(-speed * dt, min(speed * dt, dx))
                    res.x += stepv
                    res.facing = 1 if stepv > 0 else -1
                    res.step = int(self.time * 7) % 2
                else:
                    res.x = float(self.elevator_col)
                    # Ride the shaft one floor at a time.
                    res.floor += 1 if res.floor < target_floor else -1
                continue
            if abs(res.x - tx) < 0.06:
                res.x = tx
                res.path.pop(0)
                if not res.path:
                    res.activity = "work" if res.assigned_room else "idle"
                continue
            dx = tx - res.x
            stepv = max(-speed * dt, min(speed * dt, dx))
            res.x += stepv
            res.facing = 1 if stepv > 0 else -1
            res.step = int(self.time * 7) % 2

    def _worker_bonus(self, room: Room, stat: Optional[str]) -> float:
        if not stat:
            return 1.0
        total = 0
        for w in room.workers:
            res = self.residents.get(w)
            if not res or not res.alive:
                continue
            # Unhappy workers under-perform; content ones give a small lift.
            mood = 0.55 + 0.55 * (res.happiness / 100.0)
            total += res.stat_total(stat) * mood
        return 1.0 + total * 0.06

    def _produce_cycle(self, r: Room, rushed=False):
        """Run one completed production cycle, banking output in the room."""
        for k, v in r.consumes().items():
            need = v * r.level * r.width_units()
            if self.resources.get(k, 0) < need:
                return False
        for k, v in r.consumes().items():
            self.resources[k] = max(0, self.resources[k] - v * r.level * r.width_units())

        mult = self._worker_bonus(r, r.data().get("staff_stat"))
        for k, v in r.produces().items():
            amt = max(1, int(v * r.level * r.width_units() * mult))
            if k == "attract":
                self.next_wanderer_at = max(0.0, self.next_wanderer_at - 20.0)
                continue
            if k == "happy":
                for res in self.residents.values():
                    if res.alive:
                        res.happiness = min(100.0, res.happiness + amt * 0.5)
                continue
            cap_cycles = C.MAX_STORED_CYCLES * max(1, r.level)
            r.stored[k] = min(r.stored.get(k, 0) + amt, amt * cap_cycles)
        for wid in r.workers:
            res = self.residents.get(wid)
            if res and res.alive:
                res.grant_xp(3 + r.level)
        return True

    def _sim_production(self, dt):
        for r in self.rooms.values():
            if r.key in ("elevator", "storage") or not r.workers:
                continue
            if r.on_fire or r.invaders:
                continue
            if r.data().get("requires_power") and self.resources.get("power", 0) <= 0:
                continue
            cycle = max(2.0, C.PRODUCTION_INTERVAL / max(1, r.level * r.width_units()))
            r.progress += dt / cycle

            if "train_stat" in r.data():
                if r.progress >= 1.0:
                    r.progress = 0.0
                    stat = r.data()["train_stat"]
                    for wid in list(r.workers):
                        res = self.residents.get(wid)
                        if not res or not res.alive:
                            continue
                        cur = res.stats.get(stat, 1)
                        if cur >= 10:
                            continue
                        # Higher stats take longer to raise, as you'd expect.
                        odds = (0.55 + 0.12 * r.level) / (1.0 + cur * 0.45)
                        if self.rng.random() < odds:
                            res.stats[stat] = cur + 1
                            self.trainings_completed += 1
                            self._track_objective("train", n=self.trainings_completed)
                            self.notify(f"{res.name} raised {D.STAT_NAMES[stat]} to {cur + 1}.",
                                        "good")
                            self.add_floater(f"+1 {stat}", res.floor, res.x, C.UI_ACCENT)
                continue

            if r.progress >= 1.0:
                r.progress = 0.0
                r.rush_risk = max(0.0, r.rush_risk - 0.05)
                if self._produce_cycle(r) and self.auto_collect:
                    self.collect_room(r.id)

    def _sim_wellbeing(self, dt):
        pop = max(1, self.population())
        # Consumption scales with head-count, spread smoothly over time.
        food_need = pop * dt / 14.0
        water_need = pop * dt / 14.0
        food_ok = self.resources.get("food", 0) >= food_need
        water_ok = self.resources.get("water", 0) >= water_need
        power_draw = sum(r.consumes().get("power", 0) * r.level * r.width_units()
                         for r in self.rooms.values() if r.workers)
        power_ok = self.resources.get("power", 0) > 0

        if food_ok:
            self.resources["food"] -= food_need
        if water_ok:
            self.resources["water"] -= water_need

        delta = 0.0
        if food_ok:
            delta += C.HAPPY_DRIFT_PER_MIN * dt / 60
        else:
            delta -= 3.5 * dt / 60
        if water_ok:
            delta += C.HAPPY_DRIFT_PER_MIN * dt / 60
        else:
            delta -= 3.5 * dt / 60
        if not power_ok:
            delta -= 2.5 * dt / 60

        for res in self.residents.values():
            if not res.alive or res.on_expedition:
                continue
            if res.is_robot:
                # Machines neither eat nor sulk.
                res.happiness = 100.0
                res.hp = min(res.effective_max_hp(), res.hp + 1.0 * dt)
                continue
            d = delta
            # Working a job that suits you is satisfying.
            if res.assigned_room:
                room = self.rooms.get(res.assigned_room)
                if room:
                    stat = self.best_stat_for(room)
                    if stat and res.stat_total(stat) >= 5:
                        d += 0.8 * dt / 60
            else:
                d -= 0.3 * dt / 60
            res.happiness = max(0.0, min(100.0, res.happiness + d))

            # Thirst and hunger sap health, but never below a quarter of the
            # pool: going hungry should weaken a resident, not leave them one
            # stray hit from death the moment an incident starts.
            floor_hp = res.effective_max_hp() * 0.25
            if not water_ok:
                res.hp = max(floor_hp, res.hp - 0.6 * dt)
            if not food_ok:
                res.hp = max(floor_hp, res.hp - 0.6 * dt)

            room = self.find_room(res.floor, res.x)
            if room and room.key == "medbay":
                res.hp = min(res.effective_max_hp(), res.hp + 3.5 * dt)
                res.rads = max(0.0, res.rads - 0.5 * dt)
            elif food_ok and water_ok:
                res.hp = min(res.effective_max_hp(), res.hp + 0.35 * dt)
            if res.hp > res.effective_max_hp():
                res.hp = res.effective_max_hp()

    def _sim_lifecycle(self, dt):
        # Children grow up.
        for res in list(self.residents.values()):
            if not res.alive or res.is_robot:
                continue
            if res.age == "child":
                res.grow_timer -= dt
                if res.grow_timer <= 0:
                    res.age = "adult"
                    res.max_hp = 40
                    res.hp = res.max_hp
                    self.notify(f"{res.name} came of age.", "good")
                    self.add_floater("Grown up!", res.floor, res.x, C.UI_ACCENT)
                continue
            if res.pregnant:
                res.preg_timer -= dt
                if res.preg_timer <= 0:
                    res.pregnant = False
                    res.partner_id = None
                    if self.population() < self.housing_cap():
                        self._spawn_child(res)
                    else:
                        self.notify(f"{res.name} needs a free bunk for the baby.", "warn")
                        res.preg_timer = 20.0
                        res.pregnant = True

        # Pair off adults sharing Living Quarters.
        for room in self.rooms.values():
            if room.key != "living" or len(room.workers) < 2:
                continue
            here = [self.residents[w] for w in room.workers
                    if w in self.residents and self.residents[w].alive]
            here = [r for r in here if r.age == "adult" and not r.is_robot
                    and r.activity in ("work", "idle") and r.happiness >= 55]
            females = [r for r in here if r.gender == "f" and not r.pregnant]
            males = [r for r in here if r.gender == "m"]
            if not females or not males:
                continue
            if self.population() >= self.housing_cap():
                continue
            f, m = females[0], males[0]
            if f.partner_id not in (None, m.id):
                continue
            f.partner_id, m.partner_id = m.id, f.id
            f.romance_timer += dt
            if f.romance_timer >= C.ROMANCE_TIME:
                f.romance_timer = 0.0
                f.pregnant = True
                f.preg_timer = C.PREGNANCY_TIME
                f.happiness = min(100.0, f.happiness + 15)
                m.happiness = min(100.0, m.happiness + 15)
                m.partner_id = None
                self.notify(f"{f.name} is expecting a child.", "good")

    # ---- incidents ----
    def _start_incident(self, kind: str, room: Room, message: str):
        if any(i["room_id"] == room.id for i in self.incidents):
            return
        name = {"fire": "Fire", "critters": "Infestation",
                "invaders": "Intruders", "failure": "Breakdown"}.get(kind, kind)
        inc = dict(kind=kind, name=name, room_id=room.id, timer=120.0)
        self.incidents.append(inc)
        if kind == "fire":
            room.on_fire = True
        elif kind in ("critters", "invaders"):
            pool = D.ENEMIES[:4] if kind == "critters" else D.ENEMIES[1:6]
            n = self.rng.randint(2, 3)
            room.invaders = []
            for _ in range(n):
                e = self.rng.choice(pool)
                room.invaders.append(dict(enemy=list(e), hp=e[1], t=0.0))
        self.notify(message, "bad")
        A.play("alarm")
        self._dispatch_defenders(room)

    def _dispatch_defenders(self, room: Room, want: int = 3):
        """Send nearby idle residents to deal with an emergency."""
        already = set(room.workers)
        pool = [r for r in self.residents.values()
                if r.can_work() and r.id not in already and r.assigned_room is None]
        pool.sort(key=lambda r: abs(r.floor - room.floor) * 4
                  + abs(r.x - (room.x + room.width / 2)))
        for r in pool[:want]:
            self.walk_to_room(r, room)
            r.activity = "emergency"

    def _sim_incidents(self, dt):
        for inc in list(self.incidents):
            room = self.rooms.get(inc["room_id"])
            if not room:
                self.incidents.remove(inc)
                continue
            inc["timer"] -= dt

            responders = [self.residents[w] for w in room.workers
                          if w in self.residents and self.residents[w].alive]
            for res in self.residents.values():
                if (res.alive and not res.on_expedition and res.activity == "emergency"
                        and res.floor == room.floor and not res.path
                        and room.x <= res.x <= room.x + room.width
                        and res not in responders):
                    responders.append(res)

            if room.invaders:
                self._combat_tick(dt, responders, room.invaders, room)
                room.invaders = [e for e in room.invaders if e["hp"] > 0]
                if not room.invaders:
                    self.notify(f"{inc['name']} in {room.data()['name']} dealt with.", "good")
                    self._end_incident(inc, room)
                    continue
            elif inc["kind"] == "fire":
                room.hp = max(0.0, room.hp - 3.5 * dt)
                for h in responders:
                    room.hp = min(100.0, room.hp + 5.0 * dt * (1 + 0.06 * h.stat_total("S")))
                    h.grant_xp(2 * dt)
                if room.hp >= 99:
                    room.on_fire = False
                    self.notify(f"Fire in {room.data()['name']} extinguished.", "good")
                    self._end_incident(inc, room)
                    continue
            elif inc["kind"] == "failure":
                room.hp = max(0.0, room.hp - 2.0 * dt)
                for h in responders:
                    room.hp = min(100.0, room.hp + 5.0 * dt * (1 + 0.06 * h.stat_total("I")))
                    h.grant_xp(2 * dt)
                if room.hp >= 99:
                    self.notify(f"{room.data()['name']} repaired.", "good")
                    self._end_incident(inc, room)
                    continue

            # An unattended emergency hurts whoever is in the room.
            if room.on_fire:
                for h in responders:
                    h.hp = max(0.0, h.hp - 0.8 * dt)
                    self._check_death(h)

            if inc["timer"] <= 0:
                self._end_incident(inc, room)
                # Emergencies that time out spread misery.
                for res in self.residents.values():
                    if res.alive:
                        res.happiness = max(0.0, res.happiness - 6)
                self.notify(f"{inc['name']} in {room.data()['name']} took its toll.", "bad")

    def _end_incident(self, inc, room):
        if inc in self.incidents:
            self.incidents.remove(inc)
        room.on_fire = False
        room.invaders = []
        room.hp = 100.0
        for res in self.residents.values():
            if res.activity == "emergency":
                res.activity = "idle"
                if res.assigned_room:
                    home = self.rooms.get(res.assigned_room)
                    if home:
                        self.walk_to_room(res, home)

    def _check_death(self, res: Resident):
        if res.hp > 0 or not res.alive:
            return
        res.alive = False
        res.hp = 0.0
        res.activity = "dead"
        res.path = []
        if res.assigned_room:
            room = self.rooms.get(res.assigned_room)
            if room and res.id in room.workers:
                room.workers.remove(res.id)
            res.assigned_room = None
        self.deaths += 1
        self.notify(f"{res.name} has died. Revive for {res.revive_cost()} caps.", "bad")
        self.add_floater("DOWN", res.floor, res.x, C.UI_BAD)
        A.play("death")
        for other in self.residents.values():
            if other.alive:
                other.happiness = max(0.0, other.happiness - 8)

    def _combat_tick(self, dt, defenders, invaders, room=None):
        live = [d for d in defenders if d.alive and d.hp > 0]
        for d in live:
            d.step = int(self.time * 9) % 2
            if d.activity not in ("emergency",):
                d.activity = "fight"
            rate = 1.0 + 0.06 * d.stat_total("A")
            d._atk = getattr(d, "_atk", 0.0) + dt * rate
            while d._atk >= 1.0:
                d._atk -= 1.0
                alive_inv = [e for e in invaders if e["hp"] > 0]
                if not alive_inv:
                    break
                e = self.rng.choice(alive_inv)
                mn, mx = d.weapon_damage()
                dmg = self.rng.randint(mn, mx) + d.stat_total("P") // 3
                if self.rng.random() < 0.04 + 0.012 * d.stat_total("L"):
                    dmg *= 2
                    if room:
                        self.add_floater("CRIT!", room.floor, room.x + room.width / 2,
                                         C.UI_ACCENT)
                e["hp"] -= dmg
                A.play("shot", 0.5)
                if e["hp"] <= 0:
                    self.kills += 1
                    d.grant_xp(e["enemy"][4])
                    reward = e["enemy"][5]
                    self.caps += reward
                    self.caps_earned += reward
                    self._track_objective("kills", n=self.kills)
                    if room:
                        self.add_floater(f"+{reward}", room.floor,
                                         room.x + room.width / 2, C.UI_ACCENT)

        for e in invaders:
            if e["hp"] <= 0:
                continue
            e["t"] = e.get("t", 0.0) + dt
            while e["t"] >= 1.0:
                e["t"] -= 1.0
                targets = [x for x in defenders if x.alive and x.hp > 0]
                if not targets:
                    break
                t = self.rng.choice(targets)
                raw = self.rng.randint(e["enemy"][2], e["enemy"][3])
                dmg = max(1, raw - t.armor_total())
                dmg = max(1, int(dmg * (1 - t.dr_total() / 100.0)))
                t.hp = max(0.0, t.hp - dmg)
                A.play("hurt", 0.5)
                if t.power_armor:
                    t.power_armor["durability"] = max(0, t.power_armor.get("durability", 0) - 1)
                    if t.power_armor["durability"] == 0:
                        self.notify(f"{t.name}'s {t.power_armor['name']} has broken.", "warn")
                self._check_death(t)

    # ---- raids through the vault door ----
    def _start_raid(self):
        # Scale the threat to how prepared the shelter actually is — average
        # level and whether anyone is armed — rather than to raw head-count,
        # so a large but unequipped shelter is not simply wiped out.
        fighters = [r for r in self.residents.values()
                    if r.alive and r.age == "adult" and not r.is_robot]
        avg_lv = (sum(r.level for r in fighters) / len(fighters)) if fighters else 1
        armed = sum(1 for r in fighters if r.weapon) / max(1, len(fighters))
        power = avg_lv + armed * 6
        strength = max(1, min(4, 1 + int(power // 5)))
        if power >= 18:
            tier = D.ENEMIES[5:]
        elif power >= 10:
            tier = D.ENEMIES[3:7]
        else:
            tier = D.ENEMIES[1:5]
        squad = []
        for _ in range(strength):
            e = self.rng.choice(tier)
            squad.append(dict(enemy=list(e), hp=e[1], t=0.0))
        # Raiders force the door and work their way down the shaft.
        entry = min((r for r in self.rooms.values()
                     if r.key != "elevator" and r.floor == 0),
                    key=lambda r: abs(r.x - self.elevator_col), default=None)
        if entry is None:
            entry = min((r for r in self.rooms.values() if r.key != "elevator"),
                        key=lambda r: (r.floor, abs(r.x - self.elevator_col)), default=None)
        if entry is None:
            return
        entry.invaders = squad
        inc = dict(kind="raid", name="Raider Attack", room_id=entry.id,
                   timer=600.0, move_timer=18.0)
        self.incidents.append(inc)
        self.notify("Raiders have breached the shelter door!", "bad")
        A.play("alarm")
        self._dispatch_defenders(entry, want=4)

        # Raids advance room to room, so track them separately.
        self._raid_incident = inc

    def _advance_raid(self, inc, dt):
        inc["move_timer"] -= dt
        room = self.rooms.get(inc["room_id"])
        if not room:
            self.incidents.remove(inc)
            return
        if inc["move_timer"] > 0 or not room.invaders:
            return
        inc["move_timer"] = 18.0
        # Push deeper into the shelter.
        candidates = [r for r in self.rooms.values()
                      if r.key != "elevator" and r.id != room.id
                      and (r.floor > room.floor
                           or (r.floor == room.floor and r.x != room.x))]
        if not candidates:
            return
        candidates.sort(key=lambda r: (abs(r.floor - room.floor - 1),
                                       abs(r.x - room.x)))
        nxt = candidates[0]
        nxt.invaders = room.invaders
        room.invaders = []
        inc["room_id"] = nxt.id
        self.notify(f"Raiders push into {nxt.data()['name']}!", "bad")
        self._dispatch_defenders(nxt, want=4)

    # ---- expeditions ----
    def start_expedition(self, res_id: int, duration_min: float = 8.0):
        if not any(r.key == "command" for r in self.rooms.values()):
            self.notify("Build a Command Center to send expeditions.", "bad")
            return
        res = self.residents.get(res_id)
        if not res or not res.alive:
            return
        if res.age == "child":
            self.notify("Children cannot be sent outside.", "bad")
            return
        if res.on_expedition:
            self.notify(f"{res.name} is already outside.", "bad")
            return
        if res.pregnant:
            self.notify(f"{res.name} cannot travel right now.", "bad")
            return
        if res.assigned_room:
            self.assign(res_id, None)
        res.on_expedition = True
        res.activity = "expedition"
        res.path = []
        self.expeditions.append(dict(
            resident_id=res_id, duration=duration_min * 60, elapsed=0.0,
            next_event=self.rng.uniform(8, 16), caps=0, resources={},
            items=[], log=[], returning=False))
        self.notify(f"{res.name} set out into the wasteland.", "info")
        A.play("expedition")

    def recall_expedition(self, res_id: int):
        for e in self.expeditions:
            if e["resident_id"] == res_id and not e["returning"]:
                e["returning"] = True
                # Coming home takes a fraction of the time spent walking out.
                e["duration"] = e["elapsed"] + max(5.0, e["elapsed"] * 0.25)
                res = self.residents.get(res_id)
                if res:
                    self.notify(f"{res.name} is heading home.", "info")
                return

    def _sim_expeditions(self, dt):
        for e in list(self.expeditions):
            e["elapsed"] += dt
            res = self.residents.get(e["resident_id"])
            if res is None:
                self.expeditions.remove(e)
                continue
            if not res.alive:
                self.expeditions.remove(e)
                res.on_expedition = False
                continue
            if e["elapsed"] >= e["next_event"] and e["elapsed"] < e["duration"]:
                e["next_event"] = e["elapsed"] + self.rng.uniform(10, 22)
                self._roll_expedition_event(e, res)
            if e["elapsed"] >= e["duration"]:
                self._finish_expedition(e, res)

    def _finish_expedition(self, e, res: Resident):
        res.on_expedition = False
        res.activity = "idle"
        res.floor = 0
        res.x = float(self.elevator_col)
        res.path = []
        self.caps += e["caps"]
        self.caps_earned += e["caps"]
        for k, v in e["resources"].items():
            cap = self.storage_cap.get(k, 400)
            self.resources[k] = min(cap, self.resources.get(k, 0) + v)
        for it in e["items"]:
            self.add_item(it)
        self.expeditions_completed += 1
        self._track_objective("expeditions", n=self.expeditions_completed)
        self.notify(f"{res.name} returned with {e['caps']} caps and "
                    f"{len(e['items'])} item{'s' if len(e['items']) != 1 else ''}.", "good")
        A.play("expedition")
        self.expeditions.remove(e)

    def _roll_expedition_event(self, e: dict, res: Resident):
        ev = self.rng.choice(D.EXPLORATION_EVENTS)
        log = ev["text"]
        kind = ev["kind"]
        reward = ev.get("reward", {})

        if kind == "loot":
            for k, span in reward.items():
                v = self.rng.randint(*span)
                # Luck stretches every find a little further.
                v = int(v * (1.0 + res.stat_total("L") * 0.03))
                if k == "caps":
                    e["caps"] += v
                elif k == "xp":
                    res.grant_xp(v)
                else:
                    e["resources"][k] = e["resources"].get(k, 0) + v
        elif kind == "loot_weapon":
            pool = [w for w in D.WEAPONS
                    if reward.get("weapon_min_rarity", 0) <= w[-2] <= reward.get("weapon_max_rarity", 2)]
            if pool:
                w = self.rng.choice(pool)
                e["items"].append(make_weapon(w[0]).to_dict())
                log += f" ({w[0]})"
        elif kind == "loot_outfit":
            pool = [o for o in D.OUTFITS
                    if reward.get("outfit_min_rarity", 0) <= o[-2] <= reward.get("outfit_max_rarity", 2)]
            if pool:
                o = self.rng.choice(pool)
                e["items"].append(make_outfit(o[0]).to_dict())
                log += f" ({o[0]})"
        elif kind == "loot_pa":
            chance = reward.get("pa_chance", 0.35) + res.stat_total("L") * 0.01
            if self.rng.random() < chance:
                pa = self.rng.choice(D.POWER_ARMOR)
                e["items"].append(make_power_armor(pa[0]).to_dict())
                log += f" It is a {pa[0]}!"
            else:
                log += " The frame is beyond salvage."
        elif kind == "combat":
            enemy = next(en for en in D.ENEMIES if en[0] == ev["enemy"])
            hp = enemy[1]
            rounds = 0
            while hp > 0 and res.hp > 0 and rounds < 60:
                rounds += 1
                mn, mx = res.weapon_damage()
                hp -= self.rng.randint(mn, mx) + res.stat_total("P") // 3
                if hp <= 0:
                    break
                raw = self.rng.randint(enemy[2], enemy[3])
                dmg = max(1, raw - res.armor_total())
                dmg = max(1, int(dmg * (1 - res.dr_total() / 100.0)))
                res.hp = max(0.0, res.hp - dmg)
            if res.hp <= 0:
                # Out in the wasteland a stimpack is used automatically if carried.
                if self.resources.get("stim", 0) > 0:
                    self.resources["stim"] -= 1
                    res.hp = res.effective_max_hp() * 0.5
                    log += f" A stimpack saved them from the {enemy[0]}."
                else:
                    self._check_death(res)
                    log += f" They fell to the {enemy[0]}."
            else:
                e["caps"] += enemy[5]
                res.grant_xp(enemy[4])
                self.kills += 1
                self._track_objective("kills", n=self.kills)
                log += f" Defeated a {enemy[0]}."
            if res.power_armor:
                res.power_armor["durability"] = max(0, res.power_armor["durability"] - 3)
        else:  # discovery / story
            for k, span in reward.items():
                v = self.rng.randint(*span)
                if k == "caps":
                    e["caps"] += v
                elif k == "xp":
                    res.grant_xp(v)
                else:
                    e["resources"][k] = e["resources"].get(k, 0) + v

        # Radiation seeps in the longer they stay out.
        if self.rng.random() < 0.25:
            res.rads = min(res.max_hp - 1, res.rads + self.rng.uniform(1, 4))

        e["log"].append(f"[{int(e['elapsed'])}s] {log}")
        e["log"] = e["log"][-40:]

    # ---- random events ----
    def _sim_random_events(self, dt):
        self.next_incident_at -= dt
        if self.next_incident_at <= 0:
            self.next_incident_at = self.rng.uniform(150, 300)
            self._spawn_incident()

        self.next_wanderer_at -= dt
        if self.next_wanderer_at <= 0:
            self.next_wanderer_at = self.rng.uniform(150, 320)
            if self.population() < self.housing_cap() and self.rng.random() < 0.75:
                self.spawn_resident()

        self.next_raid_at -= dt
        if self.next_raid_at <= 0:
            self.next_raid_at = self.rng.uniform(420, 900)
            if self.population() >= 8:
                self._start_raid()

        for inc in list(self.incidents):
            if inc["kind"] == "raid":
                self._advance_raid(inc, dt)

    def _spawn_incident(self):
        candidates = [r for r in self.rooms.values()
                      if r.key not in ("elevator", "storage") and r.workers
                      and not r.on_fire and not r.invaders]
        if not candidates:
            return
        room = self.rng.choice(candidates)
        pool = D.INCIDENTS
        total = sum(k["weight"] for k in pool)
        pick = self.rng.uniform(0, total)
        acc = 0.0
        chosen = pool[0]
        for k in pool:
            acc += k["weight"]
            if pick <= acc:
                chosen = k
                break
        self._start_incident(chosen["kind"], room,
                             f"{chosen['name']} in {room.data()['name']}!")

    # ---- objectives ----
    def _track_objective(self, kind: str, **kw):
        for oid, obj in D.OBJECTIVES:
            if oid in self.objectives_done or obj["kind"] != kind:
                continue
            if kind == "build_room":
                if kw.get("room") == obj.get("room"):
                    cur = self.objectives_progress.get(oid, 0) + 1
                    self.objectives_progress[oid] = cur
                    if cur >= obj["n"]:
                        self._complete_objective(oid, obj)
            elif kind in ("collect", "craft", "rush"):
                cur = kw.get("n", 1)
                if kind == "collect" or kind == "craft":
                    cur = self.objectives_progress.get(oid, 0) + kw.get("n", 1)
                self.objectives_progress[oid] = cur
                if cur >= obj["n"]:
                    self._complete_objective(oid, obj)
            else:
                n = kw.get("n", 0)
                self.objectives_progress[oid] = n
                if n >= obj["n"]:
                    self._complete_objective(oid, obj)

        for oid, obj in D.OBJECTIVES:
            if oid in self.objectives_done:
                continue
            if obj["kind"] == "caps_total":
                self.objectives_progress[oid] = self.caps_earned
                if self.caps_earned >= obj["n"]:
                    self._complete_objective(oid, obj)

    def _complete_objective(self, oid, obj):
        self.objectives_done.add(oid)
        rw = obj.get("reward", {})
        caps = rw.get("caps", 0)
        boxes = rw.get("lunchbox", 0)
        self.caps += caps
        self.caps_earned += caps
        self.lunchboxes += boxes
        bits = []
        if caps:
            bits.append(f"{caps} caps")
        if boxes:
            bits.append(f"{boxes} lunchbox{'es' if boxes != 1 else ''}")
        self.notify(f"Objective complete: {obj['desc']}" +
                    (f" (+{', '.join(bits)})" if bits else ""), "good")
        A.play("cash")

    # ---- serialization ----
    def to_dict(self) -> dict:
        return dict(
            version=C.SAVE_VERSION,
            meta=dict(pop=self.population(), caps=self.caps, time=self.time,
                      saved_at=time.time(), seed=self.seed, vault=self.vault_number),
            seed=self.seed, vault_number=self.vault_number,
            time=self.time, paused=self.paused, speed=self.speed,
            caps=self.caps, resources=self.resources, storage_cap=self.storage_cap,
            rooms={rid: {k: v for k, v in r.__dict__.items() if not k.startswith("_")}
                   for rid, r in self.rooms.items()},
            residents={rid: {k: v for k, v in r.__dict__.items() if not k.startswith("_")}
                       for rid, r in self.residents.items()},
            _next_room_id=self._next_room_id,
            _next_resident_id=self._next_resident_id,
            inventory=self.inventory, pa_storage=self.pa_storage,
            lunchboxes=self.lunchboxes,
            objectives_done=list(self.objectives_done),
            objectives_progress=self.objectives_progress,
            kills=self.kills, expeditions_completed=self.expeditions_completed,
            pa_found=self.pa_found, trainings_completed=self.trainings_completed,
            caps_earned=self.caps_earned, births=self.births, deaths=self.deaths,
            rushes_ok=self.rushes_ok, rushes_failed=self.rushes_failed,
            incidents=self.incidents, expeditions=self.expeditions,
            next_incident_at=self.next_incident_at,
            next_wanderer_at=self.next_wanderer_at,
            next_raid_at=self.next_raid_at,
            elevator_col=self.elevator_col,
            auto_collect=self.auto_collect,
        )

    @staticmethod
    def from_dict(d: dict) -> "GameState":
        g = GameState.__new__(GameState)
        g.seed = d.get("seed", 0)
        g.rng = random.Random(g.seed + int(d.get("time", 0)))
        g.vault_number = d.get("vault_number", 101)
        g.time = d.get("time", 0.0)
        g.paused = d.get("paused", False)
        g.speed = d.get("speed", 1.0)
        g.caps = d.get("caps", 0)
        g.resources = d.get("resources", {})
        g.storage_cap = d.get("storage_cap", {})
        g.rooms = {int(rid): Room(**_filter_kwargs(Room, r))
                   for rid, r in d.get("rooms", {}).items()}
        g.residents = {int(rid): Resident(**_filter_kwargs(Resident, r))
                       for rid, r in d.get("residents", {}).items()}
        g._next_room_id = d.get("_next_room_id", 1)
        g._next_resident_id = d.get("_next_resident_id", 1)
        g.inventory = d.get("inventory", [])
        g.pa_storage = d.get("pa_storage", [])
        g.lunchboxes = d.get("lunchboxes", 0)
        g.objectives_done = set(d.get("objectives_done", []))
        g.objectives_progress = d.get("objectives_progress", {})
        g.kills = d.get("kills", 0)
        g.expeditions_completed = d.get("expeditions_completed", 0)
        g.pa_found = d.get("pa_found", 0)
        g.trainings_completed = d.get("trainings_completed", 0)
        g.caps_earned = d.get("caps_earned", 0)
        g.births = d.get("births", 0)
        g.deaths = d.get("deaths", 0)
        g.rushes_ok = d.get("rushes_ok", 0)
        g.rushes_failed = d.get("rushes_failed", 0)
        g.notifications = []
        g.floaters = []
        g.incidents = d.get("incidents", [])
        g.expeditions = d.get("expeditions", [])
        g.next_incident_at = d.get("next_incident_at", 150.0)
        g.next_wanderer_at = d.get("next_wanderer_at", 90.0)
        g.next_raid_at = d.get("next_raid_at", 420.0)
        g.elevator_col = d.get("elevator_col", C.COLUMNS // 2)
        g.auto_collect = d.get("auto_collect", False)
        g.last_lunchbox_reward = []
        return g
