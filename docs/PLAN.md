# Scalpellino — Working Plan & Log

**Purpose:** let any future session (human or AI) pick up work without outside context.
Update this file at the end of every step: what was done, test results, what's next,
open questions. Newest log entries at the top of the log.

Design: [`ARCHITECTURE.md`](ARCHITECTURE.md). Rules: [`../CLAUDE.md`](../CLAUDE.md).

---

## Current status

- Released: v0.1.0 (M0+M1), v0.1.1 (Retina, font, menus, Transform dialog; user
  confirmed it works), **v0.2.0 = milestone 2 Precision** (this step).
- **Next: milestone 3 — Textures.** Plan below.

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
- Releases: see log entry "GitHub Releases". To cut a version: add
  `docs/releases/vX.Y.Z.md` and push to `main`; CI creates the tag + release once the
  builds pass. (AI workspace note: pushing tags and GitHub API writes are blocked there,
  so this file-driven route is the one to use.)
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

- **Vertex tool extras:** edge handles (drag an edge = both its corners), marquee
  (drag-box) vertex selection, corner handles in the 3D view. Not in v0.2.0.
- **Grid-friendly angles:** user asked for "rotate by exact dimensions"; delivered as typed
  angles + Pythagorean angles. Note for the user: slopes like 1:2 can't keep rotated
  corners on whole numbers (irrational sin/cos) — build such slopes with the vertex tool.

- **Fly-mode up/down keys:** Q/E for now. User hasn't specified.
- **File dialogs:** open/save use an in-app path field. Native macOS dialogs planned
  (milestone 8 or earlier if wanted).
- **Multi-selection click:** a plain click (no drag) on a brush that is part of a
  multi-selection narrows the selection to that brush; dragging moves the group.

## Milestone 2 — Precision (done, v0.2.0)

Delivered: vertex tool, snap vertices to grid, pivot, grid-friendly (Pythagorean)
rotation angles, microgrid + custom grid sizes. See log. Not done (parked, below):
edge handles, marquee vertex selection, vertex handles in the 3D view.

## Milestone 3 plan — Textures

Needs (not blocking): a folder of the user's own textures to test with. Until then, use
procedurally generated test textures (checker, bricks, noise) created at runtime or by a
test script — never downloaded.

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

### 2026-11-16 — Milestone 2: Precision (v0.2.0)
- `core/hull`: brush from vertices (brute-force convex hull, merges coplanar points into
  one face, plane points = largest triangle, textures copied from the source face with
  the most similar normal), `snapBrushToGrid` (refuses if faces would split or the brush
  collapses), `brushOnGrid`.
- **Validity rule** (important): every requested corner must end up on ≥2 hull faces.
  On 0 = inside the solid, on 1 = flat in the middle of a face; both would silently drop
  the corner, so the edit is refused. The first version only required "on a face plane";
  the new UI test caught an invalid drag being applied. Core test added for that case.
- **Vertex tool** (`tools/vertex_tool`, Shift+V, Tools menu, Tools panel): corner handles
  in 2D views (selected red), stacked corners picked together, Cmd+click toggles, drag
  with grid snapping, live rebuilt preview, invalid = magenta + status "INVALID SHAPE -
  won't apply", refused on release. Move vertices dialog (Cmd+Shift+T): move by offset
  or set a single corner's position.
- **Snap vertices to grid** (Cmd+B, Transform menu).
- **Pivot** (Transform → Pivot: selection centre / world origin / typed point) used by
  Option-drag rotation and as the Transform dialog default; cyan marker in 2D views.
- **Grid-friendly angles:** I first planned slopes (1:2 etc.) — wrong: their sin/cos are
  irrational. Replaced with Pythagorean angles (3:4:5 = 36.87°, 5:12:13, 8:15:17,
  7:24:25): corners stay on whole numbers when offsets from the pivot are multiples of the
  hypotenuse. Dialog buttons + Transform → Drag rotation snapping option.
- **Grid:** size is a double; microgrid 0.125/0.25/0.5 (View → Microgrid, off by default);
  View → Set grid size... (any size, e.g. 80; custom sizes draw their own grid lines,
  highlighted every 8 cells).
