# Scalpellino — Architecture & Design

A small, fast, brush-based 3D map editor in the spirit of Valve's Hammer, for making
Half-Life / Counter-Strike–style levels with an optional Y2K render look, and exporting
them to Godot and other engines.

Personal tool. Not distributed. Simplicity and speed beat features.

---

## 1. Guiding rules

1. **Idle = zero work.** The app redraws and recomputes only when something changes
   (input, window resize, an edit). No continuous 60 fps loop when nothing happens.
2. **The map file is the truth.** Meshes, previews and caches are always rebuilt from it.
3. **The core has no graphics.** Brush math, file IO and undo live in a plain C++ library
   with no window or GPU dependency, so it can be tested headless on CI.
4. **Everything is undoable.** Every edit goes through one command path.
5. **Single-threaded first.** Heavy jobs (light baking) may move to a background thread later.
6. **No Hammer quirks.** Keep what was good, drop what was clunky (see §6).
7. **No gameplay objects.** No doors, triggers, props or other game logic entities.
   The editor makes geometry, lights and looks. Game logic belongs in the game engine.

## 2. Platform

- **macOS, Intel first**, Apple Silicon also built and tested.
- **Keyboard: Italian layout assumed.** Shortcuts are read by physical key position where
  it matters (fly-mode WASD, grid keys), so they work regardless of layout. No shortcut may
  require a character that needs Option on an Italian Mac keyboard (`[ ] { } # @ ~` etc.).
  Mac conventions: Cmd for app commands (Cmd-S, Cmd-Z, Cmd-G).
- CI builds on GitHub-hosted macOS runners (`macos-15-intel` and Apple Silicon) and produces
  a downloadable `.app` zip per push.

## 3. Privacy rules (for every contributor and every AI session)

- Never put machine details in the repo, commits or CI logs: no hostnames, usernames,
  home paths, OS versions, hardware details.
- Commits are authored as `Claude <noreply@anthropic.com>`. No links, no session URLs,
  no trailers pointing anywhere.
- Personal settings (recent files, layout, texture folders) live in the app's settings
  folder on the user's machine, never in the repo.
- Pasted logs are scrubbed of paths and usernames before anything is committed.

## 4. Layers

```
┌──────────────────────────────────────────────┐
│ UI  (Dear ImGui, docking)                    │  toolbar, texture browser,
│                                              │  properties, face edit, panels
├──────────────────────────────────────────────┤
│ Tools                                        │  select, block, clip, vertex,
│                                              │  curve, texture, light, camera
├──────────────────────────────────────────────┤
│ Render (raylib)          │ IO                │  viewports, render modes,
│                          │                   │  Cam. Prev. │ .map, .glb, export
├──────────────────────────────────────────────┤
│ Core  (no graphics, fully tested)            │  math, brushes, entities,
│                                              │  document, commands/undo
└──────────────────────────────────────────────┘
```

Each layer only talks to the one below it.

**Edit flow:** input → active tool builds a command → command mutates the document and
marks affected brushes dirty → only dirty brushes rebuild geometry/meshes → one redraw.

## 5. Core data model

- **Brush** — convex solid stored as a list of planes (Quake/Hammer style). Each face has a
  texture name and alignment (shift, scale, rotation, texture axes — Valve 220).
  Face polygons are computed from planes and cached.
- **Entity** — classname + key/value pairs. Point entities (lights, camera bookmarks) or
  the world (entity 0, holds all brushes). No gameplay entities.
- **Group** — a set of brushes. A group may carry **generator parameters** (see curve tool),
  making it regenerable until baked.
- **Document** — entities, groups, visgroups, selection, undo stack.
- **Precision** — doubles in core math, floats for rendering. Vertices snap to grid to
  avoid leaks and micro-gaps.
- **Units / axes** — 1 unit = 1 cm (decided with the user, v0.9.2; before that Hammer's
  inch), a person ≈ 175 units tall, Z-up in the editor. Exports convert to Y-up metres
  (÷ 100); imported `.glb` metres × 100.

## 6. Interaction design

### Viewports
Top (X/Y), front (Y/Z), side (X/Z), 3D. Dockable, resizable, maximisable. No mandatory
cameras; the 3D view never snaps back to the origin.

