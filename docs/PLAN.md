# Scalpellino — Working Plan & Log

**Purpose:** let any future session (human or AI) pick up work without outside context.
Update this file at the end of every step: what was done, test results, what's next,
open questions. Newest log entries at the top of the log.

Design: [`ARCHITECTURE.md`](ARCHITECTURE.md). Rules: [`../CLAUDE.md`](../CLAUDE.md).

---

## Current status

- **Milestone 0:** done.
- **Milestone 1 (brushes):** done — block tool, select/move/resize/rotate/clone/delete,
  grid snapping, undo/redo, save/open Valve 220 `.map`, unsaved-changes prompt.
  Core tests + scripted UI tests pass locally; CI runs both.
- **Waiting on user:** hands-on check of the Intel build (look, feel, idle fans, Retina
  sharpness). Nothing is blocked by it.
- **Next:** milestone 2 (textures) — see plan below.

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
- **Scripted UI tests:** `tests/ui/*.script`, run by `tests/ui/run.sh <app> [shot-dir]`
  (Linux + Xvfb; CI job `linux`). The script language is documented at the bottom of
  `src/app/editor.cpp` (press/drag/release in view pixels, key, undo, save/open, shot,
  `expect brushes|selected <n>`, `expect size x y z`). Pixel coordinates assume the
  default 1440x900 window and default layout; top view centre = local (334,224),
  zoom 0.5 (2 world units per px). Tip: snapped drags shift brushes by up to half a grid
  cell, so compute brush edges from snapped values before aiming clicks near edges.
- CLI: `--open <map>`, `--script <file>`, `--screenshot <png>`. Automated runs never
  read or write the user's layout settings.
- Screenshots: every screenshot taken goes in `docs/screenshots/`, named
  `m<milestone>-<nn>-<what>.png`.
- CI: `.github/workflows/build.yml` — macOS Intel (`macos-15-intel`) + Apple Silicon
  (`macos-15`): build, core tests, ad-hoc sign, zip artifact. Linux: build, core tests,
  scripted UI tests, screenshots artifact. (GitHub's Mac VMs have no OpenGL, so UI tests
  can only run on Linux.)

## Pinned dependencies

| Dep | Version |
|---|---|
| raylib | 6.2 |
| Dear ImGui | v1.93.0-docking |
| rlImGui | commit 5c17e70e98ae9f50ca94bdcad0e3b3536b46a203 |

## Open questions (parked)

- **Retina/HiDPI:** renders at logical resolution (may look soft on Retina). Needs the
  user's eyes on a real Mac.
- **Fly-mode up/down keys:** Q/E for now. User hasn't specified.
- **File dialogs:** open/save use an in-app path field. Native macOS dialogs (e.g. via
  osascript or a small Cocoa helper) planned; confirm user wants native ones.
- **Rotate gesture:** Option+drag on a corner handle (15° steps, Shift = free). Chosen
  to keep handles visible and avoid Hammer's click-cycling. Confirm it feels right.
- **Multi-selection click:** a plain click (no drag) on a brush that is part of a
  multi-selection narrows the selection to that brush; dragging moves the group.

## Milestone 2 plan — Textures

1. Texture library: user picks texture folders (stored in user settings, never in repo);
   load PNG/TGA lazily, thumbnails cached; nearest-neighbour filtering by default.
2. Textured 3D mode: UVs from Valve 220 axes (u = dot(p, U)/scaleU + offU, divided by
   texture width; same for v), per-texture batches. Missing texture = checker pattern.
3. Texture browser panel: grid of thumbnails, filter by name, current texture.
4. Apply: texture tool — click a face in 3D to apply current texture; Shift+click =
   whole brush; Block tool uses current texture.
5. Face edit panel: shift/scale/rotation fields, fit, justify, align world/face;
   texture lock toggle (default on).
6. Face picking in 3D (ray vs polygons, nearest face).
7. Tests: UV maths (incl. texture lock after move/rotate), face picking, `.map` round
   trip with texture names; UI script applying textures; screenshots.

## Milestone 1 plan — Brushes (done)

## Log

### 2026-11-16 — Milestone 1 complete
- Rendering: brush polygons cached per document revision (`render/scene`); 2D views draw
  projected edges (selected red), size labels, handles (middle handles hidden on small
  boxes); 3D view flat-shaded per-brush pastel colours, selection tinted red with
  outlines drawn through geometry; block preview in yellow.
- Tools (`src/tools`): Select (click/Cmd+click/marquee-free deselect, move, resize by
  handles or edges, Shift+drag clone, Option+corner rotate 15° steps, Delete, Esc) and
  Block (drag rectangle, depth from last brush, adjust in any 2D view, Return/Esc).
- Editor (`src/app/editor`): menus (File/Edit/View), Tools panel docked left, status
  bar with tool hint + selection size, window title with file name and `*` when dirty,
  path-field Open/Save dialogs, unsaved-changes prompt on New/Open/Quit/window close.
- Scripted input mode + `tests/ui/m1_brushes.script` (block, move, resize, undo/redo,
  save/open, select all, clone, delete, Esc cancel, 3D pick, Cmd+click, rotate 45°,
  undo) with `expect` checks. Added to CI (Linux + Xvfb).
- Removed the macOS smoke-screenshot CI step (no OpenGL on GitHub Mac VMs).
- **Tests:** core 288 checks, 0 failed. UI script PASS. Idle CPU with a map open:
  **0 ticks / 10 s**. Warnings clean.
- Screenshots: `screenshots/m1-01` … `m1-09` (block preview, two blocks, moved pillar,
  resized pillar, undo, reopened + select all, clone, 3D pick, 45° rotation).
- Bugs found & fixed along the way: scripted presses were being "released" by the real
  mouse code (scripted input now bypasses mouse capture); several script coordinate
  mistakes (editor was right) — see tip in "How to work".
- Design decisions made without asking (parked above for confirmation): Option+drag
  rotate, click-narrows-multiselection, path-field dialogs.

### 2026-11-16 — M1 core: brushes, document, undo, .map IO
- `core/math.hpp` (Vec3/Plane/AABB, doubles), `core/brush` (planes → CCW polygons by
  clipping, box factory, translate/remap with texture lock, ray hit), `core/document`
  (brushes, selection, command history with before/after snapshots, dirty tracking),
  `io/map_format` (Valve 220 writer; reader for Valve 220 + classic Quake faces; other
  entities preserved verbatim; invalid brushes skipped and counted; atomic save).
- Plane convention: n = normalize(cross(c - a, b - a)), same as TrenchBroom.
- **Tests:** 252 checks, 0 failed (box geometry + winding + outward normals, wedge,
  invalid brushes, texture lock on move, remap incl. mirroring, ray hits, undo/redo/
  delete/redo-clearing, dirty flag, byte-identical .map round trip incl. non-integer
  coords, classic Quake format, parse errors with line numbers).
- Compiler warnings (-Wall -Wextra) on for our code: clean.

### 2026-11-16 — First CI run
- Intel (`macos-15-intel`) + Apple Silicon (`macos-15`) build, tests, app zip: **pass**.
- Smoke screenshot on GitHub's Mac VMs: **impossible** — VMs have no OpenGL ("NSGL:
  Failed to find a suitable pixel format"). App used to segfault after that; now exits
  cleanly with code 2 and a message. Visual checks = Linux Xvfb screenshots + the user.
- Annotation noise: Node 20 deprecation on actions v4 (harmless for now).

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
