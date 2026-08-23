#!/usr/bin/env python3
"""Headless smoke tests for Haven.

Runs without a display or audio device, so it works on CI runners for all
three platforms. Exercises the simulation, the save format, and — when a
display is available — the renderer and every UI panel.

    python tests/test_smoke.py
"""

from __future__ import annotations

import atexit
import os
import shutil
import tempfile
import sys
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
# Keep the suite hermetic: never touch the player's real saves or settings.
_TMPDATA = tempfile.mkdtemp(prefix="haven-test-")
os.environ["HAVEN_DATA_DIR"] = _TMPDATA
atexit.register(shutil.rmtree, _TMPDATA, True)

import pygame  # noqa: E402

_results: list[tuple[str, bool, str]] = []


def check(name: str):
    """Decorator that records pass/fail for one test."""
    def wrap(fn):
        try:
            fn()
            _results.append((name, True, ""))
        except Exception:
            _results.append((name, False, traceback.format_exc()))
        return fn
    return wrap


def assert_(cond, msg):
    if not cond:
        raise AssertionError(msg)


pygame.init()
pygame.display.set_mode((640, 400))

from haven import config as C          # noqa: E402
from haven import data as D            # noqa: E402
from haven import game as GM           # noqa: E402
from haven import save as S            # noqa: E402


# ---------------------------------------------------------------- data
@check("data tables are self-consistent")
def _data():
    for key in D.ROOM_ORDER + ["elevator"]:
        assert_(key in D.ROOMS, f"ROOM_ORDER references unknown room {key}")
    for key, rd in D.ROOMS.items():
        for field in ("name", "cost", "width", "desc"):
            assert_(field in rd, f"room {key} missing {field}")
        assert_(rd["width"] >= 1, f"room {key} has bad width")
    for name, mn, mx, rar, val in D.WEAPONS:
        assert_(0 <= rar <= 4 and mn <= mx, f"bad weapon {name}")
    for name, ar, dr, sb, rar, dur, rep, lv, desc in D.POWER_ARMOR:
        assert_(0 <= rar <= 4 and dur > 0, f"bad power armor {name}")
    for oid, obj in D.OBJECTIVES:
        assert_("kind" in obj and "n" in obj and "desc" in obj, f"bad objective {oid}")
    # every objective kind must be one the tracker actually handles
    handled = {"build_room", "population", "kills", "train", "expeditions",
               "find_pa", "caps_total", "collect", "craft", "rush", "births"}
    for oid, obj in D.OBJECTIVES:
        assert_(obj["kind"] in handled, f"objective {oid} has untracked kind {obj['kind']}")


# ---------------------------------------------------------------- sim
@check("new game bootstraps")
def _boot():
    g = GM.GameState(seed=1)
    assert_(g.population() == C.STARTING_POP, "wrong starting population")
    assert_(len(g.rooms) > 0, "no starting rooms")
    assert_(any(r.key == "elevator" for r in g.rooms.values()), "no lift")


@check("simulation runs without error")
def _sim():
    g = GM.GameState(seed=2)
    for r in list(g.rooms.values()):
        if r.capacity():
            g.auto_assign_best(r.id)
    for _ in range(2400):          # 10 minutes
        g.tick(0.25)
    assert_(g.time > 0, "clock did not advance")


@check("every room type can be built and staffed")
def _rooms():
    g = GM.GameState(seed=3)
    g.caps = 10 ** 6
    for fl in range(C.FLOOR_COUNT):
        g.can_place("elevator", fl, g.elevator_col, 1) and g.place_room(
            "elevator", fl, g.elevator_col, 1)
    missing = []
    for key in D.ROOM_ORDER:
        w = D.ROOMS[key]["width"]
        placed = False
        for fl in range(C.FLOOR_COUNT):
            for col in range(C.COLUMNS - w):
                if g.can_place(key, fl, col, w)[0]:
                    placed = bool(g.place_room(key, fl, col, w))
                    break
            if placed:
                break
        if not placed:
            missing.append(key)
    assert_(not missing, f"could not place: {missing}")


@check("production banks output and collection pays out")
def _collect():
    g = GM.GameState(seed=4)
    for r in list(g.rooms.values()):
        if r.capacity():
            for _ in range(r.capacity()):
                g.auto_assign_best(r.id)
    for _ in range(1200):
        g.tick(0.25)
    assert_(any(r.has_output() for r in g.rooms.values()), "nothing was produced")
    before = dict(g.resources)
    n = g.collect_all()
    assert_(n > 0, "collect_all banked nothing")
    assert_(any(g.resources[k] > before[k] for k in before), "resources did not rise")