### Kept from Hammer
- Z toggles fly mode, WASD + mouse (physical keys — works on Italian layout).
- Grid halve/double on the two keys right of P (physical position of Hammer's `[` `]`;
  `è` and `+` on an Italian keyboard). Grid on/off and snap on/off toggles.
- Shift-drag duplicates.
- Cmd-G group / Cmd-Shift-G ungroup.
- Shift-click with texture tool paints all faces of a brush.
- Flat-color 3D mode (each brush its own color) to spot over-split geometry.
- Texture lock — **on by default**.
- Properties panel for the selection.

### Dropped / fixed
- No click-again-to-cycle resize → rotate → skew. Handles are always visible; a modifier
  switches mode.
- Brush tool: the brush exists as a live preview while dragging and is created with
  **Return**. Esc cancels.
- Rotation snaps to 15° by default; hold a modifier for free rotation.
- No WAD-disconnected / mixed-contents / ghost-brush style errors.
- No compile step inside the editor.

### Menus
Every shortcut and gesture is also available from the menu bar (File, Edit, Tools,
Transform, View, Help), with dialogs for typed values (e.g. Transform: exact move /
rotate / scale with pivot). Help → Controls lists all shortcuts.

### Visibility
Hide selected (H) / hide others (Shift+H) / show all (Cmd+U); visgroups (named, toggle
visible). Hidden brushes are not drawn, not pickable and skipped by Select all. Hiding is
undoable; visgroup visibility isn't (as in Hammer). Saved in the map.

### Tools
Select, Block (box, wedge, cylinder, cone, pyramid, sphere — each one convex brush), Clip, Vertex (done in M2), Carve (CSG subtract),
**Curve**, **Terrain** (M7), Texture apply / face edit, Light, Camera bookmark.

### Curve tool
Curves in the tool, flat faces in the result. Automatic slicing into convex brushes.

1. **Draw** an arc, circle or spline in a 2D view (arc: two clicks for the ends, drag for
   the bulge).
2. **Adjust** in a small panel next to the preview: segments, thickness, depth, plus
   shape-specific fields. Type values directly; Tab moves between fields; handles in the
   viewport adjust thickness/depth visually. The preview updates live.
3. **Return** generates. Esc cancels.

Shapes:
- **Arch** — curved underside sliced into segments, square top filled.
- **Carve arch** — draw the curve on an existing wall; cuts an arched opening and splits
  the remaining wall into clean convex brushes.
- **Ring / pipe** — full or partial circle with thickness (round towers, tunnel shells).
- **Curved wall** — a wall following a spline path.
- **Spiral stairs** — steps around a center with a set rise per step.

Guarantees: neighbouring slices share exact vertices (no cracks, no leaks); all generated
brushes are convex and grid-snapped where possible.

The generated group **remembers its parameters** and can be re-opened and regenerated
(e.g. 8 → 12 segments). **Bake** turns it into plain brushes for hand-editing.
In `.map` files a group is a `func_group` entity whose parameters are `_scal_*` keys;
Quake-family tools (TrenchBroom, func_godot) merge func_group into the world and ignore
the extra keys. Clicking any brush of a group selects the whole group. A group whose
last brush is deleted disappears with it (undoable). If a group was moved, editing it
follows the move; other hand edits are replaced when regenerating (bake first to keep them).
**Carve arch** cuts a rectangular hole and refills the corners above the curve with arch
slices (clean pieces instead of Hammer-style carve shards).

### Terrain (milestone 7, done; reworked in v0.7.1)
Like Hammer's displacements, with fewer rules.
- **Data:** a terrain is one face of an ordinary brush (`Brush::terrain`), with 3 or 4
  corners. The face becomes a grid of 2^power cells per side (2 to 32). Each grid point
  has a height along the face normal. The grid starts at the face corner nearest a
  stored start point, which follows moves, rotations and mirrors. A triangle is laid out
  like a quad whose far corner is the middle of the edge opposite the start (so the grid
  covers it exactly and mirroring still just transposes the grid); a new triangle
  terrain starts opposite its longest edge, so two triangles sharing that edge match.
- **Paint:** up to 7 painted textures per patch (`Terrain::layers`), on top of the
  face's own texture. Each layer has a share per grid point (0..1); the shares add up to
  at most 1 and the face's texture shows the rest. Painting a texture moves its share up
  and every other share down by the same factor, so the result doesn't depend on the
  order textures were painted. Drawing converts shares to "over" transparencies, one
  pass per layer. Layers with no paint left are dropped after a stroke.
