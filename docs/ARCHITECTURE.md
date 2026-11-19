# Scalpellino — Architecture & Design

A small, fast, brush-based 3D map editor in the spirit of Valve's Hammer, for making
Half-Life / Counter-Strike–style scenes. You build a small scene, place a camera and
render a picture in a Source / Y2K look. Pictures are the output; other engines are not a
goal.

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
7. **No gameplay objects.** No doors, triggers, physics or scripting. The editor makes
   geometry, lights, static props and looks. Game logic belongs in the game engine.

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
from the Camera Preview and the renderer (see §9).
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
  meshes (M13): fine triangles in `.glb`.

### Props (milestone 10, done)
Static models next to brushes, lights and primitives. `core/model.hpp`, `core/prop.hpp`.
- **Model:** triangles only (animations, skins, cameras and lights are ignored), several
  materials (base colour, optional texture, cutout), bounds. glTF Y-up metres become Z-up
  cm: `(x, y, z) -> (x, -z, y) * 100` (a proper rotation, winding kept). The model's own
  origin is its pivot.
- **Prop:** `{id, model name, pos, rot, scale (3), other keys}`; world = `pos + R * (scale *
  local)`, R as for primitives. It stores only the model's name (`models/tree`), so a map
  opens without the file: the prop then shows as a box and is kept on save.
- **Names:** from Bottega's `models/`: a folder's only model file is that folder's model;
  with several files the one named like the folder is; else each file is its own model;
  loose files are models. Spaces become `_`. `.mazz` will be a second reader behind
  `ModelStore` once Mazzuola's format is final.
- **Drawing:** a model is uploaded once (mesh chunks of at most 65535 vertices). Since
  v0.10.3 the props in view are grouped by model and each mesh chunk is drawn once for
  all of them (GPU instancing: each prop's matrix is an instance attribute); a plain
  one-call-per-prop shader is the fallback if a GPU refuses the instanced one. Props
  outside the 3D view's frustum are skipped; 2D views skip brushes, primitives and props
  outside the view. Editor shading, alpha cutout and fog in one shader; no light preview
  yet. 2D views show the turned bounding box and an origin cross; 3D picking uses the
  triangles. Snap to Land only tests obstacles under the landing object's footprint.
- **Benchmark** (development tool, `tests/bench/run.sh`, see DEV.md): fills a map with
  props and times scene building, 3D / 2D drawing, picking and Snap to Land.
- **Editing:** the shared object handles (rings, size handles; even scaling by default,
  Shift for one axis), Snap to Land / Ground (landing works on any "subject": lifted copies
  and ray tests on triangles), Duplicate, Delete, Multi-Replicate. All through commands.
- **File:** a `scal_prop` point entity: `model`, `origin`, `rotation`, `scale` (3 numbers),
  other keys kept.

### Snap to Land / Ground and Multi-Replicate (v0.9.2, from Bryce)
- **Snap to Land** (Cmd+J, Transform menu; `core/landing`): selected primitives drop
  straight down onto visible brushes (terrain surfaces, not their flat faces), terrain
  and other primitives; a combination lands as one; objects partly sunk in come up.
  Rays go down from points on the object's surface and up from brush corners, terrain
  points and other primitives' surfaces. Brushes themselves are never moved by it (props
  will land like primitives). **Snap to Ground**: lowest point to a typed height.
  v0.10.2: only surfaces below the object's middle height count as "below" (so it drops
  to the first thing beneath it, comes up out of what it is sunk into up to half its
  height, and ignores a shelf over it or a neighbour it overlaps), and upward rays only
  start from top surfaces. Several selected objects land lowest first; one lands on
  another selected object only if it started entirely above it. Props rest on points of
  their mesh (vertices, the lowest ones, and points across their faces), never on their
  box corners.
- **Multi-Replicate** (Cmd+Shift+D, Edit menu; `core/replicate`): N copies, each one step
  further: offset (optionally scaled), turn (X, Y, Z degrees) and scale (%) per copy,
  about the selection (travelling with the copies), the world origin or a typed point.
  Brushes only move (they stay on the grid), primitives turn and scale, lights turn.
  Groups and combinations are copied as new ones. Live preview in a non-modal window (the
  views stay navigable), Return creates, Esc cancels, one undo step.
- **Random Replicate** (Cmd+Shift+R, Edit menu; `core/replicate`, v0.10.1): N copies
  scattered around the selection. Each copy gets its own offset (-range .. +range per
  axis), turn (-range .. +range degrees about X, then Y, then Z) and size (from / to %,
  the same on all axes unless "each axis its own size"), drawn from a seed. The selection
  moves, turns and sizes as one piece about its centre. Brushes only move, by whole grid
  steps while snapping is on; primitives and props also turn and size; lights also turn.
  The numbers come from our own SplitMix64 (not the standard library's distributions,
  which differ between compilers), and copy i depends only on the seed and i: the same
  seed gives the same copies on every machine, more copies keep the earlier ones, and a
  wider turn range does not move the offsets. Same window behaviour as Multi-Replicate
  (only one of the two open at a time); the copies stay selected, so Snap to Land drops
  them onto the land.