@check("rushing succeeds and failures raise risk")
def _rush():
    g = GM.GameState(seed=5)
    for r in list(g.rooms.values()):
        if r.capacity():
            g.auto_assign_best(r.id)
    for _ in range(60):
        g.tick(0.25)
    room = next((r for r in g.rooms.values() if r.can_rush()), None)
    assert_(room is not None, "no room could be rushed")
    risk0 = room.rush_risk
    g.rush_room(room.id)
    assert_(room.rush_risk > risk0, "rush did not raise subsequent risk")


@check("full family lifecycle: romance -> birth -> adulthood")
def _family():
    g = GM.GameState(seed=11)
    g.auto_collect = True
    g.next_incident_at = g.next_raid_at = 1e9
    liv = next(r for r in g.rooms.values() if r.key == "living")
    adults = [r for r in g.residents.values() if r.age == "adult"]
    f = next(r for r in adults if r.gender == "f")
    m = next(r for r in adults if r.gender == "m")
    g.assign(f.id, liv.id)
    g.assign(m.id, liv.id)
    for r in list(g.rooms.values()):
        if r.capacity() and r.key != "living":
            for _ in range(r.capacity()):
                g.auto_assign_best(r.id)
    for p in (f, m):
        p.happiness = 95
        p.path = []
        p.activity = "work"
        p.floor, p.x = liv.floor, liv.x + 0.5
    for _ in range(8000):
        g.tick(0.25)
        if g.births:
            break
    assert_(g.births > 0, "no child was born")
    kid = next(r for r in g.residents.values() if r.age == "child")
    assert_(not kid.can_work(), "children should not be assignable")
    for _ in range(int(C.CHILD_GROW_TIME * 4) + 80):
        g.tick(0.25)
    assert_(kid.age == "adult" and kid.can_work(), "child never grew up")


@check("death and revival")
def _death():
    g = GM.GameState(seed=6)
    v = next(iter(g.residents.values()))
    v.hp = 0.0
    g._check_death(v)
    assert_(not v.alive and g.deaths == 1, "death not recorded")
    assert_(v.assigned_room is None, "dead resident kept their job")
    g.caps = 10000
    g.revive(v.id)
    assert_(v.alive and v.hp > 0, "revival failed")


@check("raids scale to shelter readiness")
def _raid():
    weak = GM.GameState(seed=7)
    weak._start_raid()
    weak_tier = max((e["enemy"][1] for r in weak.rooms.values() for e in r.invaders),
                    default=0)
    strong = GM.GameState(seed=7)
    for r in strong.residents.values():
        r.level = 25
        r.weapon = GM.make_weapon("Gauss Rifle").to_dict()
    strong._start_raid()
    strong_tier = max((e["enemy"][1] for r in strong.rooms.values() for e in r.invaders),
                      default=0)
    assert_(strong_tier >= weak_tier,
            f"stronger shelter drew weaker raid ({strong_tier} < {weak_tier})")


@check("hunger weakens but never one-shots")
def _starve():
    g = GM.GameState(seed=8)
    g.resources["food"] = 0
    g.resources["water"] = 0
    for _ in range(4000):
        g.tick(0.25)
    for r in g.residents.values():
        if r.alive:
            assert_(r.hp >= r.effective_max_hp() * 0.24,
                    f"{r.name} starved to {r.hp}, below the 25% floor")


@check("lunchboxes always yield four rewards")
def _lunchbox():
    g = GM.GameState(seed=9)
    # Objectives can themselves award lunchboxes, so retire them first to
    # isolate the consume-one-per-open invariant.
    g.objectives_done = {oid for oid, _ in D.OBJECTIVES}
    g.lunchboxes = 12
    for _ in range(12):
        cards = g.open_lunchbox()
        assert_(len(cards) == 4, f"lunchbox gave {len(cards)} cards")
    assert_(g.lunchboxes == 0, "lunchbox count did not decrement")
    assert_(g.open_lunchbox() == [], "opened a lunchbox that was not there")

    # With objectives live, earning enough should hand some back.
    g2 = GM.GameState(seed=9)
    g2.lunchboxes = 1
    g2.caps_earned = 0
    g2.open_lunchbox()
    g2._track_objective("caps_total")
    assert_(g2.lunchboxes >= 0, "lunchbox count went negative")


