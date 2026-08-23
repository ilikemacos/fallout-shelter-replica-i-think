"""Entry point, main loop, and screens for Haven."""

from __future__ import annotations

import math
import sys
import time as _time

import pygame

from . import assets as G
from . import audio as A
from . import config as C
from . import data as D
from . import game as GM
from . import render as R
from . import save as S
from . import ui as U

# HUD metrics, in design units (see ui.s()).
HUD_TOP = 96
HUD_BOT = 54
PANEL_W = 430


def res_index(name: str) -> int:
    for i, (n, _, _) in enumerate(C.RESOLUTIONS):
        if n == name:
            return i
    return 2


# ======================================================================
# Camera
# ======================================================================
class Camera:
    def __init__(self, w, h):
        self.x = C.COLUMNS * C.CELL_W / 2
        self.y = 2.5 * C.CELL_H
        self.zoom = 0.78
        self.viewport = pygame.Rect(0, 0, w, h)
        self.resize(w, h)
        self._drag = False
        self._last = None

    def resize(self, w, h):
        top = U.s(HUD_TOP)
        bot = U.s(HUD_BOT)
        self.viewport = pygame.Rect(0, top, w, max(80, h - top - bot))

    # -- zoom is quantised so cached scaled sprites are reused between frames
    @property
    def qzoom(self) -> float:
        return round(self.zoom * 20) / 20.0

    def world_to_screen(self, wx, wy):
        z = self.qzoom
        cx = self.viewport.centerx
        cy = self.viewport.centery
        return (cx + (wx - self.x) * z, cy + (wy - self.y) * z)

    def screen_to_world(self, sx, sy):
        z = self.qzoom
        cx = self.viewport.centerx
        cy = self.viewport.centery
        return ((sx - cx) / z + self.x, (sy - cy) / z + self.y)

    def clamp(self):
        self.x = max(-C.CELL_W * 2, min(C.COLUMNS * C.CELL_W + C.CELL_W * 2, self.x))
        self.y = max(-C.CELL_H * 2, min(C.FLOOR_COUNT * C.CELL_H + C.CELL_H * 2, self.y))

    def update(self, dt, keys):
        pan = 900 * dt / max(0.35, self.zoom)
        if keys[pygame.K_LEFT] or keys[pygame.K_a]:
            self.x -= pan
        if keys[pygame.K_RIGHT] or keys[pygame.K_d]:
            self.x += pan
        if keys[pygame.K_UP] or keys[pygame.K_w]:
            self.y -= pan
        if keys[pygame.K_DOWN] or keys[pygame.K_s]:
            self.y += pan
        self.clamp()

    def focus(self, floor, x):
        self.x = x * C.CELL_W
        self.y = floor * C.CELL_H + C.CELL_H / 2
        self.clamp()

    def handle(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button in (2, 3):
            self._drag = True
            self._last = event.pos
        elif event.type == pygame.MOUSEBUTTONUP and event.button in (2, 3):
            self._drag = False
            self._last = None
        elif event.type == pygame.MOUSEMOTION and self._drag and self._last:
            z = self.qzoom
            self.x -= (event.pos[0] - self._last[0]) / z
            self.y -= (event.pos[1] - self._last[1]) / z
            self._last = event.pos
            self.clamp()
        elif event.type == pygame.MOUSEWHEEL:
            mx, my = pygame.mouse.get_pos()
            before = self.screen_to_world(mx, my)
            self.zoom = max(0.25, min(1.6, self.zoom * (1.12 if event.y > 0 else 1 / 1.12)))
            after = self.screen_to_world(mx, my)
            self.x += before[0] - after[0]
            self.y += before[1] - after[1]
            self.clamp()


# ======================================================================
# Scaled-sprite cache
# ======================================================================
_scaled: dict = {}


def scaled(surf: pygame.Surface, w: int, h: int, key) -> pygame.Surface:
    w, h = max(1, int(w)), max(1, int(h))
    ck = (key, w, h)
    got = _scaled.get(ck)
    if got is None:
        if len(_scaled) > 2200:
            _scaled.clear()
        got = pygame.transform.smoothscale(surf, (w, h))
        _scaled[ck] = got
    return got


# ======================================================================
# Main menu
# ======================================================================
class MainMenuScreen:
    def __init__(self, app):
        self.app = app
        self.t = 0.0
        self._layout()

    def _layout(self):
        w, h = self.app.size
        bw, bh = U.s(300), U.s(48)
        cx = w // 2 - bw // 2
        y = int(h * 0.52)
        gap = U.s(58)
        has_save = any(s["exists"] for s in S.list_slots())
        self.buttons = [
            U.Button((cx, y, bw, bh), "Continue", self._continue,
                     style="primary", enabled=has_save, size=18),
            U.Button((cx, y + gap, bw, bh), "New Game",
                     lambda: self.app.set_screen(SlotSelectScreen(self.app, "new")), size=18),
            U.Button((cx, y + gap * 2, bw, bh), "Load Game",
                     lambda: self.app.set_screen(SlotSelectScreen(self.app, "load")), size=18),
            U.Button((cx, y + gap * 3, bw, bh), "Settings",
                     lambda: self.app.set_screen(
                         SettingsScreen(self.app, lambda: self.app.set_screen(MainMenuScreen(self.app)))),
                     size=18),
            U.Button((cx, y + gap * 4, bw, bh), "Quit",
                     lambda: setattr(self.app, "running", False), size=18),
        ]

    def resize(self):
        self._layout()

    def update(self, dt):
        self.t += dt
        for b in self.buttons:
            b.update(dt)

    def _continue(self):
        best = None
        for s in S.list_slots():
            if s["exists"] and not s["meta"].get("broken"):
                if best is None or s["meta"].get("saved_at", 0) > best["meta"].get("saved_at", 0):
                    best = s
        if best:
            d = S.load(best["slot"])
            if d:
                self.app.start_game(GM.GameState.from_dict(d), best["slot"])

    def draw(self, s):
        w, h = s.get_size()
        for y in range(0, h, 2):
            t = y / h
            c = (int(16 + 26 * t), int(20 + 24 * t), int(38 + 34 * t))
            pygame.draw.rect(s, c, (0, y, w, 2))
        # drifting dust motes
        for i in range(70):
            px = (i * 137 + self.t * (12 + i % 7) * 3) % w
            py = (i * 83 + self.t * 9) % h
            a = 40 + (i % 5) * 22
            pygame.draw.circle(s, (a + 60, a + 50, a + 40), (int(px), int(py)), U.s(1) + (i % 2))

        icon = G.app_icon(U.s(150))
        s.blit(icon, (w // 2 - icon.get_width() // 2, int(h * 0.13)))
        U.text(s, "HAVEN", (w // 2, int(h * 0.38)), C.UI_ACCENT,
               size=76, bold=True, center=True, shadow=True)
        U.text(s, "an underground shelter management game",
               (w // 2, int(h * 0.44)), C.UI_TEXT_DIM, size=17, center=True)
        for b in self.buttons:
            b.draw(s)
        U.text(s, f"v1.0.0  ·  {self.app.renderer.name} · {self.app.res_name}",
               (U.s(12), h - U.s(24)), C.UI_TEXT_DIM, size=13)
        U.text(s, "Original work — not affiliated with any other game or franchise.",
               (w - U.s(12), h - U.s(24)), C.UI_TEXT_DIM, size=13, right=True)

    def handle(self, e):
        for b in self.buttons:
            b.handle(e)


# ======================================================================
# Slot select
# ======================================================================
class SlotSelectScreen:
    def __init__(self, app, mode="new"):
        self.app = app
        self.mode = mode
        self.confirm = None
        self.refresh()

    def refresh(self):
        self.slots = S.list_slots()

    def resize(self):
        pass

    def update(self, dt):
        pass

    def draw(self, s):
        w, h = s.get_size()
        s.fill((20, 22, 32))
        U.text(s, "Choose a Slot" if self.mode == "new" else "Load Game",
               (w // 2, U.s(56)), C.UI_ACCENT, size=38, bold=True, center=True)
        self._rows = []
        cw, ch = U.s(520), U.s(104)
        x = w // 2 - cw // 2
        y = U.s(130)
        for slot in self.slots:
            r = pygame.Rect(x, y, cw, ch)
            hov = r.collidepoint(pygame.mouse.get_pos())
            pygame.draw.rect(s, C.UI_BG2 if hov else C.UI_BG, r, border_radius=U.s(6))
            pygame.draw.rect(s, C.UI_ACCENT if hov else C.UI_BORDER, r,
                             max(1, U.s(1)), border_radius=U.s(6))
            del_r = None
            if slot["exists"]:
                m = slot["meta"]
                if m.get("broken"):
                    U.text(s, f"Slot {slot['slot']+1} — damaged save",
                           (r.x + U.s(14), r.y + U.s(16)), C.UI_BAD, size=20, bold=True)
                    U.text(s, "A backup will be tried on load.",
                           (r.x + U.s(14), r.y + U.s(46)), C.UI_TEXT_DIM, size=14)
                else:
                    U.text(s, f"Haven {m.get('vault', '—')}",
                           (r.x + U.s(14), r.y + U.s(12)), C.UI_ACCENT, size=22, bold=True)
                    U.text(s, f"{m.get('pop','?')} residents · {m.get('caps','?')} caps · "
                              f"{int(m.get('time',0)//60)} min played",
                           (r.x + U.s(14), r.y + U.s(44)), C.UI_TEXT, size=15)
                    U.text(s, _time.strftime("%Y-%m-%d %H:%M",
                                             _time.localtime(m.get("saved_at", 0))),
                           (r.x + U.s(14), r.y + U.s(70)), C.UI_TEXT_DIM, size=13)
                del_r = pygame.Rect(r.right - U.s(96), r.centery - U.s(17), U.s(80), U.s(34))
                pygame.draw.rect(s, (118, 44, 44), del_r, border_radius=U.s(4))
                U.text(s, "Delete", del_r.center, C.UI_TEXT, size=14, center=True)
            else:
                U.text(s, f"Slot {slot['slot']+1}", (r.x + U.s(14), r.y + U.s(20)),
                       C.UI_TEXT_DIM, size=20, bold=True)
                U.text(s, "Empty", (r.x + U.s(14), r.y + U.s(52)), C.UI_TEXT_DIM, size=15)
            self._rows.append((slot, r, del_r))
            y += ch + U.s(16)

        self.back_r = pygame.Rect(U.s(24), h - U.s(64), U.s(110), U.s(40))
        pygame.draw.rect(s, C.UI_BG2, self.back_r, border_radius=U.s(4))
        pygame.draw.rect(s, C.UI_BORDER, self.back_r, max(1, U.s(1)), border_radius=U.s(4))
        U.text(s, "Back", self.back_r.center, C.UI_TEXT, size=16, center=True)

        if self.confirm is not None:
            veil = pygame.Surface((w, h), pygame.SRCALPHA)
            veil.fill((0, 0, 0, 180))
            s.blit(veil, (0, 0))
            box = pygame.Rect(w // 2 - U.s(220), h // 2 - U.s(90), U.s(440), U.s(180))
            U.panel(s, box, "Overwrite this slot?")
            U.text(s, "The existing shelter will be lost.",
                   (box.centerx, box.y + U.s(72)), C.UI_TEXT, size=16, center=True)
            self.yes_r = pygame.Rect(box.x + U.s(40), box.bottom - U.s(58), U.s(160), U.s(40))
            self.no_r = pygame.Rect(box.right - U.s(200), box.bottom - U.s(58), U.s(160), U.s(40))
            pygame.draw.rect(s, (124, 44, 44), self.yes_r, border_radius=U.s(4))
            U.text(s, "Overwrite", self.yes_r.center, C.UI_TEXT, size=16, center=True, bold=True)
            pygame.draw.rect(s, C.UI_BG2, self.no_r, border_radius=U.s(4))
            pygame.draw.rect(s, C.UI_BORDER, self.no_r, max(1, U.s(1)), border_radius=U.s(4))
            U.text(s, "Cancel", self.no_r.center, C.UI_TEXT, size=16, center=True)

    def handle(self, e):
        if e.type == pygame.KEYDOWN and e.key == pygame.K_ESCAPE:
            self.app.set_screen(MainMenuScreen(self.app))
            return
        if e.type != pygame.MOUSEBUTTONDOWN or e.button != 1:
            return
        if self.confirm is not None:
            if self.yes_r.collidepoint(e.pos):
                self.app.start_game(GM.GameState(), self.confirm)
            elif self.no_r.collidepoint(e.pos):
                self.confirm = None
            return
        if self.back_r.collidepoint(e.pos):
            self.app.set_screen(MainMenuScreen(self.app))
            return
        for slot, r, del_r in self._rows:
            if del_r and del_r.collidepoint(e.pos):
                S.delete(slot["slot"])
                self.refresh()
                return
            if r.collidepoint(e.pos):
                if self.mode == "new":
                    if slot["exists"]:
                        self.confirm = slot["slot"]
                    else:
                        self.app.start_game(GM.GameState(), slot["slot"])
                else:
                    d = S.load(slot["slot"])
                    if d:
                        self.app.start_game(GM.GameState.from_dict(d), slot["slot"])
                    else:
                        self.refresh()
                return


# ======================================================================
# Settings
# ======================================================================
class SettingsScreen:
    def __init__(self, app, back):
        self.app = app
        self.back = back
        self._build()

    def _build(self):
        w, h = self.app.size
        a = A.get_settings()
        cx = w // 2
        lx = cx - U.s(250)
        wx = cx + U.s(10)
        y = U.s(120)
        gap = U.s(46)
        self.rows = []
        self.widgets = []

        def slider(label, val, cb):
            nonlocal y
            sl = U.Slider((wx, y, U.s(240), U.s(20)), val, cb)
            self.rows.append((label, sl, y))
            self.widgets.append(sl)
            y += gap

        def toggle(label, val, cb):
            nonlocal y
            tg = U.Toggle((wx, y - U.s(3), U.s(62), U.s(26)), val, cb)
            self.rows.append((label, tg, y))
            self.widgets.append(tg)
            y += gap

        slider("Master volume", a["master"], lambda v: A.set_master(v))
        slider("Music volume", a["music"],
               lambda v: A.set_music(v, A.get_settings()["music_on"]))
        slider("Effects volume", a["sfx"],
               lambda v: A.set_sfx(v, A.get_settings()["sfx_on"]))
        toggle("Music", a["music_on"], lambda v: A.set_music(A.get_settings()["music"], v))
        toggle("Sound effects", a["sfx_on"], lambda v: A.set_sfx(A.get_settings()["sfx"], v))
        y += U.s(8)

        # Resolution picker
        self.res_y = y
        self.res_rects = []
        y += gap
        # Quality picker
        self.qual_y = y
        self.qual_rects = []
        y += gap

        toggle("Fullscreen", self.app.fullscreen, self.app.set_fullscreen)
        toggle("Auto-collect resources", self.app.game.auto_collect if self.app.game else False,
               self._set_autocollect)
        self.bottom_y = y

        bw, bh = U.s(120), U.s(42)
        self.back_btn = U.Button((U.s(24), h - U.s(66), bw, bh), "Back", self._go_back)
        self.reset_btn = U.Button((w - U.s(240), h - U.s(66), U.s(216), bh),
                                  "Reset This Shelter", self._reset, style="danger", size=15)

    def _set_autocollect(self, v):
        if self.app.game:
            self.app.game.auto_collect = v

    def _go_back(self):
        self.app.save_settings()
        self.back()

    def _reset(self):
        if self.app.slot is not None:
            S.delete(self.app.slot)
            self.app.start_game(GM.GameState(), self.app.slot)

    def resize(self):
        self._build()

    def update(self, dt):
        self.back_btn.update(dt)
        self.reset_btn.update(dt)

    def draw(self, s):
        w, h = s.get_size()
        s.fill((20, 22, 32))
        U.text(s, "Settings", (w // 2, U.s(54)), C.UI_ACCENT, size=38, bold=True, center=True)
        cx = w // 2
        lx = cx - U.s(250)
        for label, widget, y in self.rows:
            U.text(s, label, (lx, y), C.UI_TEXT, size=17)
            widget.draw(s)
            if isinstance(widget, U.Slider):
                U.text(s, f"{int(widget.value * 100)}%",
                       (widget.rect.right + U.s(12), y), C.UI_TEXT_DIM, size=15)
            else:
                U.text(s, "On" if widget.value else "Off",
                       (widget.rect.right + U.s(12), y), C.UI_TEXT_DIM, size=15)

        # Resolution row
        U.text(s, "Resolution", (lx, self.res_y), C.UI_TEXT, size=17)
        self.res_rects = []
        x = cx + U.s(10)
        for name, rw, rh in C.RESOLUTIONS:
            bw = U.s(86)
            r = pygame.Rect(x, self.res_y - U.s(5), bw, U.s(28))
            on = name == self.app.res_name
            pygame.draw.rect(s, C.UI_ACCENT if on else C.UI_BG2, r, border_radius=U.s(4))
            pygame.draw.rect(s, C.UI_BORDER, r, max(1, U.s(1)), border_radius=U.s(4))
            U.text(s, name, r.center, (28, 20, 6) if on else C.UI_TEXT,
                   size=13, center=True, bold=on)
            self.res_rects.append((r, name))
            x += bw + U.s(6)

        # Quality row
        U.text(s, "Graphics quality", (lx, self.qual_y), C.UI_TEXT, size=17)
        self.qual_rects = []
        x = cx + U.s(10)
        for q in C.QUALITY_LEVELS:
            bw = U.s(86)
            r = pygame.Rect(x, self.qual_y - U.s(5), bw, U.s(28))
            on = q == self.app.quality
            pygame.draw.rect(s, C.UI_ACCENT if on else C.UI_BG2, r, border_radius=U.s(4))
            pygame.draw.rect(s, C.UI_BORDER, r, max(1, U.s(1)), border_radius=U.s(4))
            U.text(s, q.title(), r.center, (28, 20, 6) if on else C.UI_TEXT,
                   size=13, center=True, bold=on)
            self.qual_rects.append((r, q))
            x += bw + U.s(6)

        y = self.bottom_y + U.s(14)
        U.text(s, f"Renderer: {self.app.renderer.name} — {self.app.renderer.gl_info}",
               (lx, y), C.UI_TEXT_DIM, size=14)
        if self.app.renderer_note:
            U.text(s, f"OpenGL unavailable: {self.app.renderer_note}",
                   (lx, y + U.s(20)), C.UI_WARN, size=13)
        U.text(s, "Camera: WASD/arrows pan · wheel zooms · right-drag pans",
               (lx, y + U.s(44)), C.UI_TEXT_DIM, size=14)
        U.text(s, "Drag a resident onto a room to put them to work.",
               (lx, y + U.s(64)), C.UI_TEXT_DIM, size=14)
        U.text(s, f"Saves: {S.user_data_dir()}", (lx, y + U.s(88)), C.UI_TEXT_DIM, size=13)

        self.back_btn.draw(s)
        if self.app.slot is not None:
            self.reset_btn.draw(s)

    def handle(self, e):
        if e.type == pygame.KEYDOWN and e.key == pygame.K_ESCAPE:
            self._go_back()
            return
        for wdg in self.widgets:
            if wdg.handle(e):
                return
        self.back_btn.handle(e)
        if self.app.slot is not None:
            self.reset_btn.handle(e)
        if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
            for r, name in self.res_rects:
                if r.collidepoint(e.pos):
                    self.app.set_resolution(name)
                    return
            for r, q in self.qual_rects:
                if r.collidepoint(e.pos):
                    self.app.set_quality(q)
                    return


# ======================================================================
# World
# ======================================================================
class WorldScreen:
    def __init__(self, app):
        self.app = app
        self.cam = Camera(*app.size)
        self.selected_room = None
        self.selected_resident = None
        self.mode = "look"
        self.build_key = None
        self.build_width = 2
        self.panel = None
        self.autosave_in = 45.0
        self.scroll = U.ScrollList()
        self.inv_tab = 0
        self.tooltip = None
        self._drag_res = None
        self._drag_from = None
        self._drag_moved = False
        self._collect_badges = []
        self._build()

    # ---------- layout ----------
    def _build(self):
        w, h = self.app.size
        by = h - U.s(HUD_BOT) + U.s(7)
        bh = U.s(40)
        x = U.s(10)

        def mk(label, cb, wdt, style="normal", tip=None):
            nonlocal x
            b = U.Button((x, by, U.s(wdt), bh), label, cb, style=style,
                         size=15, tooltip_lines=tip)
            x += U.s(wdt) + U.s(6)
            return b

        self.bottom = [
            mk("Build", lambda: self._mode("build"), 92, "primary",
               ["Build (B)", "Place new rooms on the grid."]),
            mk("Demolish", lambda: self._mode("destroy"), 100, "danger",
               ["Demolish", "Remove a room and refund a quarter of its cost."]),
            mk("Collect All", self._collect_all, 112, "good",
               ["Collect All (C)", "Bank every room's finished output."]),
            mk("Residents", lambda: self._toggle("residents"), 108, "normal",
               ["Residents (R)"]),
            mk("Inventory", lambda: self._toggle("inventory"), 104, "normal",
               ["Inventory (I)"]),
            mk("Explore", lambda: self._toggle("exploration"), 96, "normal",
               ["Expeditions (E)"]),
            mk("Objectives", lambda: self._toggle("objectives"), 108, "normal",
               ["Objectives (O)"]),
            mk("Menu", lambda: self._toggle("menu"), 84, "normal", ["Menu (Esc)"]),
        ]
        sx = w - U.s(232)
        self.speed_btns = [
            U.Button((sx, by, U.s(64), bh), "Pause", lambda: self._speed(0), size=14),
            U.Button((sx + U.s(68), by, U.s(46), bh), "1x", lambda: self._speed(1), size=14),
            U.Button((sx + U.s(118), by, U.s(46), bh), "2x", lambda: self._speed(2), size=14),
            U.Button((sx + U.s(168), by, U.s(50), bh), "4x", lambda: self._speed(4), size=14),
        ]

    def resize(self):
        self.cam.resize(*self.app.size)
        self._build()

    @property
    def g(self) -> GM.GameState:
        return self.app.game

    # ---------- actions ----------
    def _mode(self, m):
        if self.mode == m:
            self.mode = "look"
            self.build_key = None
            if self.panel == "build":
                self.panel = None
        else:
            self.mode = m
            self.panel = "build" if m == "build" else None
        A.play("click")

    def _toggle(self, p):
        self.panel = None if self.panel == p else p
        self.scroll.offset = 0
        if p != "build" and self.mode == "build":
            self.mode = "look"
            self.build_key = None
        A.play("click")

    def _speed(self, sp):
        if sp == 0:
            self.g.paused = not self.g.paused
        else:
            self.g.paused = False
            self.g.speed = sp
        A.play("click")

    def _collect_all(self):
        if self.g.collect_all() == 0:
            self.g.notify("Nothing ready to collect yet.", "warn")

    def _save(self):
        if self.app.slot is not None:
            S.save(self.app.slot, self.g.to_dict())
            self.g.notify("Game saved.", "good")
            A.play("cash")

    # ---------- update ----------
    def update(self, dt):
        self.cam.update(dt, pygame.key.get_pressed())
        self.g.tick(dt)
        for b in self.bottom + self.speed_btns:
            b.update(dt)
        self.autosave_in -= dt
        if self.autosave_in <= 0:
            self.autosave_in = 45.0
            if self.app.slot is not None:
                S.save(self.app.slot, self.g.to_dict())

    # ---------- world ----------
    def _draw_world(self, s):
        vp = self.cam.viewport
        g = self.g
        z = self.cam.qzoom
        prev_clip = s.get_clip()
        s.set_clip(vp)

        # sky gradient
        for y in range(vp.y, vp.bottom, 3):
            t = (y - vp.y) / max(1, vp.h)
            c = (int(C.BG_SKY_TOP[0] * (1 - t) + C.BG_SKY_BOTTOM[0] * t),
                 int(C.BG_SKY_TOP[1] * (1 - t) + C.BG_SKY_BOTTOM[1] * t),
                 int(C.BG_SKY_TOP[2] * (1 - t) + C.BG_SKY_BOTTOM[2] * t))
            pygame.draw.rect(s, c, (vp.x, y, vp.w, 3))

        # earth behind the shelter
        gx0, gy0 = self.cam.world_to_screen(-C.CELL_W * 3, 0)
        gx1, gy1 = self.cam.world_to_screen(C.COLUMNS * C.CELL_W + C.CELL_W * 3,
                                            C.FLOOR_COUNT * C.CELL_H + C.CELL_H * 3)
        earth = pygame.Rect(gx0, gy0, gx1 - gx0, gy1 - gy0).clip(vp)
        if earth.h > 0:
            for y in range(earth.y, earth.bottom, 4):
                t = (y - earth.y) / max(1, earth.h)
                c = (int(C.BG_DIRT_TOP[0] * (1 - t) + C.BG_DIRT_BOTTOM[0] * t),
                     int(C.BG_DIRT_TOP[1] * (1 - t) + C.BG_DIRT_BOTTOM[1] * t),
                     int(C.BG_DIRT_TOP[2] * (1 - t) + C.BG_DIRT_BOTTOM[2] * t))
                pygame.draw.rect(s, c, (earth.x, y, earth.w, 4))

        cw = C.CELL_W * z
        chh = C.CELL_H * z

        # empty grid cells
        for f in range(C.FLOOR_COUNT):
            sy = self.cam.world_to_screen(0, f * C.CELL_H)[1]
            if sy + chh < vp.y or sy > vp.bottom:
                continue
            for cx in range(C.COLUMNS):
                sx = self.cam.world_to_screen(cx * C.CELL_W, 0)[0]
                if sx + cw < vp.x or sx > vp.right:
                    continue
                r = pygame.Rect(sx, sy, cw + 1, chh + 1)
                pygame.draw.rect(s, C.GRID_EMPTY, r)
                pygame.draw.rect(s, C.GRID_LINE, r, 1)

        frame = int(g.time * 7) % 8
        self._collect_badges = []

        # rooms
        for room in g.rooms.values():
            sx, sy = self.cam.world_to_screen(room.x * C.CELL_W, room.floor * C.CELL_H)
            rw, rh = room.width * cw, chh
            if sx + rw < vp.x or sx > vp.right or sy + rh < vp.y or sy > vp.bottom:
                continue
            powered = (not room.data().get("requires_power")) or g.resources.get("power", 0) > 0
            fr = frame if room.workers or room.key == "elevator" else 0
            spr = G.room_sprite(room.key, room.width, room.level, powered, fr)
            s.blit(scaled(spr, rw, rh,
                          f"{room.key}{room.width}{room.level}{powered}{fr}"), (sx, sy))

            if room.flash > 0:
                fl = pygame.Surface((int(rw), int(rh)), pygame.SRCALPHA)
                fl.fill((255, 240, 180, int(110 * room.flash)))
                s.blit(fl, (sx, sy))
            if room.on_fire:
                ov = pygame.Surface((int(rw), int(rh)), pygame.SRCALPHA)
                ov.fill((255, 96, 24, 84))
                s.blit(ov, (sx, sy))
                for k in range(6):
                    fx = sx + 12 + ((k * 37 + int(g.time * 90)) % max(1, rw - 24))
                    fy = sy + rh - 14 - ((int(g.time * 130) + k * 29) % int(max(10, rh * 0.7)))
                    pygame.draw.circle(s, (255, 170 + (k % 3) * 26, 60),
                                       (int(fx), int(fy)), max(2, int(6 * z)))

            # production progress
            if room.workers and room.is_producer():
                br = pygame.Rect(sx + U.s(4), sy + rh - U.s(7), rw - U.s(8), U.s(4))
                pygame.draw.rect(s, (24, 24, 28), br)
                fr2 = br.copy()
                fr2.w = int(br.w * room.progress)
                pygame.draw.rect(s, C.UI_GOOD, fr2)
            if room.hp < 100:
                br = pygame.Rect(sx + U.s(4), sy + U.s(4), rw - U.s(8), U.s(4))
                pygame.draw.rect(s, (56, 18, 18), br)
                fr2 = br.copy()
                fr2.w = int(br.w * room.hp / 100)
                pygame.draw.rect(s, (226, 70, 62), fr2)

            if self.selected_room == room.id:
                pygame.draw.rect(s, C.UI_ACCENT, (sx, sy, rw, rh), max(2, U.s(2)))

            # invader marker
            if room.invaders:
                mx = sx + rw / 2
                my = sy + U.s(14)
                pulse = 1.0 + 0.18 * math.sin(g.time * 8)
                sz = int(U.s(11) * pulse)
                pygame.draw.polygon(s, (240, 74, 66),
                                    [(mx - sz, my + sz), (mx + sz, my + sz), (mx, my - sz)])
                U.text(s, str(len(room.invaders)), (mx, my + sz * 0.25),
                       (255, 235, 235), size=13, center=True, bold=True)

            # collect badge
            if room.has_output():
                bw2, bh2 = U.s(46), U.s(28)
                bob = math.sin(g.time * 4 + room.id) * U.s(3)
                br = pygame.Rect(sx + rw / 2 - bw2 / 2, sy - bh2 - U.s(4) + bob, bw2, bh2)
                pygame.draw.rect(s, (250, 200, 70), br, border_radius=U.s(6))
                pygame.draw.rect(s, (140, 100, 20), br, max(1, U.s(2)), border_radius=U.s(6))
                U.text(s, "!", br.center, (52, 36, 6), size=20, center=True, bold=True)
                self._collect_badges.append((br, room.id))

        # residents
        for res in g.residents.values():
            if res.on_expedition:
                continue
            wx = res.x * C.CELL_W + C.CELL_W / 2
            wy = res.floor * C.CELL_H + C.CELL_H - U.s(6) / max(0.1, z)
            sx, sy = self.cam.world_to_screen(wx, wy)
            if sx < vp.x - 60 or sx > vp.right + 60 or sy < vp.y - 80 or sy > vp.bottom + 80:
                continue
            spr = G.resident_sprite(
                res.portrait_seed, (res.power_armor or {}).get("name"), res.facing,
                res.step, (res.outfit or {}).get("rarity", 0), res.age,
                res.activity, res.pregnant, not res.alive, res.is_robot)
            sw = int(spr.get_width() * z * 1.35)
            sh = int(spr.get_height() * z * 1.35)
            key = (f"{res.portrait_seed}{(res.power_armor or {}).get('name')}{res.facing}"
                   f"{res.step}{(res.outfit or {}).get('rarity',0)}{res.age}{res.activity}"
                   f"{res.pregnant}{res.alive}{res.is_robot}")
            s.blit(scaled(spr, sw, sh, key), (int(sx - sw / 2), int(sy - sh)))

            if res.alive and res.hp < res.effective_max_hp():
                bw2 = U.s(28)
                br = pygame.Rect(int(sx - bw2 / 2), int(sy - sh - U.s(8)), bw2, U.s(4))
                pygame.draw.rect(s, (56, 18, 18), br)
                f2 = br.copy()
                f2.w = int(br.w * res.hp / max(1, res.effective_max_hp()))
                pygame.draw.rect(s, (226, 70, 62), f2)
            if res.pregnant:
                U.text(s, "+", (sx, sy - sh - U.s(20)), (250, 190, 230),
                       size=16, center=True, bold=True)
            if self.selected_resident == res.id:
                pygame.draw.circle(s, C.UI_ACCENT, (int(sx), int(sy - U.s(3))),
                                   int(U.s(16) * z + U.s(6)), max(2, U.s(2)))

        # floating text
        for fl in g.floaters:
            t = fl.age / fl.life
            wx = fl.x * C.CELL_W + C.CELL_W / 2
            wy = fl.floor * C.CELL_H + C.CELL_H * 0.4
            sx, sy = self.cam.world_to_screen(wx, wy)
            sy += fl.vy * fl.age
            img = U.font(16, True).render(fl.text, True, fl.color)
            img.set_alpha(max(0, int(255 * (1 - t ** 2))))
            s.blit(img, (sx - img.get_width() // 2, sy))

        # build ghost
        if self.mode == "build" and self.build_key:
            mx, my = pygame.mouse.get_pos()
            if vp.collidepoint(mx, my):
                wx, wy = self.cam.screen_to_world(mx, my)
                fl = max(0, min(C.FLOOR_COUNT - 1, int(wy // C.CELL_H)))
                width = 1 if self.build_key == "elevator" else self.build_width
                col = int(math.floor(wx / C.CELL_W - width / 2 + 0.5))
                if self.build_key == "elevator":
                    col = g.elevator_col
                self._ghost = (fl, col, width)
                ok, why = g.can_place(self.build_key, fl, col, width)
                gx, gy = self.cam.world_to_screen(col * C.CELL_W, fl * C.CELL_H)
                gw, gh = width * cw, chh
                ov = pygame.Surface((int(gw), int(gh)), pygame.SRCALPHA)
                ov.fill((96, 216, 118, 96) if ok else (222, 66, 60, 104))
                s.blit(ov, (gx, gy))
                pygame.draw.rect(s, (110, 230, 130) if ok else (232, 78, 70),
                                 (gx, gy, gw, gh), max(2, U.s(2)))
                cost = g.build_cost(self.build_key, width)
                label = f"{D.ROOMS[self.build_key]['name']} — {cost} caps" if ok else why
                U.text(s, label, (gx + gw / 2, gy - U.s(20)),
                       (210, 250, 215) if ok else (250, 200, 195),
                       size=15, center=True, bold=True, shadow=True)
            else:
                self._ghost = None
        else:
            self._ghost = None

        s.set_clip(prev_clip)

    # ---------- HUD ----------
    def _draw_hud(self, s):
        w, h = s.get_size()
        g = self.g
        top_h = U.s(HUD_TOP)
        pygame.draw.rect(s, C.UI_BG, (0, 0, w, top_h))
        pygame.draw.line(s, C.UI_BORDER, (0, top_h), (w, top_h), max(1, U.s(1)))

        tiles = [
            ("Caps", g.caps, None, C.UI_ACCENT),
            ("Power", int(g.resources.get("power", 0)), g.storage_cap["power"], C.UI_WARN),
            ("Water", int(g.resources.get("water", 0)), g.storage_cap["water"], C.UI_BLUE),
            ("Food", int(g.resources.get("food", 0)), g.storage_cap["food"], (150, 210, 128)),
            ("Materials", int(g.resources.get("materials", 0)),
             g.storage_cap["materials"], (188, 158, 124)),
            ("Stimpaks", int(g.resources.get("stim", 0)), g.storage_cap["stim"], (226, 108, 108)),
            ("RadAway", int(g.resources.get("radaway", 0)),
             g.storage_cap["radaway"], (140, 224, 214)),
            ("Residents", g.population(), g.housing_cap(), (206, 194, 246)),
        ]
        # Identity block on the left, so nothing collides with the tile row.
        idw = U.s(150)
        ident = pygame.Rect(U.s(10), U.s(13), idw, U.s(66))
        pygame.draw.rect(s, C.UI_BG2, ident, border_radius=U.s(5))
        pygame.draw.rect(s, C.UI_ACCENT_DIM, ident, max(1, U.s(1)), border_radius=U.s(5))
        U.text(s, f"HAVEN {g.vault_number}", (ident.centerx, ident.y + U.s(10)),
               C.UI_ACCENT, size=19, bold=True, center=True)
        mins = int(g.time // 60)
        U.text(s, f"Day {mins // 20 + 1} · {mins} min", (ident.centerx, ident.y + U.s(32)),
               C.UI_TEXT_DIM, size=13, center=True)
        if g.paused:
            U.text(s, "PAUSED", (ident.centerx, ident.y + U.s(49)), C.UI_WARN,
                   size=14, center=True, bold=True)
        else:
            U.text(s, f"{int(g.speed)}x speed", (ident.centerx, ident.y + U.s(49)),
                   C.UI_TEXT_DIM, size=13, center=True)

        # Tiles fill whatever space is left between the identity and right cluster.
        right_w = U.s(240)
        gap = U.s(7)
        avail = w - ident.right - gap - right_w - U.s(10)
        tw = max(U.s(78), (avail - gap * (len(tiles) - 1)) // len(tiles))
        th = U.s(66)
        x = ident.right + gap
        self._tile_rects = []
        for name, val, cap, col in tiles:
            r = pygame.Rect(x, U.s(13), tw, th)
            pygame.draw.rect(s, C.UI_BG2, r, border_radius=U.s(5))
            frac = (val / cap) if cap else 1.0
            crit, low = frac < 0.08, frac < 0.20
            edge = C.UI_BAD if crit else (C.UI_WARN if low else C.UI_BORDER)
            pygame.draw.rect(s, edge, r, max(1, U.s(2 if crit else 1)),
                             border_radius=U.s(5))
            U.text(s, U.trim(name, 12, tw - U.s(12)), (r.x + U.s(8), r.y + U.s(6)),
                   C.UI_TEXT_DIM, size=12)
            U.text(s, f"{val}", (r.x + U.s(8), r.y + U.s(21)), col, size=22, bold=True)
            if cap:
                U.draw_bar(s, pygame.Rect(r.x + U.s(8), r.bottom - U.s(13),
                                          r.w - U.s(16), U.s(6)), val, cap, col)
                U.text(s, f"/{cap}", (r.right - U.s(8), r.y + U.s(7)),
                       C.UI_TEXT_DIM, size=11, right=True)
            self._tile_rects.append((r, name, val, cap))
            x += tw + gap

        # right cluster: happiness, lunchboxes, clock
        happy = (sum(r.happiness for r in g.residents.values() if r.alive)
                 / max(1, len([r for r in g.residents.values() if r.alive])))
        hc = C.UI_GOOD if happy > 60 else C.UI_WARN if happy > 30 else C.UI_BAD
        hr = pygame.Rect(w - U.s(158), U.s(13), U.s(148), th)
        pygame.draw.rect(s, C.UI_BG2, hr, border_radius=U.s(5))
        pygame.draw.rect(s, C.UI_BORDER, hr, max(1, U.s(1)), border_radius=U.s(5))
        U.text(s, "Happiness", (hr.x + U.s(10), hr.y + U.s(6)), C.UI_TEXT_DIM, size=13)
        U.text(s, f"{int(happy)}%", (hr.x + U.s(10), hr.y + U.s(22)), hc, size=24, bold=True)
        U.draw_bar(s, pygame.Rect(hr.x + U.s(10), hr.bottom - U.s(13),
                                  hr.w - U.s(20), U.s(6)), happy, 100, hc)
        smiley = ":)" if happy > 60 else ":|" if happy > 30 else ":("
        U.text(s, smiley, (hr.right - U.s(14), hr.y + U.s(24)), hc,
               size=22, bold=True, right=True)

        lb = pygame.Rect(w - U.s(228), U.s(13), U.s(62), th)
        hov = lb.collidepoint(pygame.mouse.get_pos())
        pygame.draw.rect(s, (58, 40, 30) if g.lunchboxes else C.UI_BG2, lb,
                         border_radius=U.s(5))
        pygame.draw.rect(s, C.UI_ACCENT if (g.lunchboxes and hov) else C.UI_BORDER, lb,
                         max(1, U.s(1)), border_radius=U.s(5))
        ic = G.lunchbox_icon(U.s(34))
        s.blit(ic, (lb.centerx - ic.get_width() // 2, lb.y + U.s(6)))
        U.text(s, str(g.lunchboxes), (lb.centerx, lb.bottom - U.s(13)),
               C.UI_ACCENT if g.lunchboxes else C.UI_TEXT_DIM,
               size=15, center=True, bold=True)
        self._lunch_rect = lb

        # bottom bar
        by = h - U.s(HUD_BOT)
        pygame.draw.rect(s, C.UI_BG, (0, by, w, U.s(HUD_BOT)))
        pygame.draw.line(s, C.UI_BORDER, (0, by), (w, by), max(1, U.s(1)))
        for b in self.bottom:
            if b.label == "Build":
                b.style = "primary" if self.mode == "build" else "normal"
            elif b.label == "Demolish":
                b.style = "danger" if self.mode == "destroy" else "normal"
            elif b.label == "Collect All":
                b.style = "good" if any(r.has_output() for r in g.rooms.values()) else "normal"
            b.draw(s)
        for b in self.speed_btns:
            if b.label == "Pause":
                b.style = "primary" if g.paused else "normal"
            else:
                b.style = ("primary" if (not g.paused and b.label == f"{int(g.speed)}x")
                           else "normal")
            b.draw(s)

        # notification ticker
        y = by - U.s(26)
        for t, lvl, txt in reversed(g.notifications[-4:]):
            age = g.time - t
            if age > 10:
                break
            col = {"good": C.UI_GOOD, "bad": C.UI_BAD,
                   "warn": C.UI_WARN}.get(lvl, C.UI_TEXT)
            img = U.font(15).render("• " + txt, True, col)
            img.set_alpha(max(0, min(255, int(255 * (1 - age / 10)))))
            s.blit(img, (U.s(14), y))
            y -= U.s(22)

    # ---------- panels ----------
    def _panel_rect(self):
        w, h = self.app.size
        return pygame.Rect(w - U.s(PANEL_W) - U.s(10), U.s(HUD_TOP) + U.s(8),
                           U.s(PANEL_W), h - U.s(HUD_TOP) - U.s(HUD_BOT) - U.s(18))

    def _draw_panels(self, s):
        if self.panel:
            r = self._panel_rect()
            fn = {
                "build": self._p_build, "residents": self._p_residents,
                "inventory": self._p_inventory, "exploration": self._p_explore,
                "objectives": self._p_objectives, "menu": self._p_menu,
                "lunchbox": self._p_lunchbox,
            }.get(self.panel)
            if fn:
                fn(s, r)
        for rect, fn in self._left_panels():
            fn(s, rect)

    def _left_panels(self):
        """Left-hand detail panels, clamped so they never cover the bottom bar."""
        h = self.app.size[1]
        limit = h - U.s(HUD_BOT) - U.s(8)
        out = []
        top = U.s(HUD_TOP) + U.s(8)
        if self.selected_room and self.selected_room in self.g.rooms:
            rh = min(U.s(276), limit - top)
            if rh > U.s(120):
                out.append((pygame.Rect(U.s(14), top, U.s(340), rh), self._p_room))
                top += rh + U.s(10)
        if self.selected_resident and self.selected_resident in self.g.residents:
            rh = min(U.s(372), limit - top)
            if rh > U.s(150):
                out.append((pygame.Rect(U.s(14), top, U.s(340), rh), self._p_resident))
        return out

    # -- build picker
    def _p_build(self, s, r):
        U.panel(s, r, "Construction")
        g = self.g
        y0 = r.y + U.s(40)
        U.text(s, "Width", (r.x + U.s(12), y0 + U.s(3)), C.UI_TEXT, size=15)
        self._w_minus = pygame.Rect(r.x + U.s(70), y0, U.s(28), U.s(24))
        self._w_plus = pygame.Rect(r.x + U.s(134), y0, U.s(28), U.s(24))
        for rr, lbl in ((self._w_minus, "-"), (self._w_plus, "+")):
            pygame.draw.rect(s, C.UI_BG2, rr, border_radius=U.s(3))
            pygame.draw.rect(s, C.UI_BORDER, rr, max(1, U.s(1)), border_radius=U.s(3))
            U.text(s, lbl, rr.center, C.UI_TEXT, size=17, center=True, bold=True)
        U.text(s, str(self.build_width), (r.x + U.s(116), y0 + U.s(3)),
               C.UI_ACCENT, size=17, center=True, bold=True)
        U.text(s, "Merged rooms produce more.", (r.x + U.s(174), y0 + U.s(4)),
               C.UI_TEXT_DIM, size=13)

        area = pygame.Rect(r.x + U.s(8), y0 + U.s(34), r.w - U.s(16),
                           r.bottom - (y0 + U.s(42)))
        self.scroll.rect = area
        prev = s.get_clip()
        s.set_clip(area)
        y = area.y - int(self.scroll.offset)
        self._build_rows = []
        mouse = pygame.mouse.get_pos()
        for key in D.ROOM_ORDER + ["elevator"]:
            rd = D.ROOMS[key]
            row = pygame.Rect(area.x, y, area.w - U.s(8), U.s(66))
            if row.bottom > area.y and row.y < area.bottom:
                sel = key == self.build_key
                hov = row.collidepoint(mouse)
                bg = (62, 52, 22) if sel else (C.UI_BG2 if hov else C.UI_BG)
                pygame.draw.rect(s, bg, row, border_radius=U.s(5))
                pygame.draw.rect(s, C.UI_ACCENT if sel else C.UI_BORDER, row,
                                 max(1, U.s(1)), border_radius=U.s(5))
                thumb = G.room_sprite(key, 1, 1, True, 0)
                s.blit(scaled(thumb, U.s(62), U.s(56), f"thumb{key}"),
                       (row.x + U.s(4), row.y + U.s(5)))
                tx = row.x + U.s(74)
                U.text(s, rd["name"], (tx, row.y + U.s(7)), C.UI_ACCENT, size=16, bold=True)
                w2 = 1 if key == "elevator" else max(self.build_width, rd["width"])
                cost = g.build_cost(key, w2)
                afford = g.caps >= cost
                U.text(s, f"{cost} caps", (tx, row.y + U.s(27)),
                       C.UI_TEXT if afford else C.UI_BAD, size=14, bold=True)
                U.text(s, U.trim(rd.get("desc", ""), 12, row.w - U.s(84)),
                       (tx, row.y + U.s(45)), C.UI_TEXT_DIM, size=12)
                self._build_rows.append((row, key))
            y += U.s(70)
        self.scroll.content_h = y + int(self.scroll.offset) - area.y
        s.set_clip(prev)
        self.scroll.draw_scrollbar(s)

    # -- room detail
    def _p_room(self, s, r):
        room = self.g.rooms[self.selected_room]
        rd = room.data()
        U.panel(s, r, f"{rd['name']} · Level {room.level}")
        y = r.y + U.s(40)
        U.text(s, U.trim(rd.get("desc", ""), 13, r.w - U.s(24)),
               (r.x + U.s(12), y), C.UI_TEXT_DIM, size=13)
        y += U.s(22)
        if room.capacity():
            U.text(s, f"Staff  {len(room.workers)}/{room.capacity()}",
                   (r.x + U.s(12), y), C.UI_TEXT, size=15, bold=True)
            stat = self.g.best_stat_for(room)
            if stat:
                U.text(s, f"Best stat: {D.STAT_NAMES[stat]}",
                       (r.right - U.s(12), y), C.UI_ACCENT, size=13, right=True)
            y += U.s(20)
        prod = ", ".join(f"+{v * room.level * room.width_units()} {k}"
                         for k, v in room.produces().items() if k != "attract")
        if prod:
            U.text(s, prod, (r.x + U.s(12), y), C.UI_GOOD, size=13)
            y += U.s(17)
        cons = ", ".join(f"-{v * room.level * room.width_units()} {k}"
                         for k, v in room.consumes().items())
        if cons:
            U.text(s, cons, (r.x + U.s(12), y), C.UI_BAD, size=13)
            y += U.s(17)
        if room.stored:
            got = ", ".join(f"{int(v)} {k}" for k, v in room.stored.items() if v)
            if got:
                U.text(s, "Ready: " + got, (r.x + U.s(12), y), C.UI_ACCENT,
                       size=13, bold=True)
                y += U.s(17)

        bw, bh = U.s(100), U.s(30)
        bx, byy = r.x + U.s(12), r.bottom - U.s(80)
        self._room_btns = {}
        cost = self.g.upgrade_cost(room)
        specs = [
            ("collect", "Collect", room.has_output(), "good"),
            ("rush", "Rush", room.can_rush(), "primary"),
            ("staff", "Assign", len(room.workers) < room.capacity(), "normal"),
            ("upgrade", f"Upgrade {cost}" if cost and room.level < 3 else "Max level",
             cost > 0 and room.level < 3 and self.g.caps >= cost, "normal"),
            ("merge", "Merge", room.key != "elevator", "normal"),
            ("destroy", "Demolish", True, "danger"),
        ]
        for i, (kk, label, enabled, style) in enumerate(specs):
            rr = pygame.Rect(bx + (i % 3) * (bw + U.s(8)),
                             byy + (i // 3) * (bh + U.s(8)), bw, bh)
            b = U.Button(rr, label, None, style=style, enabled=enabled, size=13)
            b.hover = rr.collidepoint(pygame.mouse.get_pos())
            b._anim = 1.0 if b.hover else 0.0
            b.draw(s)
            self._room_btns[kk] = (rr, enabled)
        if room.can_rush():
            pct = int(room.rush_failure_chance() * 100)
            U.text(s, f"Rush risk {pct}%", (r.right - U.s(12), byy - U.s(18)),
                   C.UI_WARN, size=12, right=True)

    # -- resident detail
    def _p_resident(self, s, r):
        res = self.g.residents[self.selected_resident]
        U.panel(s, r, res.name)
        port = G.resident_portrait(res.portrait_seed, (res.outfit or {}).get("rarity", 0),
                                   (res.power_armor or {}).get("name"), res.age,
                                   res.gender, not res.alive)
        ps = U.s(104)
        s.blit(scaled(port, ps, ps, f"p{res.id}{res.alive}{(res.power_armor or {}).get('name')}"
                                    f"{(res.outfit or {}).get('rarity',0)}{res.age}"),
               (r.x + U.s(12), r.y + U.s(42)))
        tx = r.x + U.s(126)
        ty = r.y + U.s(44)
        tag = "Deceased" if not res.alive else ("Child" if res.age == "child" else
                                                res.activity.title())
        U.text(s, f"Level {res.level}", (tx, ty), C.UI_ACCENT, size=18, bold=True)
        U.text(s, tag, (tx, ty + U.s(22)), C.UI_BAD if not res.alive else C.UI_TEXT_DIM, size=14)
        if res.pregnant:
            U.text(s, f"Expecting ({int(res.preg_timer)}s)", (tx, ty + U.s(40)),
                   (250, 190, 230), size=13, bold=True)
        bw2 = r.w - U.s(138)
        U.draw_bar(s, pygame.Rect(tx, ty + U.s(58), bw2, U.s(14)), res.hp,
                   res.effective_max_hp(), C.UI_BAD,
                   f"HP {int(res.hp)}/{res.effective_max_hp()}", small=True)
        U.draw_bar(s, pygame.Rect(tx, ty + U.s(76), bw2, U.s(12)), res.xp,
                   res.xp_needed(), C.UI_BLUE, f"XP {res.xp}/{res.xp_needed()}", small=True)
        U.draw_bar(s, pygame.Rect(tx, ty + U.s(92), bw2, U.s(12)), res.happiness, 100,
                   C.UI_GOOD, f"{int(res.happiness)}% happy", small=True)

        y = r.y + U.s(162)
        if res.rads > 0:
            U.draw_bar(s, pygame.Rect(r.x + U.s(12), y, r.w - U.s(24), U.s(12)),
                       res.rads, res.max_hp, (140, 224, 120),
                       f"Radiation {int(res.rads)}", small=True)
            y += U.s(18)
        for i, k in enumerate(D.STAT_KEYS):
            col = r.x + U.s(12) + (i % 7) * U.s(45)
            base = res.stats.get(k, 1)
            tot = res.stat_total(k)
            U.text(s, k, (col + U.s(16), y), C.UI_TEXT_DIM, size=12, center=True)
            U.text(s, str(tot), (col + U.s(16), y + U.s(15)),
                   C.UI_ACCENT if tot > base else C.UI_TEXT,
                   size=17, center=True, bold=True)
        y += U.s(40)

        for label, item, col in (("Weapon", res.weapon, C.UI_TEXT),
                                 ("Outfit", res.outfit, C.UI_TEXT),
                                 ("Armor", res.power_armor, C.UI_ACCENT)):
            nm = item["name"] if item else "—"
            if item and label == "Armor":
                nm += f"  ({item['durability']}/{item['max_durability']})"
            U.text(s, f"{label}: {U.trim(nm, 13, r.w - U.s(90))}",
                   (r.x + U.s(12), y), col if item else C.UI_TEXT_DIM, size=13,
                   bold=(label == "Armor" and item is not None))
            y += U.s(18)

        bw, bh = U.s(100), U.s(30)
        bx, byy = r.x + U.s(12), r.bottom - U.s(76)
        self._res_btns = {}
        if res.alive:
            specs = [
                ("stim", "Stimpak", self.g.resources.get("stim", 0) > 0
                 and res.hp < res.effective_max_hp(), "good"),
                ("rad", "RadAway", self.g.resources.get("radaway", 0) > 0
                 and res.rads > 0, "normal"),
                ("unassign", "Unassign", res.assigned_room is not None, "normal"),
                ("explore", "Explore", res.age == "adult" and not res.on_expedition, "primary"),
                ("repair", "Repair", res.power_armor is not None, "normal"),
                ("focus", "Find", True, "normal"),
            ]
        else:
            specs = [("revive", f"Revive {res.revive_cost()}",
                      self.g.caps >= res.revive_cost(), "primary"),
                     ("focus", "Find", True, "normal")]
        for i, (kk, label, enabled, style) in enumerate(specs):
            rr = pygame.Rect(bx + (i % 3) * (bw + U.s(8)),
                             byy + (i // 3) * (bh + U.s(8)), bw, bh)
            b = U.Button(rr, label, None, style=style, enabled=enabled, size=13)
            b.hover = rr.collidepoint(pygame.mouse.get_pos())
            b._anim = 1.0 if b.hover else 0.0
            b.draw(s)
            self._res_btns[kk] = (rr, enabled)

    # -- residents list
    def _p_residents(self, s, r):
        g = self.g
        U.panel(s, r, f"Residents  ({g.population()}/{g.housing_cap()})")
        area = pygame.Rect(r.x + U.s(8), r.y + U.s(40), r.w - U.s(16), r.h - U.s(48))
        self.scroll.rect = area
        prev = s.get_clip()
        s.set_clip(area)
        y = area.y - int(self.scroll.offset)
        self._res_rows = []
        mouse = pygame.mouse.get_pos()
        order = sorted(g.residents.values(),
                       key=lambda x: (not x.alive, x.age == "child", -x.level))
        for res in order:
            row = pygame.Rect(area.x, y, area.w - U.s(8), U.s(62))
            if row.bottom > area.y and row.y < area.bottom:
                sel = res.id == self.selected_resident
                hov = row.collidepoint(mouse)
                pygame.draw.rect(s, (58, 48, 20) if sel else (C.UI_BG2 if hov else C.UI_BG),
                                 row, border_radius=U.s(5))
                pygame.draw.rect(s, C.UI_ACCENT if sel else C.UI_BORDER, row,
                                 max(1, U.s(1)), border_radius=U.s(5))
                port = G.resident_portrait(res.portrait_seed,
                                           (res.outfit or {}).get("rarity", 0),
                                           (res.power_armor or {}).get("name"),
                                           res.age, res.gender, not res.alive)
                s.blit(scaled(port, U.s(52), U.s(52), f"lp{res.id}{res.alive}"
                                                      f"{(res.power_armor or {}).get('name')}"
                                                      f"{(res.outfit or {}).get('rarity',0)}"),
                       (row.x + U.s(4), row.y + U.s(5)))
                tx = row.x + U.s(62)
                U.text(s, U.trim(res.name, 15, row.w - U.s(140)),
                       (tx, row.y + U.s(5)), C.UI_ACCENT, size=15, bold=True)
                where = "—"
                if res.on_expedition:
                    where = "Wasteland"
                elif res.assigned_room and res.assigned_room in g.rooms:
                    where = g.rooms[res.assigned_room].data()["name"]
                elif not res.alive:
                    where = "Deceased"
                elif res.age == "child":
                    where = "Child"
                U.text(s, f"Lv{res.level} · {U.trim(where, 12, row.w - U.s(150))}",
                       (tx, row.y + U.s(24)), C.UI_TEXT_DIM, size=12)
                U.draw_bar(s, pygame.Rect(tx, row.y + U.s(42), U.s(96), U.s(9)),
                           res.hp, res.effective_max_hp(), C.UI_BAD)
                U.draw_bar(s, pygame.Rect(tx + U.s(104), row.y + U.s(42), U.s(96), U.s(9)),
                           res.happiness, 100, C.UI_GOOD)
                best = max(D.STAT_KEYS, key=lambda k: res.stat_total(k))
                U.text(s, f"{best}{res.stat_total(best)}",
                       (row.right - U.s(10), row.y + U.s(6)),
                       C.UI_TEXT, size=14, right=True, bold=True)
                self._res_rows.append((row, res.id))
            y += U.s(66)
        self.scroll.content_h = y + int(self.scroll.offset) - area.y
        s.set_clip(prev)
        self.scroll.draw_scrollbar(s)

    # -- inventory
    def _p_inventory(self, s, r):
        g = self.g
        U.panel(s, r, "Inventory")
        self._inv_tabs = U.TabBar((r.x + U.s(8), r.y + U.s(38), r.w - U.s(16), U.s(28)),
                                  ["Items", "Power Armor", "Craft"], self.inv_tab)
        self._inv_tabs.draw(s)
        if self.selected_resident and self.selected_resident in g.residents:
            who = g.residents[self.selected_resident].name
            U.text(s, f"Equipping: {U.trim(who, 12, r.w - U.s(120))}",
                   (r.x + U.s(12), r.y + U.s(72)), C.UI_ACCENT, size=13, bold=True)
        else:
            U.text(s, "Select a resident to equip items.",
                   (r.x + U.s(12), r.y + U.s(72)), C.UI_TEXT_DIM, size=13)

        area = pygame.Rect(r.x + U.s(8), r.y + U.s(92), r.w - U.s(16), r.bottom - r.y - U.s(100))
        self.scroll.rect = area
        prev = s.get_clip()
        s.set_clip(area)
        y = area.y - int(self.scroll.offset)
        self._inv_rows = []
        self._craft_rows = []
        mouse = pygame.mouse.get_pos()

        if self.inv_tab == 0:
            if not g.inventory:
                U.text(s, "Nothing stored. Craft or explore to find gear.",
                       (area.x + U.s(6), area.y + U.s(6)), C.UI_TEXT_DIM, size=14)
            for i, it in enumerate(g.inventory):
                row = pygame.Rect(area.x, y, area.w - U.s(8), U.s(56))
                if row.bottom > area.y and row.y < area.bottom:
                    hov = row.collidepoint(mouse)
                    pygame.draw.rect(s, C.UI_BG2 if hov else C.UI_BG, row,
                                     border_radius=U.s(5))
                    rc = C.RARITY_COLORS[min(4, it.get("rarity", 0))]
                    pygame.draw.rect(s, rc if hov else C.UI_BORDER, row,
                                     max(1, U.s(1)), border_radius=U.s(5))
                    ico = (G.weapon_icon(it["name"], U.s(40)) if it["kind"] == "weapon"
                           else G.outfit_icon(it["name"], U.s(40)))
                    s.blit(ico, (row.x + U.s(6), row.y + U.s(8)))
                    tx = row.x + U.s(52)
                    U.text(s, U.trim(it["name"], 15, row.w - U.s(180)),
                           (tx, row.y + U.s(6)), rc, size=15, bold=True)
                    U.text(s, f"{C.RARITY_NAMES[min(4, it.get('rarity', 0))]} · "
                              f"{U.trim(it.get('desc',''), 12, row.w - U.s(190))}",
                           (tx, row.y + U.s(26)), C.UI_TEXT_DIM, size=12)
                    sb = pygame.Rect(row.right - U.s(140), row.y + U.s(13), U.s(66), U.s(30))
                    cb = pygame.Rect(row.right - U.s(70), row.y + U.s(13), U.s(62), U.s(30))
                    for rr, lbl, col in ((sb, f"Sell {max(1, it.get('value',1)//2)}", C.UI_ACCENT),
                                         (cb, "Scrap", C.UI_TEXT)):
                        pygame.draw.rect(s, C.UI_BG, rr, border_radius=U.s(3))
                        pygame.draw.rect(s, C.UI_BORDER, rr, max(1, U.s(1)),
                                         border_radius=U.s(3))
                        U.text(s, lbl, rr.center, col, size=12, center=True)
                    self._inv_rows.append((row, sb, cb, i))
                y += U.s(60)

        elif self.inv_tab == 1:
            if not g.pa_storage:
                U.text(s, "No spare Power Armor. Explore to recover suits.",
                       (area.x + U.s(6), area.y + U.s(6)), C.UI_TEXT_DIM, size=14)
            for i, it in enumerate(g.pa_storage):
                row = pygame.Rect(area.x, y, area.w - U.s(8), U.s(84))
                if row.bottom > area.y and row.y < area.bottom:
                    hov = row.collidepoint(mouse)
                    pygame.draw.rect(s, C.UI_BG2 if hov else C.UI_BG, row,
                                     border_radius=U.s(5))
                    rc = C.RARITY_COLORS[min(4, it.get("rarity", 3))]
                    pygame.draw.rect(s, rc, row, max(1, U.s(2 if hov else 1)),
                                     border_radius=U.s(5))
                    s.blit(G.pa_icon(it["name"], U.s(64)), (row.x + U.s(6), row.y + U.s(10)))
                    tx = row.x + U.s(76)
                    U.text(s, it["name"], (tx, row.y + U.s(6)), rc, size=16, bold=True)
                    U.text(s, f"Armor {it.get('armor',0)} · DR {it.get('dr',0)}% · "
                              f"Lv{it.get('level_req',0)}+",
                           (tx, row.y + U.s(26)), C.UI_TEXT, size=13)
                    bon = " ".join(f"+{v}{k}" for k, v in it.get("stat_bonus", {}).items())
                    U.text(s, bon, (tx, row.y + U.s(44)), C.UI_ACCENT, size=12)
                    U.draw_bar(s, pygame.Rect(tx, row.y + U.s(62), row.w - U.s(180), U.s(10)),
                               it.get("durability", 0), max(1, it.get("max_durability", 1)),
                               C.UI_BLUE, f"{it.get('durability',0)}/"
                                          f"{it.get('max_durability',0)}", small=True)
                    sb = pygame.Rect(row.right - U.s(78), row.y + U.s(26), U.s(68), U.s(30))
                    pygame.draw.rect(s, C.UI_BG, sb, border_radius=U.s(3))
                    pygame.draw.rect(s, C.UI_BORDER, sb, max(1, U.s(1)), border_radius=U.s(3))
                    U.text(s, f"Sell {max(50, it.get('value',100)//2)}", sb.center,
                           C.UI_ACCENT, size=11, center=True)
                    self._inv_rows.append((row, sb, None, ("pa", i)))
                y += U.s(88)

        else:
            has_ws = any(x.key == "workshop" for x in g.rooms.values())
            has_arm = any(x.key == "armory" for x in g.rooms.values())
            has_sci = any(x.key == "science" for x in g.rooms.values())
            for kind, label in (("weapon", "Weapons"), ("outfit", "Outfits")):
                U.text(s, label, (area.x + U.s(4), y), C.UI_ACCENT, size=16, bold=True)
                y += U.s(24)
                for rar in range(5):
                    row = pygame.Rect(area.x, y, area.w - U.s(8), U.s(40))
                    mats = GM.GameState.CRAFT_MATS[rar]
                    caps = GM.GameState.CRAFT_CAPS[rar]
                    gated = ((rar >= 2 and not has_ws)
                             or (rar >= 3 and kind == "weapon" and not has_arm)
                             or (rar >= 3 and kind != "weapon" and not has_sci))
                    can = (not gated and g.resources.get("materials", 0) >= mats
                           and g.caps >= caps)
                    if row.bottom > area.y and row.y < area.bottom:
                        pygame.draw.rect(s, C.UI_BG2, row, border_radius=U.s(5))
                        pygame.draw.rect(s, C.UI_BORDER, row, max(1, U.s(1)),
                                         border_radius=U.s(5))
                        rc = C.RARITY_COLORS[rar]
                        U.text(s, C.RARITY_NAMES[rar], (row.x + U.s(10), row.y + U.s(6)),
                               rc, size=14, bold=True)
                        note = (f"{mats} materials · {caps} caps" if not gated
                                else ("Needs Workshop" if rar == 2 else
                                      "Needs Armory" if kind == "weapon" else
                                      "Needs Science Lab"))
                        U.text(s, note, (row.x + U.s(10), row.y + U.s(23)),
                               C.UI_TEXT_DIM if not gated else C.UI_BAD, size=12)
                        b = pygame.Rect(row.right - U.s(84), row.y + U.s(5), U.s(76), U.s(30))
                        pygame.draw.rect(s, C.UI_ACCENT if can else C.UI_BG, b,
                                         border_radius=U.s(3))
                        pygame.draw.rect(s, C.UI_BORDER, b, max(1, U.s(1)),
                                         border_radius=U.s(3))
                        U.text(s, "Craft", b.center,
                               (28, 20, 6) if can else C.UI_TEXT_DIM,
                               size=13, center=True, bold=can)
                        self._craft_rows.append((b, kind, rar, can))
                    y += U.s(44)
                y += U.s(10)

        self.scroll.content_h = y + int(self.scroll.offset) - area.y
        s.set_clip(prev)
        self.scroll.draw_scrollbar(s)

    # -- exploration
    def _p_explore(self, s, r):
        g = self.g
        U.panel(s, r, "Expeditions")
        y = r.y + U.s(40)
        if not any(x.key == "command" for x in g.rooms.values()):
            U.text(s, "Build a Command Center to send expeditions.",
                   (r.x + U.s(12), y), C.UI_BAD, size=14, bold=True)
            y += U.s(24)
        area = pygame.Rect(r.x + U.s(8), y, r.w - U.s(16), r.bottom - y - U.s(8))
        self.scroll.rect = area
        prev = s.get_clip()
        s.set_clip(area)
        yy = area.y - int(self.scroll.offset)
        self._exp_btns = []

        if g.expeditions:
            U.text(s, "Out in the wasteland", (area.x + U.s(4), yy),
                   C.UI_ACCENT, size=16, bold=True)
            yy += U.s(24)
        for e in g.expeditions:
            res = g.residents.get(e["resident_id"])
            if not res:
                continue
            box_h = U.s(112)
            row = pygame.Rect(area.x, yy, area.w - U.s(8), box_h)
            if row.bottom > area.y and row.y < area.bottom:
                pygame.draw.rect(s, C.UI_BG2, row, border_radius=U.s(5))
                pygame.draw.rect(s, C.UI_ACCENT_DIM, row, max(1, U.s(1)),
                                 border_radius=U.s(5))
                U.text(s, res.name, (row.x + U.s(10), row.y + U.s(6)),
                       C.UI_ACCENT, size=15, bold=True)
                U.draw_bar(s, pygame.Rect(row.x + U.s(10), row.y + U.s(28),
                                          row.w - U.s(112), U.s(14)),
                           e["elapsed"], e["duration"], C.UI_ACCENT,
                           ("Returning" if e.get("returning") else
                            f"{int(e['elapsed'])}s / {int(e['duration'])}s"), small=True)
                U.text(s, f"{e['caps']} caps · {len(e['items'])} items · "
                          f"HP {int(res.hp)}",
                       (row.x + U.s(10), row.y + U.s(48)), C.UI_TEXT, size=12)
                for i, line in enumerate(e["log"][-2:]):
                    U.text(s, U.trim(line, 11, row.w - U.s(24)),
                           (row.x + U.s(10), row.y + U.s(68) + i * U.s(16)),
                           C.UI_TEXT_DIM, size=11)
                rb = pygame.Rect(row.right - U.s(94), row.y + U.s(24), U.s(84), U.s(30))
                pygame.draw.rect(s, (118, 46, 46), rb, border_radius=U.s(3))
                U.text(s, "Recall", rb.center, C.UI_TEXT, size=13, center=True)
                self._exp_btns.append((rb, "recall", res.id))
            yy += box_h + U.s(8)

        yy += U.s(6)
        U.text(s, "Send someone out", (area.x + U.s(4), yy), C.UI_ACCENT, size=16, bold=True)
        yy += U.s(24)
        ready = any(x.key == "command" for x in g.rooms.values())
        for res in g.residents.values():
            if res.on_expedition or not res.alive or res.age == "child" or res.is_robot:
                continue
            row = pygame.Rect(area.x, yy, area.w - U.s(8), U.s(56))
            if row.bottom > area.y and row.y < area.bottom:
                pygame.draw.rect(s, C.UI_BG2, row, border_radius=U.s(5))
                pygame.draw.rect(s, C.UI_BORDER, row, max(1, U.s(1)), border_radius=U.s(5))
                U.text(s, U.trim(res.name, 15, row.w - U.s(130)),
                       (row.x + U.s(10), row.y + U.s(5)), C.UI_TEXT, size=15, bold=True)
                U.text(s, f"Lv{res.level} · E{res.stat_total('E')} "
                          f"L{res.stat_total('L')} · "
                          f"{(res.weapon or {}).get('name', 'unarmed')}",
                       (row.x + U.s(10), row.y + U.s(26)), C.UI_TEXT_DIM, size=12)
                ok = ready and not res.pregnant
                b = pygame.Rect(row.right - U.s(94), row.y + U.s(13), U.s(84), U.s(30))
                pygame.draw.rect(s, C.UI_ACCENT if ok else C.UI_BG, b, border_radius=U.s(3))
                pygame.draw.rect(s, C.UI_BORDER, b, max(1, U.s(1)), border_radius=U.s(3))
                U.text(s, "Send", b.center, (28, 20, 6) if ok else C.UI_TEXT_DIM,
                       size=13, center=True, bold=ok)
                if ok:
                    self._exp_btns.append((b, "send", res.id))
            yy += U.s(60)

        self.scroll.content_h = yy + int(self.scroll.offset) - area.y
        s.set_clip(prev)
        self.scroll.draw_scrollbar(s)

    # -- objectives
    def _p_objectives(self, s, r):
        g = self.g
        done = len(g.objectives_done)
        U.panel(s, r, f"Objectives  ({done}/{len(D.OBJECTIVES)})")
        area = pygame.Rect(r.x + U.s(8), r.y + U.s(40), r.w - U.s(16), r.h - U.s(48))
        self.scroll.rect = area
        prev = s.get_clip()
        s.set_clip(area)
        y = area.y - int(self.scroll.offset)
        for oid, obj in D.OBJECTIVES:
            row = pygame.Rect(area.x, y, area.w - U.s(8), U.s(56))
            fin = oid in g.objectives_done
            if row.bottom > area.y and row.y < area.bottom:
                pygame.draw.rect(s, (26, 54, 30) if fin else C.UI_BG2, row,
                                 border_radius=U.s(5))
                pygame.draw.rect(s, C.UI_GOOD if fin else C.UI_BORDER, row,
                                 max(1, U.s(1)), border_radius=U.s(5))
                U.text(s, ("✓  " if fin else "•  ") + U.trim(obj["desc"], 14, row.w - U.s(110)),
                       (row.x + U.s(10), row.y + U.s(7)),
                       C.UI_GOOD if fin else C.UI_TEXT, size=14, bold=fin)
                rw = obj.get("reward", {})
                bits = []
                if rw.get("caps"):
                    bits.append(f"{rw['caps']} caps")
                if rw.get("lunchbox"):
                    bits.append(f"{rw['lunchbox']} lunchbox")
                U.text(s, " + ".join(bits), (row.right - U.s(10), row.y + U.s(7)),
                       C.UI_ACCENT, size=12, right=True, bold=True)
                if not fin:
                    cur = g.objectives_progress.get(oid, 0)
                    U.draw_bar(s, pygame.Rect(row.x + U.s(10), row.bottom - U.s(20),
                                              row.w - U.s(20), U.s(12)),
                               cur, obj["n"], C.UI_ACCENT, f"{cur}/{obj['n']}", small=True)
            y += U.s(60)
        self.scroll.content_h = y + int(self.scroll.offset) - area.y
        s.set_clip(prev)
        self.scroll.draw_scrollbar(s)

    # -- lunchbox
    def _p_lunchbox(self, s, r):
        g = self.g
        U.panel(s, r, "Lunchbox")
        ic = G.lunchbox_icon(U.s(86))
        s.blit(ic, (r.centerx - ic.get_width() // 2, r.y + U.s(50)))
        y = r.y + U.s(150)
        if g.last_lunchbox_reward:
            U.text(s, "You found:", (r.centerx, y), C.UI_ACCENT, size=18,
                   center=True, bold=True)
            y += U.s(32)
            for card in g.last_lunchbox_reward:
                box = pygame.Rect(r.x + U.s(24), y, r.w - U.s(48), U.s(44))
                pygame.draw.rect(s, C.UI_BG2, box, border_radius=U.s(5))
                pygame.draw.rect(s, C.UI_ACCENT_DIM, box, max(1, U.s(1)),
                                 border_radius=U.s(5))
                U.text(s, U.trim(card, 15, box.w - U.s(24)),
                       (box.centerx, box.centery), C.UI_TEXT, size=15, center=True)
                y += U.s(52)
        else:
            U.text(s, "Open a lunchbox for four random rewards.",
                   (r.centerx, y), C.UI_TEXT_DIM, size=15, center=True)
        self._lunch_open = pygame.Rect(r.centerx - U.s(110), r.bottom - U.s(58),
                                       U.s(220), U.s(42))
        can = g.lunchboxes > 0
        pygame.draw.rect(s, C.UI_ACCENT if can else C.UI_BG2, self._lunch_open,
                         border_radius=U.s(5))
        pygame.draw.rect(s, C.UI_BORDER, self._lunch_open, max(1, U.s(1)),
                         border_radius=U.s(5))
        U.text(s, f"Open ({g.lunchboxes})" if can else "None left",
               self._lunch_open.center, (28, 20, 6) if can else C.UI_TEXT_DIM,
               size=17, center=True, bold=True)

    # -- menu
    def _p_menu(self, s, r):
        g = self.g
        U.panel(s, r, "Menu")
        y = r.y + U.s(48)
        stats = [
            ("Residents", f"{g.population()} / {g.housing_cap()}"),
            ("Rooms built", str(len(g.rooms))),
            ("Caps earned", str(g.caps_earned)),
            ("Enemies defeated", str(g.kills)),
            ("Expeditions", str(g.expeditions_completed)),
            ("Children born", str(g.births)),
            ("Deaths", str(g.deaths)),
            ("Rushes", f"{g.rushes_ok} ok / {g.rushes_failed} failed"),
            ("Power Armor found", str(g.pa_found)),
        ]
        for k, v in stats:
            U.text(s, k, (r.x + U.s(16), y), C.UI_TEXT_DIM, size=14)
            U.text(s, v, (r.right - U.s(16), y), C.UI_TEXT, size=14, right=True, bold=True)
            y += U.s(22)
        y += U.s(14)
        self._menu_btns = []
        for label, cb, style in (
            ("Save Now", self._save, "good"),
            ("Settings", lambda: self.app.set_screen(
                SettingsScreen(self.app, lambda: self.app.set_screen(self))), "normal"),
            ("Main Menu", self._to_menu, "normal"),
            ("Quit to Desktop", self._quit, "danger"),
        ):
            rr = pygame.Rect(r.x + U.s(16), y, r.w - U.s(32), U.s(40))
            b = U.Button(rr, label, None, style=style, size=16)
            b.hover = rr.collidepoint(pygame.mouse.get_pos())
            b._anim = 1.0 if b.hover else 0.0
            b.draw(s)
            self._menu_btns.append((rr, cb))
            y += U.s(48)

    def _to_menu(self):
        self._save()
        self.app.set_screen(MainMenuScreen(self.app))

    def _quit(self):
        self._save()
        self.app.running = False

    # ---------- drag ghost & draw ----------
    def draw(self, s):
        self._draw_world(s)
        self._draw_hud(s)
        self._draw_panels(s)
        if self._drag_res is not None and self._drag_moved:
            res = self.g.residents.get(self._drag_res)
            if res:
                port = G.resident_portrait(res.portrait_seed,
                                           (res.outfit or {}).get("rarity", 0),
                                           (res.power_armor or {}).get("name"),
                                           res.age, res.gender, not res.alive)
                mx, my = pygame.mouse.get_pos()
                img = scaled(port, U.s(56), U.s(56), f"drag{res.id}")
                img.set_alpha(215)
                s.blit(img, (mx - U.s(28), my - U.s(28)))
                img.set_alpha(255)
                room = self._room_at(mx, my)
                if room and room.capacity() > 0:
                    sx, sy = self.cam.world_to_screen(room.x * C.CELL_W,
                                                      room.floor * C.CELL_H)
                    z = self.cam.qzoom
                    full = len(room.workers) >= room.capacity()
                    pygame.draw.rect(s, C.UI_BAD if full else C.UI_GOOD,
                                     (sx, sy, room.width * C.CELL_W * z, C.CELL_H * z),
                                     max(2, U.s(3)))
        if self.tooltip:
            U.tooltip(s, pygame.mouse.get_pos(), self.tooltip)
        self.tooltip = None

    # ---------- helpers ----------
    def _room_at(self, mx, my):
        if not self.cam.viewport.collidepoint(mx, my):
            return None
        wx, wy = self.cam.screen_to_world(mx, my)
        return self.g.find_room(int(wy // C.CELL_H), wx / C.CELL_W)

    def _resident_at(self, mx, my):
        if not self.cam.viewport.collidepoint(mx, my):
            return None
        best, bd = None, U.s(30)
        for res in self.g.residents.values():
            if res.on_expedition:
                continue
            sx, sy = self.cam.world_to_screen(
                res.x * C.CELL_W + C.CELL_W / 2,
                res.floor * C.CELL_H + C.CELL_H - 10)
            d = math.hypot(sx - mx, sy - my - U.s(14))
            if d < bd:
                bd, best = d, res
        return best

    def _over_ui(self, pos):
        w, h = self.app.size
        if pos[1] < U.s(HUD_TOP) or pos[1] > h - U.s(HUD_BOT):
            return True
        if self.panel and self._panel_rect().collidepoint(pos):
            return True
        return any(rect.collidepoint(pos) for rect, _ in self._left_panels())

    # ---------- input ----------
    def handle(self, e):
        g = self.g
        if e.type == pygame.KEYDOWN:
            if self._key(e):
                return
        if self.scroll.handle(e):
            return
        for b in self.bottom + self.speed_btns:
            if b.handle(e):
                return
        if e.type == pygame.MOUSEMOTION:
            self._hover_tooltip(e.pos)
        if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
            if self._click_panels(e.pos):
                return
        if not self._over_ui(pygame.mouse.get_pos()):
            self.cam.handle(e)
        elif e.type == pygame.MOUSEBUTTONUP and e.button in (2, 3):
            self.cam.handle(e)

        # world interaction
        if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1 and not self._over_ui(e.pos):
            for br, rid in self._collect_badges:
                if br.collidepoint(e.pos):
                    g.collect_room(rid)
                    return
            if self.mode == "build" and self.build_key and self._ghost:
                fl, col, width = self._ghost
                g.place_room(self.build_key, fl, col, width)
                return
            if self.mode == "destroy":
                room = self._room_at(*e.pos)
                if room:
                    g.destroy_room(room.id)
                return
            res = self._resident_at(*e.pos)
            if res:
                self._drag_res = res.id
                self._drag_from = e.pos
                self._drag_moved = False
                return
            room = self._room_at(*e.pos)
            self.selected_room = room.id if room else None
            if room:
                self.selected_resident = None
            return

        if e.type == pygame.MOUSEMOTION and self._drag_res is not None:
            if self._drag_from and (abs(e.pos[0] - self._drag_from[0]) > U.s(5)
                                    or abs(e.pos[1] - self._drag_from[1]) > U.s(5)):
                self._drag_moved = True
            return

        if e.type == pygame.MOUSEBUTTONUP and e.button == 1 and self._drag_res is not None:
            rid = self._drag_res
            moved = self._drag_moved
            self._drag_res = None
            self._drag_moved = False
            if moved:
                room = self._room_at(*e.pos)
                if room:
                    if room.capacity() > 0:
                        g.assign(rid, room.id)
                    else:
                        g.notify(f"{room.data()['name']} has no work posts.", "bad")
            else:
                self.selected_resident = rid
                self.selected_room = None
                if self.panel == "residents":
                    pass
            return

    def _key(self, e) -> bool:
        g = self.g
        k = e.key
        if k == pygame.K_ESCAPE:
            if self.panel:
                self.panel = None
            elif self.mode != "look":
                self.mode = "look"
                self.build_key = None
            elif self.selected_room or self.selected_resident:
                self.selected_room = self.selected_resident = None
            else:
                self._toggle("menu")
            return True
        mapping = {pygame.K_b: "build", pygame.K_r: "residents", pygame.K_i: "inventory",
                   pygame.K_e: "exploration", pygame.K_o: "objectives"}
        if k in mapping:
            if k == pygame.K_b:
                self._mode("build")
            else:
                self._toggle(mapping[k])
            return True
        if k == pygame.K_c:
            self._collect_all()
            return True
        if k == pygame.K_SPACE:
            self._speed(0)
            return True
        if k in (pygame.K_1, pygame.K_2, pygame.K_3):
            self._speed({pygame.K_1: 1, pygame.K_2: 2, pygame.K_3: 4}[k])
            return True
        if k == pygame.K_F5:
            self._save()
            return True
        if k == pygame.K_l and g.lunchboxes > 0:
            self._toggle("lunchbox")
            return True
        return False

    def _hover_tooltip(self, pos):
        for b in self.bottom:
            if b.tooltip_lines and b.rect.collidepoint(pos):
                self.tooltip = b.tooltip_lines
                return
        if getattr(self, "_lunch_rect", None) and self._lunch_rect.collidepoint(pos):
            self.tooltip = ["Lunchboxes (L)", "Four random rewards per box."]
            return
        for r, name, val, cap in getattr(self, "_tile_rects", []):
            if r.collidepoint(pos):
                lines = [name, f"{val}" + (f" of {cap}" if cap else "")]
                if cap and val < cap * 0.15:
                    lines.append("Running low!")
                self.tooltip = lines
                return
        if not self._over_ui(pos):
            room = self._room_at(*pos)
            if room:
                rd = room.data()
                lines = [f"{rd['name']} (Lv {room.level})"]
                if room.capacity():
                    lines.append(f"Staff {len(room.workers)}/{room.capacity()}")
                if room.has_output():
                    lines.append("Output ready — click the badge")
                self.tooltip = lines

    def _click_panels(self, pos) -> bool:
        g = self.g
        if getattr(self, "_lunch_rect", None) and self._lunch_rect.collidepoint(pos):
            if g.lunchboxes > 0:
                self._toggle("lunchbox")
            else:
                g.notify("No lunchboxes yet — complete objectives to earn them.", "warn")
            return True

        # room buttons
        if self.selected_room and self.selected_room in g.rooms:
            for kk, (rr, enabled) in getattr(self, "_room_btns", {}).items():
                if rr.collidepoint(pos):
                    if not enabled:
                        return True
                    rid = self.selected_room
                    if kk == "collect":
                        g.collect_room(rid)
                    elif kk == "rush":
                        g.rush_room(rid)
                    elif kk == "staff":
                        g.auto_assign_best(rid)
                    elif kk == "upgrade":
                        g.upgrade_room(rid)
                    elif kk == "merge":
                        g.try_merge(rid)
                    elif kk == "destroy":
                        g.destroy_room(rid)
                        self.selected_room = None
                    return True
        # resident buttons
        if self.selected_resident and self.selected_resident in g.residents:
            for kk, (rr, enabled) in getattr(self, "_res_btns", {}).items():
                if rr.collidepoint(pos):
                    if not enabled:
                        return True
                    rid = self.selected_resident
                    res = g.residents[rid]
                    if kk == "stim":
                        g.use_stimpack(rid)
                    elif kk == "rad":
                        g.use_radaway(rid)
                    elif kk == "unassign":
                        g.assign(rid, None)
                    elif kk == "explore":
                        g.start_expedition(rid)
                    elif kk == "repair":
                        g.repair_pa(rid)
                    elif kk == "revive":
                        g.revive(rid)
                    elif kk == "focus":
                        self.cam.focus(res.floor, res.x)
                    return True

        if not self.panel:
            return False
        pr = self._panel_rect()
        if not pr.collidepoint(pos):
            return False

        if self.panel == "build":
            if self._w_minus.collidepoint(pos):
                self.build_width = max(2, self.build_width - 1)
                return True
            if self._w_plus.collidepoint(pos):
                self.build_width = min(6, self.build_width + 1)
                return True
            for row, key in getattr(self, "_build_rows", []):
                if row.collidepoint(pos):
                    self.build_key = key
                    self.mode = "build"
                    if key == "elevator":
                        self.build_width = 1
                    else:
                        self.build_width = max(D.ROOMS[key]["width"],
                                               min(self.build_width,
                                                   D.ROOMS[key]["width"] * C.ROOM_MAX_MERGE))
                    A.play("click")
                    return True
            return True

        if self.panel == "residents":
            for row, rid in getattr(self, "_res_rows", []):
                if row.collidepoint(pos):
                    self.selected_resident = rid
                    self.selected_room = None
                    res = g.residents.get(rid)
                    if res:
                        self.cam.focus(res.floor, res.x)
                    return True
            return True

        if self.panel == "inventory":
            if self._inv_tabs.handle(pygame.event.Event(
                    pygame.MOUSEBUTTONDOWN, button=1, pos=pos)):
                self.inv_tab = self._inv_tabs.active
                self.scroll.offset = 0
                return True
            for entry in getattr(self, "_inv_rows", []):
                row, sb, cb, idx = entry
                if sb and sb.collidepoint(pos):
                    if isinstance(idx, tuple):
                        g.sell_pa(idx[1])
                    else:
                        g.sell_item(idx)
                    return True
                if cb and cb.collidepoint(pos):
                    g.scrap_item(idx)
                    return True
                if row.collidepoint(pos):
                    if not self.selected_resident or self.selected_resident not in g.residents:
                        g.notify("Select a resident first.", "warn")
                        return True
                    if isinstance(idx, tuple):
                        g.equip(self.selected_resident, "pa", idx[1])
                    else:
                        it = g.inventory[idx]
                        g.equip(self.selected_resident,
                                "weapon" if it["kind"] == "weapon" else "outfit", idx)
                    return True
            for b, kind, rar, can in getattr(self, "_craft_rows", []):
                if b.collidepoint(pos):
                    g.craft(kind, rar)
                    return True
            return True

        if self.panel == "exploration":
            for b, what, rid in getattr(self, "_exp_btns", []):
                if b.collidepoint(pos):
                    if what == "send":
                        g.start_expedition(rid)
                    else:
                        g.recall_expedition(rid)
                    return True
            return True

        if self.panel == "lunchbox":
            if getattr(self, "_lunch_open", None) and self._lunch_open.collidepoint(pos):
                g.open_lunchbox()
                return True
            return True

        if self.panel == "menu":
            for rr, cb in getattr(self, "_menu_btns", []):
                if rr.collidepoint(pos):
                    cb()
                    return True
            return True
        return True


# ======================================================================
# App
# ======================================================================
class App:
    def __init__(self):
        pygame.init()
        A.init()
        A.build_bank()

        cfg = S.load_settings()
        A.load_settings(cfg.get("audio", {}))
        self.res_name = cfg.get("resolution", C.DEFAULT_RESOLUTION)
        self.quality = cfg.get("quality", C.DEFAULT_QUALITY)
        self.fullscreen = cfg.get("fullscreen", False)
        if self.quality not in C.QUALITY_LEVELS:
            self.quality = C.DEFAULT_QUALITY

        w, h = self._res_size()
        # Never open a window larger than the desktop.
        try:
            di = pygame.display.Info()
            if di.current_w > 0 and (w > di.current_w or h > di.current_h):
                for name, rw, rh in reversed(C.RESOLUTIONS):
                    if rw <= di.current_w and rh <= di.current_h:
                        self.res_name = name
                        w, h = rw, rh
                        break
        except pygame.error:
            pass

        flags = pygame.RESIZABLE | (pygame.FULLSCREEN if self.fullscreen else 0)
        self.renderer, self.renderer_note = R.create_renderer(
            (w, h), flags, quality=self.quality)
        pygame.display.set_caption(C.TITLE)
        U.set_scale(h)
        try:
            pygame.display.set_icon(G.app_icon(64))
        except Exception:
            pass

        self.clock = pygame.time.Clock()
        self.running = True
        self.game = None
        self.slot = None
        self.screen_obj = MainMenuScreen(self)
        A.start_music()

    # -- helpers
    def _res_size(self):
        for name, w, h in C.RESOLUTIONS:
            if name == self.res_name:
                return w, h
        return C.DEFAULT_WIDTH, C.DEFAULT_HEIGHT

    @property
    def size(self):
        return self.renderer.size

    def save_settings(self):
        S.save_settings(dict(audio=A.get_settings(), resolution=self.res_name,
                             quality=self.quality, fullscreen=self.fullscreen))

    def _reflow(self):
        U.set_scale(self.size[1])
        _scaled.clear()
        if hasattr(self.screen_obj, "resize"):
            self.screen_obj.resize()

    def set_resolution(self, name):
        if name == self.res_name:
            return
        self.res_name = name
        w, h = self._res_size()
        flags = pygame.RESIZABLE | (pygame.FULLSCREEN if self.fullscreen else 0)
        self.renderer.resize((w, h), flags)
        self._reflow()
        self.save_settings()

    def set_quality(self, q):
        if q == self.quality or q not in C.QUALITY_LEVELS:
            return
        self.quality = q
        self.renderer.set_quality(q)
        self.save_settings()

    def set_fullscreen(self, on):
        self.fullscreen = bool(on)
        w, h = self.size
        flags = pygame.RESIZABLE | (pygame.FULLSCREEN if self.fullscreen else 0)
        self.renderer.resize((w, h), flags)
        self._reflow()
        self.save_settings()

    def set_screen(self, screen):
        self.screen_obj = screen
        if hasattr(screen, "resize"):
            screen.resize()

    def start_game(self, state, slot):
        self.game = state
        self.slot = slot
        S.save(slot, state.to_dict())
        self.set_screen(WorldScreen(self))

    def _autosave_on_exit(self):
        if self.game is not None and self.slot is not None:
            try:
                S.save(self.slot, self.game.to_dict())
            except Exception:
                pass

    def run(self):
        while self.running:
            dt = min(0.1, self.clock.tick(C.FPS) / 1000.0)
            for e in pygame.event.get():
                if e.type == pygame.QUIT:
                    self._autosave_on_exit()
                    self.running = False
                    break
                if e.type == pygame.VIDEORESIZE and not self.fullscreen:
                    w = max(C.MIN_WIDTH, e.w)
                    h = max(C.MIN_HEIGHT, e.h)
                    self.renderer.resize((w, h), pygame.RESIZABLE)
                    self._reflow()
                    continue
                self.screen_obj.handle(e)
            if not self.running:
                break
            if hasattr(self.screen_obj, "update"):
                self.screen_obj.update(dt)
            surf = self.renderer.begin()
            self.screen_obj.draw(surf)
            self.renderer.present(dt)

        A.stop_music()
        try:
            self.renderer.shutdown()
        except Exception:
            pass
        pygame.quit()


def run():
    App().run()


if __name__ == "__main__":
    run()
