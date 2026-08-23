"""Scale-aware UI toolkit for Haven.

Every layout value in the game is authored against a 1280x800 design
surface and multiplied by a scale factor derived from the real resolution,
so the interface keeps its proportions from 720p up to 4K instead of
shrinking into unreadability.
"""

from __future__ import annotations

import pygame

from . import config as C

# ---------------------------------------------------------------- scaling
_scale = 1.0
_fonts: dict[tuple[int, bool], pygame.font.Font] = {}


def set_scale(screen_h: int):
    """Recompute the UI scale for a screen height, clearing cached fonts."""
    global _scale
    new = max(0.75, min(3.5, screen_h / C.DESIGN_H))
    if abs(new - _scale) > 1e-6:
        _scale = new
        _fonts.clear()
    return _scale


def get_scale() -> float:
    return _scale


def s(v) -> int:
    """Scale a design-space length to device pixels."""
    return int(round(v * _scale))


def rect(x, y, w, h) -> pygame.Rect:
    """Build a device-space Rect from design-space coordinates."""
    return pygame.Rect(s(x), s(y), s(w), s(h))


def font(size: int = 16, bold: bool = False) -> pygame.font.Font:
    px = max(8, s(size))
    key = (px, bold)
    f = _fonts.get(key)
    if f is None:
        f = pygame.font.SysFont(None, px, bold=bold)
        _fonts[key] = f
    return f


# ---------------------------------------------------------------- drawing
def text(surf, msg, pos, color=C.UI_TEXT, size=16, bold=False,
         center=False, right=False, shadow=False):
    f = font(size, bold)
    if shadow:
        sh = f.render(str(msg), True, (0, 0, 0))
        r = sh.get_rect()
        off = max(1, s(1))
        if center:
            r.center = (pos[0] + off, pos[1] + off)
        elif right:
            r.topright = (pos[0] + off, pos[1] + off)
        else:
            r.topleft = (pos[0] + off, pos[1] + off)
        surf.blit(sh, r)
    img = f.render(str(msg), True, color)
    r = img.get_rect()
    if center:
        r.center = pos
    elif right:
        r.topright = pos
    else:
        r.topleft = pos
    surf.blit(img, r)
    return r


def trim(msg: str, size: int, max_w: int) -> str:
    """Ellipsize a string so it fits inside max_w device pixels."""
    f = font(size)
    if f.size(msg)[0] <= max_w:
        return msg
    ell = "..."
    lo, hi = 0, len(msg)
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if f.size(msg[:mid] + ell)[0] <= max_w:
            lo = mid
        else:
            hi = mid - 1
    return msg[:lo] + ell


def panel(surf, r, title=None, bg=None, accent=C.UI_ACCENT):
    bg = bg or C.UI_BG
    rad = s(6)
    pygame.draw.rect(surf, bg, r, border_radius=rad)
    pygame.draw.rect(surf, C.UI_BORDER, r, max(1, s(1)), border_radius=rad)
    if title:
        text(surf, title, (r.x + s(12), r.y + s(8)), accent, size=18, bold=True)
        y = r.y + s(32)
        pygame.draw.line(surf, C.UI_BORDER, (r.x + s(8), y), (r.right - s(8), y),
                         max(1, s(1)))
    return r.y + (s(40) if title else s(8))


def draw_bar(surf, r, value, cap, color, label=None, small=False, bg=None):
    rad = s(3)
    pygame.draw.rect(surf, bg or C.UI_BG2, r, border_radius=rad)
    frac = 0.0 if cap <= 0 else max(0.0, min(1.0, value / cap))
    if frac > 0:
        inner = r.copy()
        inner.w = max(s(2), int(r.w * frac))
        pygame.draw.rect(surf, color, inner, border_radius=rad)
    pygame.draw.rect(surf, C.UI_BORDER, r, max(1, s(1)), border_radius=rad)
    if label:
        text(surf, label, r.center, C.UI_TEXT, size=12 if small else 14,
             center=True, bold=True, shadow=True)


