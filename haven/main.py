"""Main entry point, main loop, and screens for Haven."""

from __future__ import annotations
import math
import os
import sys
import time as _time
import pygame

from . import config as C
from . import data as D
from . import assets as G
from . import audio as A
from . import save as S
from . import ui as U
from . import game as GM


# --------------------- Camera ---------------------
class Camera:
    def __init__(self, screen_w, screen_h):
        self.x = C.COLUMNS * C.CELL_W / 2
        self.y = 3 * C.CELL_H
        self.zoom = 1.0
        self.screen_w = screen_w
        self.screen_h = screen_h
        self.viewport = pygame.Rect(0, 96, screen_w, screen_h - 96 - 44)
        self._dragging = False
        self._drag_last = None
        self._pinch = False
        self._smooth_tx = self.x
        self._smooth_ty = self.y

    def resize(self, w, h):
        self.screen_w = w
        self.screen_h = h
        self.viewport = pygame.Rect(0, 96, w, h - 96 - 44)

    def world_to_screen(self, wx, wy) -> tuple[float, float]:
        cx = self.viewport.x + self.viewport.w / 2
        cy = self.viewport.y + self.viewport.h / 2
        return (cx + (wx - self.x) * self.zoom, cy + (wy - self.y) * self.zoom)

    def screen_to_world(self, sx, sy) -> tuple[float, float]:
        cx = self.viewport.x + self.viewport.w / 2
        cy = self.viewport.y + self.viewport.h / 2
        return ((sx - cx) / self.zoom + self.x, (sy - cy) / self.zoom + self.y)

    def update(self, dt, keys):
        pan = 480 * dt / max(0.5, self.zoom)
        if keys[pygame.K_LEFT] or keys[pygame.K_a]:
            self.x -= pan
        if keys[pygame.K_RIGHT] or keys[pygame.K_d]:
            self.x += pan
        if keys[pygame.K_UP] or keys[pygame.K_w]:
            self.y -= pan
        if keys[pygame.K_DOWN] or keys[pygame.K_s]:
            self.y += pan
        # smooth follow
        self._smooth_tx += (self.x - self._smooth_tx) * min(1, dt * 12)
        self._smooth_ty += (self.y - self._smooth_ty) * min(1, dt * 12)

    def handle(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 2:
            self._dragging = True
            self._drag_last = event.pos
        elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
            self._dragging = True
            self._drag_last = event.pos
        elif event.type == pygame.MOUSEBUTTONUP and event.button in (2, 3):
            self._dragging = False
            self._drag_last = None
        elif event.type == pygame.MOUSEMOTION and self._dragging and self._drag_last:
            dx = event.pos[0] - self._drag_last[0]
            dy = event.pos[1] - self._drag_last[1]
            self.x -= dx / self.zoom
            self.y -= dy / self.zoom
            self._drag_last = event.pos
        elif event.type == pygame.MOUSEWHEEL:
            old = self.zoom
            self.zoom = max(0.5, min(2.0, self.zoom * (1.1 if event.y > 0 else 1 / 1.1)))
            # zoom toward cursor
            mx, my = pygame.mouse.get_pos()
            wx, wy = self.screen_to_world(mx, my)
            self.x = wx - (mx - (self.viewport.x + self.viewport.w / 2)) / self.zoom
            self.y = wy - (my - (self.viewport.y + self.viewport.h / 2)) / self.zoom


# --------------------- Screens ---------------------
class MainMenuScreen:
    def __init__(self, app):
        self.app = app
        cx = app.screen.get_width() // 2
        cy = app.screen.get_height() // 2
        self.buttons = [
            U.Button((cx - 130, cy - 20, 260, 44), "Continue", self.act_continue, style="primary"),
            U.Button((cx - 130, cy + 34, 260, 40), "New Game", self.act_new),
            U.Button((cx - 130, cy + 78, 260, 40), "Load Game", self.act_load),
            U.Button((cx - 130, cy + 122, 260, 40), "Settings", self.act_settings),
            U.Button((cx - 130, cy + 166, 260, 40), "Quit", self.act_quit),
        ]
        # disable continue if no saves
        slots = S.list_slots()
        if not any(s["exists"] for s in slots):
            self.buttons[0].enabled = False

    def act_continue(self):
        slots = S.list_slots()
        latest = None
        for s in slots:
            if s["exists"]:
                if latest is None or s["meta"].get("saved_at", 0) > latest["meta"].get("saved_at", 0):
                    latest = s
        if latest:
            d = S.load(latest["slot"])
            if d:
                self.app.game = GM.GameState.from_dict(d)
                self.app.slot = latest["slot"]
                self.app.set_screen(WorldScreen(self.app))

    def act_new(self):
        self.app.set_screen(SlotSelectScreen(self.app, mode="new"))

    def act_load(self):
        self.app.set_screen(SlotSelectScreen(self.app, mode="load"))

    def act_settings(self):
        self.app.set_screen(SettingsScreen(self.app, back=lambda: self.app.set_screen(MainMenuScreen(self.app))))

    def act_quit(self):
        self.app.running = False

    def draw(self, s):
        w, h = s.get_size()
        # gradient background
        for y in range(h):
            t = y / h
            c = (int(20 + 20 * t), int(22 + 20 * t), int(44 + 18 * t))
            pygame.draw.line(s, c, (0, y), (w, y))
        # logo
        icon = G.app_icon(160)
        s.blit(icon, (w // 2 - 80, h // 3 - 180))
        U.text(s, "HAVEN", (w // 2, h // 3 + 10), C.UI_ACCENT, size=64, bold=True, center=True)
        U.text(s, "an underground shelter management game",
               (w // 2, h // 3 + 50), C.UI_TEXT_DIM, size=16, center=True)
        for b in self.buttons: b.draw(s)
        U.text(s, "v1.0.0", (10, h - 22), C.UI_TEXT_DIM, size=12)
        U.text(s, "Original project — not affiliated with any other franchise.",
               (w - 10, h - 22), C.UI_TEXT_DIM, size=12, right=True)

    def handle(self, event):
        for b in self.buttons:
            b.handle(event)


class SlotSelectScreen:
    def __init__(self, app, mode="new"):
        self.app = app
        self.mode = mode
        self.refresh()

    def refresh(self):
        self.slots = S.list_slots()

    def draw(self, s):
        w, h = s.get_size()
        s.fill((22, 24, 34))
        title = "New Game — Choose a slot" if self.mode == "new" else "Load Game"
        U.text(s, title, (w // 2, 60), C.UI_ACCENT, size=32, bold=True, center=True)
        y = 120
        self._rects = []
        for slot in self.slots:
            r = pygame.Rect(w // 2 - 240, y, 480, 90)
            pygame.draw.rect(s, C.UI_BG2, r, border_radius=6)
            pygame.draw.rect(s, C.UI_BORDER, r, 1, border_radius=6)
            if slot["exists"]:
                m = slot["meta"]
                U.text(s, f"Slot {slot['slot']+1}", (r.x + 12, r.y + 10),
                       C.UI_ACCENT, size=20, bold=True)
                U.text(s, f"Pop {m.get('pop','?')} · Caps {m.get('caps','?')} · "
                          f"{int(m.get('time',0)//60)} min played",
                       (r.x + 12, r.y + 38), C.UI_TEXT, size=14)
                U.text(s, _time.strftime("%Y-%m-%d %H:%M", _time.localtime(m.get("saved_at", 0))),
                       (r.x + 12, r.y + 60), C.UI_TEXT_DIM, size=13)
                # actions
                del_rect = pygame.Rect(r.right - 80, r.y + 30, 60, 30)
                pygame.draw.rect(s, (110, 42, 42), del_rect, border_radius=4)
                U.text(s, "Delete", del_rect.center, C.UI_TEXT, size=13, center=True)
                self._rects.append((slot["slot"], r, del_rect))
            else:
                U.text(s, f"Slot {slot['slot']+1} — empty", (r.x + 12, r.y + 30),
                       C.UI_TEXT_DIM, size=18)
                self._rects.append((slot["slot"], r, None))
            y += 100
        # Back button
        self.back_rect = pygame.Rect(20, h - 60, 100, 36)
        pygame.draw.rect(s, C.UI_BG2, self.back_rect, border_radius=4)
        pygame.draw.rect(s, C.UI_BORDER, self.back_rect, 1, border_radius=4)
        U.text(s, "Back", self.back_rect.center, C.UI_TEXT, size=15, center=True)

    def handle(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.back_rect.collidepoint(event.pos):
                self.app.set_screen(MainMenuScreen(self.app))
                return
            for slot_i, r, del_rect in self._rects:
                if del_rect and del_rect.collidepoint(event.pos):
                    S.delete(slot_i)
                    self.refresh()
                    return
                if r.collidepoint(event.pos):
                    if self.mode == "new":
                        # confirm overwrite if exists (implicit — we just start fresh)
                        self.app.game = GM.GameState()
                        self.app.slot = slot_i
                        S.save(slot_i, self.app.game.to_dict())
                        self.app.set_screen(WorldScreen(self.app))
                    else:
                        d = S.load(slot_i)
                        if d:
                            self.app.game = GM.GameState.from_dict(d)
                            self.app.slot = slot_i
                            self.app.set_screen(WorldScreen(self.app))
                    return


class SettingsScreen:
    def __init__(self, app, back):
        self.app = app
        self.back = back
        self.master = A.get_settings()["master"]
        self.music = A.get_settings()["music"]
        self.sfx = A.get_settings()["sfx"]
        self.music_on = A.get_settings()["music_on"]
        self.sfx_on = A.get_settings()["sfx_on"]
        self.fullscreen = app.fullscreen
        self.animations = app.animations
        w, h = app.screen.get_size()
        self.back_btn = U.Button((20, h - 60, 100, 36), "Back", self._go_back)
        self.reset_btn = U.Button((w - 220, h - 60, 200, 36), "Reset This Slot", self._reset_slot,
                                  style="danger")

    def _go_back(self):
        A.set_master(self.master)
        A.set_music(self.music, self.music_on)
        A.set_sfx(self.sfx, self.sfx_on)
        self.app.animations = self.animations
        self.app.save_settings()
        self.back()

    def _reset_slot(self):
        if self.app.slot is not None:
            S.delete(self.app.slot)
            self.app.game = GM.GameState()
            S.save(self.app.slot, self.app.game.to_dict())

    def draw(self, s):
        w, h = s.get_size()
        s.fill((22, 24, 34))
        U.text(s, "Settings", (w // 2, 50), C.UI_ACCENT, size=32, bold=True, center=True)
        cx = w // 2
        y = 110
        # sliders drawn as clickable strips
        self._hit = []
        for label, val, key in [
            ("Master Volume", self.master, "master"),
            ("Music Volume", self.music, "music"),
            ("SFX Volume", self.sfx, "sfx"),
        ]:
            U.text(s, label, (cx - 240, y), C.UI_TEXT, size=17)
            r = pygame.Rect(cx - 40, y - 4, 260, 22)
            pygame.draw.rect(s, C.UI_BG2, r, border_radius=4)
            fill = r.copy(); fill.w = int(r.w * val)
            pygame.draw.rect(s, C.UI_ACCENT, fill, border_radius=4)
            pygame.draw.rect(s, C.UI_BORDER, r, 1, border_radius=4)
            U.text(s, f"{int(val*100)}%", (r.right + 10, y), C.UI_TEXT, size=15)
            self._hit.append((r, key))
            y += 44
        # toggles
        for label, val, key in [
            ("Music enabled", self.music_on, "music_on"),
            ("SFX enabled", self.sfx_on, "sfx_on"),
            ("Fullscreen", self.fullscreen, "fullscreen"),
            ("Animations", self.animations, "animations"),
        ]:
            U.text(s, label, (cx - 240, y), C.UI_TEXT, size=17)
            r = pygame.Rect(cx - 40, y - 4, 60, 24)
            pygame.draw.rect(s, C.UI_ACCENT if val else C.UI_BG2, r, border_radius=12)
            pygame.draw.rect(s, C.UI_BORDER, r, 1, border_radius=12)
            knob_x = r.right - 20 if val else r.x + 4
            pygame.draw.circle(s, C.UI_TEXT, (knob_x + 8, r.y + 12), 8)
            U.text(s, "On" if val else "Off", (r.right + 10, y), C.UI_TEXT, size=15)
            self._hit.append((r, key))
            y += 44

        U.text(s, "Controls: WASD/arrows to pan · scroll wheel to zoom · right-click drag to pan",
               (cx, h - 120), C.UI_TEXT_DIM, size=14, center=True)
        self.back_btn.draw(s)
        if self.app.slot is not None:
            self.reset_btn.draw(s)

    def handle(self, event):
        self.back_btn.handle(event)
        if self.app.slot is not None:
            self.reset_btn.handle(event)
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            for r, key in self._hit:
                if r.collidepoint(event.pos):
                    if key in ("master", "music", "sfx"):
                        val = max(0, min(1, (event.pos[0] - r.x) / r.w))
                        setattr(self, key, val)
                        if key == "master": A.set_master(val)
                        elif key == "music": A.set_music(val, self.music_on)
                        elif key == "sfx": A.set_sfx(val, self.sfx_on)
                    elif key in ("music_on", "sfx_on"):
                        setattr(self, key, not getattr(self, key))
                        A.set_music(self.music, self.music_on)
                        A.set_sfx(self.sfx, self.sfx_on)
                    elif key == "fullscreen":
                        self.fullscreen = not self.fullscreen
                        self.app.set_fullscreen(self.fullscreen)
                    elif key == "animations":
                        self.animations = not self.animations
        elif event.type == pygame.MOUSEMOTION and event.buttons[0]:
            for r, key in self._hit:
                if key in ("master", "music", "sfx") and r.collidepoint(event.pos):
                    val = max(0, min(1, (event.pos[0] - r.x) / r.w))
                    setattr(self, key, val)
                    if key == "master": A.set_master(val)
                    elif key == "music": A.set_music(val, self.music_on)
                    elif key == "sfx": A.set_sfx(val, self.sfx_on)


# --------------------- World screen ---------------------
class WorldScreen:
    def __init__(self, app):
        self.app = app
        w, h = app.screen.get_size()
        self.cam = Camera(w, h)
        self.tick_accum = 0.0
        self.autosave_at = 60.0
        self.selected_room: int | None = None
        self.selected_resident: int | None = None
        self.mode = "look"           # look | build | destroy
        self.build_key: str | None = None
        self.build_width: int = 2
        self.build_floor: int = 0
        self.build_x: int = 0
        self.panel: str | None = None    # None | 'residents' | 'inventory' | 'objectives' | 'exploration' | 'menu'
        self._make_hud()
        self.list_scroll = U.ScrollList((0, 0, 400, 400))
        self._notify_display: list = []

    def _make_hud(self):
        w, h = self.app.screen.get_size()
        self.buttons_top = []
        self.buttons_bottom = [
            U.Button((10, h - 40, 110, 32), "Build", lambda: self._toggle_mode("build"), style="primary"),
            U.Button((124, h - 40, 100, 32), "Destroy", lambda: self._toggle_mode("destroy"), style="danger"),
            U.Button((228, h - 40, 110, 32), "Residents", lambda: self._toggle_panel("residents")),
            U.Button((342, h - 40, 110, 32), "Inventory", lambda: self._toggle_panel("inventory")),
            U.Button((456, h - 40, 110, 32), "Explore", lambda: self._toggle_panel("exploration")),
            U.Button((570, h - 40, 110, 32), "Objectives", lambda: self._toggle_panel("objectives")),
            U.Button((684, h - 40, 100, 32), "Save", self._save),
            U.Button((788, h - 40, 100, 32), "Menu", lambda: self._toggle_panel("menu")),
        ]
        # speed and pause
        self.buttons_speed = [
            U.Button((w - 210, h - 40, 60, 32), "Pause",
                     lambda: self._set_speed(0), style="ghost"),
            U.Button((w - 148, h - 40, 40, 32), "1x", lambda: self._set_speed(1)),
            U.Button((w - 106, h - 40, 40, 32), "2x", lambda: self._set_speed(2)),
            U.Button((w - 64, h - 40, 44, 32), "4x", lambda: self._set_speed(4)),
        ]

    def _set_speed(self, sp):
        g = self.app.game
        if sp == 0:
            g.paused = True
        else:
            g.paused = False
            g.speed = sp
        A.play("click")

    def _toggle_mode(self, m):
        if self.mode == m:
            self.mode = "look"; self.build_key = None
        else:
            self.mode = m
            self.panel = "build_picker" if m == "build" else None
        A.play("click")

    def _toggle_panel(self, p):
        self.panel = None if self.panel == p else p
        A.play("click")

    def _save(self):
        if self.app.slot is None:
            return
        S.save(self.app.slot, self.app.game.to_dict())
        self.app.game.notify("Game saved.", "good")
        A.play("cash")

    # ------------- update -------------
    def update(self, dt):
        keys = pygame.key.get_pressed()
        self.cam.update(dt, keys)
        g = self.app.game
        g.tick(dt)
        # autosave
        self.autosave_at -= dt
        if self.autosave_at <= 0:
            self.autosave_at = 60.0
            if self.app.slot is not None:
                S.save(self.app.slot, self.app.game.to_dict())

    # ------------- world drawing -------------
    def _shelter_bounds(self):
        w = C.COLUMNS * C.CELL_W
        h = C.FLOOR_COUNT * C.CELL_H
        return pygame.Rect(0, 0, w, h)

    def _draw_world(self, surf):
        # Background — sky above, dirt for the shelter area
        vp = self.cam.viewport
        pygame.draw.rect(surf, C.BG_SKY_BOTTOM, vp)
        # gradient sky
        for i in range(vp.h):
            t = i / vp.h
            c = (int(C.BG_SKY_TOP[0]*(1-t)+C.BG_SKY_BOTTOM[0]*t),
                 int(C.BG_SKY_TOP[1]*(1-t)+C.BG_SKY_BOTTOM[1]*t),
                 int(C.BG_SKY_TOP[2]*(1-t)+C.BG_SKY_BOTTOM[2]*t))
            pygame.draw.line(surf, c, (vp.x, vp.y + i), (vp.right, vp.y + i))

        # World layers
        bounds = self._shelter_bounds()
        # Dirt behind the shelter
        top_left = self.cam.world_to_screen(bounds.x - 200, 0)
        bottom_right = self.cam.world_to_screen(bounds.right + 200, bounds.h + 400)
        dirt_rect = pygame.Rect(top_left[0], top_left[1],
                                bottom_right[0] - top_left[0], bottom_right[1] - top_left[1])
        pygame.draw.rect(surf, C.BG_DIRT_TOP, dirt_rect.clip(vp))

        # Grid cells
        for f in range(C.FLOOR_COUNT):
            for cx in range(C.COLUMNS):
                wx = cx * C.CELL_W
                wy = f * C.CELL_H
                sx, sy = self.cam.world_to_screen(wx, wy)
                sw = C.CELL_W * self.cam.zoom
                sh = C.CELL_H * self.cam.zoom
                if sx + sw < vp.x or sx > vp.right: continue
                if sy + sh < vp.y or sy > vp.bottom: continue
                r = pygame.Rect(sx, sy, sw + 1, sh + 1)
                pygame.draw.rect(surf, C.GRID_EMPTY, r)
                pygame.draw.rect(surf, C.GRID_LINE, r, 1)

        # Rooms
        g = self.app.game
        for room in g.rooms.values():
            wx = room.x * C.CELL_W
            wy = room.floor * C.CELL_H
            sx, sy = self.cam.world_to_screen(wx, wy)
            sw = room.width * C.CELL_W * self.cam.zoom
            sh = C.CELL_H * self.cam.zoom
            if sx + sw < vp.x or sx > vp.right: continue
            if sy + sh < vp.y or sy > vp.bottom: continue
            powered = (not room.data().get("requires_power")) or g.resources.get("power", 0) > 0
            spr = G.room_sprite(room.key, room.width, room.level, powered)
            spr2 = pygame.transform.scale(spr, (int(sw), int(sh)))
            surf.blit(spr2, (sx, sy))
            # Overlays
            if room.on_fire:
                overlay = pygame.Surface((int(sw), int(sh)), pygame.SRCALPHA)
                overlay.fill((255, 90, 20, 90))
                surf.blit(overlay, (sx, sy))
            # progress bar
            if room.workers and room.key not in ("elevator", "storage"):
                r = pygame.Rect(sx + 4, sy + sh - 6, sw - 8, 3)
                pygame.draw.rect(surf, (30, 30, 30), r)
                fr = r.copy(); fr.w = int(r.w * room.progress)
                pygame.draw.rect(surf, C.UI_GOOD, fr)
            # hp bar if damaged
            if room.hp < 100:
                r = pygame.Rect(sx + 4, sy + 4, sw - 8, 3)
                pygame.draw.rect(surf, (60, 20, 20), r)
                fr = r.copy(); fr.w = int(r.w * room.hp / 100)
                pygame.draw.rect(surf, (220, 60, 60), fr)
            # selection outline
            if self.selected_room == room.id:
                pygame.draw.rect(surf, C.UI_ACCENT, (sx, sy, sw, sh), 2)

        # Residents
        for res in g.residents.values():
            if res.on_expedition: continue
            wx = res.x * C.CELL_W + C.CELL_W / 2
            wy = res.floor * C.CELL_H + C.CELL_H - 20
            sx, sy = self.cam.world_to_screen(wx, wy)
            sprite = G.resident_sprite(res.portrait_seed,
                                       (res.power_armor or {}).get("name"),
                                       res.facing,
                                       res.step,
                                       (res.outfit or {}).get("rarity", 0))
            ssw = int(sprite.get_width() * self.cam.zoom)
            ssh = int(sprite.get_height() * self.cam.zoom)
            scaled = pygame.transform.scale(sprite, (ssw, ssh))
            surf.blit(scaled, (int(sx - ssw / 2), int(sy - ssh)))
            # health bar if hurt
            if res.hp < res.max_hp:
                r = pygame.Rect(int(sx - 12), int(sy - ssh - 6), 24, 3)
                pygame.draw.rect(surf, (60, 20, 20), r)
                fr = r.copy(); fr.w = int(r.w * res.hp / res.max_hp)
                pygame.draw.rect(surf, (220, 60, 60), fr)
            # selection ring
            if self.selected_resident == res.id:
                pygame.draw.circle(surf, C.UI_ACCENT,
                                   (int(sx), int(sy - 2)), 12, 2)

        # Combat sparks and fire particles
        for room in g.rooms.values():
            if room.invaders:
                wx = room.x * C.CELL_W + room.width * C.CELL_W / 2
                wy = room.floor * C.CELL_H + 12
                sx, sy = self.cam.world_to_screen(wx, wy)
                pygame.draw.polygon(surf, (240, 80, 80),
                                    [(sx - 8, sy), (sx + 8, sy), (sx, sy - 10)])
                U.text(surf, f"! {len(room.invaders)}", (sx, sy - 24),
                       (255, 200, 200), size=12, center=True, bold=True)
            if room.on_fire:
                wx = room.x * C.CELL_W + room.width * C.CELL_W / 2
                wy = room.floor * C.CELL_H + C.CELL_H / 2
                sx, sy = self.cam.world_to_screen(wx, wy)
                for k in range(4):
                    off = (k * 7 + int(g.time * 8)) % 14
                    pygame.draw.circle(surf, (255, 160 + k * 20, 40),
                                       (int(sx + (k - 2) * 6), int(sy - off)), 4 - k // 2)

        # Build ghost
        if self.mode == "build" and self.build_key:
            mx, my = pygame.mouse.get_pos()
            wx, wy = self.cam.screen_to_world(mx, my)
            fl = max(0, min(C.FLOOR_COUNT - 1, int(wy // C.CELL_H)))
            col = int(wx // C.CELL_W)
            self.build_floor = fl
            self.build_x = col
            width = 1 if self.build_key == "elevator" else self.build_width
            self._draw_ghost(surf, self.build_key, fl, col, width)

    def _draw_ghost(self, surf, key, floor, x, width):
        ok, why = self.app.game.can_place(key, floor, x, width)
        wx = x * C.CELL_W
        wy = floor * C.CELL_H
        sx, sy = self.cam.world_to_screen(wx, wy)
        sw = width * C.CELL_W * self.cam.zoom
        sh = C.CELL_H * self.cam.zoom
        color = (100, 220, 120, 100) if ok else (220, 60, 60, 110)
        s = pygame.Surface((int(sw), int(sh)), pygame.SRCALPHA)
        s.fill(color)
        surf.blit(s, (sx, sy))
        pygame.draw.rect(surf, color[:3], (sx, sy, sw, sh), 2)
        if not ok:
            U.text(surf, why, (sx + sw / 2, sy - 14),
                   (240, 200, 200), size=13, center=True, bold=True)

    # ------------- HUD & panels -------------
    def _draw_hud(self, surf):
        w, h = surf.get_size()
        g = self.app.game
        # top bar
        top = pygame.Rect(0, 0, w, 88)
        pygame.draw.rect(surf, C.UI_BG, top)
        pygame.draw.line(surf, C.UI_BORDER, (0, 88), (w, 88), 1)
        # resource tiles
        tiles = [
            ("Caps", g.caps, 99999, C.UI_ACCENT),
            ("Power", int(g.resources.get("power", 0)), g.storage_cap["power"], C.UI_WARN),
            ("Water", int(g.resources.get("water", 0)), g.storage_cap["water"], C.UI_BLUE),
            ("Food",  int(g.resources.get("food", 0)),  g.storage_cap["food"],  (140, 200, 120)),
            ("Materials", int(g.resources.get("materials", 0)), g.storage_cap["materials"], (180, 150, 120)),
            ("Stimpacks", int(g.resources.get("stim", 0)), g.storage_cap["stim"], (220, 100, 100)),
            ("RadAway", int(g.resources.get("radaway", 0)), g.storage_cap["radaway"], (140, 220, 210)),
            ("Pop", len(g.residents), g.housing_cap(), (200, 190, 240)),
        ]
        x = 12
        for name, v, cap, col in tiles:
            r = pygame.Rect(x, 12, 138, 62)
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
            U.text(surf, name, (r.x + 10, r.y + 6), C.UI_TEXT_DIM, size=13)
            U.text(surf, f"{v}", (r.x + 10, r.y + 22), col, size=22, bold=True)
            bar = pygame.Rect(r.x + 10, r.bottom - 12, r.w - 20, 6)
            U.draw_bar(surf, bar, v, cap, col)
            x += 148
        # right side: time and average happiness
        avg_happy = 100.0
        if g.residents:
            avg_happy = sum(r.happiness for r in g.residents.values()) / len(g.residents)
        happy_col = C.UI_GOOD if avg_happy > 60 else C.UI_WARN if avg_happy > 30 else C.UI_BAD
        happy_r = pygame.Rect(w - 160, 12, 148, 62)
        pygame.draw.rect(surf, C.UI_BG2, happy_r, border_radius=4)
        pygame.draw.rect(surf, C.UI_BORDER, happy_r, 1, border_radius=4)
        U.text(surf, "Happiness", (happy_r.x + 10, happy_r.y + 6), C.UI_TEXT_DIM, size=13)
        U.text(surf, f"{int(avg_happy)}%", (happy_r.x + 10, happy_r.y + 22), happy_col, size=22, bold=True)
        U.draw_bar(surf, pygame.Rect(happy_r.x + 10, happy_r.bottom - 12, happy_r.w - 20, 6),
                   avg_happy, 100, happy_col)

        # bottom bar
        bot = pygame.Rect(0, h - 44, w, 44)
        pygame.draw.rect(surf, C.UI_BG, bot)
        pygame.draw.line(surf, C.UI_BORDER, (0, h - 44), (w, h - 44), 1)
        for b in self.buttons_bottom:
            if b.label == "Build":
                b.style = "primary" if self.mode == "build" else "normal"
            if b.label == "Destroy":
                b.style = "danger" if self.mode == "destroy" else "normal"
            b.draw(surf)
        for b in self.buttons_speed:
            if b.label == "Pause":
                b.style = "primary" if g.paused else "ghost"
            elif b.label == f"{int(g.speed)}x" and not g.paused:
                b.style = "primary"
            else:
                b.style = "normal" if b.label != "Pause" else b.style
            b.draw(surf)

        # notifications ticker
        recent = g.notifications[-3:]
        for i, (t, lvl, txt) in enumerate(reversed(recent)):
            age = g.time - t
            if age > 12: break
            alpha = max(0, min(255, int(255 * (1 - age / 12))))
            col = {"good": C.UI_GOOD, "bad": C.UI_BAD,
                   "warn": C.UI_WARN, "info": C.UI_TEXT}.get(lvl, C.UI_TEXT)
            r = U.font(14).render(f"• {txt}", True, col)
            r.set_alpha(alpha)
            surf.blit(r, (16, h - 100 - i * 18))

    # ------------- panels -------------
    def _draw_panel(self, surf):
        if self.panel is None: return
        w, h = surf.get_size()
        rect = pygame.Rect(w - 420, 92, 400, h - 92 - 48)
        if self.panel == "build_picker":
            self._draw_build_picker(surf, rect)
        elif self.panel == "residents":
            self._draw_residents_panel(surf, rect)
        elif self.panel == "inventory":
            self._draw_inventory_panel(surf, rect)
        elif self.panel == "exploration":
            self._draw_exploration_panel(surf, rect)
        elif self.panel == "objectives":
            self._draw_objectives_panel(surf, rect)
        elif self.panel == "menu":
            self._draw_menu_panel(surf, rect)
        # room detail panel (opens on the left when a room is selected)
        if self.selected_room:
            room = self.app.game.rooms.get(self.selected_room)
            if room:
                left = pygame.Rect(16, 92, 320, 260)
                self._draw_room_detail(surf, left, room)
        if self.selected_resident:
            res = self.app.game.residents.get(self.selected_resident)
            if res:
                left = pygame.Rect(16, 360, 320, 320)
                self._draw_resident_detail(surf, left, res)

    def _draw_build_picker(self, surf, rect):
        U.panel(surf, rect, "Construction")
        # width control
        U.text(surf, f"Width: {self.build_width} cells", (rect.x + 12, rect.y + 40))
        self._bp_wm = pygame.Rect(rect.x + 130, rect.y + 38, 28, 20)
        self._bp_wp = pygame.Rect(rect.x + 160, rect.y + 38, 28, 20)
        for r, lbl in [(self._bp_wm, "-"), (self._bp_wp, "+")]:
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=3)
            pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=3)
            U.text(surf, lbl, r.center, C.UI_TEXT, size=15, center=True, bold=True)
        # list rooms
        list_r = pygame.Rect(rect.x + 8, rect.y + 64, rect.w - 16, rect.h - 72)
        self._bp_items = []
        y = list_r.y - self.list_scroll.offset
        clip = surf.get_clip()
        surf.set_clip(list_r)
        for key in D.ROOM_ORDER + ["elevator"]:
            rd = D.ROOMS[key]
            r = pygame.Rect(list_r.x, y, list_r.w - 6, 56)
            hovered = r.collidepoint(pygame.mouse.get_pos())
            selected = key == self.build_key
            bg = C.UI_BG2 if hovered else C.UI_BG
            if selected: bg = (60, 50, 20)
            pygame.draw.rect(surf, bg, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_ACCENT_DIM if selected else C.UI_BORDER, r, 1, border_radius=4)
            spr = G.room_sprite(key, max(1, rd.get("width", 2)), 1, True)
            spr2 = pygame.transform.scale(spr, (50, 50))
            surf.blit(spr2, (r.x + 4, r.y + 3))
            U.text(surf, rd["name"], (r.x + 60, r.y + 6), C.UI_ACCENT, size=15, bold=True)
            U.text(surf, f"{rd['cost']} caps", (r.x + 60, r.y + 24), C.UI_TEXT_DIM, size=13)
            U.text(surf, rd.get("desc", "")[:38], (r.x + 60, r.y + 38), C.UI_TEXT_DIM, size=12)
            self._bp_items.append((r, key))
            y += 60
        self.list_scroll.rect = list_r
        self.list_scroll.content_h = y + int(self.list_scroll.offset) - list_r.y
        surf.set_clip(clip)
        self.list_scroll.draw_scrollbar(surf)

    def _draw_room_detail(self, surf, rect, room):
        U.panel(surf, rect, room.data()["name"] + f" — Lv {room.level}")
        y = rect.y + 40
        rd = room.data()
        U.text(surf, rd.get("desc", ""), (rect.x + 12, y), C.UI_TEXT_DIM, size=13)
        y += 22
        # capacity + workers
        U.text(surf, f"Workers: {len(room.workers)}/{room.capacity()}",
               (rect.x + 12, y), C.UI_TEXT, size=14)
        y += 18
        prod = ", ".join(f"+{v * room.level * room.width_units()} {k}"
                         for k, v in room.produces().items())
        if prod: U.text(surf, "Produces: " + prod, (rect.x + 12, y), C.UI_GOOD, size=13); y += 16
        cons = ", ".join(f"-{v * room.level * room.width_units()} {k}"
                         for k, v in room.consumes().items())
        if cons: U.text(surf, "Consumes: " + cons, (rect.x + 12, y), C.UI_BAD, size=13); y += 16
        # buttons
        by = rect.bottom - 40
        self._rd_upgrade = pygame.Rect(rect.x + 12, by, 100, 28)
        self._rd_merge = pygame.Rect(rect.x + 118, by, 100, 28)
        self._rd_destroy = pygame.Rect(rect.right - 92, by, 80, 28)
        cost = rd.get("upgrade", 0) * room.level * room.width_units()
        for r, lbl, col in [
            (self._rd_upgrade, f"Upgrade {cost}c" if cost > 0 else "—",
                C.UI_ACCENT_DIM if cost == 0 else C.UI_ACCENT),
            (self._rd_merge, "Merge", C.UI_ACCENT),
            (self._rd_destroy, "Destroy", (180, 80, 80)),
        ]:
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, col, r, 1, border_radius=4)
            U.text(surf, lbl, r.center, col, size=13, center=True, bold=True)

    def _draw_resident_detail(self, surf, rect, res: GM.Resident):
        U.panel(surf, rect, res.name)
        # portrait
        port = G.resident_portrait(res.portrait_seed, (res.outfit or {}).get("rarity", 0),
                                   (res.power_armor or {}).get("name"))
        p2 = pygame.transform.scale(port, (96, 96))
        surf.blit(p2, (rect.x + 12, rect.y + 40))
        # header info
        U.text(surf, f"Level {res.level}", (rect.x + 120, rect.y + 42), C.UI_ACCENT, size=16, bold=True)
        U.text(surf, res.activity.title(), (rect.x + 120, rect.y + 62), C.UI_TEXT_DIM, size=13)
        # HP
        hp_r = pygame.Rect(rect.x + 120, rect.y + 82, 180, 12)
        U.draw_bar(surf, hp_r, res.hp, res.max_hp, C.UI_BAD, f"HP {int(res.hp)}/{res.max_hp}", small=True)
        # XP
        xp_r = pygame.Rect(rect.x + 120, rect.y + 96, 180, 10)
        U.draw_bar(surf, xp_r, res.xp, res.xp_needed(), C.UI_ACCENT, f"XP {res.xp}/{res.xp_needed()}", small=True)
        # happiness
        hp2 = pygame.Rect(rect.x + 120, rect.y + 110, 180, 10)
        U.draw_bar(surf, hp2, res.happiness, 100, C.UI_GOOD, f"{int(res.happiness)}% happy", small=True)

        # SPECIAL
        y = rect.y + 148
        for i, k in enumerate(D.STAT_KEYS):
            col = rect.x + 12 + (i % 4) * 74
            row = y + (i // 4) * 20
            U.text(surf, f"{k}: {res.stat_total(k)}", (col, row), C.UI_TEXT, size=13, bold=True)
        y = rect.y + 200
        # equipment
        U.text(surf, "Weapon: " + (res.weapon["name"] if res.weapon else "—"),
               (rect.x + 12, y), C.UI_TEXT, size=13); y += 16
        U.text(surf, "Outfit: " + (res.outfit["name"] if res.outfit else "—"),
               (rect.x + 12, y), C.UI_TEXT, size=13); y += 16
        pa = res.power_armor
        if pa:
            U.text(surf, f"Power Armor: {pa['name']} ({pa['durability']}/{pa['max_durability']})",
                   (rect.x + 12, y), C.UI_ACCENT, size=13, bold=True); y += 16
        else:
            U.text(surf, "Power Armor: —", (rect.x + 12, y), C.UI_TEXT_DIM, size=13); y += 16
        # actions
        by = rect.bottom - 40
        self._rs_stim = pygame.Rect(rect.x + 12, by, 100, 28)
        self._rs_expl = pygame.Rect(rect.x + 118, by, 100, 28)
        self._rs_repair = pygame.Rect(rect.right - 92, by, 80, 28)
        for r, lbl in [(self._rs_stim, "Stimpack"), (self._rs_expl, "Expedition"),
                       (self._rs_repair, "Repair PA")]:
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_ACCENT, r, 1, border_radius=4)
            U.text(surf, lbl, r.center, C.UI_ACCENT, size=12, center=True, bold=True)

    def _draw_residents_panel(self, surf, rect):
        U.panel(surf, rect, "Residents")
        g = self.app.game
        list_r = pygame.Rect(rect.x + 8, rect.y + 40, rect.w - 16, rect.h - 48)
        surf.set_clip(list_r)
        y = list_r.y - self.list_scroll.offset
        self._rp_items = []
        for res in g.residents.values():
            r = pygame.Rect(list_r.x, y, list_r.w - 6, 54)
            sel = res.id == self.selected_resident
            pygame.draw.rect(surf, C.UI_BG2 if sel else C.UI_BG, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_ACCENT if sel else C.UI_BORDER, r, 1, border_radius=4)
            port = G.resident_portrait(res.portrait_seed, (res.outfit or {}).get("rarity", 0),
                                       (res.power_armor or {}).get("name"))
            surf.blit(pygame.transform.scale(port, (48, 48)), (r.x + 4, r.y + 3))
            U.text(surf, res.name, (r.x + 56, r.y + 4), C.UI_ACCENT, size=14, bold=True)
            U.text(surf, f"Lv {res.level} · {res.activity}", (r.x + 56, r.y + 22),
                   C.UI_TEXT_DIM, size=12)
            U.text(surf, f"HP {int(res.hp)}/{res.max_hp} · {int(res.happiness)}% happy",
                   (r.x + 56, r.y + 36), C.UI_TEXT_DIM, size=11)
            self._rp_items.append((r, res.id))
            y += 58
        self.list_scroll.rect = list_r
        self.list_scroll.content_h = y + int(self.list_scroll.offset) - list_r.y
        surf.set_clip(None)
        self.list_scroll.draw_scrollbar(surf)

    def _draw_inventory_panel(self, surf, rect):
        U.panel(surf, rect, f"Inventory ({len(self.app.game.inventory)}) · PA ({len(self.app.game.pa_storage)})")
        g = self.app.game
        # Tabs
        self.inv_tabs = getattr(self, "inv_tabs", U.TabBar((rect.x + 8, rect.y + 36, rect.w - 16, 26),
                                                          ["Items", "Power Armor", "Crafting"]))
        self.inv_tabs.rect.topleft = (rect.x + 8, rect.y + 36)
        self.inv_tabs.rect.width = rect.w - 16
        self.inv_tabs.draw(surf)
        area = pygame.Rect(rect.x + 8, rect.y + 68, rect.w - 16, rect.h - 76)
        surf.set_clip(area)
        self._inv_items = []
        y = area.y - self.list_scroll.offset
        if self.inv_tabs.active == 0:
            for i, it in enumerate(g.inventory):
                r = pygame.Rect(area.x, y, area.w - 6, 46)
                pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
                pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
                if it["kind"] == "weapon":
                    ico = G.weapon_icon(it["name"])
                elif it["kind"] == "outfit":
                    ico = G.outfit_icon(it["name"])
                else:
                    ico = U.font(14).render("*", True, C.UI_TEXT)
                surf.blit(ico, (r.x + 4, r.y + 6))
                U.text(surf, it["name"], (r.x + 44, r.y + 6),
                       [C.UI_TEXT, C.UI_TEXT, C.UI_BLUE, (200, 140, 240), C.UI_ACCENT][min(4, it.get("rarity", 0))],
                       size=15, bold=True)
                U.text(surf, it.get("desc", ""), (r.x + 44, r.y + 24), C.UI_TEXT_DIM, size=12)
                # sell / scrap
                sb = pygame.Rect(r.right - 130, r.y + 8, 60, 28)
                cb = pygame.Rect(r.right - 66, r.y + 8, 56, 28)
                pygame.draw.rect(surf, C.UI_BG, sb, border_radius=3)
                pygame.draw.rect(surf, C.UI_BORDER, sb, 1, border_radius=3)
                U.text(surf, f"Sell {it.get('value',1)//2}", sb.center, C.UI_ACCENT, size=12, center=True)
                pygame.draw.rect(surf, C.UI_BG, cb, border_radius=3)
                pygame.draw.rect(surf, C.UI_BORDER, cb, 1, border_radius=3)
                U.text(surf, "Scrap", cb.center, C.UI_TEXT, size=12, center=True)
                self._inv_items.append((r, sb, cb, i))
                y += 48
        elif self.inv_tabs.active == 1:
            for i, it in enumerate(g.pa_storage):
                r = pygame.Rect(area.x, y, area.w - 6, 66)
                pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
                pygame.draw.rect(surf, C.UI_ACCENT_DIM, r, 1, border_radius=4)
                surf.blit(G.pa_icon(it["name"]), (r.x + 4, r.y + 8))
                U.text(surf, it["name"], (r.x + 60, r.y + 6), C.UI_ACCENT, size=16, bold=True)
                U.text(surf, f"AR {it.get('armor',0)} · DR {it.get('dr',0)}%", (r.x + 60, r.y + 26),
                       C.UI_TEXT, size=12)
                U.text(surf, it.get("desc", "")[:44], (r.x + 60, r.y + 42), C.UI_TEXT_DIM, size=11)
                self._inv_items.append((r, None, None, ("pa", i)))
                y += 68
        else:
            # crafting UI (data-driven)
            U.text(surf, "Weapons", (area.x + 4, y), C.UI_ACCENT, size=15, bold=True); y += 22
            self._craft_btns = []
            for rar in range(5):
                r = pygame.Rect(area.x, y, area.w - 6, 34)
                pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
                pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
                cost = {0: 5, 1: 12, 2: 25, 3: 60, 4: 140}[rar]
                caps = {0: 20, 1: 60, 2: 150, 3: 400, 4: 900}[rar]
                U.text(surf, f"Craft T{rar+1} Weapon", (r.x + 8, r.y + 8), C.UI_TEXT, size=13, bold=True)
                U.text(surf, f"{cost} mat · {caps} caps", (r.x + 8, r.y + 22), C.UI_TEXT_DIM, size=12)
                b = pygame.Rect(r.right - 76, r.y + 4, 68, 26)
                pygame.draw.rect(surf, C.UI_ACCENT, b, border_radius=3)
                U.text(surf, "Craft", b.center, (30, 22, 8), size=13, center=True, bold=True)
                self._craft_btns.append((b, "weapon", rar))
                y += 38
            y += 6
            U.text(surf, "Outfits", (area.x + 4, y), C.UI_ACCENT, size=15, bold=True); y += 22
            for rar in range(5):
                r = pygame.Rect(area.x, y, area.w - 6, 34)
                pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
                pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
                cost = {0: 5, 1: 12, 2: 25, 3: 60, 4: 140}[rar]
                caps = {0: 20, 1: 60, 2: 150, 3: 400, 4: 900}[rar]
                U.text(surf, f"Craft T{rar+1} Outfit", (r.x + 8, r.y + 8), C.UI_TEXT, size=13, bold=True)
                U.text(surf, f"{cost} mat · {caps} caps", (r.x + 8, r.y + 22), C.UI_TEXT_DIM, size=12)
                b = pygame.Rect(r.right - 76, r.y + 4, 68, 26)
                pygame.draw.rect(surf, C.UI_ACCENT, b, border_radius=3)
                U.text(surf, "Craft", b.center, (30, 22, 8), size=13, center=True, bold=True)
                self._craft_btns.append((b, "outfit", rar))
                y += 38
        self.list_scroll.rect = area
        self.list_scroll.content_h = y + int(self.list_scroll.offset) - area.y
        surf.set_clip(None)
        self.list_scroll.draw_scrollbar(surf)

    def _draw_exploration_panel(self, surf, rect):
        U.panel(surf, rect, "Expeditions")
        g = self.app.game
        y = rect.y + 40
        need_cmd = not any(r.key == "command" for r in g.rooms.values())
        if need_cmd:
            U.text(surf, "Build a Command Center to send expeditions.",
                   (rect.x + 12, y), C.UI_BAD, size=13); y += 22
        # Active expeditions
        U.text(surf, "Active", (rect.x + 12, y), C.UI_ACCENT, size=15, bold=True); y += 22
        self._exp_recall = []
        for e in g.expeditions:
            res = g.residents.get(e["resident_id"])
            if not res: continue
            r = pygame.Rect(rect.x + 8, y, rect.w - 16, 58)
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
            U.text(surf, res.name, (r.x + 8, r.y + 4), C.UI_ACCENT, size=14, bold=True)
            U.draw_bar(surf, pygame.Rect(r.x + 8, r.y + 22, r.w - 96, 10),
                       e["elapsed"], e["duration"], C.UI_ACCENT,
                       f"{int(e['elapsed'])}/{int(e['duration'])}s", small=True)
            U.text(surf, f"{e['caps']} caps · {len(e['items'])} items",
                   (r.x + 8, r.y + 36), C.UI_TEXT_DIM, size=12)
            rb = pygame.Rect(r.right - 88, r.y + 8, 80, 24)
            pygame.draw.rect(surf, (110, 42, 42), rb, border_radius=3)
            U.text(surf, "Recall", rb.center, C.UI_TEXT, size=13, center=True)
            self._exp_recall.append((rb, res.id))
            y += 62
        # Available residents to send
        y += 6
        U.text(surf, "Send…", (rect.x + 12, y), C.UI_ACCENT, size=15, bold=True); y += 22
        self._exp_send = []
        area = pygame.Rect(rect.x + 8, y, rect.w - 16, rect.bottom - y - 8)
        surf.set_clip(area)
        yy = area.y - self.list_scroll.offset
        for res in g.residents.values():
            if res.on_expedition: continue
            r = pygame.Rect(area.x, yy, area.w - 6, 46)
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
            U.text(surf, res.name, (r.x + 8, r.y + 4), C.UI_TEXT, size=14, bold=True)
            U.text(surf, f"Lv{res.level} · S{res.stat_total('S')} P{res.stat_total('P')} "
                          f"E{res.stat_total('E')} L{res.stat_total('L')}",
                   (r.x + 8, r.y + 22), C.UI_TEXT_DIM, size=12)
            b = pygame.Rect(r.right - 78, r.y + 8, 70, 28)
            enabled = not need_cmd
            pygame.draw.rect(surf, C.UI_ACCENT if enabled else C.UI_BG, b, border_radius=3)
            U.text(surf, "Send 8m", b.center, (30, 22, 8) if enabled else C.UI_TEXT_DIM,
                   size=12, center=True, bold=True)
            self._exp_send.append((b, res.id, enabled))
            yy += 50
        self.list_scroll.rect = area
        self.list_scroll.content_h = yy + int(self.list_scroll.offset) - area.y
        surf.set_clip(None)
        self.list_scroll.draw_scrollbar(surf)

    def _draw_objectives_panel(self, surf, rect):
        U.panel(surf, rect, "Objectives")
        g = self.app.game
        y = rect.y + 40
        for oid, obj in D.OBJECTIVES:
            r = pygame.Rect(rect.x + 8, y, rect.w - 16, 44)
            done = oid in g.objectives_done
            pygame.draw.rect(surf, (30, 60, 30) if done else C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_GOOD if done else C.UI_BORDER, r, 1, border_radius=4)
            U.text(surf, ("✓ " if done else "· ") + obj["desc"],
                   (r.x + 8, r.y + 6), C.UI_GOOD if done else C.UI_TEXT, size=14, bold=done)
            if not done:
                cur = g.objectives_progress.get(oid, 0)
                U.draw_bar(surf, pygame.Rect(r.x + 8, r.bottom - 12, r.w - 100, 6),
                           cur, obj["n"], C.UI_ACCENT)
                U.text(surf, f"{cur}/{obj['n']}", (r.right - 12, r.y + 6),
                       C.UI_TEXT_DIM, size=12, right=True)
            else:
                U.text(surf, f"+{obj['reward'].get('caps',0)} caps",
                       (r.right - 12, r.y + 6), C.UI_ACCENT, size=12, right=True, bold=True)
            y += 48
            if y > rect.bottom - 40: break

    def _draw_menu_panel(self, surf, rect):
        U.panel(surf, rect, "Menu")
        y = rect.y + 50
        self._menu_btns = []
        for lbl, cb in [
            ("Save Now", self._save),
            ("Settings", lambda: self.app.set_screen(SettingsScreen(self.app, back=lambda: self.app.set_screen(WorldScreen(self.app))))),
            ("Main Menu", lambda: self.app.set_screen(MainMenuScreen(self.app))),
            ("Quit to Desktop", lambda: setattr(self.app, "running", False)),
        ]:
            r = pygame.Rect(rect.x + 12, y, rect.w - 24, 36)
            pygame.draw.rect(surf, C.UI_BG2, r, border_radius=4)
            pygame.draw.rect(surf, C.UI_ACCENT_DIM, r, 1, border_radius=4)
            U.text(surf, lbl, r.center, C.UI_ACCENT, size=15, center=True, bold=True)
            self._menu_btns.append((r, cb))
            y += 44

    # ------------- draw main -------------
    def draw(self, surf):
        self._draw_world(surf)
        self._draw_hud(surf)
        self._draw_panel(surf)

    # ------------- input -------------
    def handle(self, event):
        # camera
        # skip camera when the mouse is over a UI panel
        mx, my = pygame.mouse.get_pos()
        over_ui = my < 88 or my > self.app.screen.get_height() - 44
        if self.panel and event.type in (pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP,
                                         pygame.MOUSEMOTION, pygame.MOUSEWHEEL):
            # scroll list
            if self.list_scroll.handle(event):
                return
        if not over_ui:
            self.cam.handle(event)

        for b in self.buttons_bottom:
            b.handle(event)
        for b in self.buttons_speed:
            b.handle(event)

        # keyboard shortcuts
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_ESCAPE:
                if self.panel: self.panel = None
                elif self.mode != "look": self.mode = "look"; self.build_key = None
                else: self._toggle_panel("menu")
            elif event.key == pygame.K_SPACE:
                self._set_speed(0 if not self.app.game.paused else int(self.app.game.speed))
            elif event.key == pygame.K_1:
                self._set_speed(1)
            elif event.key == pygame.K_2:
                self._set_speed(2)
            elif event.key == pygame.K_3:
                self._set_speed(4)
            elif event.key == pygame.K_b:
                self._toggle_mode("build")
            elif event.key == pygame.K_r:
                self._toggle_panel("residents")
            elif event.key == pygame.K_i:
                self._toggle_panel("inventory")
            elif event.key == pygame.K_o:
                self._toggle_panel("objectives")
            elif event.key == pygame.K_e:
                self._toggle_panel("exploration")
            elif event.key == pygame.K_F5:
                self._save()

        # panel-specific handling
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.panel == "build_picker":
                if getattr(self, "_bp_wm", None) and self._bp_wm.collidepoint(event.pos):
                    self.build_width = max(2, self.build_width - 1)
                elif getattr(self, "_bp_wp", None) and self._bp_wp.collidepoint(event.pos):
                    self.build_width = min(C.ROOM_MAX_MERGE * 2, self.build_width + 1)
                for r, key in getattr(self, "_bp_items", []):
                    if r.collidepoint(event.pos):
                        self.build_key = key
                        if key == "elevator":
                            self.build_width = 1
                        else:
                            self.build_width = max(2, D.ROOMS[key]["width"])
            elif self.panel == "residents":
                for r, rid in getattr(self, "_rp_items", []):
                    if r.collidepoint(event.pos):
                        self.selected_resident = rid
            elif self.panel == "inventory":
                self.inv_tabs.handle(event)
                for entry in getattr(self, "_inv_items", []):
                    r, sb, cb, i = entry
                    if r.collidepoint(event.pos):
                        # equip on selected resident
                        if self.selected_resident:
                            g = self.app.game
                            if isinstance(i, tuple) and i[0] == "pa":
                                g.equip(self.selected_resident, "pa", i[1])
                            else:
                                it = g.inventory[i]
                                if it["kind"] == "weapon":
                                    g.equip(self.selected_resident, "weapon", i)
                                elif it["kind"] == "outfit":
                                    g.equip(self.selected_resident, "outfit", i)
                                elif it["kind"] == "consumable" and it["name"] == "Stimpack":
                                    g.use_stimpack(self.selected_resident)
                                    g.inventory.pop(i)
                    if sb and sb.collidepoint(event.pos):
                        self.app.game.sell_item(i)
                    if cb and cb.collidepoint(event.pos):
                        self.app.game.scrap_item(i)
                for b, kind, rar in getattr(self, "_craft_btns", []):
                    if b.collidepoint(event.pos):
                        self.app.game.craft(kind, rar)
            elif self.panel == "exploration":
                for b, rid in getattr(self, "_exp_recall", []):
                    if b.collidepoint(event.pos):
                        self.app.game.recall_expedition(rid)
                for b, rid, enabled in getattr(self, "_exp_send", []):
                    if enabled and b.collidepoint(event.pos):
                        self.app.game.start_expedition(rid)
            elif self.panel == "menu":
                for r, cb in getattr(self, "_menu_btns", []):
                    if r.collidepoint(event.pos):
                        cb()

            # room detail actions
            if self.selected_room:
                if getattr(self, "_rd_upgrade", None) and self._rd_upgrade.collidepoint(event.pos):
                    self.app.game.upgrade_room(self.selected_room)
                elif getattr(self, "_rd_merge", None) and self._rd_merge.collidepoint(event.pos):
                    self.app.game.try_merge(self.selected_room)
                elif getattr(self, "_rd_destroy", None) and self._rd_destroy.collidepoint(event.pos):
                    self.app.game.destroy_room(self.selected_room)
                    self.selected_room = None
            if self.selected_resident:
                if getattr(self, "_rs_stim", None) and self._rs_stim.collidepoint(event.pos):
                    self.app.game.use_stimpack(self.selected_resident)
                elif getattr(self, "_rs_expl", None) and self._rs_expl.collidepoint(event.pos):
                    self.app.game.start_expedition(self.selected_resident)
                elif getattr(self, "_rs_repair", None) and self._rs_repair.collidepoint(event.pos):
                    self.app.game.repair_pa(self.selected_resident)

        # world click
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and not over_ui:
            # panels can occupy right/left; if click is under a panel skip
            if self.panel:
                w, h = self.app.screen.get_size()
                pr = pygame.Rect(w - 420, 92, 400, h - 92 - 48)
                if pr.collidepoint(event.pos):
                    return
                if self.selected_room:
                    left = pygame.Rect(16, 92, 320, 260)
                    if left.collidepoint(event.pos):
                        return
                if self.selected_resident:
                    left = pygame.Rect(16, 360, 320, 320)
                    if left.collidepoint(event.pos):
                        return
            mx, my = event.pos
            wx, wy = self.cam.screen_to_world(mx, my)
            fl = int(wy // C.CELL_H)
            col_f = wx / C.CELL_W
            col = int(col_f)
            if self.mode == "build" and self.build_key:
                width = 1 if self.build_key == "elevator" else self.build_width
                self.app.game.place_room(self.build_key, fl, col, width)
                return
            if self.mode == "destroy":
                room = self.app.game.find_room(fl, col_f)
                if room and room.key != "elevator":
                    self.app.game.destroy_room(room.id)
                return
            # select
            room = self.app.game.find_room(fl, col_f)
            if room:
                self.selected_room = room.id
                self.selected_resident = None
                # If a resident is selected in the residents panel and click is on a room, assign
            else:
                # pick a nearby resident
                best = None
                best_d = 40
                for res in self.app.game.residents.values():
                    if res.on_expedition: continue
                    rsx, rsy = self.cam.world_to_screen(
                        res.x * C.CELL_W + C.CELL_W / 2,
                        res.floor * C.CELL_H + C.CELL_H - 20)
                    d = math.hypot(rsx - mx, rsy - my)
                    if d < best_d:
                        best_d = d; best = res
                if best:
                    self.selected_resident = best.id
                    self.selected_room = None

        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 3 and not over_ui:
            # right click: assign selected resident to that room
            if self.selected_resident:
                mx, my = event.pos
                wx, wy = self.cam.screen_to_world(mx, my)
                fl = int(wy // C.CELL_H)
                col_f = wx / C.CELL_W
                room = self.app.game.find_room(fl, col_f)
                if room and room.data().get("staff_stat") is not None or (room and "train_stat" in room.data()):
                    self.app.game.assign(self.selected_resident, room.id)


# --------------------- App ---------------------
class App:
    def __init__(self):
        pygame.init()
        A.init()
        A.build_bank()
        self.animations = True
        self.fullscreen = False
        cfg = S.load_settings()
        if cfg:
            A.load_settings(cfg.get("audio", {}))
            self.fullscreen = cfg.get("fullscreen", False)
            self.animations = cfg.get("animations", True)
        flags = pygame.RESIZABLE | pygame.DOUBLEBUF
        if self.fullscreen: flags |= pygame.FULLSCREEN
        self.screen = pygame.display.set_mode((C.DEFAULT_WIDTH, C.DEFAULT_HEIGHT), flags)
        pygame.display.set_caption(C.TITLE)
        try:
            pygame.display.set_icon(G.app_icon(64))
        except Exception:
            pass
        self.clock = pygame.time.Clock()
        self.running = True
        self.game: GM.GameState | None = None
        self.slot: int | None = None
        self.screen_obj = MainMenuScreen(self)
        A.start_music()

    def save_settings(self):
        S.save_settings(dict(audio=A.get_settings(),
                             fullscreen=self.fullscreen,
                             animations=self.animations))

    def set_fullscreen(self, on: bool):
        self.fullscreen = on
        flags = pygame.RESIZABLE | pygame.DOUBLEBUF
        if on: flags |= pygame.FULLSCREEN
        self.screen = pygame.display.set_mode(self.screen.get_size(), flags)

    def set_screen(self, screen):
        self.screen_obj = screen

    def run(self):
        while self.running:
            dt = self.clock.tick(C.FPS) / 1000.0
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    if self.game is not None and self.slot is not None:
                        S.save(self.slot, self.game.to_dict())
                    self.running = False
                elif event.type == pygame.VIDEORESIZE:
                    self.screen = pygame.display.set_mode(
                        (max(C.MIN_WIDTH, event.w), max(C.MIN_HEIGHT, event.h)),
                        pygame.RESIZABLE | pygame.DOUBLEBUF | (pygame.FULLSCREEN if self.fullscreen else 0))
                    if isinstance(self.screen_obj, WorldScreen):
                        self.screen_obj.cam.resize(*self.screen.get_size())
                        self.screen_obj._make_hud()
                self.screen_obj.handle(event)
            if hasattr(self.screen_obj, "update"):
                self.screen_obj.update(dt)
            self.screen_obj.draw(self.screen)
            pygame.display.flip()
        A.stop_music()
        pygame.quit()


def run():
    App().run()


if __name__ == "__main__":
    run()