- **Making:** Terrain → Make terrain switches the terrain tool to picking faces: any face
  in the 3D view (Shift+click adds or removes), or in a 2D view the face of the clicked
  brush that looks toward the viewer. One face per brush. Grid size in Properties,
  Return makes them, Esc stops. Faces picked with the Texture tool also work. Terrain →
  Terrain grid size changes the selected terrain's grid (shape and paint resampled).
- **Editing** is one terrain tool (Shift+G) with modes: raise, lower, smooth, flatten (to
  the stroke's start height), noise, paint, erase paint. Radius, amount or strength, and
  falloff are typed in the Properties panel. A circle on the surface shows the radius.
  Strokes work in the 3D view or in a 2D view facing the terrain. Dabs land every fifth
  of the radius, so speed doesn't matter. One drag is one undo step. A dab reaches points
  within the radius across the face (flat distance) and only surfaces within the radius
  of the dab along their normal, so a wall beside a floor is left alone.
- **Point by point:** the vertex tool shows the grid points of selected terrain (small
  handles, also in the 3D view, the only handles picked there). Dragging moves them along
  the normal, snapped to the grid; in 2D only views that see the normal side-on change
  anything. Typed: raise by / set height (Cmd+Shift+T). Brush corners win over terrain
  points on the same spot in 2D.
- **Stitching:** grid points of different patches at the same spot (same flat position,
  same normal) are treated as one point by every operation. Neighbours never crack. Edges
  only match where the grid points meet, so patches sharing an edge need the same grid
  size along it. "Stitch neighbouring edges" repairs edges that drifted apart (paint is
  averaged per texture over the patches that have it).
- **Heightmap import:** a grey image (black = low, white = high, both typed) is laid over
  the selected terrain as seen along its normal.
- **Brush edits:** move, rotate, mirror and scale keep the terrain, and heights scale
  along the normal. Vertex editing and snap to grid keep it if the terrain face is still
  one face with the same (moved) corners; otherwise it becomes a plain brush. Clip,
  carve and hollow always give plain brushes (undo restores the terrain).
- **Files:** `//@ terrain face=… power=… start=x,y,z` plus one `//@ th` line of heights
  per grid row; then for each painted texture `//@ tl <texture>` and its `//@ ta` lines
  of shares. v0.7.0 files (`tex2=` in the header, `ta` lines without `tl`) still load.
  Other tools see the plain brush.
- **Renders/export (later):** the terrain surface is the visible geometry. The brush's
  other faces count as hidden, as with Hammer displacements.

### World (milestone 8, done)
The editor shows only a rough idea of the world, like Hammer did; the real look comes
from the renderer (M11) and the Camera Preview look (M12).
- **Lights** (`core/world.hpp`, `Document::lights()`): point, spot, sun. Colour (0–255),
  brightness (Hammer-style, 200 = normal), reach (point, spot), direction as pitch/yaw
  (spot, sun; pitch −90 = down), inner/outer angles (spot). A light is a marker in every
  view in its own colour (square; a sun is a circle), a spot has a wire cone, a sun an
  arrow; a selected light shows its reach (2D circle, 3D three great circles).
- **Placing:** the Light tool (Shift+L, World menu). Kind and settings in Properties, click
  in a 2D view (the depth stays from the last click / the last brush) or on a surface in
  3D (32 units out), click again to adjust, Return creates, Esc cancels.
- **Editing:** lights are selected separately from brushes (a plain click on one
  deselects the other), in the Select and Light tools: click a marker; drag it in 2D to
  move (snapped); Cmd+click adds/removes; Delete, Cmd+D. **Rotation rings** around a
  selected spot/sun: three in 3D (X red, Y green, Z blue), one per 2D view (around the
  depth axis). Dragging a ring turns the direction about that world axis in 15° steps.
  Properties edit every selected light (one undo step per finished field). All light
  edits are commands (`LightChange`), so undo/redo cover them.
- **Environment** (World → Environment, a command too): sky name, ambient colour, fog
  (on/off, colour, clear-up-to and fully-fogged-from distances). Sun direction is typed
  on the sun light, separate from the sky (user's choice).
- **Skies** (`core/sky.hpp`, `render/sky_library`): built-in panoramas plus the user's sky
  folders (saved in the settings like texture folders). 2:1 equirectangular panoramas
  or 4:3 horizontal cube crosses; a cross is converted to a panorama on load (mapping
  checked against a pack's matching panorama/cross pair). The panorama's middle faces
  +X; turning right moves right in the image.
- **View menu toggles** (3D view only, editor settings, not saved): sky (on: a sphere
  behind everything), **rough light preview** (off; ambient + lights, Lambert with a
  smooth falloff to the reach and the spot cone, no shadows; faces cut into ~64-unit
  triangles; computed only while on and only when the map changes), fog (off; a small
  shader fading to the fog colour by distance, only if the map has fog), scale figure.
- **Camera bookmarks** (World menu): name + 3D camera position/yaw/pitch; go / delete.
  Editor-only, saved with the map, not undone (like visgroups).

### Math primitives (milestone 9, done)
Bryce-style exact shapes, next to brushes (brushes and low-poly shapes stay as they
are; brush tools never touch primitives). `core/mathprim.hpp`.
- **Kinds:** sphere, cube (optional rounded corners in units), cylinder, cone, torus (tube
  thickness), capsule, disc. Each is a unit shape inside [-1, 1]³ of its own axes, placed
  by position, rotation (degrees about X, Y, Z: R = Rz·Ry·Rx) and size (full extents
  along its own axes; any per-axis size, so a sphere can be an ellipsoid).
- **Drawn exactly:** the 3D view draws each shape's box and a shader finds the true
  surface per pixel (same maths as the core, which picks and tests), writing depth so
  brushes and primitives cut into each other. Texture mapped by kind (spherical,
  cylindrical with planar caps, box, torus angles) × repeats, tinted by a colour, or a
  flat colour. Light preview and fog are done in the same shader. 2D views show outline
  wires (lilac; subtract pieces orange, intersect pieces teal).
- **Combining (Bryce):** primitives with the same combination number form one shape:
  add pieces join, subtract pieces cut away (the cut shows the cutter's texture),
  intersect pieces keep only the overlap. Per-ray stretches inside each shape are joined,
  cut and intersected. Clicking selects the whole combination; Option+click (or the
  list in Properties) picks one piece, which then moves, turns and sizes on its own. Combine / Separate in the Primitives menu or Properties.
- **Blending (v0.9.2):** a piece's `blend` (units) melts its joint into the rest of the
  combination: smooth necks between add pieces, soft rims on cuts, soft edges on
  intersections (polynomial smooth min / max). Blended combinations use distance
  functions (exact for spheres and rounded cubes, safe underestimates for stretched
  shapes) and ray marching, in the core (picking, tests) and the shader; colours and
  textures melt across the joints over the combination's widest blend. Sharp
  combinations keep the exact path.
- **Objects:** primitives are objects like lights (`tools/object_edit`): click to select
  (brushes and lights deselected), drag in 2D to move (snapped), rings around the
  selection to turn (15° steps), size handles on a single primitive's faces (opposite
  face stays; Shift = evenly about the centre; sizes snap). Typed position, rotation,
  size, rounding, thickness, texture, colour, repeats in Properties. Delete, Cmd+D
  (copies of a combination form a new one), undo for all.
- **Creating:** the Primitive tool (Shift+M, Primitives menu): kind and settings in
  Properties, drag a box in a 2D view (depth from the last box; drag in another view to
  set the rest), or type centre and size; Return creates, Esc cancels. A disc lies across
  the axis of the view it was drawn in.
- **Files:** saved as our own point entity `scal_primitive` (kind, origin, rotation,
  size, round, thickness, combo, op, texture, color, texscale; unknown keys kept), so a
  map reopened in Scalpellino keeps them as primitives. Only exports turn them into
  meshes (M13): Godot's own round meshes where they match, fine triangles in `.glb`.

### Snap to Land / Ground and Multi-Replicate (v0.9.2, from Bryce)
- **Snap to Land** (Cmd+J, Transform menu; `core/landing`): selected primitives drop
  straight down onto visible brushes (terrain surfaces, not their flat faces), terrain
  and other primitives; a combination lands as one; objects partly sunk in come up.
  Rays go down from points on the object's surface and up from brush corners, terrain
  points and other primitives' surfaces. Brushes themselves are never moved by it (props
  will land like primitives). **Snap to Ground**: lowest point to a typed height.
- **Multi-Replicate** (Cmd+Shift+D, Edit menu; `core/replicate`): N copies, each one step
  further: offset (optionally scaled), turn (X, Y, Z degrees) and scale (%) per copy,
  about the selection (travelling with the copies), the world origin or a typed point.
  Brushes only move (they stay on the grid), primitives turn and scale, lights turn.
  Groups and combinations are copied as new ones. Live preview in a non-modal window (the
  views stay navigable), Return creates, Esc cancels, one undo step.

### Scale reference
Toggleable ghost human figure (175 units = 1.75 m tall, blocky, see-through) to judge
proportions. Its feet position is saved with the map (World → Scale figure position, or
it appears below the top view's centre). Editor-only, never exported.

## 7. Entities (complete list)

- **Lights** — point, spot, sun.
- **Math primitives** — exact shapes, combinable (milestone 9).
- **Props** — static models (`.glb` and Mazzuola's `.mazz`) with their textures; placed,
  turned and sized like primitives. No physics (milestone 10).
- **Environment** — sky color/texture, fog, ambient light.
- **Camera bookmarks** — saved viewpoints for renders. Editor-only.

Lights are saved as Hammer entities other tools understand: `light`, `light_spot`,
`light_environment` with `origin`, `_light "r g b brightness"`, `angles` (pitch positive
down) + `pitch` (negative down), `_inner_cone` / `_cone`, `_zero_percent_distance`
(reach). Keys we don't use are kept. Environment, bookmarks and the scale figure are
worldspawn keys: `_scal_sky`, `_scal_ambient`, `_scal_fog "on r g b start end"`,
`_scal_cameras "x y z yaw pitch name;…"`, `_scal_figure`.

That's all. No gameplay entities (doors, triggers, physics, scripting).

## 8. File formats

- **Our own extended `.map`** (decided with the user): one file holds everything — brushes,
  generated groups, visgroups/hidden state, and later terrain — while staying a valid
  Valve 220 `.map` that other tools can still open. Extra data lives where other tools
  ignore it: `_scal_*` keys, `func_group` entities, and `//@` metadata comment lines inside
  brush blocks. How terrain and other extras go to other apps is decided at the export
  milestone (likely: terrain as a mesh in `.glb` / the Godot package).
- **Native: Valve 220 `.map`.** Plain text, readable by func_godot and TrenchBroom.
  Editor-only data (visgroups, camera bookmarks, curve-generator parameters) stored as
  extra keys other tools ignore.
- **nodraw:** faces textured `nodraw` (also recognised: `tools/toolsnodraw`, `common/nodraw`)
  are shown in the editor but skipped by every render and export (Camera Preview, PNG
  renders, `.glb`, Godot package). The `.map` export keeps them, as other tools expect.
- **Textures:** built-in CC0 textures inside the app + the user's folders of PNG/TGA/JPG/BMP
  (name = file name without extension). Half-Life `.wad` support later.

## 9. Rendering

- **2D views:** wireframe, redrawn only on change.
- **3D modes:** wireframe, flat-color, textured, **Camera Preview** (the Y2K look).
- **Camera Preview pipeline** (each step toggleable): low-res render target (e.g. 640×480) →
  nearest-neighbor upscale → distance fog → per-vertex lighting from light entities →
  chrome / environment maps → bloom → dithering.
- **Render button:** saves a PNG still at a chosen resolution.
- **Baked lighting** (shadows, bounce) later, as an explicit on-demand bake.

## 10. Export tiers

**`.glb` is the export that matters.** The user expects Scalpellino will probably never
be used with Godot, so Godot must not drive design choices; tiers 1 and 3 stay ideas.

1. **`.map` for func_godot** — geometry + texture names. Materials set up in Godot.
2. **`.glb`** — geometry, UVs, embedded textures, nearest-filter samplers. Optional baked
   lighting as vertex colors or lightmap. Works in Godot, Unity, Unreal, Blender.
3. **Godot scene package** — `.tscn` + textures + the Camera Preview shaders ported to Godot shading
   language + matching Environment (fog, glow). Opens in Godot looking like the preview.
4. **Baked-look `.glb`** — for engines where shaders don't transfer: surface effects
   (lighting, chrome, dither) baked into textures. Camera effects (bloom, fog) can't be
   baked; the export includes notes on what to enable in the engine.

Plus PNG stills. Turntable animations maybe later.

## 11. Undo

Each command stores before/after copies of the brushes/entities it touches. Simple,
robust, cheap (brushes are tiny).

## 12. Dependencies

Fetched automatically by CMake, versions pinned:
- raylib — window, input, drawing
- Dear ImGui (docking branch) + rlImGui — UI
- cgltf — glTF writing
- stb_image — textures (bundled with raylib)
- Lua — later, for brush generator scripts

## 13. Repo layout

```
src/core/     brushes, entities, document, commands (no raylib)
src/io/       map reader/writer, glb export, godot export, textures
src/render/   viewports, render modes, y2k pipeline, shaders/
src/tools/    select, block, clip, vertex, carve, curve, texture, light, camera
src/ui/       panels, menus, shortcuts
src/app/      main loop, settings, startup
tests/        core + io tests, sample .map files
docs/         this file, roadmap
.github/      macOS builds (Intel + Apple Silicon) + tests
```

## 14. Testing

- Core and IO built as a library with unit tests, run on every push (macOS Intel,
  Apple Silicon, and Linux for speed).
- `.map` round-trip golden tests: load → save → identical output.
- Brush math tests: planes → polygons, CSG carve, curve slicing (convexity, shared
  vertices, no gaps).
- UI feel is judged by hand on a real Mac from the CI-built `.app`.

## 15. Roadmap

| # | Milestone | Status |
|---|-----------|--------|
| 0 | Skeleton | done (v0.1.0) |
| 1 | Brushes (+ v0.1.1 Retina, menus, Transform dialog) | done |
| 2 | Precision: vertex tool, snap to grid, pivot, Pythagorean angles, microgrid | done (v0.2.0) |
| 3 | Textures: library, browser, texture tool, face panel, nodraw, wrap | done (v0.3.x, v0.5.0) |
| 4 | Shaping: clip (2/3-point), carve, hollow | done (v0.4.0) |
| 5 | Curve tool: arch, ring, curved wall, spiral stairs, arched opening; groups | done (v0.5.0) |
| 6 | Workflow + shapes: hide/show + visgroups, overlap finder, views to origin, block primitives (wedge, cylinder, cone, pyramid, sphere), more curves (dome, helix ramp, straight stairs, stairs column) | done (v0.6.0) |
| 7 | Terrain: grid patches from any picked face (3 or 4 corners), sculpt (raise/lower/smooth/flatten/noise; typed radius/strength), point-by-point editing with the vertex tool, painting up to 8 textures per patch, auto-stitched neighbours, heightmap import; stored in our `.map` | done (v0.7.0, v0.7.1) |
| 8 | World: lights (point, spot, sun; rotation rings), sky picker (a setting, not a sealed box; panoramas and cube crosses), ambient, fog, rough editor preview (sky, light preview, fog toggles), camera bookmarks, scale figure | done (v0.8.0) |
| 9 | Math primitives (Bryce-style): sphere, cube (rounded), cylinder, cone, torus, capsule, disc; exact per-pixel drawing; combining (add / subtract / intersect) and blending; single pieces movable; object handles shared with lights; 1 unit = 1 cm; Snap to Land / Ground; Multi-Replicate | done (v0.9.0 … v0.9.3) |
| 10 | Props: static models (`.glb` first, then `.mazz`) with their textures, placed / turned / sized as objects; Snap to Land; reload when the file changes; no physics | next |
| 10b | Random Replicate (Bryce): random offset / turn / size ranges for props and primitives | |
| 10c | Sky Lab (Bryce, all of it): generated skies (sun, moon, stars, clouds, atmosphere) with live preview, baked to panoramas; settings kept in the map; tick box to lock the sun to the map's sun light | |
| 11 | Renderer: PNG renders, only what the camera sees (frustum, back faces, nodraw, optional outside-only), terminal-style render log window (+ log file); endless ground / water / cloud planes | |
| 12 | Camera Preview (was "Y2K"): shaders (low-res, fog, chrome, bloom, dithering) | |
| 13 | Export: `.glb`; Godot package only if asked for, terrain as mesh, primitives as meshes (Godot's round meshes where they match); nodraw skipped | |
| later | `.wad` textures, Lua generators, baked lighting, texture tags | |
