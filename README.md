# Haven: Deep Shelter

A polished, original 3D underground shelter-management game — a shelter
built as a real, explorable multi-floor 3D facility, not a 2D board with a
camera on it. Gritty, retro-futuristic, industrial-underground atmosphere;
detailed 3D presentation and dynamic lighting. **Apple Silicon macOS only**
(M1 and newer). All artwork, geometry and audio are original or generated
at build/runtime — nothing is copied from any existing game.

## Play in a browser

`web/index.html` is the whole game as a **single self-contained HTML file** —
no install, no build, no server. Open it in any browser with WebGL2 (current
Safari, Chrome or Firefox) and play. It saves to `localStorage`, so closing
the tab keeps your shelter.

The overview is presented as an overseer's terminal: a Vault-Tec HUD with
segmented power/water/food meters that turn amber and red as stocks run
short, floating name plates over every room showing its level pips and
staffing, a vault happiness readout, a dweller roster listing everyone and
their post, a build tray along the bottom, and a room inspector on the right.

Two ways to play it: a three-quarter overview for building and managing, and
a **first-person mode** — press **Tab** (or *Walk in*) to drop inside the
shelter and walk its rooms with mouse-look, WASD, sprint and elevators
between floors.

It is a real port, not a stripped-down demo: the same procedural-texture
shading model (concrete, brick, rust, painted and brushed metal, tile, wood,
fabric — all generated in the fragment shader, zero texture files), the same
ACES filmic tonemap and colour grade, the same ray-traced shadows traced
per-pixel against a box soup of rooms, and the same shelter economy
(power/water/food/materials, staffing, morale, breakdowns, newcomers).

The shading is procedural and so entirely fragment-bound, which makes pixels
the lever that matters: the page renders at a capped device pixel ratio and
scales resolution adaptively to hold ~60fps, and the quality button cycles
Fast / Balanced / Maximum (ray-traced shadows off, light, low, with matching
detail and resolution budgets). The fps readout shows the scale in use.

Its functional tests run in headless Chromium — `tools/run_web_tests.sh`
drives the real input handlers and simulation and checks 28 behaviours
(build validation, click-to-build, production, save/load round-trip,
first-person collision and elevator travel).

## Installing the native macOS build

The installer is a single self-contained **HTML page** in `dist/` — its
filename is randomised on every build, so it will be something like
`yulakx.html`. Open it in a browser, click **Download installer**, then run
the command it shows you:

```bash
chmod +x ~/Downloads/yulakx.sh && ~/Downloads/yulakx.sh
```

The page carries the whole shell installer base64-encoded inside it, and the
shell installer in turn carries the game's entire source, so that one `.html`
file is everything a player needs — no repository checkout, no network fetch
for the game itself.

The installer detects Apple Silicon and your macOS version, installs a
compiler toolchain automatically if needed, downloads CMake only if it isn't
already present, builds Haven from source in Release mode, **verifies the
build by actually running its test suite on your machine**, installs
`Haven.app` to `/Applications`, and creates a `haven` command-line launcher.
No Xcode, Homebrew, Python or Node required. Re-running it rebuilds and
updates in place; saves live under `~/Library/Application Support/Haven` and
are never touched.

Run `scripts/make_installer.sh` to regenerate the page (under a fresh random
name) after changing the source.

## Building from source directly

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/haven_tests        # headless simulation test suite
open build/Haven.app       # macOS arm64 only
```

`-DHAVEN_BUILD_APP=OFF` builds just the platform-independent simulation
core and its test suite, which also builds and runs on Linux/CI — useful
for iterating on gameplay without a Mac.

## Architecture

- **`src/core/`** — math, RNG, logging, profiling, the job system, settings,
  serialization, platform paths.
- **`src/ecs/`** — a sparse-set entity-component registry.
- **`src/sim/`** — the shelter grid, room database, residents, items, random
  events/emergencies, and the authoritative `World` simulation tick.
- **`src/gameplay/`** — the tech tree, combat resolution, the procedural
  surface map, autonomous expeditions, and the quest/objective system.
- **`src/save/`** — crash-safe atomic saves with CRC32 corruption detection,
  multiple slots, autosave, and backup rotation.
- **`src/renderer/`** — the `RenderDevice`/`CommandBuffer`/`Camera`/
  `LightingSystem` abstraction, with an OpenGL 4.1 core-profile backend
  (`renderer/gl/`). A Vulkan/MoltenVK backend slot exists in the build
  (`HAVEN_ENABLE_VULKAN`) for future work; until it lands, the renderer
  honestly falls back to OpenGL rather than faking a device.
- **`src/scene/`** — procedural mesh generation and the bridge from
  `sim::World` to instanced 3D draws.
- **`src/ai/`** — resident steering and activity selection.
- **`src/ui/`** — an immediate-mode HUD with a built-in bitmap font.
- **`src/audio/`** — procedurally synthesized sound (CoreAudio backend);
  no licensed or third-party audio assets anywhere in the build.
- **`src/platform/macos/`** — the Cocoa window and live system-info queries.
- **`src/game/`** — the `App` game loop tying everything together.
- **`tests/`** — the headless test suite (1000+ assertions) covering math,
  the ECS, the shelter build rules, combat resolution, save round-trips,
  expeditions, and quest progression.

## Status

The simulation core, renderer abstraction, OpenGL backend, scene layer, and
save system are built and unit-tested (`cmake --build && ./build/haven_tests`
passes, including a full round-trip test of the exact source embedded in
the installer page in `dist/`). The macOS-specific window/audio/UI/app layer is
Cocoa/OpenGL/AudioToolbox code that cannot be compiled or run outside a real
macOS + Xcode toolchain, so it has not been build-verified in this
environment — run the installer on Apple Silicon hardware to build and play
it, and report back anything that fails to build so it can be fixed.

## License

MIT — see `LICENSE`.