### Selecting and showing (v0.10.2)
- **Rectangle select in the 3D view** (Select tool): a drag that doesn't start on a handle
  or ring draws a rectangle; on release everything that lies wholly inside it is
  selected: visible brushes (all corners; groups whole), primitives (points on their
  surface; combinations whole), props (points on their mesh) and lights (their position).
  Wholly inside, so the floor or terrain under everything isn't swept up. Cmd+drag adds;
  Esc during the drag keeps the selection from before the press.
- **View → Show primitives / Show props**: view options kept in the Document (not saved,
  not undone). Hidden ones are not drawn, picked, selected or landed on; hiding drops them
  from the selection; Show all (Cmd+U) and adding new ones show them again.
- **Unsaved changes** follow the undo history: each command gets a serial number, and
  the map is unsaved when the newest command isn't the one it was saved at (or a
  visgroup, bookmark or the figure, which aren't undoable, changed since the save).

### Scale reference
Toggleable ghost human figure (175 units = 1.75 m tall, blocky, see-through) to judge
proportions. Its feet position is saved with the map (World → Scale figure position, or
it appears below the top view's centre). Editor-only, never exported.

## 7. Entities (complete list)

- **Lights** — point, spot, sun.
- **Math primitives** — exact shapes, combinable (milestone 9).
- **Props** — static models (`.glb` and Mazzuola's `.mazz`) with their textures; placed,
  turned and sized like primitives. No physics (milestone 10).
- **Cameras** (planned) — objects placed and aimed like a spot light, with many options
  and presets: lens (field of view or mm, roll, near / far distance, orthographic mode),
  image size, aim at a point, depth of field and exposure later; the user can save their
  own presets (saved in Bottega's `presets/camera_presets`). Full list in PLAN.md. A map can hold several; exactly one is
  **active**, the only one the Camera Preview and Render use.
- **Reflection probes** (planned) — placed by hand; Render bakes a picture of their
  surroundings for shiny materials to reflect.
- **Environment** — sky color/texture, fog, ambient light.
- **Camera bookmarks** — saved viewpoints of the editor's 3D view, for moving around.
  Editor-only, and not the same thing as the camera objects.

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
  milestone (likely: terrain as a mesh in `.glb`).
- **Native: Valve 220 `.map`.** Plain text, readable by func_godot and TrenchBroom.
  Editor-only data (visgroups, camera bookmarks, curve-generator parameters) stored as
  extra keys other tools ignore.
- **Props** (v0.10.0): `scal_prop` point entities with `model` ("models/tree"), `origin`,
  `rotation` (degrees X Y Z), `scale` (3 numbers) and any other keys, kept as they are.
- **nodraw:** faces textured `nodraw` (also recognised: `tools/toolsnodraw`, `common/nodraw`)
  are shown in the editor but skipped by every render and export (Camera Preview, PNG
  renders, `.glb`). The `.map` export keeps them, as other tools expect.
- **Textures:** built-in CC0 textures inside the app + the user's folders of PNG/TGA/JPG/BMP
  (name = file name without extension) + the Bottega's `textures/` (below). Half-Life `.wad`
  support later.
- **Bottega** (built, v0.9.4): one folder shared by Mazzuola, Scalpellino and later tools.
  A normal folder anywhere; each tool asks once, in its settings, where it is (File >
  Bottega folder...). A marker file `bottega.txt` at its root holds the format version
  (`bottega 1`); a newer version is left alone. Sub-folders: `textures/`, `models/`,
  `maps/`, `palettes/`, `skies/`, `shaders/`, `renders/`, `presets/` (camera presets in
  `presets/camera_presets`). **Never rescanned automatically** (idle stays free): File >
  Rescan Bottega (Cmd+R) or the Textures panel's Rescan button. Search order: Bottega,
  the user's extra folders, then the built-ins; the first match wins.
  **Names in maps:** things from the Bottega go by their path relative to the root, without
  extension, spaces as `_` (`textures/walls/Brick_Red`, `skies/Dusk`; models later). Built-in
  textures and the user's extra folders keep plain names (`Bricks_24`). A name that can't be
  found (Bottega not set, file gone) stays in the map and the texture shows as missing.

## 9. Rendering

Scalpellino exists to make pictures: you build a small scene and render it from a camera.
**Nothing moves or animates in the editor**; shaders and heavy effects show in the Camera
Preview and in renders.

- **2D views:** wireframe, redrawn only on change.
- **3D view modes:** wireframe, flat-color, textured, plus the rough light preview, sky and
  fog toggles (milestone 8). Materials show as a simple placeholder here (planned).
- **Camera Preview** (planned): a small window showing the active camera's view, only when
  the map has a camera. An approximation of the final picture, in between a full render
  and the editor view, heavy stuff and shaders included. Updated **only on a keybind or
  button**, never live.
- **Render** (planned): the Render button opens the render window (image size and render
  options). A terminal-style log shows the steps as they run (baking the lights, building
  the reflection cubemaps, drawing...); it takes as long as it needs. The result is a PNG
  saved to `renders/`. Only what the camera sees is drawn (frustum, back faces, nodraw).
- **The Source look** comes from **how Source renders**: fast, on the graphics card, with
  baked lightmaps (light, shadows, bounce), baked reflection probes, real textures and
  fog. It is **never faked with filters** laid over the picture (user: that is not the
  point). Not a ray tracer.
- **Source features** (user's choice from Source's feature list): radiosity lightmaps with
  several bounces, a baked light grid for primitives and props, reflection probes, water
  with reflection / refraction / Fresnel, materials with specular, emissive, detail
  textures and multi-texture blending, alpha cutouts, fog, rim lighting, HDR lighting as a
  later refinement. Bloom and colour correction are optional extras. Nothing dynamic
  (shadows of moving things, particles, motion blur): nothing moves.
- **Reflections:** a material chooses: sky only; baked reflection from the nearest
  probe; live mirror reflection on flat surfaces (water, floors). Probe quality is a
  render-option preset.
- **Materials and shaders:** a surface's look is its material (texture, reflection,
  transparency, shader), built into the renderer from the start. Shaders are GLSL files
  with a settings header, dropped into Bottega's `shaders/`; they run in renders and the
  Camera Preview. Water, clouds and endless planes are materials made with it. Optional
  camera effects (dither, palette, bloom) are separate from the look and matter least.
- **Maybe later:** a ray-tracing renderer (Bryce look: real reflections on any shape,
  bending glass). Not pressing; the Source renderer may be enough.

## 10. Output and export

**Pictures are the output.** The user probably will never open exports in another editor,
so export is not a goal now and Godot never drives choices. A `.glb` export (terrain and
primitives as meshes, nodraw skipped) is possible last, at milestone 13. A walkthrough tool
would be a separate tool in the far future.

## 11. Undo

Each command stores before/after copies of the brushes/entities it touches. Simple,
robust, cheap (brushes are tiny).

## 12. Dependencies

Fetched automatically by CMake, versions pinned:
- raylib — window, input, drawing
- Dear ImGui (docking branch) + rlImGui — UI
- cgltf — reading `.glb` models (vendored in `third_party/cgltf`, MIT, v1.15; later also glTF writing)
- stb_image — textures (bundled with raylib)
- Lua — later, for brush generator scripts

## 13. Repo layout

```
src/core/     brushes, entities, document, commands, Bottega folder logic (no raylib)
src/io/       map reader/writer, glb import/export, textures
src/render/   viewports, render modes, camera preview, shaders/
src/tools/    select, block, clip, vertex, carve, curve, texture, light, camera
src/ui/       panels, menus, shortcuts
src/app/      main loop, settings, startup
              (the Editor class is split by topic over editor*.cpp; see editor.hpp)
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
| 10 | Props: static models (`.glb` first, then `.mazz`) with their textures, from Bottega's `models/`, placed / turned / sized as objects (uniform scale by default), Snap to Land, instanced; no physics. Preceded by the idle CPU test and Bottega basics (folder setting, Rescan, marker file) | done (v0.10.0; `.mazz` later) |
| 10b | Random Replicate (Bryce): random offset / turn / size ranges for props and primitives | done (v0.10.1) |
| 10c | Sky Lab (Bryce, all of it): generated skies (sun, moon, stars, clouds, atmosphere) with live preview, baked to panoramas; settings kept in the map; tick box to lock the sun to the map's sun light | |
| 11 | Cameras and Camera Preview (proposed order): camera objects (many, one active), options and presets, the on-request preview | |
| 12 | Render, the Source look, with materials (proposed): render window, terminal log, light baking, reflection probes, mirrors, the material system (GLSL shaders from Bottega's `shaders/`, reflection kind per material), PNG to `renders/` | |
| 13 | Water, cloud and endless planes (proposed): materials made with the system above | |
| 14 | Camera effects (proposed, lowest priority): dithering, palette, bloom | |
| 15 | Export: `.glb`, optional and last | |
| later / maybe | ray-tracing renderer (Bryce look), `.wad` textures, Lua generators, texture tags | |
