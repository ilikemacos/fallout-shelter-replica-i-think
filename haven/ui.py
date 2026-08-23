"""UI toolkit and panels for Haven.

Provides Button, tooltip, scroll list, and the various in-game panels.
"""

from __future__ import annotations
import pygame

from . import config as C
from . import data as D


# ---------------- helpers ----------------
_fonts: dict[tuple[int, bool], pygame.font.Font] = {}


def font(size: int = 16, bold: bool = False) -> pygame.font.Font:
    k = (size, bold)
    f = _fonts.get(k)
    if f is None:
        f = pygame.font.SysFont(None, size, bold=bold)
        _fonts[k] = f
    return f


def text(surf, s, pos, color=C.UI_TEXT, size=16, bold=False, center=False, right=False):
    r = font(size, bold).render(str(s), True, color)
    rect = r.get_rect()
    if center:
        rect.center = pos
    elif right:
        rect.topright = pos
    else:
        rect.topleft = pos
    surf.blit(r, rect)
    return rect


def panel(surf, rect, title: str | None = None, bg=None):
    bg = bg or C.UI_BG
    pygame.draw.rect(surf, bg, rect, border_radius=6)
    pygame.draw.rect(surf, C.UI_BORDER, rect, 1, border_radius=6)
    if title:
        text(surf, title, (rect.x + 12, rect.y + 8), C.UI_ACCENT, size=18, bold=True)
        pygame.draw.line(surf, C.UI_BORDER,
                         (rect.x + 8, rect.y + 32),
                         (rect.right - 8, rect.y + 32), 1)


