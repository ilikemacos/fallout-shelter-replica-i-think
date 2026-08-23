"""Save/load for Haven — JSON, multi-slot, autosave, backup."""

from __future__ import annotations
import json
import os
import shutil
import time
from pathlib import Path

from . import config as C

APP_NAME = "Haven"


def user_data_dir() -> Path:
    """Platform-appropriate user data directory."""
    if os.name == "nt":
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData/Roaming")
        return Path(base) / APP_NAME
    if os.uname().sysname == "Darwin":  # macOS
        return Path.home() / "Library/Application Support" / APP_NAME
    xdg = os.environ.get("XDG_DATA_HOME")
    base = Path(xdg) if xdg else Path.home() / ".local/share"
    return base / APP_NAME


def ensure_dirs():
    d = user_data_dir()
    (d / "saves").mkdir(parents=True, exist_ok=True)
    (d / "backups").mkdir(parents=True, exist_ok=True)
    return d


def save_path(slot: int) -> Path:
    return user_data_dir() / "saves" / f"slot_{slot}.json"


def settings_path() -> Path:
    return user_data_dir() / "settings.json"


def list_slots() -> list[dict]:
    ensure_dirs()
    out = []
    for i in range(C.SAVE_SLOTS):
        p = save_path(i)
        if p.exists():
            try:
                with p.open("r", encoding="utf-8") as f:
                    data = json.load(f)
                meta = data.get("meta", {})
                out.append(dict(slot=i, exists=True, meta=meta))
            except Exception:
                out.append(dict(slot=i, exists=True, meta={"broken": True}))
        else:
            out.append(dict(slot=i, exists=False))
    return out


def save(slot: int, data: dict, backup: bool = True):
    ensure_dirs()
    p = save_path(slot)
    if backup and p.exists():
        bak = user_data_dir() / "backups" / f"slot_{slot}_{int(time.time())}.json"
        try:
            shutil.copy2(p, bak)
        except Exception:
            pass
        # keep last 5 backups
        bks = sorted((user_data_dir() / "backups").glob(f"slot_{slot}_*.json"))
        for b in bks[:-5]:
            try:
                b.unlink()
            except Exception:
                pass
    tmp = p.with_suffix(".tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(data, f)
    tmp.replace(p)


def load(slot: int) -> dict | None:
    p = save_path(slot)
    if not p.exists():
        return None
    try:
        with p.open("r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        # try latest backup
        bks = sorted((user_data_dir() / "backups").glob(f"slot_{slot}_*.json"))
        if bks:
            try:
                with bks[-1].open("r", encoding="utf-8") as f:
                    return json.load(f)
            except Exception:
                return None
        return None


def delete(slot: int):
    p = save_path(slot)
    if p.exists():
        try:
            p.unlink()
        except Exception:
            pass


def load_settings() -> dict:
    p = settings_path()
    if p.exists():
        try:
            with p.open("r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}
    return {}


def save_settings(data: dict):
    ensure_dirs()
    p = settings_path()
    tmp = p.with_suffix(".tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    tmp.replace(p)
