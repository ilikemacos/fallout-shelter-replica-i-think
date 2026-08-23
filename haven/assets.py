"""Procedurally generated pixel-art assets for Haven.

Everything is drawn at runtime with pygame so no proprietary art is ever bundled.
"""

from __future__ import annotations
import math
import random
import pygame

from . import config as C

_cache: dict[str, pygame.Surface] = {}


def _s(w: int, h: int) -> pygame.Surface:
    return pygame.Surface((w, h), pygame.SRCALPHA)


def _shade(color, factor):
    return tuple(max(0, min(255, int(c * factor))) for c in color[:3])


def room_sprite(room_key: str, width_cells: int, level: int, powered: bool) -> pygame.Surface:
    """Return the artwork for a room instance. Cached per (key,width,level,powered)."""
    ck = f"room:{room_key}:{width_cells}:{level}:{powered}"
    if ck in _cache:
        return _cache[ck]
    w = C.CELL_W * width_cells
    h = C.CELL_H
    surf = _s(w, h)

    # Wall/floor palettes vary per room and per level
    base_colors = {
        "power":     ((70, 60, 40), (110, 90, 42), (255, 210, 80)),
        "water":     ((30, 50, 70), (52, 96, 128), (110, 190, 240)),
        "diner":     ((80, 40, 40), (150, 76, 60), (255, 176, 90)),
        "farm":      ((36, 60, 30), (80, 130, 60), (180, 230, 110)),
        "living":    ((60, 44, 66), (110, 84, 122), (220, 190, 250)),
        "storage":   ((60, 50, 40), (110, 92, 68), (200, 180, 130)),
        "medbay":    ((40, 60, 60), (80, 130, 130), (200, 250, 240)),
        "science":   ((32, 40, 70), (78, 100, 160), (180, 210, 255)),
        "workshop":  ((60, 50, 32), (120, 96, 58), (240, 180, 90)),
        "armory":    ((50, 40, 40), (110, 90, 88), (220, 200, 200)),
        "gym":       ((44, 60, 44), (90, 130, 90), (200, 240, 200)),
        "range":     ((44, 44, 60), (96, 96, 140), (200, 200, 240)),
        "athletic":  ((60, 52, 34), (130, 108, 68), (240, 220, 160)),
        "lounge":    ((60, 40, 60), (128, 84, 128), (240, 180, 240)),
        "classroom": ((44, 52, 66), (100, 110, 140), (220, 220, 240)),
        "arcade":    ((30, 30, 60), (72, 60, 160), (255, 120, 220)),
        "gamble":    ((60, 40, 30), (130, 90, 60), (255, 200, 90)),
        "radio":     ((36, 44, 36), (86, 108, 86), (200, 240, 180)),
        "command":   ((32, 44, 44), (72, 108, 108), (200, 240, 240)),
        "security":  ((50, 32, 32), (120, 74, 74), (240, 180, 180)),
        "elevator":  ((30, 30, 32), (70, 70, 78), (220, 200, 100)),
    }
    dark, mid, hi = base_colors.get(room_key, ((40, 40, 40), (80, 80, 80), (200, 200, 200)))

    # Level tints
    tint_factor = 1.0 + 0.06 * (level - 1)
    mid = _shade(mid, tint_factor)
    hi = _shade(hi, tint_factor)

    # Floor and walls
    pygame.draw.rect(surf, dark, (0, 0, w, h))
    pygame.draw.rect(surf, mid, (2, 2, w - 4, h - 4))
    # floor tiles
    floor_y = h - 12
    pygame.draw.rect(surf, _shade(dark, 0.9), (0, floor_y, w, 12))
    for x in range(0, w, 16):
        pygame.draw.line(surf, _shade(dark, 0.7), (x, floor_y), (x, h), 1)

    if not powered and room_key != "elevator":
        overlay = _s(w, h)
        overlay.fill((0, 0, 0, 90))
        surf.blit(overlay, (0, 0))

    # Room-specific decor
    if room_key == "elevator":
        _draw_elevator(surf, w, h, hi, mid)
    elif room_key == "power":
        _draw_generator(surf, w, h, hi, mid, level)
    elif room_key == "water":
        _draw_water(surf, w, h, hi, mid, level)
    elif room_key == "diner":
        _draw_diner(surf, w, h, hi, mid, level)
    elif room_key == "farm":
        _draw_farm(surf, w, h, hi, mid, level)
    elif room_key == "living":
        _draw_living(surf, w, h, hi, mid, level)
    elif room_key == "storage":
        _draw_storage(surf, w, h, hi, mid, level)
    elif room_key == "medbay":
        _draw_medbay(surf, w, h, hi, mid, level)
    elif room_key == "science":
        _draw_science(surf, w, h, hi, mid, level)
    elif room_key == "workshop":
        _draw_workshop(surf, w, h, hi, mid, level)
    elif room_key == "armory":
        _draw_armory(surf, w, h, hi, mid, level)
    elif room_key in ("gym", "athletic"):
        _draw_gym(surf, w, h, hi, mid, level)
    elif room_key == "range":
        _draw_range(surf, w, h, hi, mid, level)
    elif room_key == "lounge":
        _draw_lounge(surf, w, h, hi, mid, level)
    elif room_key == "classroom":
        _draw_classroom(surf, w, h, hi, mid, level)
    elif room_key == "arcade":
        _draw_arcade(surf, w, h, hi, mid, level)
    elif room_key == "gamble":
        _draw_gamble(surf, w, h, hi, mid, level)
    elif room_key == "radio":
        _draw_radio(surf, w, h, hi, mid, level)
    elif room_key == "command":
        _draw_command(surf, w, h, hi, mid, level)
    elif room_key == "security":
        _draw_security(surf, w, h, hi, mid, level)

    # Level chevrons in the corner
    for i in range(level):
        pygame.draw.polygon(
            surf, C.UI_ACCENT,
            [(w - 8 - i * 6, 4), (w - 5 - i * 6, 8), (w - 8 - i * 6, 12)],
        )
    # Frame
    pygame.draw.rect(surf, _shade(dark, 0.6), (0, 0, w, h), 1)
    _cache[ck] = surf
    return surf


