# Scalpellino — Working Plan & Log

**Purpose:** let any future session (human or AI) pick up work without outside context.
Update this file at the end of every step: what was done, test results, what's next,
open questions. Newest log entries at the top of the log.

Design: [`ARCHITECTURE.md`](ARCHITECTURE.md). Rules: [`../CLAUDE.md`](../CLAUDE.md).

---

## Current status

- **Milestone:** 0 (skeleton) — code done, waiting for first CI run on GitHub Macs and
  user's hands-on check on Intel Mac.
- **Next:** milestone 1 (brushes) — see plan below.

## How to work in this repo

- Build (Mac): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`
- Headless screenshot: `Scalpellino --screenshot name.png` (renders a few frames, saves in
  the current directory, exits). On Linux: `xvfb-run -a -s "-screen 0 1600x1000x24" ...`
- **AI cloud workspace note:** archive downloads from GitHub are blocked there, so CMake's
  FetchContent fails. Clone deps with git at the pinned refs (see `cmake/Deps.cmake`) and
  configure with `-DFETCHCONTENT_SOURCE_DIR_RAYLIB=... -DFETCHCONTENT_SOURCE_DIR_IMGUI=...
  -DFETCHCONTENT_SOURCE_DIR_RLIMGUI=...`. Linux needs `libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libgl1-mesa-dev`.
- Screenshots: every screenshot taken goes in `docs/screenshots/`, named
  `m<milestone>-<nn>-<what>.png`.
- CI: `.github/workflows/build.yml` — macOS Intel (`macos-15-intel`) + Apple Silicon
  (`macos-15`) build, test, ad-hoc sign, zip artifact; Linux core tests.

## Pinned dependencies

| Dep | Version |
|---|---|
| raylib | 6.2 |
| Dear ImGui | v1.93.0-docking |
| rlImGui | commit 5c17e70e98ae9f50ca94bdcad0e3b3536b46a203 |

## Open questions (parked)

- **Retina/HiDPI:** the app currently renders at logical resolution (may look soft on
  Retina screens). Needs the user's eyes on a real Mac before deciding how to handle it.
- **Fly-mode up/down keys:** using Q/E for now. User hasn't specified.

## Milestone 1 plan — Brushes

Goal: block tool, select/move/resize, grid snapping, undo/redo, save/load `.map`.

1. `core/math` — Vec3 (double), Plane, AABB.
2. `core/brush` — convex brush from planes; planes → face polygons (clip a big quad by
   all other planes); box brush factory; AABB.
3. `core/document` — world entity with brushes; stable brush ids; selection set.
4. `core/command` — command interface + undo stack (before/after brush snapshots).
5. `io/map` — Valve 220 `.map` writer + reader; round-trip test.
6. Render: brush wireframes in 2D views (selected = red, others = white-ish),
   flat/solid brushes in 3D.
7. Tools: Block (drag box in 2D → live preview → Return creates, Esc cancels; height from
   grid default 64 or last brush), Select (click to pick in 2D/3D, drag to move with snap,
   handles to resize), Delete key.
8. Cmd-Z / Cmd-Shift-Z, Cmd-S / Cmd-O (native file dialogs later; simple path field first).

## Log

### 2026-11-16 — Milestone 0 skeleton
- Added CMake project, pinned deps, `scal_core` (header-only so far: `core/grid.hpp`),
  app with 4 docked viewports (3D top-left, Top, Front, Side), Hammer-style 2D grid
  (minor / 64 / 1024 lines, teal axes, red world border at ±4096), 3D ground grid + axes,
  fly mode, right-mouse look, 2D pan/zoom, grid keys, status bar with cursor coords and a
  redraw counter, menu bar, layout saved to the user's settings folder.
- Idle policy: raylib `EnableEventWaiting` + 2 settle frames after each event; continuous
  only while flying / right-mouse looking or in `--screenshot` mode.
- **Tests:**
  - Core tests: pass (Linux).
  - Headless screenshot (Linux, Xvfb + Mesa software GL): renders correctly →
    `screenshots/m0-01-four-viewports-linux.png`. Pixel check confirmed both axes in
    every 2D view.
  - Idle CPU (Linux, Xvfb): **0 CPU ticks over 10 s** of idle. RSS ~135 MB, inflated by the
    software GL renderer; expect less on a real Mac.
- Bug found & fixed: in `--screenshot` mode the app went idle and never finished (proved
  idle works). Screenshot mode now counts as busy.
- Changed: 2D grid lines drawn as 1px rectangles instead of GL lines (driver-independent).
- CI: first run pending.