def tooltip(surf, pos, lines):
    if not lines:
        return
    pad = s(8)
    lh = s(18)
    f = font(14)
    w = max(f.size(l)[0] for l in lines) + pad * 2
    h = len(lines) * lh + pad * 2
    x = min(surf.get_width() - w - s(4), pos[0] + s(14))
    y = min(surf.get_height() - h - s(4), pos[1] + s(18))
    x, y = max(s(4), x), max(s(4), y)
    r = pygame.Rect(x, y, w, h)
    shade = pygame.Surface(r.size, pygame.SRCALPHA)
    shade.fill((6, 7, 10, 238))
    surf.blit(shade, r.topleft)
    pygame.draw.rect(surf, C.UI_ACCENT_DIM, r, max(1, s(1)), border_radius=s(4))
    for i, line in enumerate(lines):
        text(surf, line, (r.x + pad, r.y + pad + i * lh),
             C.UI_ACCENT if i == 0 else C.UI_TEXT, size=14, bold=(i == 0))


# ---------------------------------------------------------------- widgets
class Button:
    """A button laid out in device pixels with hover/press animation."""

    def __init__(self, r, label, on_click=None, style="normal",
                 enabled=True, tooltip_lines=None, size=16, icon=None):
        self.rect = pygame.Rect(r)
        self.label = label
        self.on_click = on_click
        self.style = style
        self.enabled = enabled
        self.tooltip_lines = tooltip_lines
        self.size = size
        self.icon = icon
        self.hover = False
        self._press = False
        self._anim = 0.0

    STYLES = {
        "normal":  (C.UI_BG2, C.UI_TEXT, C.UI_BORDER),
        "primary": (C.UI_ACCENT, (28, 20, 6), C.UI_ACCENT_DIM),
        "danger":  ((124, 44, 44), C.UI_TEXT, (190, 84, 84)),
        "good":    ((44, 104, 54), C.UI_TEXT, (96, 180, 110)),
        "ghost":   (None, C.UI_TEXT, C.UI_BORDER),
    }

    def update(self, dt):
        target = 1.0 if (self.hover and self.enabled) else 0.0
        self._anim += (target - self._anim) * min(1.0, dt * 14)

    def draw(self, surf):
        bg, fg, bd = self.STYLES.get(self.style, self.STYLES["normal"])
        r = self.rect
        rad = s(4)
        if self._press and self.enabled:
            r = r.inflate(-s(2), -s(2))
        if bg is not None:
            lift = int(26 * self._anim)
            col = tuple(min(255, c + lift) for c in bg)
            pygame.draw.rect(surf, col, r, border_radius=rad)
        pygame.draw.rect(surf, bd, r, max(1, s(1)), border_radius=rad)
        cx = r.centerx
        if self.icon is not None:
            iw = self.icon.get_width()
            surf.blit(self.icon, (r.x + s(6), r.centery - self.icon.get_height() // 2))
            cx += iw // 2
        text(surf, self.label, (cx, r.centery), fg, size=self.size,
             center=True, bold=(self.style in ("primary", "good")))
        if not self.enabled:
            veil = pygame.Surface(r.size, pygame.SRCALPHA)
            veil.fill((10, 10, 12, 140))
            surf.blit(veil, r.topleft)

    def handle(self, event) -> bool:
        if not self.enabled:
            return False
        if event.type == pygame.MOUSEMOTION:
            self.hover = self.rect.collidepoint(event.pos)
        elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                self._press = True
                return True
        elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            was = self._press
            self._press = False
            if was and self.rect.collidepoint(event.pos):
                if self.on_click:
                    self.on_click()
                return True
        return False


class TabBar:
    def __init__(self, r, tabs, active=0, on_change=None):
        self.rect = pygame.Rect(r)
        self.tabs = list(tabs)
        self.active = active
        self.on_change = on_change

    def draw(self, surf):
        if not self.tabs:
            return
        w = self.rect.w // len(self.tabs)
        for i, t in enumerate(self.tabs):
            r = pygame.Rect(self.rect.x + i * w, self.rect.y, w, self.rect.h)
            on = i == self.active
            pygame.draw.rect(surf, C.UI_BG2 if on else C.UI_BG, r, border_radius=s(4))
            pygame.draw.rect(surf, C.UI_ACCENT if on else C.UI_BORDER, r,
                             max(1, s(1)), border_radius=s(4))
            text(surf, t, r.center, C.UI_ACCENT if on else C.UI_TEXT_DIM,
                 size=15, bold=on, center=True)

    def handle(self, event) -> bool:
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos) and self.tabs:
                w = self.rect.w // len(self.tabs)
                i = (event.pos[0] - self.rect.x) // w
                if 0 <= i < len(self.tabs):
                    if i != self.active:
                        self.active = int(i)
                        if self.on_change:
                            self.on_change(self.active)
                    return True
        return False


class ScrollList:
    """A clipped scrolling region with an inertia-free scrollbar."""

    def __init__(self, r=(0, 0, 10, 10)):
        self.rect = pygame.Rect(r)
        self.offset = 0.0
        self.content_h = 0

    def clamp(self):
        self.offset = max(0.0, min(max(0.0, self.content_h - self.rect.h), self.offset))

    def scroll(self, dy):
        self.offset -= dy * s(56)
        self.clamp()

    def draw_scrollbar(self, surf):
        if self.content_h <= self.rect.h:
            return
        track_w = s(4)
        x = self.rect.right - s(6)
        pygame.draw.rect(surf, C.UI_BG2, (x, self.rect.y, track_w, self.rect.h),
                         border_radius=s(2))
        bar_h = max(s(20), int(self.rect.h * self.rect.h / self.content_h))
        span = max(1.0, self.content_h - self.rect.h)
        y = self.rect.y + int((self.rect.h - bar_h) * (self.offset / span))
        pygame.draw.rect(surf, C.UI_ACCENT_DIM, (x, y, track_w, bar_h),
                         border_radius=s(2))

    def handle(self, event) -> bool:
        if event.type == pygame.MOUSEWHEEL:
            if self.rect.collidepoint(pygame.mouse.get_pos()):
                self.scroll(event.y)
                return True
        return False


class Slider:
    def __init__(self, r, value=1.0, on_change=None):
        self.rect = pygame.Rect(r)
        self.value = value
        self.on_change = on_change
        self._drag = False

    def draw(self, surf):
        r = self.rect
        pygame.draw.rect(surf, C.UI_BG2, r, border_radius=s(4))
        fill = r.copy()
        fill.w = int(r.w * max(0.0, min(1.0, self.value)))
        if fill.w > 0:
            pygame.draw.rect(surf, C.UI_ACCENT, fill, border_radius=s(4))
        pygame.draw.rect(surf, C.UI_BORDER, r, max(1, s(1)), border_radius=s(4))
        knob = r.x + fill.w
        pygame.draw.circle(surf, C.UI_TEXT, (knob, r.centery), s(7))
        pygame.draw.circle(surf, C.UI_ACCENT_DIM, (knob, r.centery), s(7), max(1, s(1)))

    def _set_from(self, x):
        v = (x - self.rect.x) / max(1, self.rect.w)
        self.value = max(0.0, min(1.0, v))
        if self.on_change:
            self.on_change(self.value)

    def handle(self, event) -> bool:
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.inflate(s(10), s(10)).collidepoint(event.pos):
                self._drag = True
                self._set_from(event.pos[0])
                return True
        elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            self._drag = False
        elif event.type == pygame.MOUSEMOTION and self._drag:
            self._set_from(event.pos[0])
            return True
        return False


class Toggle:
    def __init__(self, r, value=False, on_change=None):
        self.rect = pygame.Rect(r)
        self.value = value
        self.on_change = on_change

    def draw(self, surf):
        r = self.rect
        pygame.draw.rect(surf, C.UI_ACCENT if self.value else C.UI_BG2, r,
                         border_radius=r.h // 2)
        pygame.draw.rect(surf, C.UI_BORDER, r, max(1, s(1)), border_radius=r.h // 2)
        kx = r.right - r.h // 2 if self.value else r.x + r.h // 2
        pygame.draw.circle(surf, C.UI_TEXT, (kx, r.centery), r.h // 2 - s(3))

    def handle(self, event) -> bool:
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                self.value = not self.value
                if self.on_change:
                    self.on_change(self.value)
                return True
        return False
