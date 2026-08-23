# Haven

An original side-view underground shelter-management game. You dig floors,
build rooms, assign residents, keep power / water / food flowing, defend
against intruders, send explorers into the wasteland, craft weapons and
outfits, recover Power Armor, and grow your shelter across a large multi-
floor grid.

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

## Packaging native builds

Both platforms build the app with PyInstaller, which produces a standalone
binary that already contains Python and pygame. Users need no development
tools installed.

### Windows (`Haven.exe`, `Haven-Windows.zip`, optional `Haven.msi`)

On a Windows 10/11 x64 machine:

```
py -3 -m pip install pygame pyinstaller pillow
py -3 scripts\build_windows.py
```

Outputs land in `dist\Windows`:

* `Haven.exe` — one-file, double-clickable, no runtime required
* `Haven-Windows.zip` — portable, extract and launch
* `Haven.msi` — produced only if the WiX Toolset (`candle`, `light`) is on
  PATH; per-user install with Start Menu shortcut and uninstall entry

`scripts\build_windows.bat` runs the same steps and pip-installs anything
missing.

### macOS (`Haven.app`, `Haven-macOS.zip`, optional `Haven.dmg`)

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

### Note about cross-compilation

PyInstaller cannot cross-compile. To produce a native `Haven.exe` you
must run the Windows script on Windows; to produce `Haven.app` you must
run the macOS script on macOS. This repository ships both build scripts
so a checkout on each host produces the corresponding deliverable.

## Playing

* **WASD / arrow keys** — pan the camera
* **Mouse wheel** — zoom
* **Right-drag / middle-drag** — pan
* **Left click** — select room or resident; place a ghost when Build is on
* **Right click** — assign selected resident to a room
* **B** — toggle Build · **R** — Residents · **I** — Inventory ·
  **E** — Expeditions · **O** — Objectives
* **Space** — pause · **1 / 2 / 3** — 1× / 2× / 4× speed
* **F5** — save · **Esc** — close panel / open Menu

The very first thing to try: hit **B**, choose *Power Generator*, click on
an empty cell adjacent to your existing rooms, then open **Residents** and
right-click a room to send someone there to work it.

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
* Continuous resource simulation with storage caps and warnings
* Residents with SPECIAL, XP/levelling, portraits, on-world sprites,
  pathfinding via elevators, activities (idle/walk/work/train/fight)
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
* Settings for volume, fullscreen, animations, audio toggles, slot reset
* Procedurally synthesized ambient music and sound effects
* HiDPI-friendly rendering (SDL2 backing) and window resizing

## Layout

```
haven/           # game package (all cross-platform)
  main.py        # entry, main loop, screens (menu, world, settings)
  game.py        # simulation state and tick loop
  data.py        # room/item/enemy/event tables
  assets.py      # procedural pixel-art (rooms, residents, PA, icon)
  audio.py       # procedural music and SFX
  ui.py          # widgets, panels, HUD helpers
  save.py        # JSON save/load, slots, backups
  config.py      # constants
run.py           # cross-platform entry
scripts/        # build_windows.py, build_macos.py, batch/shell helpers
requirements.txt
LICENSE
```

## License

MIT — see [LICENSE](LICENSE).
