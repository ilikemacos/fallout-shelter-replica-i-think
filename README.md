# Haven

An original side-view underground shelter-management game. You dig floors,
build rooms, assign residents by dragging them into place, tap rooms to
collect what they produce, rush production for a risky bonus, keep power /
water / food flowing, defend against raiders who breach the door and push
room to room, send explorers into the wasteland, craft weapons and outfits,
recover Power Armor, raise the next generation, and grow your shelter across
a large multi-floor grid.

It renders through an **OpenGL 4.1 core-profile** pipeline at up to **4K**,
with a bloom / colour-grade / vignette post-processing chain.

Haven is a personal, single-player project. It has no accounts, no ads,
no analytics, no online services, no cloud saves, and no third-party
copyrighted content. All artwork and audio are generated procedurally at
runtime — nothing from any existing franchise is used.

## Running from source

Requires Python 3.11 or newer.

```bash
python3 -m pip install -r requirements.txt
python3 run.py
```

The same command line works on macOS 13+ (Apple Silicon or Intel), Windows
10/11, and Linux.

Set `HAVEN_FORCE_SDL=1` to skip OpenGL and use plain SDL2 presentation.

## Packaging native builds

Both platforms build the app with PyInstaller, which produces a standalone
binary that already contains Python and pygame. Users need no development
tools installed.

### Windows — one-file installer (recommended)

`dist/Windows/haven.ps1` is a self-contained installer: the entire game is
embedded in that single PowerShell script. Copy it to your PC and run:

```powershell
powershell -ExecutionPolicy Bypass -File haven.ps1 -Run
```

It finds Python 3.10+, unpacks the source to `%LOCALAPPDATA%\Haven\src`,
builds `Haven.exe` in an isolated venv, installs it to
`%LOCALAPPDATA%\Haven\bin`, and adds a Start Menu shortcut. No admin rights,
nothing machine-wide.

| Flag | Effect |
| --- | --- |
| *(none)* | install only |
| `-Run` | install, then launch |
| `-Desktop` | also create a desktop shortcut |
| `-SourceOnly` | unpack the source, skip the exe build |
| `-Uninstall` | remove the app, source, venv and shortcuts |
| `-Uninstall -KeepSaves` | same, but keep your save files |

### Windows — manual build (`Haven.exe`, `Haven-Windows.zip`, optional `Haven.msi`)

On a Windows 10/11 x64 machine:

```
py -3 -m pip install -r requirements.txt pyinstaller pillow
py -3 scripts\build_windows.py
```

Outputs land in `dist\Windows`:

* `Haven.exe` — one-file, double-clickable, no runtime required
* `Haven-Windows.zip` — portable, extract and launch
* `Haven.msi` — produced only if the WiX Toolset (`candle`, `light`) is on
  PATH; per-user install with Start Menu shortcut and uninstall entry

`scripts\build_windows.bat` runs the same steps and pip-installs anything
missing.

### macOS — one-file installer (recommended)

`dist/macOS/haven.sh` is a self-contained installer: the entire game is
embedded inside that single shell script. Copy it to your Mac (for
example into `~/Downloads`) and run:

```bash
chmod +x haven.sh
./haven.sh --run
```

It will:

1. Check you are on macOS and find a Python 3.10+ interpreter
2. Unpack the embedded game source into
   `~/Library/Application Support/Haven/src`
3. Create an isolated virtualenv there and install pygame + PyInstaller
4. Generate the app icon and build `Haven.app`
5. Install it to `~/Applications/Haven.app`, clear the quarantine flag,
   and write a portable `Haven-macOS.zip` alongside the installer

No administrator password is required and nothing is installed
system-wide. Other flags:

| Flag | Effect |
| --- | --- |
| *(none)* | install only |
| `--run` | install, then launch |
| `--source-only` | unpack the source, skip the app build |
| `--uninstall` | remove the app, source, and venv |
| `--uninstall --keep-saves` | same, but keep your save files |

The only prerequisite is Python 3.10 or newer. If it is missing the
installer says so and points at python.org / Homebrew rather than
failing obscurely.

To regenerate the installers after changing the game:

```bash
python3 scripts/make_installer.py           # both platforms
python3 scripts/make_installer.py macos     # or just one
```

### macOS — manual build

On macOS 13+ (Apple Silicon recommended):

```
scripts/build_macos.sh
```

Outputs land in `dist/macOS`:

* `Haven.app` — normal Finder-launchable bundle (`app.haven.game`)
* `Haven-macOS.zip` — zipped `.app`
* `Haven.dmg` — produced with `create-dmg` if present, else `hdiutil`

For distribution to other users you may want to code-sign and notarise
the `.app`; that is outside the automated build.

### Continuous builds

PyInstaller cannot cross-compile: `Haven.exe` must be built on Windows and
`Haven.app` on macOS. [`.github/workflows/build.yml`](.github/workflows/build.yml)
does exactly that — it runs the test suite on Linux, Windows and macOS, then
builds each deliverable on its own runner (`windows-latest` and `macos-14`
for Apple Silicon) and uploads them as artifacts.

Each build job then **runs the packaged binary itself** with
`HAVEN_SELFTEST=1`, which boots the game, simulates, opens the panels, saves
and reloads, and exits non-zero on any failure. That is what proves the
bundle works on a machine with no development runtime installed.

You can run the same check locally:

```bash
HAVEN_SELFTEST=1 python3 run.py     # boots, plays, saves, exits 0
python3 tests/test_smoke.py         # 21 headless checks
```

## Graphics

Haven composes each frame into a single high-resolution surface, then streams
it to the GPU through double-buffered pixel buffer objects and presents it
through an OpenGL 4.1 core-profile post-processing chain:

```
scene ──► bright-pass ──► separable gaussian (ping-pong FBOs, 4 passes)
   │                              │
   └──────────► composite ◄───────┘
                    │
   bloom · contrast · saturation · warm/cool grade · vignette
   scanlines · chromatic aberration · Bayer ordered dither
```

**Why 4.1 rather than a higher version:** macOS caps out at OpenGL 4.1 core —
Apple froze its GL implementation there — so 4.1 is the highest version that
is genuinely portable across macOS and Windows. Everything used here is
4.1-clean: core-profile VAOs, FBOs, `#version 410 core` GLSL, and PBO
streaming.

Four quality presets control the chain (`low` disables bloom entirely and
halves the work; `ultra` runs bloom at half resolution with all effects on).
If PyOpenGL is missing or context creation fails, the game falls back to
plain SDL2 presentation automatically and says so on the Settings screen.

Measured on this project's own reference run at 1440p, the CPU-side cost of
building a frame — simulation plus all drawing — is about **8.8 ms**, and
about **11.9 ms** at 4K, leaving headroom inside a 16.7 ms budget for 60 FPS.
The post-processing chain itself is ordinary GPU work (a handful of
fullscreen passes) and is negligible on real hardware; it only becomes a
bottleneck under a software rasteriser. 4K streams roughly 33 MB per frame to
the GPU, so it is best on a machine with fast memory bandwidth — drop to
1440p or lower the quality preset if frames get long.

## Playing

* **WASD / arrow keys** — pan the camera
* **Mouse wheel** — zoom · **right-drag / middle-drag** — pan
* **Drag a resident onto a room** — put them to work there
* **Click the `!` badge above a room** — collect what it has produced
* **Left click** — select a room or resident; place a room when Build is on
* **B** Build · **R** Residents · **I** Inventory · **E** Expeditions ·
  **O** Objectives · **C** Collect All · **L** Lunchboxes
* **Space** — pause · **1 / 2 / 3** — 1× / 2× / 4× speed
* **F5** — save · **Esc** — close panel / open Menu

The first things to try: press **B**, pick *Power Generator*, and place it
next to an existing room on a floor that already has a lift. Then drag a
resident onto it from the world, wait for the `!` badge, and click it to
bank the power. When a room is nearly finished, **Rush** it for an instant
cycle plus bonus caps — but a failed rush starts a fire.

Save files live under:

* Windows — `%APPDATA%\Haven\`
* macOS — `~/Library/Application Support/Haven/`
* Linux — `$XDG_DATA_HOME/Haven/` (or `~/.local/share/Haven/`)

Three save slots, autosave every 60 s, five rolling backups per slot,
graceful recovery on corrupt files.

## What's implemented

Simulation is data-driven — all rooms, weapons, outfits, power armor,
enemies, exploration events, incidents, and objectives live in
[`haven/data.py`](haven/data.py). Add rows and they immediately appear
in-game.

* 20+ room types across production, social, advanced, training, command
* Multi-floor grid, elevators, camera pan/zoom, room selection & merging
* **Tap-to-collect**: rooms bank their output and wait for you, with a
  Collect All button and an optional auto-collect setting
* **Rushing**: force an immediate production cycle for bonus caps, at a
  rising risk of starting a fire or an infestation
* **Raids**: attackers breach the shelter door and advance room to room,
  scaling to how well-levelled and armed your residents actually are
* **Growth**: two content adults sharing Living Quarters start a family;
  children grow up and join the workforce
* **Death and revival**: residents can be lost, and brought back for caps
* **Lunchboxes**: four-card reward crates earned from objectives
* **Caretaker robots**: assemble one at a Workshop and it patrols the
  shelter on its own, banking output so you do not have to
* Continuous resource simulation with storage caps and warnings
* Residents with SPECIAL, XP/levelling, portraits, on-world sprites,
  pathfinding via elevators, activities (idle/walk/work/train/fight)
* Drag-and-drop staffing plus one-click best-fit assignment by SPECIAL
* Full construction, upgrade to level 3, destroy, merge
* Equipment: weapons, outfits, consumables, Power Armor with
  durability and repair; visible on the resident sprite
* Five original Power Armor variants (Heavy Industrial, Scout Rig,
  Guardian Mk II, Experimental X-01, Havenite Vanguard) with unique
  bonuses, rarity, and visual design
* Random incidents (fires, invaders, infestations, equipment failures)
  with real-time combat and defenders auto-dispatched
* Expeditions: send a resident with duration/gear, roll data-driven
  events, return with caps / resources / items / rare Power Armor
* Crafting from materials + caps with room-gated rarity tiers
* Training rooms for each SPECIAL stat
* Objectives with progress tracking and cap rewards
* Save / load with 3 slots, autosave, backups
* Settings for volume, resolution, graphics quality, fullscreen, audio
  toggles, auto-collect and slot reset
* Procedurally synthesized ambient music and sound effects
* Resolution presets from 720p to 4K, with the whole interface scaled from
  the real screen height so it stays crisp and correctly proportioned

## Layout

```
haven/           # game package (all cross-platform)
  main.py        # entry, main loop, screens (menu, world, settings)
  render.py      # OpenGL 4.1 core renderer + SDL fallback
  game.py        # simulation state and tick loop
  data.py        # room/item/enemy/event tables
  assets.py      # procedural pixel-art (rooms, residents, PA, icon)
  audio.py       # procedural music and SFX
  ui.py          # scale-aware widgets, panels, HUD helpers
  save.py        # JSON save/load, slots, backups
  config.py      # constants
run.py           # cross-platform entry
tests/
  test_smoke.py  # 21 headless checks, run on all three platforms in CI
scripts/        # build_windows.py, build_macos.py, batch/shell helpers
requirements.txt
LICENSE
```

## License

MIT — see [LICENSE](LICENSE).