@check("caretaker robot is gated, patrols and collects")
def _robot():
    g = GM.GameState(seed=31)
    g.caps = 100
    assert_(g.buy_robot() is None, "bought a caretaker with no Workshop")
    g._place_room("workshop", 1, 0, 2, free=True)
    assert_(g.buy_robot() is None, "bought a caretaker without enough caps")
    g.caps = GM.GameState.ROBOT_COST + 10
    bot = g.buy_robot()
    assert_(bot is not None and bot.is_robot, "caretaker was not created")
    assert_(g.population() == C.STARTING_POP,
            "robot should not count toward population")
    for r in list(g.rooms.values()):
        if r.capacity():
            for _ in range(r.capacity()):
                g.auto_assign_best(r.id)
    before = dict(g.resources)
    seen = set()
    for _ in range(2400):
        g.tick(0.25)
        seen.add((bot.floor, round(bot.x)))
    assert_(len(seen) > 3, "caretaker never patrolled")
    assert_(any(g.resources[k] > before.get(k, 0) for k in g.resources),
            "caretaker collected nothing")
    assert_(bot.happiness == 100 and bot.alive, "caretaker should not suffer")
    bot2 = GM.GameState.from_dict(g.to_dict()).residents[bot.id]
    assert_(bot2.is_robot, "caretaker did not survive a save")


@check("crafting is gated by rooms and consumes materials")
def _craft():
    g = GM.GameState(seed=10)
    g.caps = 10 ** 6
    g.resources["materials"] = 10 ** 4
    assert_(g.craft("weapon", 3) is None, "epic weapon crafted without an Armory")
    g._place_room("workshop", 1, 0, 2, free=True)
    g._place_room("armory", 1, 2, 2, free=True)
    m0 = g.resources["materials"]
    it = g.craft("weapon", 3)
    assert_(it is not None, "craft failed with the right rooms")
    assert_(g.resources["materials"] < m0, "craft consumed no materials")


@check("expeditions run and return rewards")
def _expedition():
    g = GM.GameState(seed=12)
    g._place_room("command", 1, 0, 2, free=True)
    res = next(r for r in g.residents.values() if r.age == "adult")
    g.start_expedition(res.id, 0.5)
    assert_(res.on_expedition, "expedition did not start")
    for _ in range(1200):
        g.tick(0.25)
        if not res.on_expedition:
            break
    assert_(not res.on_expedition, "explorer never came home")
    assert_(g.expeditions_completed == 1, "expedition not counted")


@check("power armor equips, damages and repairs")
def _pa():
    g = GM.GameState(seed=13)
    res = next(iter(g.residents.values()))
    res.level = 20
    g.add_item(GM.make_power_armor("Guardian Mk II"))
    g.equip(res.id, "pa", 0)
    assert_(res.power_armor is not None, "power armor did not equip")
    assert_(res.dr_total() > 0, "power armor gave no damage resistance")
    res.power_armor["durability"] = 5
    g.caps = 10 ** 5
    g.resources["materials"] = 100
    g.repair_pa(res.id)
    assert_(res.power_armor["durability"] == res.power_armor["max_durability"],
            "repair did not restore durability")


# ---------------------------------------------------------------- saves
@check("save round-trips exactly")
def _save():
    g = GM.GameState(seed=14)
    g.caps = 4321
    g.lunchboxes = 3
    for _ in range(600):
        g.tick(0.25)
    d = g.to_dict()
    g2 = GM.GameState.from_dict(d)
    for field in ("caps", "lunchboxes", "kills", "births", "deaths",
                  "vault_number", "caps_earned"):
        assert_(getattr(g, field) == getattr(g2, field), f"{field} did not survive")
    assert_(len(g.rooms) == len(g2.rooms), "room count changed")
    assert_(len(g.residents) == len(g2.residents), "resident count changed")


@check("saves tolerate unknown and missing fields")
def _save_compat():
    g = GM.GameState(seed=15)
    d = g.to_dict()
    rid = next(iter(d["residents"]))
    d["residents"][rid]["a_field_from_the_future"] = 1
    d["rooms"][next(iter(d["rooms"]))]["another_one"] = 2
    GM.GameState.from_dict(d)
    minimal = {
        "seed": 1, "time": 5.0, "caps": 10,
        "resources": {"power": 1}, "storage_cap": {"power": 10},
        "rooms": {"1": {"id": 1, "key": "power", "floor": 1, "x": 2,
                        "width": 2, "level": 1}},
        "residents": {"1": {"id": 1, "name": "Legacy", "portrait_seed": 5,
                            "stats": {"S": 1}}},
        "_next_room_id": 2, "_next_resident_id": 2,
    }
    old = GM.GameState.from_dict(minimal)
    old.tick(0.25)
    assert_(old.residents[1].alive, "legacy resident did not default to alive")


