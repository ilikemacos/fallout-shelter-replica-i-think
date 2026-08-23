"""Build Haven.app on macOS using PyInstaller.

Prerequisites (one-time, on macOS 13+ Apple Silicon):
    python3 -m pip install --upgrade pip
    python3 -m pip install pygame pyinstaller pillow

From the repository root:
    python3 scripts/build_macos.py

Outputs (in ``dist/macOS``):
    Haven.app                 — universal-ready .app bundle
    Haven-macOS.zip           — zipped copy of the .app
    Haven.dmg                 — optional (only if `create-dmg` or `hdiutil` is available)
"""

from __future__ import annotations
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist" / "macOS"


def _write_icns():
    """Build a Haven.icns from the procedural icon."""
    import pygame
    from PIL import Image
    pygame.init()
    pygame.display.set_mode((1, 1))
    from haven import assets as G
    iconset = ROOT / "build" / "Haven.iconset"
    iconset.mkdir(parents=True, exist_ok=True)
    for size in (16, 32, 64, 128, 256, 512, 1024):
        surf = G.app_icon(size)
        raw = pygame.image.tobytes(surf, "RGBA")
        im = Image.frombytes("RGBA", surf.get_size(), raw)
        im.save(iconset / f"icon_{size}x{size}.png")
        if size <= 512:
            im.save(iconset / f"icon_{size}x{size}@2x.png")
    icns = ROOT / "build" / "Haven.icns"
    subprocess.check_call(["iconutil", "-c", "icns", str(iconset), "-o", str(icns)])
    return icns


def main():
    if sys.platform != "darwin":
        print("This script must be run on macOS.")
        sys.exit(1)
    DIST.mkdir(parents=True, exist_ok=True)
    try:
        icns = _write_icns()
    except Exception as e:
        print(f"Warning: could not build .icns ({e}); building without one.")
        icns = None

    args = [
        sys.executable, "-m", "PyInstaller",
        "--noconfirm", "--windowed",
        "--name", "Haven",
        "--osx-bundle-identifier", "app.haven.game",
        "--distpath", str(DIST),
        "--workpath", str(ROOT / "build"),
        "--specpath", str(ROOT / "build"),
        str(ROOT / "run.py"),
    ]
    if icns:
        args += ["--icon", str(icns)]

    subprocess.check_call(args, cwd=str(ROOT))

    app = DIST / "Haven.app"
    if not app.exists():
        print("Haven.app not produced.")
        sys.exit(1)

    # Zip
    zip_path = DIST / "Haven-macOS.zip"
    shutil.make_archive(str(zip_path.with_suffix("")), "zip",
                        root_dir=str(DIST), base_dir="Haven.app")
    print(f"Wrote {zip_path}")

    # DMG
    dmg = DIST / "Haven.dmg"
    try:
        if shutil.which("create-dmg"):
            subprocess.check_call(["create-dmg", "--overwrite", str(app), str(DIST)])
        else:
            subprocess.check_call([
                "hdiutil", "create", "-volname", "Haven", "-srcfolder", str(app),
                "-ov", "-format", "UDZO", str(dmg),
            ])
        print(f"Wrote {dmg}")
    except Exception as e:
        print(f"Skipping .dmg ({e})")

    print(f"\nDone. Outputs in {DIST}")


if __name__ == "__main__":
    main()