class Button:
    def __init__(self, rect, label, on_click=None, icon=None,
                 style="normal", enabled=True, tooltip: str | None = None):
        self.rect = pygame.Rect(rect)
        self.label = label
        self.on_click = on_click
        self.icon = icon
        self.style = style      # normal | primary | danger | ghost
        self.enabled = enabled
        self.tooltip = tooltip
        self.hover = False
        self._pressed = False

    def draw(self, surf):
        colors = {
            "normal":  (C.UI_BG2, C.UI_TEXT, C.UI_BORDER),
            "primary": (C.UI_ACCENT, (30, 22, 8), C.UI_ACCENT_DIM),
            "danger":  ((110, 42, 42), C.UI_TEXT, (180, 80, 80)),
            "ghost":   ((0, 0, 0, 0), C.UI_TEXT, C.UI_BORDER),
        }
        bg, fg, bd = colors[self.style]
        r = self.rect
        if self.style == "ghost":
            pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
        else:
            b = bg
            if self.hover and self.enabled:
                b = tuple(min(255, c + 22) for c in bg[:3])
            pygame.draw.rect(surf, b, r, border_radius=4)
            pygame.draw.rect(surf, bd, r, 1, border_radius=4)
        if self.icon:
            surf.blit(self.icon, (r.x + 6, r.y + (r.h - self.icon.get_height()) // 2))
            text(surf, self.label,
                 (r.x + 12 + self.icon.get_width(), r.y + r.h // 2), fg,
                 size=16, center=False)
        else:
            text(surf, self.label, r.center, fg, size=16, center=True, bold=(self.style == "primary"))
        if not self.enabled:
            overlay = pygame.Surface(r.size, pygame.SRCALPHA)
            overlay.fill((10, 10, 10, 130))
            surf.blit(overlay, r.topleft)

    def handle(self, event) -> bool:
        if not self.enabled: return False
        if event.type == pygame.MOUSEMOTION:
            self.hover = self.rect.collidepoint(event.pos)
        elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                self._pressed = True
        elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            if self._pressed and self.rect.collidepoint(event.pos):
                self._pressed = False
                if self.on_click:
                    self.on_click()
                return True
            self._pressed = False
        return False


class TabBar:
    def __init__(self, rect, tabs: list[str], active=0, on_change=None):
        self.rect = pygame.Rect(rect)
        self.tabs = tabs
        self.active = active
        self.on_change = on_change

    def draw(self, surf):
        w = self.rect.w // max(1, len(self.tabs))
        for i, t in enumerate(self.tabs):
            r = pygame.Rect(self.rect.x + i * w, self.rect.y, w, self.rect.h)
            active = i == self.active
            pygame.draw.rect(surf, C.UI_BG2 if active else C.UI_BG,
                             r, border_radius=4)
            if active:
                pygame.draw.rect(surf, C.UI_ACCENT, r, 1, border_radius=4)
            else:
                pygame.draw.rect(surf, C.UI_BORDER, r, 1, border_radius=4)
            text(surf, t, r.center,
                 C.UI_ACCENT if active else C.UI_TEXT,
                 size=15, bold=active, center=True)

    def handle(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                w = self.rect.w // max(1, len(self.tabs))
                i = (event.pos[0] - self.rect.x) // w
                if 0 <= i < len(self.tabs) and i != self.active:
                    self.active = int(i)
                    if self.on_change: self.on_change(self.active)


class ScrollList:
    def __init__(self, rect):
        self.rect = pygame.Rect(rect)
        self.offset = 0.0
        self.content_h = 0
        self._drag = False
        self._drag_y = 0
        self._drag_off = 0

    def clamp(self):
        max_off = max(0, self.content_h - self.rect.h)
        self.offset = max(0, min(max_off, self.offset))

    def scroll(self, dy):
        self.offset -= dy * 40
        self.clamp()

    def begin(self, surf):
        return surf.subsurface(self.rect), -self.offset

    def draw_scrollbar(self, surf):
        if self.content_h <= self.rect.h: return
        bar_h = max(20, int(self.rect.h * self.rect.h / self.content_h))
        bar_y = self.rect.y + int((self.rect.h - bar_h) * self.offset / max(1, self.content_h - self.rect.h))
        pygame.draw.rect(surf, C.UI_BORDER,
                         (self.rect.right - 6, self.rect.y, 4, self.rect.h), border_radius=2)
        pygame.draw.rect(surf, C.UI_ACCENT_DIM,
                         (self.rect.right - 6, bar_y, 4, bar_h), border_radius=2)

    def handle(self, event) -> bool:
        if event.type == pygame.MOUSEWHEEL and self.rect.collidepoint(pygame.mouse.get_pos()):
            self.scroll(event.y)
            return True
        return False


def draw_bar(surf, rect, value, cap, color, label: str | None = None, small=False):
    pygame.draw.rect(surf, C.UI_BG2, rect, border_radius=3)
    frac = max(0.0, min(1.0, value / max(1, cap)))
    inner = rect.copy()
    inner.w = int(rect.w * frac)
    pygame.draw.rect(surf, color, inner, border_radius=3)
    pygame.draw.rect(surf, C.UI_BORDER, rect, 1, border_radius=3)
    if label:
        text(surf, label, rect.center, C.UI_TEXT,
             size=12 if small else 14, center=True, bold=True)


def tooltip(surf, screen_pos, lines: list[str]):
    pad = 8
    fnt = font(14)
    w = max(fnt.size(l)[0] for l in lines) + pad * 2
    h = len(lines) * 18 + pad * 2
    x, y = screen_pos
    x = min(surf.get_width() - w - 4, x + 12)
    y = min(surf.get_height() - h - 4, y + 16)
    r = pygame.Rect(x, y, w, h)
    pygame.draw.rect(surf, (0, 0, 0, 240), r, border_radius=4)
    s = pygame.Surface(r.size, pygame.SRCALPHA)
    s.fill((0, 0, 0, 220))
    surf.blit(s, r.topleft)
    pygame.draw.rect(surf, C.UI_ACCENT_DIM, r, 1, border_radius=4)
    for i, ln in enumerate(lines):
        text(surf, ln, (r.x + pad, r.y + pad + i * 18), C.UI_TEXT, size=14)
