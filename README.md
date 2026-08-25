# Haven: Deep Shelter

A polished, original 3D underground shelter-management game — a shelter
built as a real, explorable multi-floor 3D facility, not a 2D board with a
camera on it. Gritty, retro-futuristic, industrial-underground atmosphere;
detailed 3D presentation and dynamic lighting. **Apple Silicon macOS only**
(M1 and newer). All artwork, geometry and audio are original or generated
at build/runtime — nothing is copied from any existing game.

## Installing

There is exactly one installer:

```bash
~/Downloads/hyu.sh
```

Download `dist/hyu.sh` from this repository, put it wherever you
like (`~/Downloads/` is just the expected drop location — the script
works from anywhere), and run it:

```bash
chmod +x hyu.sh
./hyu.sh
```

It detects Apple Silicon and your macOS version, installs a compiler
toolchain automatically if needed, downloads CMake if it isn't already on
your system, builds Haven from source in Release mode, **verifies the build
by actually running its test suite on your machine**, installs `Haven.app`
to `/Applications`, and creates a `haven` command-line launcher. Nothing
else is required — no Xcode, no Homebrew, no Python, no Node. Run the same
script again any time to rebuild and update; your saves live under
`~/Library/Application Support/Haven` and are never touched.

The installer embeds the game's full source directly in the script (as a
compressed archive) and builds it locally, so what you get is a real,
optimized, native arm64 build — see `scripts/make_installer.sh` if you want
to regenerate `dist/hyu.sh` after changing the source.

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
`dist/hyu.sh`). The macOS-specific window/audio/UI/app layer is
Cocoa/OpenGL/AudioToolbox code that cannot be compiled or run outside a real
macOS + Xcode toolchain, so it has not been build-verified in this
environment — run the installer on Apple Silicon hardware to build and play
it, and report back anything that fails to build so it can be fixed.

## License

MIT — see `LICENSE`.
