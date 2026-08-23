"""Procedurally generated artwork for Haven.

Every pixel is drawn at runtime, so nothing copyrighted is ever bundled.
Sprites are authored at the native cell size (192x176) so they stay sharp
at 1440p and 4K, and each surface is cached by the parameters that
produced it.
"""

from __future__ import annotations

import math
import random

import pygame

from . import config as C

_cache: dict[str, pygame.Surface] = {}
_MAX_CACHE = 4096


def _s(w: int, h: int) -> pygame.Surface:
    return pygame.Surface((int(w), int(h)), pygame.SRCALPHA)


def _shade(color, f: float):
    return tuple(max(0, min(255, int(c * f))) for c in color[:3])


def _mix(a, b, t: float):
    return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3))


def _store(key: str, surf: pygame.Surface) -> pygame.Surface:
    if len(_cache) > _MAX_CACHE:
        _cache.clear()
    _cache[key] = surf
    return surf


def clear_cache():
    _cache.clear()


# --------------------------------------------------------------- palettes
ROOM_PALETTE = {
    "power":     ((58, 46, 26), (128, 100, 40), (255, 214, 92)),
    "water":     ((22, 44, 62), (48, 100, 138), (118, 202, 248)),
    "diner":     ((62, 32, 30), (152, 74, 58), (255, 182, 104)),
    "farm":      ((26, 52, 26), (74, 130, 56), (176, 234, 112)),
    "living":    ((48, 34, 58), (108, 80, 126), (222, 190, 252)),
    "storage":   ((50, 42, 32), (114, 94, 66), (208, 186, 136)),
    "medbay":    ((26, 52, 52), (70, 132, 130), (200, 252, 244)),
    "science":   ((26, 34, 62), (72, 98, 158), (176, 212, 255)),
    "workshop":  ((54, 42, 24), (124, 96, 52), (246, 186, 92)),
    "armory":    ((40, 32, 32), (104, 86, 84), (226, 206, 206)),
    "gym":       ((32, 50, 34), (82, 126, 84), (198, 242, 200)),
    "range":     ((34, 34, 50), (90, 90, 136), (198, 198, 244)),
    "athletic":  ((50, 42, 24), (126, 104, 60), (244, 222, 158)),
    "lounge":    ((50, 30, 52), (124, 78, 126), (244, 178, 244)),
    "classroom": ((34, 42, 56), (94, 108, 140), (222, 226, 244)),
    "arcade":    ((24, 22, 52), (68, 56, 156), (255, 116, 224)),
    "gamble":    ((50, 32, 22), (126, 86, 52), (255, 202, 92)),
    "radio":     ((28, 38, 28), (80, 106, 80), (198, 244, 176)),
    "command":   ((24, 40, 40), (66, 104, 104), (196, 244, 244)),
    "security":  ((44, 26, 26), (116, 70, 70), (244, 176, 176)),
    "elevator":  ((22, 22, 26), (64, 64, 74), (222, 202, 104)),
}

SKIN_TONES = [(246, 208, 176), (232, 188, 150), (206, 160, 118), (172, 122, 84),
              (134, 92, 62), (98, 66, 46), (72, 48, 34), (250, 222, 198)]
HAIR_TONES = [(28, 20, 16), (58, 38, 24), (112, 68, 30), (168, 118, 48),
              (214, 176, 84), (176, 52, 36), (86, 86, 92), (226, 226, 226),
              (44, 60, 96), (96, 40, 96)]

PA_COLORS = {
    "Heavy Industrial":  (152, 140, 104),
    "Scout Rig":         (108, 136, 148),
    "Guardian Mk II":    (104, 146, 118),
    "Experimental X-01": (162, 128, 196),
    "Havenite Vanguard": (214, 176, 88),
}


# --------------------------------------------------------------- rooms
def room_sprite(room_key: str, width_cells: int, level: int,
                powered: bool = True, frame: int = 0) -> pygame.Surface:
    """Artwork for one room instance. `frame` drives machinery animation."""
    key = f"room|{room_key}|{width_cells}|{level}|{powered}|{frame}"
    hit = _cache.get(key)
    if hit is not None:
        return hit

    w = C.CELL_W * width_cells
    h = C.CELL_H
    surf = _s(w, h)
    dark, mid, hi = ROOM_PALETTE.get(room_key, ((40, 40, 40), (86, 86, 86), (200, 200, 200)))
    tint = 1.0 + 0.07 * (level - 1)
    mid = _shade(mid, tint)
    hi = _shade(hi, tint)

    # --- shell: back wall with a soft vertical gradient, floor, ceiling ---
    pygame.draw.rect(surf, dark, (0, 0, w, h))
    for y in range(4, h - 14):
        t = (y - 4) / max(1, h - 18)
        pygame.draw.line(surf, _mix(_shade(mid, 1.12), _shade(mid, 0.72), t),
                         (4, y), (w - 4, y))

    # wall panel seams
    for x in range(0, w, 48):
        pygame.draw.line(surf, _shade(dark, 1.25), (x, 4), (x, h - 14), 1)
    # ceiling strip
    pygame.draw.rect(surf, _shade(dark, 0.72), (0, 0, w, 10))
    for x in range(20, w - 12, 72):
        pygame.draw.rect(surf, _shade(hi, 0.85) if powered else _shade(hi, 0.25),
                         (x, 4, 16, 3))

    # floor with tiles and a specular sheen
    floor_y = h - 14
    pygame.draw.rect(surf, _shade(dark, 0.86), (0, floor_y, w, 14))
    for x in range(0, w, 24):
        pygame.draw.line(surf, _shade(dark, 0.62), (x, floor_y), (x, h), 1)
    pygame.draw.line(surf, _shade(mid, 1.1), (0, floor_y), (w, floor_y), 2)

    # --- machinery ---
    drawer = _ROOM_DRAWERS.get(room_key)
    if drawer:
        drawer(surf, w, h, hi, mid, dark, level, frame)

    # --- lighting: warm pool under each ceiling lamp ---
    if powered:
        # A short, dim falloff below each lamp. Anything larger blooms into the
        # empty cells around the room and reads as fog rather than lighting.
        glow = _s(w, h)
        for x in range(20, w - 12, 72):
            cx = x + 8
            for r in range(13, 0, -3):
                a = int(5 * (1 - r / 13))
                pygame.draw.circle(glow, (*hi, a), (cx, 7), r)
        surf.blit(glow, (0, 0), special_flags=pygame.BLEND_RGBA_ADD)
    else:
        veil = _s(w, h)
        veil.fill((2, 4, 16, 132))
        surf.blit(veil, (0, 0))

    # --- level chevrons ---
    for i in range(level):
        x = w - 12 - i * 9
        pygame.draw.polygon(surf, C.UI_ACCENT,
                            [(x, 8), (x + 5, 14), (x, 20), (x - 3, 20), (x + 2, 14), (x - 3, 8)])

    pygame.draw.rect(surf, _shade(dark, 0.5), (0, 0, w, h), 2)
    return _store(key, surf)


