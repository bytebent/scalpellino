# Scalpellino — Working Plan & Log

**Purpose:** let any future session (human or AI) pick up work without outside context.
Update this file at the end of every step: what was done, test results, what's next,
open questions. Newest log entries at the top of the log.

Design: [`ARCHITECTURE.md`](ARCHITECTURE.md). Rules: [`../CLAUDE.md`](../CLAUDE.md).

---

## Current status

- Released: v0.1.0 … v0.4.0, **v0.5.0 = milestone 5 Curve tool** + texture wrapping +
  CI trimmed to Intel/Linux (this step). User asked to report only at v0.5.0.
- **v0.6.0 = milestone 6 Workflow + shapes**. Reported to the user.
- **v0.6.1 = fixes** the user reported for v0.6.0: rounded domes overlapped themselves;
  spheres lagged when resized. Also fixed the spiral stairs column. See log.
- **v0.7.0 = milestone 7 Terrain**. Reported to the user.
- **v0.7.1 = terrain fixes** the user asked for after v0.7.0 (this step): make terrain
  from any picked face (and triangles), the vertex tool edits terrain point by point,
  up to 8 textures per patch. Report to the user, then wait for the go-ahead on
  milestone 8.
- **Next: milestone 8 — World**: lights, sky picker (a setting, not a sealed box), fog,
  camera bookmarks, 72-unit scale figure.

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
  (Linux + Xvfb; CI job `linux`, at 1x and forced 2x). The script language is documented
  in `src/app/editor.cpp` above `loadScript`. **Use world coordinates** for 2D input
  (`wpress <view> <u> <v> [mods]`, `wdrag <u> <v>`, `release`) and `press3d <dx> <dy>`
  (relative to the 3D view centre) so scripts don't depend on the window layout.
  (Pixel `press/drag` still exist but broke once when the layout changed.) Default 2D
  zoom is 0.5 px/unit. Tip: snapped drags shift brushes by up to half a grid cell, so
  compute brush edges from snapped values before aiming clicks near edges.
  `printviews` prints each view's origin/size (debugging).
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

- **Curve tool extras:** drag curved-wall depth in other views; stairs centre column;
  dome / cone / torus / helix ramp; per-shape remembered settings.