# ---- individual room decor helpers ----
def _draw_elevator(s, w, h, hi, mid):
    pygame.draw.rect(s, _shade(mid, 0.6), (w // 2 - 12, 6, 24, h - 18))
    pygame.draw.line(s, hi, (w // 2, 6), (w // 2, h - 12), 2)
    pygame.draw.rect(s, hi, (w // 2 - 10, h - 30, 20, 12), 1)


def _draw_generator(s, w, h, hi, mid, lvl):
    for i in range(2):
        cx = 22 + i * (w // 2 + 8)
        pygame.draw.rect(s, _shade(mid, 0.7), (cx, h - 60, 44, 44))
        pygame.draw.circle(s, hi, (cx + 22, h - 40), 10)
        pygame.draw.circle(s, _shade(hi, 0.6), (cx + 22, h - 40), 6)
        pygame.draw.rect(s, hi, (cx + 6, h - 62, 32, 4))
    # sparks
    for k in range(3 + lvl):
        pygame.draw.line(s, (255, 220, 100), (10 + k * 12, 18), (16 + k * 12, 26), 1)


def _draw_water(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, _shade(hi, 0.5), (12, 32, w - 24, h - 56))
    pygame.draw.rect(s, hi, (12, 32, w - 24, 6))
    for i in range(3):
        pygame.draw.line(s, _shade(hi, 0.8),
                         (18 + i * 20, 40 + (i % 2) * 6),
                         (w - 18 - i * 20, 60 + (i % 2) * 6), 2)


def _draw_diner(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, _shade(mid, 0.6), (10, h - 40, w - 20, 12))
    for i in range(3):
        pygame.draw.circle(s, hi, (24 + i * 30, h - 46), 6)
        pygame.draw.rect(s, hi, (18 + i * 30, h - 40, 12, 4))


def _draw_farm(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, (56, 32, 20), (8, h - 28, w - 16, 12))
    for i in range((w - 16) // 12):
        x = 12 + i * 12
        pygame.draw.rect(s, (60, 130, 40), (x, h - 40, 4, 12))
        pygame.draw.circle(s, (150, 200, 80), (x + 2, h - 42), 3)


def _draw_living(s, w, h, hi, mid, lvl):
    for i in range(2):
        x = 10 + i * (w // 2)
        pygame.draw.rect(s, _shade(mid, 0.6), (x, h - 40, w // 2 - 12, 24))
        pygame.draw.rect(s, hi, (x + 4, h - 36, 16, 16))
        pygame.draw.rect(s, _shade(hi, 0.6), (x + 26, h - 36, 16, 16))


def _draw_storage(s, w, h, hi, mid, lvl):
    for i in range(4):
        x = 10 + i * 20
        pygame.draw.rect(s, _shade(mid, 0.5), (x, h - 40, 16, 24))
        pygame.draw.rect(s, hi, (x + 2, h - 34, 12, 3))


def _draw_medbay(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, hi, (w // 2 - 10, 16, 20, 20))
    pygame.draw.rect(s, _shade(hi, 0.5), (w // 2 - 3, 20, 6, 12))
    pygame.draw.rect(s, _shade(hi, 0.5), (w // 2 - 8, 24, 16, 4))
    pygame.draw.rect(s, _shade(mid, 0.6), (12, h - 34, w - 24, 12))


def _draw_science(s, w, h, hi, mid, lvl):
    for i in range(3):
        x = 16 + i * 32
        pygame.draw.rect(s, _shade(mid, 0.4), (x, h - 46, 20, 26))
        pygame.draw.rect(s, hi, (x + 4, h - 42, 12, 8))


def _draw_workshop(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, _shade(mid, 0.5), (10, h - 36, w - 20, 14))
    for i in range(4):
        x = 16 + i * 22
        pygame.draw.line(s, hi, (x, h - 44), (x + 12, h - 36), 2)


def _draw_armory(s, w, h, hi, mid, lvl):
    for i in range(3):
        x = 14 + i * 30
        pygame.draw.rect(s, _shade(mid, 0.3), (x, h - 50, 22, 32))
        pygame.draw.line(s, hi, (x + 4, h - 30), (x + 18, h - 30), 2)


def _draw_gym(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, hi, (16, h - 28, 32, 6))
    pygame.draw.circle(s, hi, (16, h - 25), 6)
    pygame.draw.circle(s, hi, (48, h - 25), 6)


def _draw_range(s, w, h, hi, mid, lvl):
    for i in range(3):
        y = 20 + i * 16
        pygame.draw.circle(s, hi, (w - 30, y), 8, 1)
        pygame.draw.circle(s, hi, (w - 30, y), 4)


def _draw_lounge(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, _shade(mid, 0.5), (12, h - 36, 40, 14))
    pygame.draw.circle(s, hi, (30, h - 42), 6)


def _draw_classroom(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, hi, (10, 18, w - 20, 22))
    for i in range(4):
        x = 20 + i * 26
        pygame.draw.rect(s, _shade(mid, 0.4), (x, h - 36, 14, 8))


def _draw_arcade(s, w, h, hi, mid, lvl):
    for i in range(3):
        x = 16 + i * 30
        pygame.draw.rect(s, _shade(mid, 0.3), (x, h - 44, 22, 26))
        pygame.draw.rect(s, hi, (x + 3, h - 40, 16, 8))


def _draw_gamble(s, w, h, hi, mid, lvl):
    for i in range(3):
        x = 14 + i * 30
        pygame.draw.rect(s, _shade(mid, 0.4), (x, h - 40, 20, 22))
        pygame.draw.circle(s, hi, (x + 10, h - 30), 6)


def _draw_radio(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, _shade(mid, 0.4), (w // 2 - 16, h - 34, 32, 14))
    pygame.draw.line(s, hi, (w // 2, h - 34), (w // 2 - 12, 8), 1)
    pygame.draw.line(s, hi, (w // 2, h - 34), (w // 2 + 12, 8), 1)


def _draw_command(s, w, h, hi, mid, lvl):
    pygame.draw.rect(s, hi, (12, 20, w - 24, 26))
    for i in range(6):
        pygame.draw.line(s, _shade(hi, 0.4), (16 + i * 10, 24), (16 + i * 10, 42), 1)


def _draw_security(s, w, h, hi, mid, lvl):
    pygame.draw.polygon(s, hi, [(w // 2, 14), (w // 2 - 10, 22), (w // 2 - 6, 40),
                                (w // 2 + 6, 40), (w // 2 + 10, 22)])


# ---------- Resident portrait ----------
def resident_portrait(seed: int, outfit_rarity: int = 0, power_armor: str | None = None) -> pygame.Surface:
    ck = f"port:{seed}:{outfit_rarity}:{power_armor}"
    if ck in _cache:
        return _cache[ck]
    rng = random.Random(seed)
    s = _s(48, 48)
    skin = rng.choice([(240, 200, 170), (200, 160, 120), (150, 108, 76),
                       (110, 76, 52), (78, 52, 40), (220, 180, 140)])
    hair = rng.choice([(30, 20, 16), (120, 74, 30), (200, 160, 60),
                       (180, 60, 40), (60, 60, 60), (220, 220, 220)])
    # head
    pygame.draw.rect(s, skin, (14, 14, 20, 22))
    # hair
    pygame.draw.rect(s, hair, (13, 10, 22, 8))
    pygame.draw.rect(s, hair, (13, 14, 4, 8))
    pygame.draw.rect(s, hair, (31, 14, 4, 8))
    # eyes
    pygame.draw.rect(s, (20, 20, 30), (18, 22, 3, 3))
    pygame.draw.rect(s, (20, 20, 30), (27, 22, 3, 3))
    # mouth
    pygame.draw.rect(s, (120, 40, 40), (21, 30, 6, 2))
    # body (jumpsuit)
    body_color = [(70, 100, 150), (120, 100, 60), (60, 130, 90),
                  (140, 60, 60), (140, 130, 120), (60, 60, 90)][outfit_rarity % 6]
    pygame.draw.rect(s, body_color, (10, 36, 28, 12))
    pygame.draw.rect(s, _shade(body_color, 0.7), (10, 45, 28, 3))

    if power_armor:
        # bulky plate over body and head
        pa_color = {
            "Heavy Industrial": (140, 130, 100),
            "Scout Rig":        (110, 130, 140),
            "Guardian Mk II":   (110, 140, 120),
            "Experimental X-01":(160, 130, 190),
            "Havenite Vanguard":(200, 170, 90),
        }.get(power_armor, (150, 150, 150))
        pygame.draw.rect(s, pa_color, (8, 12, 32, 34))
        pygame.draw.rect(s, _shade(pa_color, 0.6), (8, 12, 32, 34), 2)
        pygame.draw.rect(s, (20, 30, 40), (16, 18, 16, 8))    # visor
        pygame.draw.rect(s, (120, 200, 255), (18, 20, 12, 4))
        pygame.draw.rect(s, _shade(pa_color, 1.15), (10, 28, 4, 12))
        pygame.draw.rect(s, _shade(pa_color, 1.15), (34, 28, 4, 12))
    _cache[ck] = s
    return s


def resident_sprite(seed: int, power_armor: str | None, facing: int, step: int, outfit_rarity: int = 0) -> pygame.Surface:
    """Small in-world resident sprite. facing: -1 left, 1 right. step 0/1 anim."""
    ck = f"body:{seed}:{power_armor}:{facing}:{step}:{outfit_rarity}"
    if ck in _cache:
        return _cache[ck]
    s = _s(20, 36)
    rng = random.Random(seed)
    skin = rng.choice([(240, 200, 170), (200, 160, 120), (150, 108, 76),
                       (110, 76, 52), (78, 52, 40), (220, 180, 140)])
    body = [(70, 100, 150), (120, 100, 60), (60, 130, 90),
            (140, 60, 60), (140, 130, 120), (60, 60, 90)][outfit_rarity % 6]
    if power_armor:
        pa_color = {
            "Heavy Industrial": (140, 130, 100),
            "Scout Rig":        (110, 130, 140),
            "Guardian Mk II":   (110, 140, 120),
            "Experimental X-01":(160, 130, 190),
            "Havenite Vanguard":(200, 170, 90),
        }.get(power_armor, (150, 150, 150))
        pygame.draw.rect(s, pa_color, (2, 6, 16, 20))
        pygame.draw.rect(s, _shade(pa_color, 0.6), (2, 6, 16, 20), 1)
        pygame.draw.rect(s, (30, 40, 50), (5, 10, 10, 5))
        pygame.draw.rect(s, (120, 200, 255), (7, 11, 6, 2))
        pygame.draw.rect(s, _shade(pa_color, 0.7), (2, 26, 6 + step, 8))
        pygame.draw.rect(s, _shade(pa_color, 0.7), (12 - step, 26, 6, 8))
    else:
        # head
        pygame.draw.rect(s, skin, (6, 2, 8, 8))
        pygame.draw.rect(s, (30, 20, 16), (6, 2, 8, 3))
        pygame.draw.rect(s, (20, 20, 30), (8, 5, 2, 2))
        pygame.draw.rect(s, (20, 20, 30), (11, 5, 2, 2))
        # body
        pygame.draw.rect(s, body, (5, 10, 10, 14))
        pygame.draw.rect(s, _shade(body, 0.7), (5, 22, 10, 2))
        # arms
        pygame.draw.rect(s, body, (2, 12, 3, 8))
        pygame.draw.rect(s, body, (15, 12, 3, 8))
        # legs walking
        pygame.draw.rect(s, (40, 40, 50), (6, 24, 3, 8 - step))
        pygame.draw.rect(s, (40, 40, 50), (11, 24, 3, 8 + step - 1))
    if facing < 0:
        s = pygame.transform.flip(s, True, False)
    _cache[ck] = s
    return s


# ---------- Item icons ----------
def weapon_icon(name: str) -> pygame.Surface:
    ck = f"wpn:{name}"
    if ck in _cache: return _cache[ck]
    s = _s(32, 32)
    # simple silhouettes by name
    if "Pistol" in name:
        pygame.draw.rect(s, (120, 120, 130), (6, 14, 18, 6))
        pygame.draw.rect(s, (80, 80, 90), (14, 20, 6, 8))
    elif "Rifle" in name or "Cannon" in name:
        pygame.draw.rect(s, (120, 120, 130), (2, 14, 26, 4))
        pygame.draw.rect(s, (80, 60, 40), (18, 12, 8, 10))
    elif "Shotgun" in name:
        pygame.draw.rect(s, (100, 100, 110), (2, 14, 24, 6))
        pygame.draw.rect(s, (70, 50, 30), (20, 12, 8, 10))
    elif "Knife" in name:
        pygame.draw.polygon(s, (200, 200, 210), [(6, 18), (22, 12), (24, 16), (8, 22)])
        pygame.draw.rect(s, (80, 50, 30), (4, 18, 6, 5))
    elif "Bat" in name:
        pygame.draw.polygon(s, (170, 130, 80), [(6, 26), (22, 6), (26, 10), (10, 30)])
    elif "Laser" in name or "Plasma" in name or "Gauss" in name:
        pygame.draw.rect(s, (60, 200, 220), (4, 14, 24, 4))
        pygame.draw.rect(s, (240, 240, 100), (26, 16, 4, 2))
    else:
        pygame.draw.rect(s, (140, 140, 140), (4, 14, 24, 4))
    return s


def outfit_icon(name: str) -> pygame.Surface:
    ck = f"otf:{name}"
    if ck in _cache: return _cache[ck]
    s = _s(32, 32)
    palette = {
        "Jumpsuit": (70, 100, 150),
        "Wasteland Gear": (120, 100, 60),
        "Lab Coat": (220, 220, 220),
        "Merchant Suit": (60, 60, 90),
        "Mercenary Vest": (60, 60, 60),
        "Stealth Armor": (40, 60, 80),
        "Guardian Armor": (110, 120, 100),
        "Marauder Kit": (100, 40, 40),
    }.get(name, (100, 100, 100))
    pygame.draw.polygon(s, palette, [(6, 8), (26, 8), (28, 24), (16, 30), (4, 24)])
    pygame.draw.rect(s, _shade(palette, 0.6), (14, 8, 4, 22))
    return s


def pa_icon(name: str) -> pygame.Surface:
    ck = f"pa:{name}"
    if ck in _cache: return _cache[ck]
    s = _s(48, 48)
    pa_color = {
        "Heavy Industrial": (140, 130, 100),
        "Scout Rig":        (110, 130, 140),
        "Guardian Mk II":   (110, 140, 120),
        "Experimental X-01":(160, 130, 190),
        "Havenite Vanguard":(200, 170, 90),
    }.get(name, (150, 150, 150))
    pygame.draw.rect(s, pa_color, (12, 10, 24, 32))
    pygame.draw.rect(s, _shade(pa_color, 0.5), (12, 10, 24, 32), 2)
    pygame.draw.rect(s, (30, 40, 50), (18, 16, 12, 6))
    pygame.draw.rect(s, (120, 220, 255), (20, 17, 8, 3))
    pygame.draw.rect(s, _shade(pa_color, 1.15), (10, 22, 4, 14))
    pygame.draw.rect(s, _shade(pa_color, 1.15), (34, 22, 4, 14))
    pygame.draw.rect(s, _shade(pa_color, 0.7), (12, 40, 10, 6))
    pygame.draw.rect(s, _shade(pa_color, 0.7), (26, 40, 10, 6))
    _cache[ck] = s
    return s


def app_icon(size: int = 256) -> pygame.Surface:
    """Original Haven application icon (a rising sun over a shelter)."""
    s = _s(size, size)
    # gradient background
    for y in range(size):
        t = y / size
        c = (int(24 + 20 * t), int(36 + 30 * t), int(72 + 30 * t))
        pygame.draw.line(s, c, (0, y), (size, y))
    # sunrise
    sun_c = (255, 210, 110)
    pygame.draw.circle(s, sun_c, (size // 2, size // 2 + 10), size // 4)
    for i in range(12):
        a = i * (math.pi * 2 / 12)
        x1 = size // 2 + math.cos(a) * (size * 0.32)
        y1 = size // 2 + 10 + math.sin(a) * (size * 0.32)
        x2 = size // 2 + math.cos(a) * (size * 0.42)
        y2 = size // 2 + 10 + math.sin(a) * (size * 0.42)
        pygame.draw.line(s, sun_c, (x1, y1), (x2, y2), max(2, size // 80))
    # shelter silhouette
    ground = (36, 30, 26)
    pygame.draw.rect(s, ground, (0, size // 2 + 40, size, size // 2 - 40))
    door = (200, 160, 60)
    pygame.draw.rect(s, door, (size // 2 - 30, size - 90, 60, 70))
    pygame.draw.rect(s, (60, 40, 20), (size // 2 - 30, size - 90, 60, 70), 4)
    pygame.draw.circle(s, (60, 40, 20), (size // 2, size - 55), 8)
    # H letter
    font = pygame.font.SysFont(None, size // 2, bold=True)
    letter = font.render("H", True, (40, 30, 20))
    s.blit(letter, letter.get_rect(center=(size // 2, size - 55)))
    return s