def _d_elevator(s, w, h, hi, mid, dark, lvl, fr):
    cx = w // 2
    pygame.draw.rect(s, _shade(dark, 1.3), (cx - 30, 8, 60, h - 22))
    for y in range(14, h - 18, 14):
        pygame.draw.line(s, _shade(mid, 0.8), (cx - 26, y), (cx + 26, y), 2)
    # cables and car
    pygame.draw.line(s, _shade(hi, 0.7), (cx - 18, 8), (cx - 18, h - 16), 2)
    pygame.draw.line(s, _shade(hi, 0.7), (cx + 18, 8), (cx + 18, h - 16), 2)
    car_y = h - 62 + int(math.sin(fr * 0.5) * 5)
    pygame.draw.rect(s, _shade(mid, 1.15), (cx - 24, car_y, 48, 46))
    pygame.draw.rect(s, _shade(dark, 1.4), (cx - 24, car_y, 48, 46), 2)
    pygame.draw.rect(s, hi, (cx - 18, car_y + 6, 36, 5))
    pygame.draw.line(s, _shade(dark, 1.5), (cx, car_y + 14), (cx, car_y + 46), 2)


def _d_power(s, w, h, hi, mid, dark, lvl, fr):
    n = max(1, w // 120)
    for i in range(n):
        bx = 22 + i * 120
        pygame.draw.rect(s, _shade(mid, 0.62), (bx, h - 92, 82, 78))
        pygame.draw.rect(s, _shade(dark, 1.4), (bx, h - 92, 82, 78), 2)
        # spinning turbine
        cx, cy, r = bx + 41, h - 54, 22
        pygame.draw.circle(s, _shade(dark, 1.2), (cx, cy), r)
        for k in range(6):
            a = fr * 0.55 + k * math.pi / 3
            pygame.draw.line(s, hi, (cx, cy),
                             (cx + math.cos(a) * r, cy + math.sin(a) * r), 3)
        pygame.draw.circle(s, _shade(hi, 1.1), (cx, cy), 6)
        pygame.draw.circle(s, _shade(dark, 1.5), (cx, cy), r, 2)
        # output gauge
        lit = (fr // 3) % 4
        for g in range(4):
            col = hi if g <= lit else _shade(hi, 0.25)
            pygame.draw.rect(s, col, (bx + 8 + g * 17, h - 88, 12, 6))
    # arcing sparks
    for k in range(3):
        sx = 24 + ((fr * 7 + k * 53) % max(1, w - 48))
        pygame.draw.line(s, (255, 240, 160), (sx, 18), (sx + 6, 30), 2)


def _d_water(s, w, h, hi, mid, dark, lvl, fr):
    tank = pygame.Rect(18, 34, w - 36, h - 62)
    pygame.draw.rect(s, _shade(dark, 1.25), tank)
    # water body with animated surface
    surface_y = tank.y + 14
    pygame.draw.rect(s, _shade(hi, 0.42),
                     (tank.x + 3, surface_y, tank.w - 6, tank.bottom - surface_y - 3))
    for x in range(tank.x + 4, tank.right - 4, 8):
        yy = surface_y + int(math.sin((x + fr * 4) * 0.12) * 3)
        pygame.draw.line(s, _shade(hi, 1.05), (x, yy), (x + 8, yy), 3)
    # rising bubbles
    for k in range(6):
        bx = tank.x + 20 + (k * 61) % max(1, tank.w - 40)
        by = tank.bottom - 8 - ((fr * 3 + k * 29) % max(1, tank.h - 24))
        pygame.draw.circle(s, _shade(hi, 1.2), (bx, by), 3 - (k % 2))
    pygame.draw.rect(s, _shade(mid, 1.2), tank, 3)
    # pipework
    pygame.draw.rect(s, _shade(mid, 0.8), (tank.x - 12, tank.y + 8, 14, 20))
    pygame.draw.rect(s, _shade(mid, 0.8), (tank.right - 2, tank.y + 8, 14, 20))


def _d_diner(s, w, h, hi, mid, dark, lvl, fr):
    counter = pygame.Rect(14, h - 60, w - 28, 20)
    pygame.draw.rect(s, _shade(mid, 0.66), counter)
    pygame.draw.rect(s, _shade(hi, 0.85), (counter.x, counter.y, counter.w, 5))
    # stove with flickering burners
    for i in range((w - 40) // 76):
        bx = 24 + i * 76
        pygame.draw.rect(s, _shade(dark, 1.35), (bx, h - 92, 54, 32))
        for b in range(2):
            on = ((fr // 2) + i + b) % 3 != 0
            col = (255, 150 + 40 * (b % 2), 60) if on else _shade(dark, 1.6)
            pygame.draw.circle(s, col, (bx + 15 + b * 24, h - 76), 8)
            if on:
                pygame.draw.circle(s, (255, 226, 150), (bx + 15 + b * 24, h - 76), 4)
        # steam
        for k in range(2):
            sy = h - 96 - ((fr * 2 + k * 13) % 26)
            pygame.draw.circle(s, (230, 230, 230, 90), (bx + 27, sy), 4 - k)
    # hanging pans
    for i in range(3):
        px = 40 + i * 70
        pygame.draw.circle(s, _shade(mid, 1.3), (px, 26), 11)
        pygame.draw.circle(s, _shade(dark, 1.4), (px, 26), 11, 2)


def _d_farm(s, w, h, hi, mid, dark, lvl, fr):
    for row in range(2):
        by = h - 40 - row * 42
        pygame.draw.rect(s, _shade(dark, 1.3), (12, by, w - 24, 14))
        pygame.draw.rect(s, (62, 40, 26), (14, by + 2, w - 28, 10))
        for i in range((w - 32) // 22):
            x = 20 + i * 22
            sway = int(math.sin((fr * 0.25) + i * 0.7) * 2)
            pygame.draw.line(s, (72, 142, 48), (x, by + 2), (x + sway, by - 16), 3)
            pygame.draw.circle(s, (168, 216, 96), (x + sway, by - 18), 5)
            pygame.draw.circle(s, (206, 240, 130), (x + sway - 1, by - 20), 2)
        # grow lamps
        pygame.draw.rect(s, _shade(hi, 0.8), (16, by - 34, w - 32, 4))


def _d_living(s, w, h, hi, mid, dark, lvl, fr):
    per = max(1, w // 110)
    for i in range(per):
        bx = 16 + i * 110
        # bunk frame
        pygame.draw.rect(s, _shade(mid, 0.6), (bx, h - 74, 88, 60))
        pygame.draw.rect(s, _shade(dark, 1.4), (bx, h - 74, 88, 60), 2)
        for lvl_i in range(2):
            by = h - 70 + lvl_i * 28
            pygame.draw.rect(s, _shade(hi, 0.9), (bx + 4, by, 80, 8))
            pygame.draw.rect(s, _shade(hi, 1.15), (bx + 6, by - 4, 22, 8))  # pillow
        pygame.draw.line(s, _shade(dark, 1.5), (bx + 44, h - 74), (bx + 44, h - 14), 1)
    # a soft lamp
    pygame.draw.circle(s, _shade(hi, 1.1), (w - 26, 34), 7)


def _d_storage(s, w, h, hi, mid, dark, lvl, fr):
    for row in range(2):
        by = h - 34 - row * 40
        for i in range((w - 24) // 44):
            x = 16 + i * 44
            c = _shade(mid, 0.7 + 0.12 * ((i + row) % 3))
            pygame.draw.rect(s, c, (x, by - 30, 36, 32))
            pygame.draw.rect(s, _shade(dark, 1.4), (x, by - 30, 36, 32), 2)
            pygame.draw.rect(s, _shade(hi, 0.8), (x + 4, by - 22, 28, 5))
            pygame.draw.rect(s, _shade(hi, 0.55), (x + 12, by - 12, 12, 8))
        pygame.draw.rect(s, _shade(dark, 1.2), (12, by + 2, w - 24, 5))


def _d_medbay(s, w, h, hi, mid, dark, lvl, fr):
    # cross sign
    pygame.draw.rect(s, _shade(hi, 1.0), (w // 2 - 18, 18, 36, 30), border_radius=4)
    pygame.draw.rect(s, (210, 60, 60), (w // 2 - 5, 23, 10, 20))
    pygame.draw.rect(s, (210, 60, 60), (w // 2 - 13, 31, 26, 8))
    # bed with a monitor
    bed = pygame.Rect(20, h - 62, 96, 24)
    pygame.draw.rect(s, _shade(mid, 0.72), bed)
    pygame.draw.rect(s, _shade(hi, 1.05), (bed.x + 4, bed.y - 6, 30, 8))
    pygame.draw.rect(s, _shade(dark, 1.4), bed, 2)
    mon = pygame.Rect(w - 92, h - 84, 62, 40)
    pygame.draw.rect(s, _shade(dark, 1.3), mon)
    pygame.draw.rect(s, _shade(mid, 1.2), mon, 2)
    # ECG trace
    pts = []
    for i in range(mon.w - 8):
        t = (i + fr * 3) % 40
        spike = 0
        if t == 12: spike = -10
        elif t == 14: spike = 8
        pts.append((mon.x + 4 + i, mon.centery + spike))
    if len(pts) > 1:
        pygame.draw.lines(s, (120, 250, 160), False, pts, 2)


def _d_science(s, w, h, hi, mid, dark, lvl, fr):
    bench = pygame.Rect(14, h - 46, w - 28, 12)
    pygame.draw.rect(s, _shade(mid, 0.68), bench)
    for i in range((w - 40) // 58):
        bx = 26 + i * 58
        # bubbling flask
        pygame.draw.polygon(s, _shade(hi, 0.5),
                            [(bx + 6, h - 78), (bx + 18, h - 78),
                             (bx + 24, h - 46), (bx, h - 46)])
        lvl_y = h - 58 + int(math.sin(fr * 0.3 + i) * 2)
        pygame.draw.polygon(s, _shade(hi, 0.95),
                            [(bx + 2, lvl_y), (bx + 22, lvl_y),
                             (bx + 24, h - 46), (bx, h - 46)])
        for k in range(2):
            by = lvl_y - ((fr * 2 + k * 11) % 14)
            pygame.draw.circle(s, _shade(hi, 1.3), (bx + 8 + k * 7, by), 2)
        pygame.draw.rect(s, _shade(dark, 1.5), (bx + 6, h - 82, 12, 5))
    # wall terminal
    pygame.draw.rect(s, _shade(dark, 1.35), (w - 74, 20, 58, 34))
    for r in range(3):
        ln = 12 + ((fr + r * 5) % 30)
        pygame.draw.line(s, (140, 220, 255), (w - 68, 28 + r * 9),
                         (w - 68 + ln, 28 + r * 9), 2)


def _d_workshop(s, w, h, hi, mid, dark, lvl, fr):
    bench = pygame.Rect(12, h - 50, w - 24, 16)
    pygame.draw.rect(s, _shade(mid, 0.6), bench)
    pygame.draw.rect(s, _shade(dark, 1.4), bench, 2)
    # press that hammers up and down
    px = w // 2 - 26
    stroke = abs(math.sin(fr * 0.35)) * 14
    pygame.draw.rect(s, _shade(mid, 0.9), (px, 18, 52, 26))
    pygame.draw.rect(s, _shade(dark, 1.4), (px + 16, 44, 20, 20 + stroke))
    pygame.draw.rect(s, _shade(hi, 0.9), (px + 8, 44 + 20 + stroke, 36, 8))
    if stroke > 12:
        for k in range(4):
            pygame.draw.line(s, (255, 216, 120),
                             (px + 26, h - 52), (px + 26 + (k - 2) * 9, h - 62), 2)
    # tool rack
    for i in range(4):
        tx = 22 + i * 30
        pygame.draw.line(s, _shade(hi, 0.9), (tx, 24), (tx, 46), 3)
        pygame.draw.circle(s, _shade(mid, 1.3), (tx, 22), 4)


def _d_armory(s, w, h, hi, mid, dark, lvl, fr):
    for i in range((w - 24) // 62):
        rx = 16 + i * 62
        pygame.draw.rect(s, _shade(dark, 1.3), (rx, h - 84, 48, 70))
        pygame.draw.rect(s, _shade(mid, 1.1), (rx, h - 84, 48, 70), 2)
        # racked rifles
        for k in range(2):
            gx = rx + 12 + k * 20
            pygame.draw.line(s, _shade(hi, 0.85), (gx, h - 76), (gx, h - 30), 4)
            pygame.draw.rect(s, _shade(mid, 0.7), (gx - 4, h - 44, 10, 14))
        # status lamp
        on = (fr // 4 + i) % 2 == 0
        pygame.draw.circle(s, (120, 250, 140) if on else (60, 90, 60),
                           (rx + 40, h - 78), 3)


def _d_gym(s, w, h, hi, mid, dark, lvl, fr):
    # weight rack
    pygame.draw.rect(s, _shade(mid, 0.7), (18, h - 30, w - 36, 8))
    for i in range(3):
        bx = 30 + i * 66
        lift = int(abs(math.sin(fr * 0.3 + i)) * 6)
        pygame.draw.line(s, _shade(hi, 0.95), (bx, h - 46 - lift),
                         (bx + 44, h - 46 - lift), 5)
        for side in (0, 44):
            pygame.draw.circle(s, _shade(dark, 1.5), (bx + side, h - 46 - lift), 10)
            pygame.draw.circle(s, _shade(mid, 1.25), (bx + side, h - 46 - lift), 6)
    # mirror strip
    pygame.draw.rect(s, _shade(hi, 0.35), (16, 20, w - 32, 30))
    pygame.draw.rect(s, _shade(mid, 1.2), (16, 20, w - 32, 30), 2)


def _d_range(s, w, h, hi, mid, dark, lvl, fr):
    for i in range(3):
        tx = w - 54 - i * 58
        bob = int(math.sin(fr * 0.4 + i * 1.3) * 5)
        cy = 44 + bob
        pygame.draw.circle(s, (240, 240, 240), (tx, cy), 18)
        pygame.draw.circle(s, (210, 70, 60), (tx, cy), 12)
        pygame.draw.circle(s, (240, 240, 240), (tx, cy), 6)
        pygame.draw.circle(s, (210, 70, 60), (tx, cy), 2)
        pygame.draw.line(s, _shade(dark, 1.4), (tx, cy - 18), (tx, 8), 2)
    # firing line
    pygame.draw.rect(s, _shade(mid, 0.6), (12, h - 34, 60, 20))
    pygame.draw.line(s, _shade(hi, 0.9), (12, h - 40), (w - 12, h - 40), 1)


def _d_athletic(s, w, h, hi, mid, dark, lvl, fr):
    # treadmills with a scrolling belt
    for i in range(max(1, w // 130)):
        bx = 20 + i * 130
        pygame.draw.rect(s, _shade(dark, 1.3), (bx, h - 44, 96, 26))
        for k in range(6):
            lx = bx + 6 + ((k * 16 + fr * 4) % 84)
            pygame.draw.line(s, _shade(hi, 0.7), (lx, h - 40), (lx, h - 24), 2)
        pygame.draw.rect(s, _shade(mid, 1.1), (bx, h - 44, 96, 26), 2)
        pygame.draw.rect(s, _shade(mid, 0.9), (bx + 84, h - 76, 8, 34))
        pygame.draw.rect(s, _shade(hi, 0.9), (bx + 74, h - 82, 26, 10))


def _d_lounge(s, w, h, hi, mid, dark, lvl, fr):
    sofa = pygame.Rect(18, h - 54, 108, 30)
    pygame.draw.rect(s, _shade(mid, 0.75), sofa, border_radius=6)
    pygame.draw.rect(s, _shade(hi, 0.7), (sofa.x + 6, sofa.y + 4, 40, 12), border_radius=4)
    pygame.draw.rect(s, _shade(hi, 0.7), (sofa.x + 56, sofa.y + 4, 40, 12), border_radius=4)
    # a television with changing picture
    tv = pygame.Rect(w - 96, h - 88, 74, 50)
    pygame.draw.rect(s, _shade(dark, 1.4), tv)
    for r in range(4):
        col = _shade(hi, 0.4 + 0.16 * ((fr + r) % 4))
        pygame.draw.rect(s, col, (tv.x + 5, tv.y + 5 + r * 10, tv.w - 10, 8))
    pygame.draw.rect(s, _shade(mid, 1.2), tv, 3)


def _d_classroom(s, w, h, hi, mid, dark, lvl, fr):
    board = pygame.Rect(14, 18, w - 28, 44)
    pygame.draw.rect(s, (40, 62, 48), board)
    pygame.draw.rect(s, _shade(mid, 1.2), board, 3)
    for r in range(3):
        ln = 20 + ((fr * 2 + r * 17) % max(20, board.w - 40))
        pygame.draw.line(s, (226, 232, 220), (board.x + 10, board.y + 10 + r * 12),
                         (board.x + 10 + ln, board.y + 10 + r * 12), 2)
    for i in range((w - 30) // 56):
        dx = 22 + i * 56
        pygame.draw.rect(s, _shade(mid, 0.7), (dx, h - 40, 40, 8))
        pygame.draw.rect(s, _shade(dark, 1.4), (dx + 4, h - 32, 6, 18))
        pygame.draw.rect(s, _shade(dark, 1.4), (dx + 30, h - 32, 6, 18))


def _d_arcade(s, w, h, hi, mid, dark, lvl, fr):
    for i in range(max(1, (w - 24) // 62)):
        cx = 18 + i * 62
        pygame.draw.rect(s, _shade(dark, 1.35), (cx, h - 96, 46, 82), border_radius=5)
        scr = pygame.Rect(cx + 6, h - 88, 34, 26)
        pygame.draw.rect(s, (12, 10, 30), scr)
        for k in range(3):
            px = scr.x + 4 + ((fr * 3 + k * 11) % (scr.w - 8))
            py = scr.y + 6 + ((k * 9 + fr) % (scr.h - 12))
            pygame.draw.rect(s, _shade(hi, 1.0 - k * 0.2), (px, py, 5, 5))
        pygame.draw.rect(s, _shade(mid, 1.15), (cx, h - 96, 46, 82), 2, border_radius=5)
        pygame.draw.circle(s, (230, 80, 80), (cx + 14, h - 52), 4)
        pygame.draw.circle(s, (80, 160, 230), (cx + 30, h - 52), 4)


def _d_gamble(s, w, h, hi, mid, dark, lvl, fr):
    for i in range(max(1, (w - 24) // 66)):
        mx = 20 + i * 66
        pygame.draw.rect(s, _shade(mid, 0.8), (mx, h - 92, 50, 78), border_radius=5)
        pygame.draw.rect(s, _shade(dark, 1.4), (mx, h - 92, 50, 78), 2, border_radius=5)
        for reel in range(3):
            sym = (fr // (2 + reel) + i) % 3
            col = [(240, 90, 80), (250, 210, 90), (110, 220, 140)][sym]
            pygame.draw.rect(s, (20, 18, 24), (mx + 6 + reel * 13, h - 82, 11, 20))
            pygame.draw.circle(s, col, (mx + 11 + reel * 13, h - 72), 4)
        lit = (fr // 3 + i) % 2 == 0
        pygame.draw.circle(s, (255, 230, 120) if lit else _shade(dark, 1.6),
                           (mx + 25, h - 88), 4)


def _d_radio(s, w, h, hi, mid, dark, lvl, fr):
    desk = pygame.Rect(14, h - 50, w - 90, 16)
    pygame.draw.rect(s, _shade(mid, 0.7), desk)
    pygame.draw.rect(s, _shade(dark, 1.35), (desk.x + 8, h - 74, 60, 24))
    for k in range(4):
        on = (fr // 2 + k) % 4 != 0
        pygame.draw.circle(s, _shade(hi, 1.1) if on else _shade(dark, 1.6),
                           (desk.x + 18 + k * 13, h - 62), 4)
    # dish and broadcast rings
    cx, cy = w - 52, 54
    pygame.draw.line(s, _shade(mid, 1.2), (cx, cy), (cx, h - 34), 4)
    pygame.draw.arc(s, _shade(hi, 1.0), (cx - 26, cy - 26, 52, 52), 0.6, 2.6, 5)
    for k in range(3):
        r = 12 + ((fr * 3 + k * 14) % 42)
        a = max(0, 150 - r * 3)
        ring = _s(w, h)
        pygame.draw.circle(ring, (*hi, a), (cx, cy - 6), r, 2)
        s.blit(ring, (0, 0))


def _d_command(s, w, h, hi, mid, dark, lvl, fr):
    board = pygame.Rect(14, 16, w - 28, 52)
    pygame.draw.rect(s, (14, 26, 30), board)
    pygame.draw.rect(s, _shade(mid, 1.2), board, 3)
    # scrolling map grid + blips
    for x in range(board.x + 6, board.right - 6, 18):
        pygame.draw.line(s, _shade(hi, 0.28), (x, board.y + 5), (x, board.bottom - 5), 1)
    for y in range(board.y + 8, board.bottom - 4, 14):
        pygame.draw.line(s, _shade(hi, 0.28), (board.x + 5, y), (board.right - 5, y), 1)
    for k in range(3):
        bx = board.x + 20 + ((fr * 2 + k * 47) % max(1, board.w - 40))
        by = board.y + 14 + (k * 13) % max(1, board.h - 24)
        pygame.draw.circle(s, (140, 250, 200), (bx, by), 3)
    # consoles
    for i in range(max(1, (w - 30) // 84)):
        cx = 22 + i * 84
        pygame.draw.rect(s, _shade(mid, 0.72), (cx, h - 54, 62, 20))
        pygame.draw.rect(s, _shade(dark, 1.4), (cx + 6, h - 76, 50, 22))
        for r in range(2):
            ln = 8 + ((fr + r * 7 + i * 3) % 34)
            pygame.draw.line(s, _shade(hi, 0.9), (cx + 10, h - 70 + r * 8),
                             (cx + 10 + ln, h - 70 + r * 8), 2)


def _d_security(s, w, h, hi, mid, dark, lvl, fr):
    # shield emblem
    cx = w // 2
    pygame.draw.polygon(s, _shade(hi, 0.9),
                        [(cx, 16), (cx - 22, 28), (cx - 16, 58), (cx, 68),
                         (cx + 16, 58), (cx + 22, 28)])
    pygame.draw.polygon(s, _shade(dark, 1.4),
                        [(cx, 16), (cx - 22, 28), (cx - 16, 58), (cx, 68),
                         (cx + 16, 58), (cx + 22, 28)], 3)
    # lockers and a rotating alarm light
    for i in range(max(1, (w - 40) // 70)):
        lx = 20 + i * 70
        pygame.draw.rect(s, _shade(mid, 0.7), (lx, h - 78, 44, 64))
        pygame.draw.rect(s, _shade(dark, 1.4), (lx, h - 78, 44, 64), 2)
        pygame.draw.circle(s, _shade(hi, 1.0), (lx + 34, h - 46), 3)
    sweep = (fr * 12) % 360
    beam = _s(w, h)
    pygame.draw.circle(beam, (255, 90, 70, 200), (w - 30, 26), 8)
    for a in range(-18, 18, 3):
        rad = math.radians(sweep + a)
        pygame.draw.line(beam, (255, 90, 70, 40), (w - 30, 26),
                         (w - 30 + math.cos(rad) * 60, 26 + math.sin(rad) * 60), 3)
    s.blit(beam, (0, 0))


_ROOM_DRAWERS = {
    "elevator": _d_elevator, "power": _d_power, "water": _d_water,
    "diner": _d_diner, "farm": _d_farm, "living": _d_living,
    "storage": _d_storage, "medbay": _d_medbay, "science": _d_science,
    "workshop": _d_workshop, "armory": _d_armory, "gym": _d_gym,
    "range": _d_range, "athletic": _d_athletic, "lounge": _d_lounge,
    "classroom": _d_classroom, "arcade": _d_arcade, "gamble": _d_gamble,
    "radio": _d_radio, "command": _d_command, "security": _d_security,
}


# --------------------------------------------------------------- people
def _person_colors(seed: int):
    rng = random.Random(seed)
    return rng.choice(SKIN_TONES), rng.choice(HAIR_TONES), rng


OUTFIT_COLORS = [(62, 96, 148), (122, 100, 58), (226, 226, 226), (54, 54, 82),
                 (58, 58, 58), (40, 62, 84), (104, 116, 96), (104, 42, 42)]


def resident_portrait(seed: int, outfit_rarity: int = 0, power_armor=None,
                      age: str = "adult", gender: str = "f",
                      dead: bool = False) -> pygame.Surface:
    key = f"port|{seed}|{outfit_rarity}|{power_armor}|{age}|{gender}|{dead}"
    hit = _cache.get(key)
    if hit is not None:
        return hit

    size = 96
    s = _s(size, size)
    skin, hair, rng = _person_colors(seed)
    body = OUTFIT_COLORS[outfit_rarity % len(OUTFIT_COLORS)]

    # background vignette plate
    for r in range(size // 2, 0, -2):
        a = int(90 * (1 - r / (size / 2)))
        pygame.draw.circle(s, (40, 44, 60, a), (size // 2, size // 2), r)

    child = age == "child"
    head_r = 20 if not child else 17
    cx, cy = size // 2, 40 if not child else 44

    # shoulders / torso
    tw = 54 if not child else 40
    pygame.draw.rect(s, body, (cx - tw // 2, cy + head_r - 2, tw, size - (cy + head_r) + 2),
                     border_radius=8)
    pygame.draw.rect(s, _shade(body, 0.75),
                     (cx - tw // 2, size - 14, tw, 14), border_radius=6)
    # collar
    pygame.draw.polygon(s, _shade(body, 1.2),
                        [(cx - 12, cy + head_r - 2), (cx, cy + head_r + 10),
                         (cx + 12, cy + head_r - 2)])

    # head
    pygame.draw.circle(s, skin, (cx, cy), head_r)
    pygame.draw.circle(s, _shade(skin, 0.82), (cx, cy), head_r, 2)
    # hair
    if gender == "f":
        pygame.draw.circle(s, hair, (cx, cy - 4), head_r)
        pygame.draw.rect(s, hair, (cx - head_r, cy - 4, head_r * 2, head_r + 6))
        pygame.draw.circle(s, skin, (cx, cy + 2), head_r - 3)
        pygame.draw.rect(s, skin, (cx - head_r + 5, cy - 2, (head_r - 5) * 2, head_r))
    else:
        pygame.draw.circle(s, hair, (cx, cy - 5), head_r - 1)
        pygame.draw.rect(s, hair, (cx - head_r + 1, cy - 16, (head_r - 1) * 2, 12))
    # face
    eye_y = cy + (1 if not child else 2)
    for ex in (-7, 7):
        pygame.draw.ellipse(s, (250, 250, 252), (cx + ex - 4, eye_y - 4, 9, 8))
        pygame.draw.circle(s, (38, 42, 58), (cx + ex, eye_y), 3)
        pygame.draw.circle(s, (255, 255, 255), (cx + ex + 1, eye_y - 1), 1)
    pygame.draw.arc(s, _shade(skin, 0.6), (cx - 9, eye_y + 4, 18, 12), 3.5, 5.9, 2)
    pygame.draw.line(s, _shade(skin, 0.7), (cx, eye_y + 1), (cx, eye_y + 6), 2)

    if power_armor:
        pa = PA_COLORS.get(power_armor, (150, 150, 150))
        # helmet and pauldrons over everything
        pygame.draw.rect(s, pa, (cx - 30, cy - head_r - 6, 60, head_r * 2 + 12),
                         border_radius=10)
        pygame.draw.rect(s, _shade(pa, 0.62), (cx - 30, cy - head_r - 6, 60, head_r * 2 + 12),
                         3, border_radius=10)
        visor = pygame.Rect(cx - 20, cy - 8, 40, 16)
        pygame.draw.rect(s, (16, 24, 34), visor, border_radius=5)
        pygame.draw.rect(s, (120, 214, 255), visor.inflate(-8, -8), border_radius=3)
        pygame.draw.circle(s, (200, 245, 255), (visor.x + 10, visor.centery - 1), 2)
        pygame.draw.rect(s, _shade(pa, 1.18), (cx - 40, cy + head_r, 18, 26), border_radius=6)
        pygame.draw.rect(s, _shade(pa, 1.18), (cx + 22, cy + head_r, 18, 26), border_radius=6)
        pygame.draw.circle(s, (255, 210, 120), (cx, cy + head_r + 16), 4)

    if dead:
        veil = _s(size, size)
        veil.fill((30, 30, 40, 150))
        s.blit(veil, (0, 0))
        pygame.draw.line(s, (210, 70, 70), (18, 18), (size - 18, size - 18), 4)
        pygame.draw.line(s, (210, 70, 70), (size - 18, 18), (18, size - 18), 4)
    return _store(key, s)


def resident_sprite(seed: int, power_armor=None, facing: int = 1, step: int = 0,
                    outfit_rarity: int = 0, age: str = "adult",
                    activity: str = "idle", pregnant: bool = False,
                    dead: bool = False, is_robot: bool = False) -> pygame.Surface:
    key = (f"body|{seed}|{power_armor}|{facing}|{step}|{outfit_rarity}|{age}|"
           f"{activity}|{pregnant}|{dead}|{is_robot}")
    hit = _cache.get(key)
    if hit is not None:
        return hit

    W, H = 40, 72
    s = _s(W, H)

    if is_robot:
        s = _robot_sprite(step)
        if facing < 0:
            s = pygame.transform.flip(s, True, False)
        return _store(key, s)

    skin, hair, rng = _person_colors(seed)
    body = OUTFIT_COLORS[outfit_rarity % len(OUTFIT_COLORS)]
    child = age == "child"
    scale = 0.68 if child else 1.0

    cx = W // 2
    foot = H - 4
    leg_h = int(20 * scale)
    torso_h = int(26 * scale)
    head_r = int(9 * scale)

    if dead:
        # lying down
        pygame.draw.rect(s, body, (4, H - 18, 32, 12), border_radius=5)
        pygame.draw.circle(s, skin, (32, H - 20), head_r)
        pygame.draw.circle(s, hair, (33, H - 23), head_r - 2)
        pygame.draw.line(s, (200, 70, 70), (10, H - 26), (18, H - 34), 2)
        pygame.draw.line(s, (200, 70, 70), (18, H - 26), (10, H - 34), 2)
        return _store(key, s)

    swing = (1 if step else -1)
    if activity in ("walk", "emergency"):
        swing *= 4
    elif activity == "fight":
        swing *= 2
    else:
        swing = 0

    if power_armor:
        pa = PA_COLORS.get(power_armor, (150, 150, 150))
        # legs
        pygame.draw.rect(s, _shade(pa, 0.72), (cx - 13, foot - 24 + swing // 2, 12, 24),
                         border_radius=3)
        pygame.draw.rect(s, _shade(pa, 0.72), (cx + 1, foot - 24 - swing // 2, 12, 24),
                         border_radius=3)
        # torso
        torso = pygame.Rect(cx - 16, foot - 50, 32, 28)
        pygame.draw.rect(s, pa, torso, border_radius=7)
        pygame.draw.rect(s, _shade(pa, 0.6), torso, 2, border_radius=7)
        pygame.draw.line(s, _shade(pa, 1.3), (cx, torso.y + 4), (cx, torso.bottom - 4), 2)
        # pauldrons
        pygame.draw.rect(s, _shade(pa, 1.2), (cx - 22, foot - 50, 10, 16), border_radius=4)
        pygame.draw.rect(s, _shade(pa, 1.2), (cx + 12, foot - 50, 10, 16), border_radius=4)
        # helmet
        head = pygame.Rect(cx - 12, foot - 68, 24, 20)
        pygame.draw.rect(s, pa, head, border_radius=6)
        pygame.draw.rect(s, _shade(pa, 0.6), head, 2, border_radius=6)
        pygame.draw.rect(s, (16, 24, 34), (cx - 8, foot - 62, 16, 8), border_radius=2)
        pygame.draw.rect(s, (120, 214, 255), (cx - 6, foot - 61, 12, 4))
        # exhaust glow
        pygame.draw.circle(s, (255, 190, 110), (cx - 15, foot - 30), 3)
        pygame.draw.circle(s, (255, 190, 110), (cx + 15, foot - 30), 3)
    else:
        # legs
        pygame.draw.rect(s, (46, 48, 62),
                         (cx - 8, foot - leg_h + swing, 7, leg_h), border_radius=2)
        pygame.draw.rect(s, (46, 48, 62),
                         (cx + 1, foot - leg_h - swing, 7, leg_h), border_radius=2)
        pygame.draw.rect(s, (30, 30, 38), (cx - 9, foot - 3 + swing, 9, 4), border_radius=2)
        pygame.draw.rect(s, (30, 30, 38), (cx, foot - 3 - swing, 9, 4), border_radius=2)
        # torso
        ty = foot - leg_h - torso_h
        torso = pygame.Rect(cx - int(9 * scale), ty, int(18 * scale), torso_h)
        if pregnant:
            pygame.draw.ellipse(s, body, torso.inflate(int(8 * scale), 2))
        pygame.draw.rect(s, body, torso, border_radius=4)
        pygame.draw.rect(s, _shade(body, 0.72), (torso.x, torso.bottom - 4, torso.w, 4))
        pygame.draw.line(s, _shade(body, 1.25), (cx, ty + 3), (cx, torso.bottom - 3), 1)
        # arms
        pygame.draw.rect(s, body, (torso.x - int(5 * scale), ty + 3,
                                   int(5 * scale), int(16 * scale)), border_radius=2)
        pygame.draw.rect(s, body, (torso.right, ty + 3,
                                   int(5 * scale), int(16 * scale)), border_radius=2)
        pygame.draw.circle(s, skin, (torso.x - int(3 * scale), ty + int(19 * scale)),
                           int(3 * scale))
        pygame.draw.circle(s, skin, (torso.right + int(2 * scale), ty + int(19 * scale)),
                           int(3 * scale))
        # head
        hy = ty - head_r + 1
        pygame.draw.circle(s, skin, (cx, hy), head_r)
        pygame.draw.circle(s, _shade(skin, 0.8), (cx, hy), head_r, 1)
        pygame.draw.circle(s, hair, (cx, hy - 3), head_r - 1)
        pygame.draw.rect(s, hair, (cx - head_r + 1, hy - head_r, (head_r - 1) * 2, 6))
        pygame.draw.circle(s, (36, 40, 56), (cx + int(3 * scale), hy + 1), max(1, int(1.6 * scale)))
        pygame.draw.circle(s, (36, 40, 56), (cx - int(3 * scale), hy + 1), max(1, int(1.6 * scale)))

    if activity == "fight":
        pygame.draw.line(s, (255, 226, 130), (cx + 14, foot - 40), (cx + 22, foot - 44), 3)
    if facing < 0:
        s = pygame.transform.flip(s, True, False)
    return _store(key, s)


def _robot_sprite(step: int) -> pygame.Surface:
    s = _s(40, 72)
    bob = -2 if step else 0
    cx, cy = 20, 34 + bob
    # hover thruster flame
    pygame.draw.polygon(s, (255, 180, 90), [(cx - 6, cy + 20), (cx + 6, cy + 20), (cx, cy + 32)])
    pygame.draw.polygon(s, (255, 240, 180), [(cx - 3, cy + 20), (cx + 3, cy + 20), (cx, cy + 27)])
    # chassis
    pygame.draw.circle(s, (150, 146, 128), (cx, cy), 15)
    pygame.draw.circle(s, (96, 92, 78), (cx, cy), 15, 3)
    pygame.draw.circle(s, (60, 66, 74), (cx, cy - 3), 8)
    pygame.draw.circle(s, (140, 220, 255), (cx, cy - 3), 5)
    pygame.draw.circle(s, (250, 250, 255), (cx - 2, cy - 5), 2)
    # arms
    pygame.draw.line(s, (110, 106, 92), (cx - 15, cy), (cx - 24, cy + 6), 3)
    pygame.draw.line(s, (110, 106, 92), (cx + 15, cy), (cx + 24, cy + 6), 3)
    pygame.draw.circle(s, (170, 166, 148), (cx - 25, cy + 7), 3)
    pygame.draw.circle(s, (170, 166, 148), (cx + 25, cy + 7), 3)
    return s


# --------------------------------------------------------------- items
def weapon_icon(name: str, size: int = 48) -> pygame.Surface:
    key = f"wpn|{name}|{size}"
    hit = _cache.get(key)
    if hit is not None:
        return hit
    s = _s(size, size)
    k = size / 48.0
    steel, dark_steel, wood = (168, 172, 182), (92, 96, 108), (122, 82, 44)
    energy = (90, 226, 236)

    def R(x, y, w, h, c):
        pygame.draw.rect(s, c, (x * k, y * k, w * k, h * k), border_radius=int(2 * k))

    if "Gauss" in name or "Plasma" in name or "Laser" in name:
        R(4, 21, 34, 7, dark_steel)
        R(8, 18, 18, 5, steel)
        R(30, 19, 12, 10, energy)
        pygame.draw.circle(s, (230, 255, 255), (int(40 * k), int(24 * k)), int(3 * k))
        R(16, 28, 7, 11, dark_steel)
        for i in range(3):
            R(10 + i * 5, 15, 3, 4, energy)
    elif "Rifle" in name or "Cannon" in name:
        R(3, 22, 38, 5, dark_steel)
        R(24, 18, 14, 11, wood)
        R(14, 27, 7, 12, wood)
        R(10, 19, 12, 4, steel)
        pygame.draw.circle(s, steel, (int(30 * k), int(17 * k)), int(3 * k))
    elif "Shotgun" in name:
        R(3, 21, 36, 8, dark_steel)
        R(26, 18, 16, 13, wood)
        R(16, 29, 7, 11, wood)
        pygame.draw.line(s, steel, (4 * k, 23 * k), (24 * k, 23 * k), max(1, int(2 * k)))
    elif "Pistol" in name:
        R(10, 20, 24, 7, dark_steel)
        R(20, 26, 8, 14, wood)
        R(12, 17, 8, 4, steel)
    elif "Knife" in name:
        pygame.draw.polygon(s, (216, 220, 230),
                            [(10 * k, 30 * k), (34 * k, 14 * k), (38 * k, 20 * k), (14 * k, 36 * k)])
        R(6, 30, 10, 7, wood)
    elif "Bat" in name:
        pygame.draw.polygon(s, wood,
                            [(8 * k, 40 * k), (30 * k, 10 * k), (38 * k, 16 * k), (16 * k, 44 * k)])
        R(6, 38, 9, 7, (70, 48, 30))
    else:  # fists
        pygame.draw.circle(s, (216, 176, 148), (int(24 * k), int(24 * k)), int(11 * k))
        pygame.draw.circle(s, (170, 130, 100), (int(24 * k), int(24 * k)), int(11 * k), max(1, int(2 * k)))
    return _store(key, s)


def outfit_icon(name: str, size: int = 48) -> pygame.Surface:
    key = f"otf|{name}|{size}"
    hit = _cache.get(key)
    if hit is not None:
        return hit
    s = _s(size, size)
    k = size / 48.0
    col = {
        "Jumpsuit": (62, 96, 148), "Wasteland Gear": (122, 100, 58),
        "Lab Coat": (232, 232, 236), "Merchant Suit": (54, 54, 82),
        "Mercenary Vest": (58, 58, 58), "Stealth Armor": (40, 62, 84),
        "Guardian Armor": (104, 116, 96), "Marauder Kit": (104, 42, 42),
    }.get(name, (110, 110, 110))
    pygame.draw.polygon(s, col, [
        (16 * k, 8 * k), (32 * k, 8 * k), (40 * k, 16 * k), (36 * k, 22 * k),
        (34 * k, 42 * k), (14 * k, 42 * k), (12 * k, 22 * k), (8 * k, 16 * k)])
    pygame.draw.polygon(s, _shade(col, 0.62), [
        (16 * k, 8 * k), (24 * k, 16 * k), (32 * k, 8 * k)])
    pygame.draw.line(s, _shade(col, 1.3), (24 * k, 16 * k), (24 * k, 42 * k), max(1, int(2 * k)))
    pygame.draw.rect(s, _shade(col, 0.7), (14 * k, 30 * k, 20 * k, 4 * k))
    return _store(key, s)


def pa_icon(name: str, size: int = 64) -> pygame.Surface:
    key = f"pa|{name}|{size}"
    hit = _cache.get(key)
    if hit is not None:
        return hit
    s = _s(size, size)
    k = size / 64.0
    c = PA_COLORS.get(name, (150, 150, 150))

    def R(x, y, w, h, col, rad=3):
        pygame.draw.rect(s, col, (x * k, y * k, w * k, h * k), border_radius=int(rad * k))

    R(18, 6, 28, 20, c, 6)                     # helmet
    R(20, 12, 24, 9, (16, 24, 34), 2)          # visor recess
    R(22, 14, 20, 5, (120, 214, 255), 1)
    R(16, 26, 32, 24, c, 5)                    # torso
    pygame.draw.line(s, _shade(c, 1.3), (32 * k, 28 * k), (32 * k, 48 * k), max(1, int(2 * k)))
    R(6, 26, 12, 20, _shade(c, 1.18), 4)       # pauldrons
    R(46, 26, 12, 20, _shade(c, 1.18), 4)
    R(18, 50, 12, 12, _shade(c, 0.72), 3)      # legs
    R(34, 50, 12, 12, _shade(c, 0.72), 3)
    pygame.draw.circle(s, (255, 206, 120), (int(32 * k), int(38 * k)), int(4 * k))
    pygame.draw.rect(s, _shade(c, 0.55), (16 * k, 26 * k, 32 * k, 24 * k),
                     max(1, int(2 * k)), border_radius=int(5 * k))
    return _store(key, s)


def lunchbox_icon(size: int = 64) -> pygame.Surface:
    key = f"lunch|{size}"
    hit = _cache.get(key)
    if hit is not None:
        return hit
    s = _s(size, size)
    k = size / 64.0
    body = pygame.Rect(8 * k, 18 * k, 48 * k, 34 * k)
    pygame.draw.rect(s, (206, 74, 62), body, border_radius=int(5 * k))
    pygame.draw.rect(s, (240, 200, 90), (8 * k, 30 * k, 48 * k, 8 * k))
    pygame.draw.rect(s, (120, 40, 34), body, max(1, int(3 * k)), border_radius=int(5 * k))
    pygame.draw.rect(s, (170, 172, 180), (24 * k, 10 * k, 16 * k, 9 * k),
                     border_radius=int(3 * k))
    pygame.draw.rect(s, (120, 122, 130), (24 * k, 10 * k, 16 * k, 9 * k),
                     max(1, int(2 * k)), border_radius=int(3 * k))
    pygame.draw.rect(s, (240, 226, 180), (28 * k, 32 * k, 8 * k, 12 * k),
                     border_radius=int(2 * k))
    return _store(key, s)


def caps_icon(size: int = 32) -> pygame.Surface:
    key = f"caps|{size}"
    hit = _cache.get(key)
    if hit is not None:
        return hit
    s = _s(size, size)
    r = size // 2 - 2
    pygame.draw.circle(s, (196, 168, 96), (size // 2, size // 2), r)
    pygame.draw.circle(s, (128, 104, 52), (size // 2, size // 2), r, max(1, size // 16))
    for i in range(12):
        a = i * math.pi / 6
        pygame.draw.circle(s, (150, 124, 66),
                           (int(size // 2 + math.cos(a) * r), int(size // 2 + math.sin(a) * r)),
                           max(1, size // 20))
    pygame.draw.circle(s, (232, 208, 140), (size // 2 - r // 3, size // 2 - r // 3), r // 3)
    return _store(key, s)


# --------------------------------------------------------------- icon
def app_icon(size: int = 256) -> pygame.Surface:
    """The Haven application icon: a shelter door under a rising sun."""
    s = _s(size, size)
    k = size / 256.0

    # rounded plate with a sky gradient
    plate = pygame.Rect(0, 0, size, size)
    for y in range(size):
        t = y / size
        pygame.draw.line(s, _mix((26, 34, 68), (74, 52, 96), t), (0, y), (size, y))

    # sun
    sun = (255, 206, 108)
    cy = int(150 * k)
    pygame.draw.circle(s, (255, 232, 168), (size // 2, cy), int(62 * k))
    pygame.draw.circle(s, sun, (size // 2, cy), int(52 * k))
    for i in range(16):
        a = i * math.pi / 8
        r1, r2 = 70 * k, 92 * k
        pygame.draw.line(s, (255, 220, 140),
                         (size // 2 + math.cos(a) * r1, cy + math.sin(a) * r1),
                         (size // 2 + math.cos(a) * r2, cy + math.sin(a) * r2),
                         max(2, int(5 * k)))

    # ground
    pygame.draw.rect(s, (52, 40, 32), (0, int(168 * k), size, size - int(168 * k)))
    pygame.draw.rect(s, (38, 29, 24), (0, int(182 * k), size, size - int(182 * k)))

    # vault door: a cog set in a frame
    ccx, ccy, cr = size // 2, int(196 * k), int(52 * k)
    pygame.draw.rect(s, (74, 62, 48),
                     (ccx - int(70 * k), ccy - int(58 * k), int(140 * k), int(116 * k)),
                     border_radius=int(10 * k))
    for i in range(12):
        a = i * math.pi / 6
        pygame.draw.circle(s, (168, 142, 78),
                           (int(ccx + math.cos(a) * cr), int(ccy + math.sin(a) * cr)),
                           int(11 * k))
    pygame.draw.circle(s, (212, 178, 96), (ccx, ccy), cr)
    pygame.draw.circle(s, (124, 100, 48), (ccx, ccy), cr, max(2, int(6 * k)))
    pygame.draw.circle(s, (86, 70, 40), (ccx, ccy), int(20 * k))

    # the H
    f = pygame.font.SysFont(None, int(58 * k), bold=True)
    letter = f.render("H", True, (240, 224, 170))
    s.blit(letter, letter.get_rect(center=(ccx, ccy)))
    return s