- **Terrain, decided without asking (confirm with the user):**
  - A sculpt stroke is applied when the mouse is released. It's an edit, like moving,
    not a creation, so the Return rule doesn't apply. Esc during a drag cancels it.
  - Clip, carve and hollow turn a terrain brush back into a plain brush. Vertex editing
    and snap to grid keep the terrain while its face stays one face (v0.7.1).
  - In 2D views the full grid is drawn. In 3D, a selected terrain shows only its outline.
  - v0.7.1: Make terrain = pick faces (Return makes, Esc stops); selecting brushes no
    longer takes their top face. Max 8 textures per patch (face + 7 painted). Painting
    is by shares (order doesn't matter). In 2D, brush corners win over terrain points
    on the same spot; terrain points are the only handles picked in the 3D view.
- **Terrain extras, later:** faces with more than 4 corners (e.g. a cylinder's cap: clip
  it for now); marquee (box) selection of terrain points; sculpting limited to the
  selection; edges between different grid sizes or where a triangle's split edge meets
  a quad's plain edge (they only match where grid points meet).

- **Texture browser, later:** tags, folders/collections, favourites, recently used,
  sorting by size, previews of how a texture tiles (user: "eventually a lot of
  functionality, e.g. tags; for now searchable by name is fine").
- **Native dialogs are untested on a real Mac by me** (CI compiles them; no GUI there).
  Ask the user to try Cmd+O / Cmd+Shift+S / Texture → Add folder → Browse.

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

## Milestone 3 — Textures (done, v0.3.0)

Delivered: texture library (built-in + user folders), textured 3D view, browser with
search by name, texture tool (pick faces), Face panel, Texture menu. User wants later:
**tags and much more browser functionality** (parked below).

## Milestone 4 — Shaping (done, v0.4.0)

Delivered: clip tool (2-point drag + 3 draggable points, keep both/front/back, typed
points), carve with selection, hollow. Not done: merging carve fragments into fewer
pieces (carve can leave more pieces than strictly needed).

## Milestone 5 — Curve tool (done, v0.5.0)

Delivered: arch (square top, legs), ring / pipe / cylinder (sweep), curved wall
(quadratic, bend handle), spiral stairs (top view), arched opening (box cut + arch-slice
refill), generated groups (regenerable, follow moves, bake), Properties panel with typed
fields, handles in all 2D views (incl. depth), Return/Esc. Not done (parked): editing
curved-wall depth by dragging in other views (typed only), centre column option for
stairs, more shapes (dome, cone, torus, helix ramp).

## Milestone 6 — Workflow + shapes (done, v0.6.0)

Delivered: block primitives, dome / helix ramp / straight stairs / stairs column,
hide/show + visgroups (saved), overlap finder with camera jumps, views to origin.

## Milestone 7 — Terrain (done, v0.7.0)

Delivered: terrain from 4-cornered faces (2–32 cells), resampling grid-size change,
sculpt (raise / lower / smooth / flatten / noise) and paint / erase with typed radius,
amount, strength and falloff, a circle laid on the surface, dab spacing, one undo step
per stroke, stitched neighbours (shared points), heightmap import, `.map` storage, 3D
blend of two textures, picking raised terrain. Original draft kept below.

### Draft (as planned)

User: "a terrain tool like Hammer's, maybe better"; keep it in our own `.map`; export
decided at the end.
1. Data: a terrain patch = a brush face turned into an N x N grid (power, e.g. 8/16/32)
   of height offsets along the face normal + per-vertex blend alpha (2 textures). Stored
   in the `.map` as an entity (`classname` `scal_terrain`) or `//@` metadata on the brush.
2. Rendering: triangulated grid in 2D/3D views; textured with blend in the 3D view.
3. Sculpt tool: raise / lower / smooth / flatten / noise; radius, strength, falloff as
   typed fields + on-screen circle (no mouse wheel); works across patch borders.
4. Paint tool: blend between the face texture and a second texture.
5. Neighbouring patches stay stitched (shared edge heights averaged / locked).
6. Heightmap import (PNG -> heights). Undo for strokes (one command per stroke).
7. Tests: grid maths, stitching, smoothing, round trip; UI scripts.

## Later milestones (agreed order)

7 Terrain (in our `.map`), 8 World (lights, sky picker setting — not a sealed box, fog,
camera bookmarks, scale figure), 9 Renderer (PNG, cull what the camera can't see,
terminal-style render log like Hammer's compile window + log file), 10 Y2K shaders,
11 Export.

## Milestone 1 plan — Brushes (done)

## Log

### 2026-11-16 — v0.7.1: terrain on any face, terrain points, more textures
User feedback on v0.7.0: Make terrain took a block's top face (wanted: any face); the
vertex tool should work on terrain, point by point; more than two textures (at least 3).
- **Core** (`core/terrain`, `core/brush.hpp`):
  - `Terrain::layers` (texture + share per point) replaces `texture2` / `alpha`.
    `kMaxTerrainLayers = 7`. Paint/erase work point by point on shares: painting a
    texture raises its share and lowers the others by the same factor (order doesn't
    matter, so stitched points stay equal); painting the face's own texture uncovers it.
    `terrainLayer`, `pruneTerrainLayers`; `sculpt(…, bool* full)` reports a full patch.
  - 3-cornered faces: `faceQuad` lays a triangle out as a quad with its far corner in the
    middle of the edge opposite the start. New triangle terrain starts opposite its
    longest edge (two triangles sharing it match).
  - `moveTerrainPoints` (raise / set, through shared points), `carryTerrain` (keeps a
    terrain through corner edits when its face survives), `faceFacing` (2D face pick).
  - **Bug found and fixed:** a dab reached any terrain whose flat distance (in its own
    plane) was within the radius, so a stroke on a floor also raised/painted a wall next
    to it. Now points must also be within the radius along their normal.
- **File:** `//@ tl <texture>` before each layer's `//@ ta` rows; `tex2=` and bare `ta`
  rows from v0.7.0 still load.
- **Drawing:** one pass per layer; shares converted to "over" transparencies in
  `makeSceneBrush`. `Scene::outlines` (picked faces, yellow, all views). Terrain points
  drawn as small blue handles in 2D and 3D (`Viewport::worldToLocal3D`).
- **Terrain tool:** face-picking state (`startPicking`, `pickedFaces`); hover/strokes off
  while picking. Properties panel: picking panel (count, cells per side, Make / Stop),
  and the list of textures on the selected patch.
- **Vertex tool:** terrain points of selected terrain (stacked in 2D on terrain facing the
  same way, single in 3D),
  Cmd+click add/remove, drag along the normal (2D side views, or the 3D view by the
  normal's screen direction), snapped; live preview through `Scene::replaced`. Corner
  moves and snap to grid keep the terrain (`carryTerrain`). Move vertices dialog has
  "raise by / set height" for terrain points.
- **Editor/menus:** Terrain → Make terrain (pick faces)…, Terrain grid size…; Help →
  Controls and README updated.
- **Script:** `terrain pick`, `drag3d`, `vertexraise`, `vertexheight`,
  `expect terrainpoints`, `expect paintlayers`. `m7_terrain` now picks faces in the top
  view. New `v071_terrain` (wall face from the front view, two triangles from a diagonal
  clip, raising a row in the front view, a seam point in 3D, typed height, wall corner
  move keeps terrain, four textures, uncovering, save/open). Test textures
  `tests/ui/assets/textures/{grass,sand,rock}.png` (generated, ours).
- **Tests:** core **3454 checks, 0 failed** (new: layered paint and order, full patch,
  v0.7.0 file layout, triangle grid inside the triangle + mirror, point moves across a
  seam, corner edits keep / drop terrain, strokes don't reach a nearby wall, triangle
  seams on both diagonals). UI: all 12 scripts pass at 1× and 2×. Idle with a terrain
  map open: 0 CPU ticks over 10 s.
- **Screenshots:** `m7-05-make-terrain-picked-faces.png`,
  `m7-06-vertex-tool-terrain-points.png`, `m7-07-four-painted-textures.png`.

### 2026-11-16 — Milestone 7: Terrain (v0.7.0)
- **Core** (`core/terrain.{hpp,cpp}`, data in `Brush::terrain`):
  - The grid is bilinear over the face's 4 corners; heights go along the face normal.
  - The start corner is a stored point. It follows translate, rotate and remap; a mirror
    transposes the grid, and scaling scales heights along the normal.
  - `makeTerrain` (new, or a resample keeping the shape), `topQuadFace`, `terrainGrid`.
  - `sculpt` modes: raise, lower, smooth, flatten, noise, paint, erase. Falloffs: soft,
    linear, hard.
  - `stitchTerrain`, `applyHeightmap`, `rayHitGrid` / `rayHitTerrain`.
  - All edits go through "shared points" (same flat spot and normal = one point), so
    neighbouring patches never crack.
  - `splitBrush` drops terrain from its pieces.
- **File:** `//@ terrain …`, `//@ th` rows, `//@ ta` rows (only when painted).
  Incomplete data is a load error.
- **Drawing:**
  - `SceneBrush` replaces the terrain face with one polygon per cell, with per-corner
    blend values and smooth normals.
  - 3D: the second texture is drawn over the first, with the blend as transparency.
  - The selection outline shows only the terrain's border.
  - The brush circle is laid onto the surface in 3D (48 rays) and drawn flat in 2D.
- **Tool** (`tools/terrain_tool`):
  - Strokes work in 3D, or in 2D along the view depth.
  - Dabs every radius/5. One `makeModify` per stroke, so one undo step.
  - Esc cancels a stroke. The new `Tool::hover` hook keeps the circle under the mouse.
- **Editor:**
  - Terrain menu: make / grid size, remove, tool + modes, heightmap, stitch.
  - Shift+G, a tool button, the Properties panel, and Make terrain / Import heightmap
    dialogs (Browse… uses the native panel on macOS).
  - Every terrain edit is stitched against all other terrain (`applyTerrainEdit`).
  - `pick3D` and the Texture tool's face picking now hit raised terrain.
- **Script:**
  - New commands: `terrain make|remove|stitch|heightmap`, `terrainset`, `whover`,
    `camera`, `expect terrains`, `expect terrainheight`.
  - `tests/ui/run.sh` now copies `tests/ui/assets/` into each run (heightmap image).
- **Tests:**
  - Core: **3140 checks, 0 failed**. New terrain checks cover:
    - the grid, bounds and ray hits;
    - move, rotate, mirror, scale and upside-down;
    - cut pieces and 3-corner faces;
    - every mode and the falloffs;
    - seams through every mode, and stitch repair;
    - resample exactness, heightmap orientation, and the `.map` round trip, including
      broken data.
  - UI: all 11 scripts PASS at 1× and 2×. New: `m7_terrain`, covering:
    - two patches, a raise across the seam (equal on both sides), undo/redo;
    - a multi-dab stroke as one undo step;
    - smooth, flatten, noise, and paint from the 3D view;
    - a grid-size change, heightmap import, move, save + open, remove.
  - Perf: 16 patches of 33×33 points take 5 ms per dab, and a ray over all of them
    0.4 ms.
  - Idle CPU with a terrain map open: 0 ticks over 10 s.
  - Screenshots:
    - `screenshots/m7-01-terrain-tool-circle.png`
    - `screenshots/m7-02-sculpted-top-view.png`
    - `screenshots/m7-03-painted-3d.png`: Tile_19 blended into Bricks_24 over a hill,
      with the circle lying on the surface.
    - `screenshots/m7-04-heightmap-hill.png`
- Next: report to the user; milestone 8 (World) after the go-ahead.

### 2026-11-16 — v0.6.1: self-overlapping shapes, fast round shapes and hull
- **User report:** rounded domes are made of overlapping pieces; spheres lag when resized.
- **Measured first:** a probe ran `findOverlaps` on every curve shape, with rounding on
  and off. Results:
  - Rounded dome: 56 overlaps. Without rounding: 0.
  - Spiral stairs + column, 330° turn: 6–7 overlaps.
  - Everything else: 0.
  - Sphere build: 50 ms at 16 sides, 14.4 s at 32 sides.
- **Cause of the sphere lag:** not vertex snapping. Two things together:
  - The O(n⁴) brute-force hull (`brushFromVertices`), used for building primitives.
  - The block tool rebuilding its preview in `decorate()` 4× per frame.
- **Dome fix** (`curves.cpp`):
  - Pieces are now built from planes (`Builder::planes`, 3 points per face).
  - Neighbours pass the identical 3 points for a shared side, so the planes are identical.
  - Ring boundaries are horizontal (outer and inner corner at the same height), so they
    stay flat when rounded.
  - In the top ring, the sides pass through the centre line, so pieces meeting at the
    pole can't cross.
  - Trade-off: with rounding about 29% of the corners land slightly off whole units
    (plane intersections). Previously all corners were on whole units but the pieces
    overlapped.
- **Stairs fix:**
  - The step angles are wrapped to one turn (`stepAngle`), so a boundary rounds to the
    same corner on every turn. Values landing exactly on .5 used to round differently
    per turn.
  - The column's corners are the steps' inner corners, plus arc points for the gap.
  - With several turns, the steps per turn must divide 360°; otherwise a clear error.
- **Speed:**
  - Cylinder, cone and sphere are now built from planes (`fromTriples` in
    `primitives.cpp`). Sphere at 32 sides: 14.4 s → ~0.12 s; at 64 sides: ~1 s.
  - The block tool caches the preview brush and its drawable polygons, keyed on box,
    shape, sides, axes, rounding and texture.
  - The hull is now gift wrapping: find one face, then pivot around each face edge to
    the neighbour with the smallest fold angle. The old brute force remains as a
    fallback if anything looks degenerate.
  - Vertex-editing rebuild of a 32-sided sphere: 28 s → 0.1 s.
  - Bug found on the way: `atan2(-0.0, x)` gave −π for points on the face itself; fixed.
- **Tests:**
  - Core: **2197 checks, 0 failed**. New:
    - Every curve shape, rounding on and off, uneven sizes: no overlaps, all valid.
    - An uneven multi-turn column is refused.
    - The fast hull equals brute force on 400 random clouds, including heavily coplanar
      integer grids.
    - A 32-sided sphere rebuilds.
  - Probes (not committed):
    - Fast hull vs brute force on 3000 clouds: 0 mismatches.
    - Dome sweep of 540 settings: 0 overlaps. 12 refusals, only for tiny domes where the
      inner radius is ~4 units with many rings and rounding.
  - UI: all 10 scripts PASS at 1× and 2×. New: `v061_fixes`:
    - A rounded dome (13×5) and a 720° column stair, both 0 overlaps.
    - A 32-sided sphere drawn, resized with a handle and created.
  - Screenshots: `screenshots/m6-10-rounded-dome-no-overlaps.png`,
    `screenshots/m6-11-sphere-32-resized.png`.
- Next: report to the user; then milestone 7 Terrain (waiting for go-ahead).

### 2026-11-16 — Milestone 6: Workflow + shapes (v0.6.0)
- `core/primitives`: box, wedge (triangle in the drawing view), cylinder, cone, pyramid,
  sphere — each ONE convex brush (hull of points), optional whole-unit rounding. Block
  tool Properties panel (shape, sides), real shape preview.
- `core/curves`: dome (lat/long shell pieces), helix ramp (shared edge corners between
  segments), straight stairs (solid or slabs, flip), spiral stairs centre column. Single-
  value key aliases (steps, rise, turn, inner_radius) after a script silently ignored
  `curveset steps`. Upright shapes (stairs, ramp, dome) only in the top view.
- Visibility: `Brush::hidden`, `Brush::visgroup`, `Document::visgroups`/`visible()`.
  Scene cache contains visible brushes only (so hidden = not drawn and not pickable);
  3D picking skips hidden; Select all skips hidden. Hide (H) / hide others (Shift+H) /
  show all (Cmd+U, also shows all visgroups). Visgroups tab next to Textures (toggle,
  rename, select, add selection, delete, new from selection). `.map`: worldspawn
  `_scal_visgroups` = `id|name|visible;...`, per-brush `//@ visgroup=N hidden=1` comment
  lines (lexer emits Meta tokens for `//@`; other tools skip them as comments). Editor
  Open passes visgroups through (remembered the v0.5.0 lesson).
- `core/csg`: `overlapVolume`, `findOverlaps` (intersection = both plane sets; touching
  doesn't count). Overlaps window (Cmd+Shift+O; list, Prev/Next) selects the pair,
  centres + zooms 2D views and moves the 3D camera (`Viewport::lookAt`).
- Views to origin: `Viewport::resetToOrigin`, corner "origin" button per view, View menu
  + Cmd+0.
- ImGui error found: restoring the cursor after the corner button "extends window
  boundaries" -> add a Dummy item. All UI runs now checked for `imgui-error` = 0.
- **Tests:** core **1026 checks, 0 failed** (primitive bounds + exact volumes, rounding,
  dome/helix/straight/column, visibility + .map round trip + metadata stripped for other
  tools, overlaps incl. touching / ring slices). UI `m6_workflow` + all older scripts:
  **PASS at 1x and 2x**. Idle: **0 ticks / 10 s**.
- Screenshots: `m6-01-cylinder-preview`, `m6-02-overlap-found`, `m6-03-new-shapes`.

### 2026-11-16 — Milestone 5: Curve tool (v0.5.0)
- `core/curves`: generators for arch (flat top + legs), ring / pipe / cylinder (sweep),
  curved wall (quadratic Bezier, offset by +-thickness/2), spiral stairs, carve arch
  (cutter box + arch-slice fill). Slices are built as prisms via the convex hull of their
  corners; neighbours reuse the same computed points (shared exact corners); optional
  rounding to whole units (default on). Params <-> key/values; `shiftCurve`.
- Document: `Brush::group`, `Group {id, keys}`, `GroupChange` in commands (undo/redo),
  `groupMembers`, `expandToGroups`, automatic removal of groups that lose their last
  brush (`pruneEmptyGroups`, undoable). `.map`: groups as `func_group` + `_scal_*` keys;
  plain func_groups from other tools stay opaque.
- Curve tool (Shift+C, Tools menu, Tools panel): Properties panel with shape + typed
  fields + box/depth; drawing in a 2D view (stairs: top view only; ring/stairs kept
  square); box handles in all 2D views (depth in the others); curved wall points 1/2/3;
  preview; Return creates / updates; Esc cancels; Tools -> Edit curve (follows a moved
  group), Bake curve. Select tool selects whole groups.
- Bugs found by the new UI test: (1) groups survived deleting all their brushes ->
  `pruneEmptyGroups`; (2) **File -> Open dropped the groups** (editor called
  `Document::reset` without them; the core round-trip test didn't cover the editor path).
  Both fixed. Script mistakes: clicked within 6 px of an edge (= resize handle).
- Carve arch first used fan wedges / one convex cutter -> wall shards; now box cut + arch
  slice refill = clean rectangles + radial slices (same exact volume).
- GCC 13 `-Wnonnull` false positive on `vector = {..}` assignment; rewritten with
  push_back to keep the build warning-free.
- A `cd …; rm -f *` debug command was blocked by the safety check (nothing deleted); use
  fresh `mktemp -d` folders for scratch runs instead.
- **Tests:** core **689 checks, 0 failed** (arch volume exact vs polygon maths, shared
  corners, rounding to whole units, too-low arch error, ring volume + closing gap,
  cylinder, half ring, >=180-degree slice error, curved wall shared edges + too-tight
  error, spiral stairs heights, carve arch volume + clean pieces, params round trip,
  groups undo / select / map round trip / prune). UI `m5_curves` (arch create, group
  select, move + edit follows, bake + undo, ring, stairs refused in front view, curved
  wall, arched opening exact volume, save/open/edit) + all older scripts: **PASS at 1x
  and 2x**. Idle: **0 ticks / 10 s**.
- Screenshots: `m5-01-arch-preview`, `m5-02-arch-edited-12-segments`,
  `m5-03-stairs-ring-arch`, `m5-04-curved-wall-preview`, `m5-05-arched-opening-preview`,
  `m5-06-arched-opening`.

### 2026-11-16 — CI trimmed + texture wrap (no separate release; ships in v0.5.0)
User: Apple Silicon CI felt slow; asked to skip it unless needed; asked for wrapping a
texture around a corner; asked to go straight on to milestone 5 and only report at
v0.5.0 (no v0.4.1 release).
- Measured: Apple Silicon jobs *run* in 20-50 s but *queue* 5-10 min (once 15). They only
  duplicated the Intel build + core tests. Removed from per-push CI; on-demand
  `.github/workflows/apple-silicon.yml` (workflow_dispatch) kept. Releases now carry the
  Intel zip only (INSTALL notes + README updated).
- `core/texture::wrapTexture`: folds the source face's axes about the shared edge
  (rotation that maps one normal onto the other), copies texture/scale/rotation, sets
  offsets so texels match along the edge. Tests: pillar +X -> +Y -> -X continuity at
  several heights, axes in the new plane, non-adjacent refused.
- Texture tool: Option+Shift+click = wrap from the last clicked face onto this one (then
  it becomes the start: walk around a pillar). Face panel / Texture menu **Wrap around**:
  from the start face through all connected picked faces (BFS, one undo step).
- Tests: core 579 checks, 0 failed; new UI script `m4_wrap` PASS; screenshot
  `m4-06-wrapped-corner`.

### 2026-11-16 — Milestone 4: Shaping (v0.4.0)
- `core/csg`: `brushVolume`, `removeUnusedFaces`, `faceFromPoints`, `splitBrush` (plane
  through 3 points, drops slivers < 0.001), `carveBrush` (subtract a convex cutter: split
  by each cutter plane, keep outside pieces), `hollowBrush` (carve a copy shrunk by the
  wall thickness; refuses if too thin). `Document::makeChange` (modify + add + delete
  as one undo step).
- **Clip tool** (Shift+X; again cycles keep both/front/back; Tools menu): drag a line in
  a 2D view -> plane straight into the screen; three numbered point handles (green 1,
  blue 2, yellow 3) draggable in any 2D view; point 3 in another view tilts the plane;
  Tools -> Clip points... to type them; preview shows only kept pieces; Return applies
  (cap faces get the brush's own texture); Esc cancels.
- **Carve with selection** (Cmd+Shift+C) and **Hollow...** (Cmd+Shift+H, thickness dialog
  with quick buttons). Delete/Backspace now works in every tool (found by the UI test:
  it only worked in the Select tool).
- Process slip: committed CMakeLists referencing `csg.cpp` before committing the file ->
  one red CI run; fixed by the next commit. Always `git status` before committing.
- **Tests:** core **570 checks, 0 failed** (split straight/miss/diagonal with volume
  preservation, carve tunnel volumes + no overlap + no-op + fully inside, hollow 6 walls
  with exact volume + textures + too-thin refusal, makeChange undo/redo). UI
  `m4_shaping` (2-point clip both, 3-point clip front = exactly half volume, hollow,
  carve + delete cutter) and all older scripts: **PASS at 1x and 2x**.
- Screenshots: `m4-01-clip-preview`, `m4-02-three-point-clip-preview`,
  `m4-03-three-point-clip-result`, `m4-04-hollow`, `m4-05-carved`.

### 2026-11-16 — v0.3.1: nodraw, modifier swap, native file dialogs
User feedback on v0.3.0: works (incl. save/load); wants a don't-draw texture for faces
that renders/exports must skip; in the texture tool Cmd+click should take the whole brush
and Shift+click add/remove single faces; file navigation instead of typing paths.
- `nodraw` built-in texture (`assets/textures/nodraw.png`, ours, CC0): yellow, labelled,
  striped. `core/texture`: `kNoDrawTexture`, `isNoDraw()` (also accepts
  `tools/toolsnodraw`, `common/nodraw`). Texture → Apply nodraw. ARCHITECTURE: every
  render/export except `.map` must skip nodraw faces.
- Texture tool: Cmd+click = whole brush (Cmd+Shift+click adds it), Shift+click =
  add/remove one face; 2D Shift+click adds. Help/README/hints updated.
- Native dialogs: `src/platform/file_dialog_mac.mm` (NSOpenPanel / NSSavePanel, AppKit),
  stub elsewhere (falls back to the typed-path dialogs). Used by Open, Save as, the
  unsaved-changes "Save", and Texture folders → Browse. **Opened only once no modifier
  key or mouse button is held**, because the system panel swallows key-up events and Cmd
  would otherwise stay "stuck" (next click = Cmd+click). File → Open by typing a path...
  kept (goes through the unsaved-changes check). Automated runs never use native panels.
- **Tests:** core 499 checks, 0 failed. UI scripts all PASS at 1x and 2x
  (`m3_textures_apply` now covers Cmd+click brush, Shift+click toggling, nodraw).
- Screenshot: `m3-06-nodraw-floor-top`.
- CI failure on first push: `enable_language(OBJCXX)` inside `if(SCAL_BUILD_APP)` after
  the dependencies -> "CMAKE_OBJCXX_COMPILE_OBJECT not set". Fixed by enabling OBJCXX right
  after `project()`. (Can't build macOS locally; CI is the compile check for `.mm`.)

### 2026-11-16 — Endless 2D grid + Milestone 3: Textures (v0.3.0)
- **Endless grid** (user request): 2D grid drawn over the whole visible range, views can
  pan to ±1M units; red square kept as a reference for the classic ±4096 area; brush
  clipping start polygon enlarged so brushes far outside work (core test). 3D grid
  unchanged.
- **Textures from the user:** Tile_19 and Bricks_24 (Screaming Brain Studios, CC0),
  stored in `assets/textures/` as 8-bit RGB (from 16-bit RGBA, no visual change) with
  their license; bundled into the app (macOS `Contents/Resources/textures`, Linux next to
  the binary); listed in `docs/THIRD_PARTY.md`. CI checks the bundle contains them.
- `core/texture`: Valve 220 UVs, fit (n x m), justify L/R/T/B/C, align to world/face,
  rotate, offset wrapping (no "-0"), ray -> face picking.
- `render/texture_library`: built-in + user folders (recursive; PNG/TGA/JPG/BMP; name =
  file stem, spaces -> `_`; user folders override built-ins), lazy GPU loading with
  mipmaps, crisp (nearest) or smooth filtering, magenta checker for missing textures.
  User folders saved in the settings folder (`texture_folders.txt`), never in the repo.
- 3D view: textured (default) or flat colours (View menu); selected brushes tinted;
  texture tool hides brush tint and highlights picked faces (tint + yellow outline).
- UI: right-hand **Textures** panel (current texture, search by name, thumbnails,
  Apply, Add folder, Rescan) and **Face** panel (shift, scale, rotation ±90, fit with
  times, justify, align, apply, use this texture, texture lock). **Texture tool**
  (Shift+A): click / Cmd+click / Shift+click whole brush / Option+click eyedropper.
  **Texture menu**: apply (Shift+T), tool, texture lock, default scale (0.25 default),
  filtering, add folder, rescan. New brushes use the current texture + default scale.
- Layout v3 (right panel column). This broke the old pixel-based UI scripts (the old
  layout had unequal view heights, 449 vs 407); converted all scripts to world
  coordinates (`wpress`/`wdrag`/`press3d`) computed from the old geometry — now
  layout-independent.
- **Tests:** core **497 checks, 0 failed** (+ texture maths, far-away brushes). UI:
  `m1_brushes`, `m2_precision`, `m3_textures` (endless grid), `m3_textures_apply`
  (textured room, pick face, rotate + fit, eyedropper, Shift+click brush, apply, undo,
  flat mode, save/open), `v011_transform_menus` — **all PASS at 1x and 2x**.
  Idle: **0 ticks / 10 s**.
- Screenshots: `m3-01-endless-grid`, `m3-02-past-the-border`, `m3-03-textured-room`,
  `m3-04-face-picked-rotated-fitted`, `m3-05-flat-mode`.
- Process note: I used `git stash` once to compare builds — risky with uncommitted work
  (it came back fine). Don't; compare with a separate clone or printed values instead.

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
