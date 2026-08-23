"""Procedurally synthesized audio for Haven.

All sounds are generated at runtime — no external audio files, no third-party clips.
"""

from __future__ import annotations
import math
import array
import pygame

_inited = False
_sounds: dict[str, pygame.mixer.Sound] = {}
_music_on = True
_sfx_on = True
_master_vol = 0.7
_music_vol = 0.4
_sfx_vol = 0.9


def init():
    global _inited
    try:
        pygame.mixer.pre_init(44100, -16, 1, 512)
        pygame.mixer.init()
        _inited = True
    except pygame.error:
        _inited = False


def _tone(freq: float, dur: float, wave: str = "sine", vol: float = 0.6, attack=0.01, decay=0.15):
    if not _inited:
        return None
    sr = 44100
    n = int(sr * dur)
    buf = array.array("h", [0] * n)
    for i in range(n):
        t = i / sr
        if wave == "sine":
            v = math.sin(2 * math.pi * freq * t)
        elif wave == "square":
            v = 1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0
        elif wave == "saw":
            v = 2 * (t * freq - math.floor(t * freq + 0.5))
        else:
            v = math.sin(2 * math.pi * freq * t)
        # envelope
        env = 1.0
        if t < attack:
            env = t / attack
        elif dur - t < decay:
            env = max(0.0, (dur - t) / decay)
        buf[i] = int(max(-1, min(1, v * env * vol)) * 32767)
    try:
        return pygame.mixer.Sound(buffer=buf.tobytes())
    except pygame.error:
        return None


def _noise(dur: float, vol: float = 0.5, low: float = 0.0):
    if not _inited:
        return None
    import random
    sr = 44100
    n = int(sr * dur)
    buf = array.array("h", [0] * n)
    last = 0.0
    for i in range(n):
        t = i / sr
        v = random.uniform(-1.0, 1.0)
        v = low * last + (1 - low) * v
        last = v
        env = 1.0
        if t < 0.01: env = t / 0.01
        elif dur - t < 0.1: env = max(0.0, (dur - t) / 0.1)
        buf[i] = int(v * env * vol * 32767)
    try:
        return pygame.mixer.Sound(buffer=buf.tobytes())
    except pygame.error:
        return None


def build_bank():
    if not _inited:
        return
    _sounds["click"]     = _tone(880, 0.05, "square", 0.4, 0.001, 0.04)
    _sounds["hover"]     = _tone(1400, 0.02, "sine", 0.15, 0.001, 0.02)
    _sounds["build"]     = _tone(440, 0.14, "square", 0.5, 0.005, 0.1)
    _sounds["upgrade"]   = _tone(660, 0.16, "square", 0.5, 0.005, 0.12)
    _sounds["cash"]      = _tone(1200, 0.08, "square", 0.4, 0.002, 0.06)
    _sounds["alarm"]     = _tone(880, 0.3, "square", 0.55, 0.005, 0.1)
    _sounds["hurt"]      = _noise(0.15, 0.6, 0.2)
    _sounds["death"]     = _noise(0.4, 0.6, 0.4)
    _sounds["shot"]      = _noise(0.08, 0.7, 0.1)
    _sounds["heal"]      = _tone(720, 0.2, "sine", 0.5, 0.02, 0.15)
    _sounds["notify"]    = _tone(1000, 0.09, "sine", 0.4, 0.01, 0.07)
    _sounds["expedition"] = _tone(520, 0.2, "sine", 0.5, 0.02, 0.16)
    _sounds["fire"]      = _noise(0.7, 0.4, 0.6)


def play(name: str, vol_scale: float = 1.0):
    if not _inited or not _sfx_on:
        return
    s = _sounds.get(name)
    if s:
        s.set_volume(max(0.0, min(1.0, _master_vol * _sfx_vol * vol_scale)))
        try:
            s.play()
        except pygame.error:
            pass


# ---------- Music (procedural ambient loop) ----------
_music_channel = None
_music_sound = None


def _make_ambient(seconds: float = 12.0):
    if not _inited:
        return None
    sr = 22050
    n = int(sr * seconds)
    buf = array.array("h", [0] * n)
    # slow pentatonic pad
    notes = [220, 261.6, 293.66, 329.63, 392.0, 523.25]
    for i in range(n):
        t = i / sr
        v = 0.0
        for j, f in enumerate(notes):
            phase = 2 * math.pi * f * t
            amp = 0.09 * (0.5 + 0.5 * math.sin(2 * math.pi * (0.05 + 0.03 * j) * t))
            v += amp * math.sin(phase)
        # subtle noise texture
        import random
        v += 0.02 * random.uniform(-1, 1)
        buf[i] = int(max(-0.9, min(0.9, v)) * 32767)
    try:
        return pygame.mixer.Sound(buffer=buf.tobytes())
    except pygame.error:
        return None


def start_music():
    global _music_channel, _music_sound
    if not _inited or not _music_on:
        return
    if _music_sound is None:
        _music_sound = _make_ambient()
    if _music_sound is None:
        return
    _music_sound.set_volume(_master_vol * _music_vol)
    _music_channel = _music_sound.play(loops=-1)


def stop_music():
    global _music_channel
    if _music_channel:
        try:
            _music_channel.stop()
        except pygame.error:
            pass
    _music_channel = None


def set_master(v: float):
    global _master_vol
    _master_vol = max(0.0, min(1.0, v))
    if _music_sound:
        _music_sound.set_volume(_master_vol * _music_vol)


def set_music(v: float, enabled: bool = True):
    global _music_vol, _music_on
    _music_vol = max(0.0, min(1.0, v))
    _music_on = enabled
    if _music_sound:
        _music_sound.set_volume(_master_vol * _music_vol)
    if not enabled:
        stop_music()
    elif _music_channel is None:
        start_music()


def set_sfx(v: float, enabled: bool = True):
    global _sfx_vol, _sfx_on
    _sfx_vol = max(0.0, min(1.0, v))
    _sfx_on = enabled


def get_settings():
    return dict(master=_master_vol, music=_music_vol, sfx=_sfx_vol,
                music_on=_music_on, sfx_on=_sfx_on)


def load_settings(d: dict):
    global _master_vol, _music_vol, _sfx_vol, _music_on, _sfx_on
    _master_vol = float(d.get("master", _master_vol))
    _music_vol = float(d.get("music", _music_vol))
    _sfx_vol = float(d.get("sfx", _sfx_vol))
    _music_on = bool(d.get("music_on", _music_on))
    _sfx_on = bool(d.get("sfx_on", _sfx_on))