- **Tests:** core **478 checks, 0 failed** (hull rebuild, wedge by merging, lifted corner
  splits face, corner inside / flat-in-face refused, flat = no volume, texture carry-over,
  snap after 30° rotation, coarse-grid collapse refused, Pythagorean rotation on grid 8
  vs 1:2 off grid, microgrid / custom grid). UI scripts `m1_brushes`, `m2_precision`,
  `v011_transform_menus`: **PASS at 1x and 2x**. Idle: **0 ticks / 10 s**.
- Screenshots: `m2-01-vertex-moved`, `m2-02-vertex-invalid-preview`,
  `m2-03-pythagorean-rotation`.

### 2026-11-16 — v0.1.1: Retina, font, vanishing lines, menus, Transform dialog
User feedback on v0.1.0 (Intel iMac, Retina): app blurry; some brush/preview edges
vanish at certain positions; pixel font disliked; wants menus for everything incl. a
rotate-by-amount dialog; off-grid vertices after rotation (→ milestone 2).
- **Retina:** `FLAG_WINDOW_HIGHDPI`; viewports render into textures sized
  logical × display scale; ImGui fonts rasterized at framebuffer scale.
- **Font:** Roboto Medium (Apache-2.0, from Dear ImGui's misc/fonts) embedded at build
  time (`cmake/EmbedFile.cmake`). Size labels moved from raylib's pixel font into the UI
  layer. `docs/THIRD_PARTY.md` added.
- **Vanishing lines — cause:** views were pasted at fractional screen positions, so the
  sampler skipped texture rows/columns and 1-px lines disappeared. **Fix:** views are
  placed on whole physical pixels with 1:1 texture size; all 2D lines are drawn as
  pixel-exact rectangles/quads (no GL lines).
- **New test `expect edges`:** reads back the real screen and verifies every brush /
  preview edge in all 2D views has visible pixels. With `SCAL_VIEW_SCALE=2` (forced
  Retina rendering on a 1x test display) it checks each view's own 2x image instead.
  It reproduced the dropped-row effect when screen and view scales mismatched.
- **Menus:** File, Edit (undo/redo, duplicate, delete, select all/none), Tools (select,
  block, create block, cancel), Transform (Move/Rotate/Scale dialog Cmd+T, rotate 90°
  about X/Y/Z, flip X/Y/Z), View (grid, snap, center views Cmd+E, fly mode, reset
  layout), Help (Controls window, F1).
- **Transform dialog:** exact move offset; rotate by typed degrees about X/Y/Z with
  quick buttons; scale factors (negative = mirror); pivot = selection centre / world
  origin / typed point; texture lock toggle; invalid results refused with a message.
  Core: `core/transform` (tested).
- **Tests:** core **444 checks, 0 failed** (incl. transform move/rotate 90 & 30 /
  scale / mirror / zero-scale rejection). UI scripts `m1_brushes` and
  `v011_transform_menus` **PASS at 1x and forced 2x**. Idle: **0 ticks / 10 s**.
- Bug found in my own test code: range-for over a member of a temporary (dangling) →
  segfault; fixed.
- Screenshots: `m1-10-rotated-30-exact`, `m1-11-duplicated`, `m1-12-transform-dialog`,
  `m1-13-controls-window`, `m1-14-roboto-font-block-preview`.
- Roadmap reordered (ARCHITECTURE §15): M2 Precision, M3 Textures, M4 Shaping (clip incl.
  3-point), M5 Curves, M6 Look, M7 Export, M8 Extras.

### 2026-11-16 — GitHub Releases
- CI `release` job (after Mac + Linux jobs pass): every push to `main` replaces the
  `latest` pre-release with fresh Intel + Apple Silicon zips, and creates a permanent
  release for each `docs/releases/vX.Y.Z.md` that has none yet (tag made by CI at that
  commit). Pushing a `vX.Y.Z` tag manually also works. Release notes contain no links or
  machine details. Tag push and API tag creation were refused by the AI workspace proxy,
  hence the file-driven route.
- First run: **v0.1.0 published** (Intel + Apple Silicon zips). Bug: two runs raced and
  the older one deleted the newer "latest" and then failed. Fix: release jobs share a
  concurrency group and skip if `main` has moved past their commit.
- Versioning: tag `v0.<milestone>.<patch>` at the end of each milestone.
  `v0.1.0` = milestones 0 + 1.

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
- **CI (run 43779079748): all green** — Linux build + core tests + scripted UI tests on
  Xvfb; Intel and Apple Silicon builds + core tests + app zips.

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
