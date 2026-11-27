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

### Sky Lab (milestone 10c, done: v0.10.4 to v0.10.6; from Bryce)
Makes skies instead of only loading images. Agreed with the user 2026-11-19; full list in
PLAN.md (Roadmap item 3).
- **Settings** (`core/skylab.hpp`): everything Bryce's Sky Lab has, saved in the map as the
  worldspawn key `_scal_skylab` (unknown fields kept). The parts that make up the sky
  *picture* are generated now; the parts that light the *scene* (shadows, sun diffuse /
  specular, sky dome colour, cloud shadows, colour perspective, Volumetric World, fog and
  haze on objects, IBL) are stored for the renderer (M12).
- **Generation** in the core (no graphics): a pure function of the settings and the image
  size, giving an RGB panorama in the layout of `core/sky.hpp` (middle faces +X). Below the
  horizon the haze colour fades to a ground colour. Clouds are two layers of noise on
  shells over the planet (stratus above cumulus), each octave faded where it is finer
  than a pixel. Rows can be made on several threads; the result is the same. Stars and
  comets are placed once and drawn row by row (Earth's sky from a compiled-in catalogue);
  the moon is a lit ball with a texture handed in by the app (the core loads no images);
  the map's fog comes in with it (`SkyExtras`).
- **Baking:** Apply writes the panorama as a PNG to Bottega's `skies/` (Sky Lab asks for a
  Bottega if none is set) and sets the map's sky to it; one undo step. A missing PNG is
  rebaked from the map's settings.
- **Live preview:** while the window is open the 3D view shows the generated sky (small
  while dragging, ~1024x512 on release); nothing is computed while idle.
- **Sun lock:** with a sun light in the map, a tick box ties the sky's sun to it: moving
  the sun in Sky Lab turns the light (same undo step); turning the light marks the baked
  sky out of date, with a Rebake command. Directions: the sky's sun is where the light
  comes *from* (azimuth = light yaw + 180, altitude = −light pitch).
- **The moon moves freely**; phase is a slider; solar and lunar eclipses (PLAN.md).
- **Saved and built-in skies:** Save sky / Load a saved sky (the Bottega's
  `presets/skies`, `.skylab` text); 13 built-in presets (v0.11.2, `core/sky_presets`: the
  skies Sky Lab was tested with) listed under "Built-in", loaded the same way, no Bottega
  needed. Loading keeps the map's sun lock and baked size.
- **Memory** (v0.11.2, any sky picture, `core/sky.hpp` `skyRowsToKeep`): a plain-coloured
  bottom is not kept on the graphics card: only the rows down to 5 degrees past where the
  colour stops changing (never above the horizon; only if it saves 5% or more); the 3D
  view and the Camera Preview draw the rest of the sphere in that colour. The PNG files
  stay whole 2:1 panoramas (other tools and the renderer read them as they are). A Sky Lab
  sky keeps about 60% of its height, so about 40% less graphics memory (an 8192 x 4096
  sky is 96 MB uploaded as RGB, 128 MB where the driver pads it to RGBA).

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

### Decals (v0.12.5, done)
Hammer's `infodecal` and `info_overlay` as one object, always called **decal** (agreed with
the user 2026-11-22, "Batch 6 decided"). `core/decal.hpp`.
- **Decal:** `{id, material, pos, normal, up, width, height (cm), reach, order, visgroup}`.
  Frame: n = normal, up in the surface, right = up x n (seen from the front). Picture
  coordinates u along right, v down the picture.
- **Where it lands:** projected straight along its facing onto every surface in its box
  (the rectangle +- `reach`, default 4 Hammer units) that faces it within 75 degrees: brush
  faces and terrain cells, its own brush's or others'. Not on props; on primitives when they
  get materials.
- **Belongs to a brush:** `Brush::decals`. Moving, turning, sizing and mirroring a brush
  maps its decals (`mapDecal`: the picture is never mirrored); copies carry them;
  deleting the brush deletes them; undo is the brush's. A command that leaves one decal in
  several pieces (clip, carve) keeps it on the piece nearest to it; copies get new ids
  (`Document::normalizeDecals`, at apply). The vertex tool and snap-to-grid keep them where
  they are.
- **Size:** placed at its picture's size: texels x `$decalscale` (a material setting,
  default 1) x 1 Hammer unit, as Source sizes an infodecal; then any size.
- **Order:** drawn by `order` (higher on top), equal ones in the order they were placed.
  Bring forward / Send back change it by 1; the number can be typed.
- **Selecting and editing:** like props (`tools/decal_edit.hpp`, inside the shared object
  editing): click it (3D: its picture unless something is in front; 2D: its outline),
  Cmd+click, rectangle-select in 3D, Delete; 3D handles: corners (size, the opposite corner
  stays; Shift keeps proportions), a ring in its plane (turn: 15 degree steps, Shift free),
  drag to slide over the surfaces (snapped to the grid along the surface; onto another
  brush, it belongs to that one). Properties and Decals > Decal... take typed values.