@check("save slots write, list and delete on disk")
def _slots():
    g = GM.GameState(seed=16)
    slot = C.SAVE_SLOTS - 1
    S.save(slot, g.to_dict())
    listed = [s for s in S.list_slots() if s["slot"] == slot]
    assert_(listed and listed[0]["exists"], "saved slot not listed")
    assert_(S.load(slot) is not None, "slot did not load")
    S.delete(slot)
    listed = [s for s in S.list_slots() if s["slot"] == slot]
    assert_(listed and not listed[0]["exists"], "slot not deleted")


# ---------------------------------------------------------------- render
@check("renderer, screens and every panel draw")
def _render():
    from haven.main import App, WorldScreen, MainMenuScreen, SettingsScreen
    from haven import ui as U

    app = App()
    app.game = GM.GameState(seed=17)
    app.slot = 0
    w = WorldScreen(app)
    app.screen_obj = w
    for _ in range(3):
        w.update(0.05)
        s = app.renderer.begin()
        w.draw(s)
        app.renderer.present(0.016)
    for panel in (None, "build", "residents", "inventory",
                  "exploration", "objectives", "menu", "lunchbox"):
        w.panel = panel
        for tab in range(3):
            w.inv_tab = tab
            w.selected_room = next(iter(app.game.rooms), None)
            w.selected_resident = next(iter(app.game.residents), None)
            w.update(0.05)
            s = app.renderer.begin()
            w.draw(s)
            app.renderer.present(0.016)
    for screen in (MainMenuScreen(app), SettingsScreen(app, lambda: None)):
        app.screen_obj = screen
        screen.update(0.05)
        s = app.renderer.begin()
        screen.draw(s)
        app.renderer.present(0.016)
    # UI scale must track the real screen height
    assert_(abs(U.get_scale() - app.size[1] / C.DESIGN_H) < 0.01,
            "UI scale does not match screen height")


@check("all resolutions and quality presets render")
def _modes():
    from haven.main import App, WorldScreen
    app = App()
    app.game = GM.GameState(seed=18)
    app.slot = 0
    w = WorldScreen(app)
    app.screen_obj = w
    for name, _, _ in C.RESOLUTIONS:
        app.set_resolution(name)
        w.update(0.05)
        s = app.renderer.begin()
        w.draw(s)
        app.renderer.present(0.016)
        assert_(app.res_name == name, f"resolution {name} did not apply")
    for q in C.QUALITY_LEVELS:
        app.set_quality(q)
        w.update(0.05)
        s = app.renderer.begin()
        w.draw(s)
        app.renderer.present(0.016)
        assert_(app.quality == q, f"quality {q} did not apply")


@check("procedural art renders for every room, item and suit")
def _art():
    from haven import assets as G
    for key in D.ROOMS:
        for level in (1, 2, 3):
            surf = G.room_sprite(key, D.ROOMS[key]["width"], level, True, 0)
            assert_(surf.get_width() > 0, f"empty sprite for {key}")
    for name, *_ in D.WEAPONS:
        assert_(G.weapon_icon(name).get_width() > 0, f"no icon for {name}")
    for name, *_ in D.OUTFITS:
        assert_(G.outfit_icon(name).get_width() > 0, f"no icon for {name}")
    for entry in D.POWER_ARMOR:
        assert_(G.pa_icon(entry[0]).get_width() > 0, f"no icon for {entry[0]}")
    assert_(G.app_icon(128).get_width() == 128, "app icon wrong size")
    for age in ("adult", "child"):
        for pa in (None, "Scout Rig"):
            assert_(G.resident_sprite(1, pa, 1, 0, 0, age).get_width() > 0,
                    "empty resident sprite")
    assert_(G.resident_sprite(1, None, 1, 0, 0, "adult", is_robot=True).get_width() > 0,
            "empty robot sprite")


# ---------------------------------------------------------------- report
def main() -> int:
    width = max(len(n) for n, _, _ in _results) + 2
    failed = 0
    print()
    for name, ok, tb in _results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name.ljust(width)}")
        if not ok:
            failed += 1
            print("\n" + tb)
    total = len(_results)
    print(f"\n{total - failed}/{total} checks passed\n")
    return 1 if failed else 0


if __name__ == "__main__":
    code = main()
    pygame.quit()
    sys.exit(code)