- **Showing:** View > Show decals; its own visgroup; hidden with its brush. Renders draw
  every decal (as they draw hidden brushes).
- **Drawing:** editor 3D: the picture flat on the surfaces, shaded like them, DecalModulate
  multiplied; 2D: the outline and a tick along its facing. Renders: §9.
- **File:** a `//@ decal pos= n= up= size=w,h reach= order= [vg=] mat=<name>` line inside
  its brush's block.

### Reflection probes (v0.12.6, done)
Hammer's `env_cubemap` (agreed with the user 2026-11-22, "Batch 7 decided"). `core/probe.hpp`
(data, placing, blending weights, cube faces), `tools/probe_tool.*`, `render/probes.*`.
- **Probe:** `{id, pos, size}`; `size` is Hammer's cubemapsize (0 = default 32 a side, n =
  2^(n-1), up to 9 = 256). Unknown keys kept (Hammer's `sides`; the face list itself is parked).
- **Probe tool** (Shift+E, World and Tools menus): a 3D click on a floor puts it 170 cm above
  it (the user's height; Source's eye height is 163), on a wall or ceiling 81 cm (32 Hammer
  units) out; snapped to the grid along the surface. A 2D click: the depth of the last box.
  Return creates, Esc cancels; the size is chosen in Properties first.
- **Editing:** like lights (shared object editing): click the marker (a small grey-blue ball),
  Cmd+click, 3D rectangle select, drag in a 2D view, Properties (position, size; several
  selected: the size of all), Delete, Cmd+D. No rings, visgroups, hiding or Replicate.
- **Which probes a surface reflects: blended** (a deliberate difference from Source, whose
  VBSP gives each face the probe nearest its centre, preferring probes in front of it, and
  each model one probe): every shaded point takes its 4 nearest probes, each weighed
  1 / (d^2 + 16^2), times 0.05 for a probe behind the surface (Source's preference for probes
  in front), with the 5th largest weight taken off the others (so the blend changes smoothly
  when another probe comes into the 4), normalised. Brushes, terrain, decals and props alike.
  No box (parallax) correction, as Source 2013.
- **View menu** (editor 3D view, both off by default): the probes as mirror balls (24 cm, each
  its own last built picture), and where the selected probe reflects (shiny brush faces and
  terrain tinted blue by its share).
- **File:** an `env_cubemap` point entity: `origin`, `cubemapsize`, other keys kept.

### Mirrors and reflective glass (v0.12.7, done)
Source's `LightmappedReflective` (agreed with the user 2026-11-24, "Batch 8 decided").
`core/mirror.hpp` (which materials, the mirrored camera, which planes), `render/mirrors.*`.
- **The material makes the mirror:** a `.vmt` with the `LightmappedReflective` shader on a
  flat brush face (no brush entity, unlike Hammer's `func_reflective_glass`). Its parts:
  reflection (`$reflecttexture`, Source's default `_rt_WaterReflection`: on) and see-through
  (`$refracttexture`, default `_rt_WaterRefraction`: on); any value not starting with
  `_rt_` (e.g. `none`) turns that part off; the see-through also when `$minreflectivity` >= 1.
- **Which planes get pictures:** brush faces (not terrain, not hidden) with such a material,
  the camera in front of them, merged by plane (normals within about 0.8 degrees, distances
  within 0.5 cm), ranked by how much of the picture they cover (clipped to the picture);
  planes under 0.05% are left out; at most 8 (Source: only the first in view). The others,
  mirror materials on terrain and props, and mirrors inside a mirror's picture reflect the
  reflection probes (or the sky); the log warns when planes were left out.
- **The pictures** (render's own size, half at Draft and in the Camera Preview; Source:
  1024 x 1024 stretched): drawn through the renderer into 16-bit float targets, linear light
  before exposure. Reflection: from the mirrored camera (Source's `CReflectiveGlassView`:
  position, forward and up reflected), keeping only what is in front of the glass (0.1 cm
  off, as Source); its picture is the reflection flipped left to right, read at (1 - x, y)
  of the face's place on the screen. See-through: from the camera itself, keeping only
  what is behind the glass. "Keeping" is a clip plane in the material, primitive and decal
  shaders (`clipOn`, `clipPlane`: a discard); the sky is never clipped.
- **The shader** (`LightmappedReflective.glsl`, Source's maths): reflection x $reflecttint and
  see-through x $refracttint (both gamma, as Source), mixed by Fresnel
  ((max - min) x (1 - N.V)^power + min, Source's $fresnelpower 5, $min/$maxreflectivity),
  times $envmapmask; a normal map shifts both pictures by its x, y times its alpha times
  $reflectamount / $refractamount (picture widths); an optional $basetexture lit by the
  baked light is added. Drawn with the solid faces.
- **The bake:** see-through reflective glass lets light through (as VRAD with glass brush
  entities and see-through materials); a mirror that only reflects is solid.
- **`%tooltexture`** (Hammer's): the picture the editor shows for a material without
  `$basetexture`.
- **Starter materials:** a new Bottega gets `textures/starter/mirror.vmt`,
  `polished_stone.vmt` and (v0.12.7b) `glass_pane.vmt` (agreed; they may be removed in a
  later version).
- **Bending glass (v0.12.7b, Source's `Refract`):** drawn with the see-through faces, far to
  near; just before each such face the picture drawn so far is copied (a framebuffer blit,
  `Viewport::copyFrame`; Source copies its frame buffer before each renderable that needs
  it) and the shader reads it at its own place shifted by the normal map's x, y times its
  alpha times `$refractamount` (picture widths), tinted ($refracttint gamma, or 2 x tint x
  $refracttinttexture), optionally blurred (Source's 4 taps for 9, 1/512 apart), with
  `$fadeoutonsilhouette` and an `$envmap` reflection on top (x normal alpha, tint,
  contrast, saturation); it replaces what is behind (alpha 1). A copy of an 8-bit picture is
  turned back into linear light before exposure (`screenBehind`). It can only bend what is
  already drawn (as Source). On terrain and props, and in the editor, there is no copy: the
  probes straight through. Lets light through in the bake. A built-in ripple normal map
  (`glass_ripple_normal`, made for Scalpellino) serves the starter pane.

### Water and endless planes (v0.13.0, done)
Milestone 13, agreed with the user 2026-11-24 ("Milestone 13 decided with the user" in PLAN.md).
`core/plane.*` (planes, their kind, clouds' noise), `core/mirror.*` (water materials and their
murk), `assets/shaders/Water.glsl` and `Clouds.glsl`.
- **Water (Source's `Water` shader):** on the top face of a brush (Source's way; the other faces
  would be nodraw in Hammer) or on an endless water plane. Drawn through the mirror machinery
  (§6 "Mirrors"): the reflection from the mirrored camera and what is under the surface from
  the camera itself, mixed by Fresnel (Source's fixed power 5), rippled by `$normalmap` read
  through `$bumptransform` (built-in `water_ripple_normal` when none is named). The reflection
  shows the map (brushes, terrain, decals, planes) and the sky; props and primitives only with
  `$reflectentities 1`. **Murk:** the under-the-surface picture is drawn with the water's
  `$fogcolor` from `$fogstart` to `$fogend` (Hammer units, x 2.54) as height fog from far below
  up to the surface (the view's stretch under the water), and the murk in place of the sky; no
  `$fogend` > `$fogstart`: clear water. **Cheap water** (Source's): the probes (or the sky:
  `$envmap` other than env_cubemap) mixed with `$fogcolor` by Fresnel; always with
  `$forcecheap`, for water past the 8-picture budget, tilted water, water seen from below, and
  faded in with distance from `$cheapwaterstartdistance` to `$cheapwaterenddistance` (500 and
  1000 Hammer units). Water must be level (normal within ~2.6 degrees of up). Water and mirrors
  share the 8 pictures per render. Lets light through in the bake.
- **Endless planes** (`EndlessPlane {id, z, face, otherKeys}`; `face` holds the material and the
  texture axes as a brush face's): a flat layer at height z running to the horizon. **The kind
  follows from the material** (`planeKind`): Water shader = water, Clouds shader = clouds,
  anything else = ground. Renders draw it 10 km around the camera (`kPlaneReach`): a grid inside
  90% of the camera's far distance and a ring beyond it kept just inside the far clip by the
  vertex stage (`planeFar`), so it reaches the horizon whatever the far distance; both sides are
  drawn. Ground and water with the solid faces, clouds with the see-through ones (first).
- **Light on planes: sun and sky only, everywhere** (the user's choice, as Bryce: nothing
  shadows a plane). The bake gives each plane a `PlaneLight`: the suns x N.L and the open sky
  above its horizon (VRAD's sky gather without rays, the plane hiding the lower half), for the
  flat normal and Source's 3 bump directions (scaled like bumped lightmaps); the shader
  (`planeLightOn`, `planeSky`, `planeSun`) adds them, the sun dimmed per pixel by the cloud
  planes above (`cloudSun`). No point or spot lights; planes don't bounce light.
- **Planes in the bake:** a ground plane blocks light (two huge triangles in the tracer; it
  reflects nothing back), water lets it through, **clouds dim the sun**: a sun sample ray from
  a point below a cloud plane is weighed by `1 - $opacity x thickness` where it crosses the
  plane (`cloudSunThrough`; Source models' sun light too, at the prop).
- **Clouds** (`Clouds.glsl`; separate from Sky Lab's clouds, only alike): value noise (an integer
  hash, smooth interpolation, `$complexity` octaves 2.03 apart) over the plane, `$cloudsize` cm
  a formation; the edge sits where `$cover` % of the plane is covered (the noise is about
  normal: its measured mean and spread), `$softness` widens the edge, the thickness grows
  inside. `core/plane.cpp` (`cloudThickness`) and the GLSL twin compute the same numbers, so
  the bake's cloud shadows lie under the clouds a render shows. Lit by the plane light; seen
  from below the thick parts turn `$shadowcolor`; far away a pixel takes the average thickness
  (no flicker).
- **Editor:** side views draw a line across the view at the plane's height (dashed; red when
  selected); the top view draws nothing; the 3D view a square of the material's picture around
  the map, fading out, see-through, unlit. Select: click the line (unless a brush is under the
  cursor), the surface in 3D (unless something is in front), or World > Endless planes. Drag the
  line in a side view (snapped) or type the height in Properties (also the material: "Use the
  current texture", and the texture scale); Delete, Cmd+D (one grid step up). World > Endless
  water / ground / cloud plane make one with a starter material (water and ground at 0, clouds
  50 m above the map's top); "with the current texture" uses the Textures panel's pick.
- **File:** a `scal_plane` point entity: `origin` "0 0 z", `material`, `uaxis` / `vaxis`
  ("[x y z offset] scale"), `rotation`, other keys kept.
- **Starter materials** (a new Bottega; the menu items write theirs into an older one):
  `textures/starter/water` (clear blue-green, reflects everything), `murky_water` (brown, murky
  within 60 units), `ground` (Grass_20), `clouds`. Built-in pictures made for Scalpellino:
  `water_ripple_normal`, `tool_water`, `tool_clouds`.
- **Later (parked):** a camera under water (Source's underwater fog and `$bottommaterial`).

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

### Cameras (milestone 11, done: v0.11.0 and v0.11.1)
The objects Render and the Camera Preview look through. `core/camera.hpp`,
`Document::cameras()`, `tools/camera_tool`, `app/editor_cameras.cpp`.
- **Camera:** name, position, pitch / yaw (as a light's) and roll; field of view in degrees
  across the picture's longer side, or as a lens length in mm on a 36 mm sensor (they
  convert; presets 14, 24, 50, 85, 135 mm); near and far distance; picture size (presets
  or typed); orthographic with a width across; exposure (stored for the renderer's HDR);
  marker colour; active. Depth of field comes with the camera effects.
- **One active camera:** whenever a map has cameras, exactly one is active. The first one
  made is; new and duplicated cameras never take over; making one active switches the
  others off (one undo step); deleting the active one makes the first left active; a file
  with none or several opens with exactly one.
- **Placing:** the Camera tool (Shift+K, World and Tools menus): settings in Properties,
  click in a view (as the Light tool), Return creates, Esc cancels. Names `camera 1`,
  `camera 2`...
- **Editing:** cameras are objects like lights (`tools/object_edit`): click the marker (a
  diamond in the camera's colour; the active one has a white dot) in any tool that
  selects; drag it in 2D; three rings turn it about its position (15 degree steps) and a
  yellow ring around its view direction rolls it (in a 2D view only when the view
  direction points enough toward or away from you); everything typed in Properties.
  **Aim at a point:** typed (Properties), or World > Aim selected cameras at a clicked
  point, then a click: a surface in 3D, a spot in a 2D view (at the camera's own depth, so
  the top view aims level). Delete, Cmd+D, 3D rectangle select include cameras.
- **Drawing:** a pyramid from the camera to its picture frame (56 px long on screen; a box
  for an orthographic camera, as wide as its picture), a small triangle on the frame's top
  edge for "up"; inactive cameras dimmer.
- **Files:** our own point entity `scal_camera` (name, origin, angles "pitch yaw roll" with
  pitch positive down as Hammer writes it, fov, near, far, size "w h", ortho, ortho_size,
  exposure, color, active; unknown keys kept).
- **Presets** (v0.11.1): the camera's settings (fov, near, far, size, ortho, ortho_size,
  exposure; not where it is, which way it looks, its name or colour) as a text file of
  "key value" lines, `<name>.camera` in the Bottega's `presets/camera_presets`. Save /
  Load in Properties: from / into the selected camera (loading is one undo step), or the
  camera tool's settings for the next camera when none is selected.
- **Camera Preview** (v0.11.1): see §9.

### Scale reference
Toggleable ghost human figure (175 units = 1.75 m tall, blocky, see-through) to judge
proportions. Its feet position is saved with the map (World → Scale figure position, or
it appears below the top view's centre). Editor-only, never exported.

## 7. Entities (complete list)

- **Lights** — point, spot, sun.
- **Math primitives** — exact shapes, combinable (milestone 9).
- **Props** — static models (`.glb` and Mazzuola's `.mazz`) with their textures; placed,
  turned and sized like primitives. No physics (milestone 10).
- **Cameras** (v0.11.0, v0.11.1) — objects placed and aimed like a spot light, with many options
  and presets: lens (field of view or mm, roll, near / far distance, orthographic mode),
  image size, aim at a point, depth of field and exposure later; the user can save their
  own presets (saved in Bottega's `presets/camera_presets`). Details in §6 "Cameras". A map can hold several; exactly one is
  **active**, the only one the Camera Preview and Render use.
- **Decals** (v0.12.5) — pictures laid on surfaces (Hammer's infodecal / info_overlay as
  one object); kept in the brush they were put on. Details in §6 "Decals".
- **Reflection probes** (v0.12.6) — Hammer's `env_cubemap`, placed by hand; Render builds a
  picture of their surroundings ("building cubemaps") for shiny materials to reflect. §6.
- **Endless planes** (v0.13.0) — a flat layer at a height to the horizon carrying any material
  (water, ground, clouds; from Bryce). §6 "Water and endless planes".
- **Environment** — sky color/texture, fog, ambient light.
- **Camera bookmarks** — saved viewpoints of the editor's 3D view, for moving around.
  Editor-only, and not the same thing as the camera objects.

Lights are saved as Hammer entities other tools understand: `light`, `light_spot`,
`light_environment` with `origin`, `_light "r g b brightness"`, `angles` (pitch positive
down) + `pitch` (negative down), `_inner_cone` / `_cone`, `_zero_percent_distance`
(reach). Keys we don't use are kept. Environment, bookmarks and the scale figure are
worldspawn keys: `_scal_sky`, `_scal_ambient`, `_scal_fog "on r g b start end"`,
`_scal_cameras "x y z yaw pitch name;…"`, `_scal_figure`, `_scal_skylab` (Sky Lab
settings, `name=value` pairs separated by `;`).

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
- **Cameras** (v0.11.0): `scal_camera` point entities (see §6 "Cameras").
- **Decals** (v0.12.5): `//@ decal ...` lines inside their brush's block (see §6 "Decals").
- **Reflection probes** (v0.12.6): Hammer's `env_cubemap` point entities (`origin`,
  `cubemapsize`; see §6 "Reflection probes").
- **Endless planes** (v0.13.0): `scal_plane` point entities (`origin` "0 0 height", `material`,
  `uaxis` / `vaxis`, `rotation`; see §6 "Water and endless planes").
- **Props** (v0.10.0): `scal_prop` point entities with `model` ("models/tree"), `origin`,
  `rotation` (degrees X Y Z), `scale` (3 numbers) and any other keys, kept as they are.
- **nodraw:** faces textured `nodraw` (also recognised: `tools/toolsnodraw`, `common/nodraw`)
  are shown in the editor but skipped by every render and export (Camera Preview, PNG
  renders, `.glb`). The `.map` export keeps them, as other tools expect.
- **Textures:** built-in CC0 textures inside the app + the user's folders of PNG/TGA/JPG/BMP
  (name = file name without extension) + the Bottega's `textures/` (below). Half-Life `.wad`
  support later.
- **Materials** (v0.12.4, `core/material.*`): Source's `.vmt` files, Source's syntax and key
  names (lower case), **beside their texture** in the texture folders (no separate
  `materials/` folder). Every texture is a material already (LightmappedGeneric with that
  picture); `<name>.vmt` beside `<name>.png` adds settings; a `.vmt` with another name whose
  `$basetexture` is a texture of the library is a **variant** (its own entry, on faces like a
  texture); a `.vmt` without a picture is only a **parent**. Parents are Source's `patch`
  shader: `include` (the path from the library folder's root, the Bottega root for the
  Bottega: `textures/walls/brick_base.vmt`), `insert` (adds or overrides; "" removes),
  `replace` (only keys the parent has); chains allowed. **Helper maps** `<name>_normal`,
  `_normalmap`, `_bump`, `_ssbump` (also `$ssbump 1`), `_mask`, `_envmapmask`, `_spec`,
  `_selfillum`, `_illum` in the same folder fill `$bumpmap`, `$envmapmask` or
  `$selfillummask` when the material doesn't, and are hidden from the browser. Faces store
  the material name (texture or variant), so maps don't change. Built-in textures are read
  only (a variant of one goes in the Bottega's `textures/`). Material edits are commands
  holding file changes (`Command::fileChanges`): undoable, and they don't mark the map as
  changed. Compiler keys use Source's `%` prefix: `%texlight "r g b brightness"` (lights.rad's
  format) makes a texlight. **Props** (v0.12.4b) take settings from a `.vmt` beside the
  material's texture file (same name), else `<glTF material name>.vmt` in the model's folder;
  texture values in it are files in that folder first, then textures of the library; the
  glTF's own normal map is the `$bumpmap` (green up: `%flipgreen 1`) unless the `.vmt` names
  another.
- **Shader files** (v0.12.4, `core/shader_file.*`, `render/shader_library.*`): GLSL, one file
  per shader, a comment header (`// scalpellino shader 1`, `// name:`, `// kind: material`,
  `// about:`, `// setting: <key> <type> [default] | help`; types texture, float, int, bool,
  color, text, light; optional `// blend: modulate`, v0.12.5) and a body defining `vec4 shade()` (linear colour before exposure,
  alpha). The app compiles prelude + body for each kind of geometry (World: brushes and
  terrain; Model: props); the prelude gives `baseColor()`, `baseUV()`, `bakedLight()`,
  `worldPos()`, `worldNormal()`, `eyeVector()`, `envmap(dir)` (the probes or the sky, v0.12.6), `toLinear`, `toSrgb`,
  `luminance`; bump maps (v0.12.4b): `bumpNormal(texel, flipGreen)` (Source's convention, X+
  Y- Z+), `bakedLightBumped(n)` (SDK 2013's radiosity normal mapping: squared weights over
  their sum), `bakedLightSSBump(w)`, `tangentToWorld(n)` (the texture's frame from screen
  derivatives: x along u, y along v, the same frame the bake used); for props
  `ambientCube(n)`, `modelLightCount()`, `modelLightAt(i, L, col)` (the 2 strongest lights,
  for Phong and rim light); and each setting as a uniform (`$detailscale` -> `u_detailscale`; textures
  also `u_x_set`; text: `u_x_set` only). Source's LightmappedGeneric, UnlitGeneric and
  VertexLitGeneric ship in `assets/shaders/` (the app's `shaders/` folder); the Bottega's
  `shaders/` is searched first. No `#version` line in the body.
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
  fog toggles (milestone 8). Materials show only their base texture here (a variant: its
  picture); no shine, glow or cutouts in the editor.
- **Camera Preview** (v0.11.1; a draft render since v0.12.0): a docked panel under
  Properties showing the active camera's view, only while the map has a camera. Updated
  **only on a keybind or button** (Cmd+K, its Update button, Render > Update Camera
  Preview), never live; it says when the map has changed since. It is the **renderer
  itself at Draft settings** (no anti-aliasing), at the picture's shape, as large as the
  panel, so it can't disagree with a render.
- **Render** (M12 in 7 batches, see PLAN.md Roadmap item 5; v0.12.0 the pipeline): the
  Render button (left end of the status bar), Render > Render... or Cmd+Shift+K. The
  picture size is the active camera's; the options are in the Render menu (Quality Draft /
  Normal / High, anti-aliasing, texture filtering), kept in the user's settings
  (`render.txt`), not in the map. A modal terminal window shows the steps as they run
  (gathering the map, lighting, drawing, scaling down, saving; later baking the lights,
  building the reflection cubemaps); the editor waits, one step per frame, and Esc stops
  it between steps. The result is a PNG in the Bottega's `renders/`
  (`<map>_<camera>_<date-time>.png`, the log beside it as `.log`); no Bottega: it asks
  for one.
  - **Code:** `render/renderer.*` builds the picture's scene and draws it through a
    `Viewport` given the camera as its lens (`setLens`: field of view, near / far,
    orthographic with parallel rays for the primitives' shader; nothing of the editor's:
    grid, axes, markers, outlines, selection, figure). `core/render_job.*` holds what
    needs no graphics (options, anti-aliasing size limits, the downscale, file names).
    The job runs in `app/editor_render.cpp`.
  - **What a picture holds:** the map as it is, the View menu ignored: every brush
    (hidden ones and hidden visgroups too: hiding is an editing aid), nodraw faces left
    out, primitives, props, the map's sky, its fog including Sky Lab's height (the line
    of sight's stretch inside the fog layer, as in the baked sky; the editor's own fog
    stays distance-only).
  - **Anti-aliasing:** drawn n x n times larger, averaged down in linear light; lowered
    automatically when the drawn picture would pass 16384 pixels a side or 50 million
    pixels.
  - **Lighting since v0.12.1: baked** (`core/bake.*`: no graphics; `render/baked_light.*`:
    float atlas pages and the lightmap shader; primitives read their 3D lightmap in their
    shader). The render job bakes on other threads, kept in memory per quality while the
    bake key (a hash of everything that lights the map) is the same. Props (v0.12.3): baked
    per vertex, or "like a Source model" (the light grid's nearest ambient cube + 2
    lights), drawn by `ModelLibrary` from a float texture of vertex light; the light grid
    (HL2's 48 x 48 x 96) is in `BakeResult::grid`. Bounces (v0.12.2): VRAD's radiosity on patches (brush faces,
    terrain, primitives, props), blended onto the lightmaps. Before v0.12.1 renders used the rough light preview's light. Done
    the way VRAD does them (agreed
    2026-11-20; details and formulas in PLAN.md Roadmap item 5): lightmaps per brush
    face and terrain, Hammer's light keys and falloff (Source numbers converted from
    inches to cm), sky light from the sky picture, shadows from everything, 3D lightmaps
    for primitives, linear light; bounces in v0.12.2, ambient cubes and prop vertex
    light in v0.12.3.
    **Rays** (`core/tracer.*`, sped up before v0.12.4b): a tree of boxes per mesh split by
    surface area (binned SAH), searched nearer box first; boxes and triangles stored in
    single precision, the triangle test itself in double (as before, so edge cases fall the
    same way); a prop model is stored once and placed per prop (instances: the ray goes into
    the model's space), with a small tree over the instances and the world mesh. A face's
    patches are found through a grid in lightmap space (`StGrid`) instead of checking each
    one. Same light as before (renders match within 2 / 255).
    **Bump maps** (v0.12.4b): a face, terrain or prop whose material has a `$bumpmap` gets
    the light along Source's 3 bump directions too (VRAD's `GetBumpNormals` from the
    texture's s / t axes): direct, sky and bounce (patches keep 3 more totals; each transfer
    is weighed by `max(0, b.dir) / n.dir`). Faces: `FaceLightmap::bump` (3 maps), packed in
    the page beside the flat map (or below it when too wide), the offset passed to the
    shader in the vertex normal's z. Props per vertex: 4 values a vertex (flat, then the 3
    directions; tangents from the UVs). Props like a Source model: the bump normal is lit
    per pixel by the ambient cube and the 2 lights. At the end, each luxel's (and vertex's)
    3 bump values are scaled per colour so their average is the flat value: the engine's
    conversion (VRAD's copy, `LinearToBumpedLightmap`), so a flat normal gets the flat
    light with SDK 2013's blend. The bake key carries the bake's own version.
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
- **Reflections:** a material chooses: sky only (`$envmap` with any value but
  `env_cubemap`: cheap chrome); the reflection probes (`$envmap env_cubemap`, v0.12.6; the sky
  if the map has none, as Source's default cubemap is the skybox); live mirror reflection on
  flat surfaces (`LightmappedReflective`: mirrors, polished floors, reflective glass;
  v0.12.7, §6 "Mirrors and reflective glass"); water (v0.13.0, Source's `Water` shader) reuses
  the mirror pictures (§6 "Water and endless planes").
- **Mirror pictures** (v0.12.7, `render/mirrors.*`): after the cubemaps, before the picture:
  each mirror plane in view (up to 8) drawn again from the mirrored camera and, for glass
  that sees through, from the camera behind the glass; texture units 12 and 13 (material
  textures now use 7..11); `Scene::mirrors` / `BakedDrawContext::mirrors`, a face's plane
  found when the brushes are drawn (`onMirrorPlane`).
- **Reflection probes** (v0.12.6, `render/probes.*`): a render's "building cubemaps" step,
  after the bake: each probe drawn 6 times (OpenGL's cube face order) through the renderer
  itself, into a 16-bit float target, in linear light before exposure (fog mixed in linear
  light), 2x larger and averaged down (Draft: as is), with a 1-pixel margin. Sizes: the
  probe's own (Hammer's cubemapsize), halved at Draft, doubled at High; at every quality at
  least 4 (a 1 x 1 or 2 x 2 probe is built at 4 x 4; the list says so) and at most 512. **Two passes** at Normal and High (the 2nd sees the 1st's probes on shiny things:
  reflections in reflections), one at Draft (shiny things in it reflect the sky). All probes
  go into one RGB16F atlas (tiles of 3 x 2 face slots; a slot is the face plus a border of
  side / 4, at least 2, filled from the neighbouring faces, so bilinear filtering never
  crosses into another face: **seamless filtering, a difference from Source**, whose DirectX
  9 filtering can show the cube's edges at 32 a side), with mipmaps made per slot; the shader
  picks the level from how fast the reflection turns per pixel (soft on far or bumpy
  surfaces, as Source's mipmapped cubemaps), at most as many levels as the border allows. A
  2-texel-per-probe float texture holds positions and tiles (texelFetch), so any number of
  probes works. Texture units 14 and 15 (material textures 7..11; 12 and 13 are the mirrors'). Kept in memory per
  quality while the bake key, the probes, the decals, material edits and texture filtering
  are the same (`Editor::probeKey`). The Camera Preview builds Draft probes on Update. The
  log: one line per probe (position, size, time, how many shiny surfaces (faces, decals) it reflects on), the
  total; warnings for a probe inside a brush and for shiny surfaces in a map without probes.
- **Materials and shaders** (v0.12.4; formats in §8): a surface's look is its material.
  In renders and the Camera Preview each brush face and terrain layer is drawn by its
  material's shader file (grouped by material, then lightmap page; terrain layers after the
  rest); see-through materials (`$translucent`, `$alpha` < 1) are drawn after the props, far
  to near, without writing depth. Props are drawn by the VertexLitGeneric file with their
  model's own materials, plus a `.vmt` per material (§8): bump maps, Phong and rim light
  from the prop's 2 strongest lights and its ambient cube (Source's `phong`; not shadowed).
  The sky drawn behind the scene takes the render's exposure (v0.12.4b).
  **Decals** (v0.12.5) are drawn after the opaque surfaces and props, before see-through
  faces, in their order: each clips the lit triangles in its box (carrying their lightmap
  coordinates, bump offset and normal), so its light is the surface's baked light (and its
  bump maps when the decal's material has a bump map); its material's World program with
  `decalPass` = 1 (the picture's alpha always blends), without writing depth, lifted towards
  the camera by about 3 depth steps at its distance. A shader file with `// blend: modulate`
  (DecalModulate) multiplies what is there, twice (0.5 grey = no change), and fog fades it
  to grey. Primitives keep their own shader and show
  their material's base texture. The normal travels in the vertex colour (8 bits a channel);
  cheap chrome reads a 128-pixel-wide copy of the sky (about Source's default cubemap size);
`$envmap env_cubemap` reads the reflection probes (above).
  A shader that doesn't compile is reported in the render log and LightmappedGeneric draws
  instead. The bake takes from materials: `$reflectivity`, `%texlight` (VRAD's surface
  lights: one per patch, intensity = baselight x the texture's repeats on the patch x
  DIRECT_SCALE 10000, cos x cos / d^2; checked in VRAD's code), `$alphatest` (cut-out shadows: rays pass where the picture's alpha is
  under the reference) and `$translucent` (blocks no light). Water and clouds are materials too
  (v0.13.0, §6 "Water and endless planes"): water lets light through, endless ground planes
  block it, cloud planes dim the sun. Optional camera effects (dither, palette, bloom) are separate from
  the look and matter least.
- **Maybe later:** a ray-tracing renderer (Bryce look: real reflections on any shape,
  bending glass). Not pressing; the Source renderer may be enough.

## 10. Output and export

**Pictures are the output.** The user probably will never open exports in another editor,
so export is not a goal now and Godot never drives choices. A `.glb` export (terrain and
primitives as meshes, nodraw skipped) is possible last, at milestone 13. A walkthrough tool
would be a separate tool in the far future.

## 11. Undo

Each command stores before/after copies of the brushes/entities it touches. Simple,
robust, cheap (brushes are tiny; terrain is not: a 32 x 32 patch with paint is tens of
KB per copy). The history has a memory limit, 64 MB as in Mazzuola
(`Document::kHistoryBytes`): past it the oldest steps are dropped; the newest step always
stays. Undoing everything that is left then doesn't count as the saved state.

Caches of drawing data (editor views, light preview, renderer) follow the edit flow in §4:
`Document::brushStamp` changes for every brush a command (or its undo / redo) touches, and
only those brushes are rebuilt. Scenes share the cached list instead of copying it.

## 12. Dependencies

Fetched automatically by CMake, versions pinned and checked against a SHA-256 fingerprint:
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
| 10c | Sky Lab (Bryce, all of it): generated skies (sun, moon, stars, clouds, atmosphere) with live preview in the 3D view, baked to panoramas in Bottega's `skies/`; settings kept in the map; tick box to lock the sun to the map's sun light; eclipses. Steps: v0.10.4 window / bake / sky modes / sun / haze / lock, v0.10.5 clouds, v0.10.6 night sky and effects | done (v0.10.6) |
| 11 | Cameras and Camera Preview: camera objects (many, one active), options and presets, the on-request preview in a docked panel (editor look until M12 replaces it). Steps: v0.11.0 camera objects and options, v0.11.1 presets and the preview | done (v0.11.0, v0.11.1) |
| 12 | Render, the Source look, with materials: render window, terminal log, light baking, reflection probes, mirrors, the material system (GLSL shaders from Bottega's `shaders/`, reflection kind per material), PNG to `renders/`. Steps: v0.12.0 pipeline, v0.12.1 direct light and shadows, v0.12.2 bounces, v0.12.3 props and the light grid, v0.12.4 materials, v0.12.4b bump maps, v0.12.5 decals, v0.12.6 probes, v0.12.7 mirrors, v0.12.7b bending glass | done (v0.12.7b) |
| 13 | Water, cloud and endless planes: materials made with the system above: Source's Water shader on brushes and endless planes (reflection, murk, cheap water), endless planes (water, ground, clouds; sun and sky light), cloud shadows in the bake | done (v0.13.0) |
| 14 | Camera effects (lowest priority): dithering, palette, bloom | dropped (the user, 2026-11-27) |
| 15 | Export: `.glb`, optional and last (not confirmed by the user) | dropped for now (the user, 2026-11-27; maybe in the far future) |
| later / maybe | ray-tracing renderer (Bryce look), `.wad` textures, Lua generators, texture tags | |
