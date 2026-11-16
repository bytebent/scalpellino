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
│                          │                   │  Y2K preview │ .map, .glb, export
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
- **Units / axes** — Hammer units (1 unit ≈ 1 inch, player ≈ 72 units tall), Z-up in the
  editor. Exports convert to Y-up and meters with an adjustable scale.

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

### Tools
Select, Block (box, wedge, cylinder, pyramid), Clip, Vertex (done in M2), Carve (CSG subtract),
**Curve**, Texture apply / face edit, Light, Camera bookmark.

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

### Scale reference
Toggleable ghost human figure (72 units tall) to judge proportions. Editor-only,
never exported.

## 7. Entities (complete list)

- **Lights** — point, spot, sun.
- **Environment** — sky color/texture, fog, ambient light.
- **Camera bookmarks** — saved viewpoints for renders. Editor-only.

That's all. No gameplay entities.

## 8. File formats

- **Native: Valve 220 `.map`.** Plain text, readable by func_godot and TrenchBroom.
  Editor-only data (visgroups, camera bookmarks, curve-generator parameters) stored as
  extra keys other tools ignore.
- **nodraw:** faces textured `nodraw` (also recognised: `tools/toolsnodraw`, `common/nodraw`)
  are shown in the editor but skipped by every render and export (Y2K preview, PNG
  renders, `.glb`, Godot package). The `.map` export keeps them, as other tools expect.
- **Textures:** built-in CC0 textures inside the app + the user's folders of PNG/TGA/JPG/BMP
  (name = file name without extension). Half-Life `.wad` support later.

## 9. Rendering

- **2D views:** wireframe, redrawn only on change.
- **3D modes:** wireframe, flat-color, textured, **Y2K preview**.
- **Y2K preview pipeline** (each step toggleable): low-res render target (e.g. 640×480) →
  nearest-neighbor upscale → distance fog → per-vertex lighting from light entities →
  chrome / environment maps → bloom → dithering.
- **Render button:** saves a PNG still at a chosen resolution.
- **Baked lighting** (shadows, bounce) later, as an explicit on-demand bake.

## 10. Export tiers

1. **`.map` for func_godot** — geometry + texture names. Materials set up in Godot.
2. **`.glb`** — geometry, UVs, embedded textures, nearest-filter samplers. Optional baked
   lighting as vertex colors or lightmap. Works in Godot, Unity, Unreal, Blender.
3. **Godot scene package** — `.tscn` + textures + the Y2K shaders ported to Godot shading
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

| # | Milestone | Done when |
|---|-----------|-----------|
| 0 | Skeleton | Window with 4 viewports + grids, fly camera, near-zero idle CPU, CI builds `.app` for Intel + Apple Silicon |
| 1 | Brushes | Block tool, select/move/resize/rotate, grid snapping, undo/redo, save/load `.map` (+ v0.1.1: Retina, menus, Transform dialog) |
| 2 | Precision | Vertex tool (realtime, invalid shapes highlighted), snap vertices to grid, pivot point, grid-friendly (Pythagorean) rotation angles, microgrid + custom grid sizes (inspired by Hammer++) |
| 3 | Textures | Texture folder loading, browser (search by name), apply, texture tool + face edit, texture lock (done, v0.3.0). Later: tags and richer browser |
| 4 | Shaping | Clip (2- and 3-point), carve (CSG), hollow (done, v0.4.0) |
| 5 | Curve tool | Arch, carve arch, ring, curved wall, spiral stairs; regenerable + bake (done, v0.5.0) |
| 6 | Look | Lights, environment, camera bookmarks, scale figure, Y2K preview, PNG render |
| 7 | Export | `.glb`, Godot scene package, baked-look `.glb`; verified in Godot via func_godot and glTF import |
| 8 | Extras | `.wad` textures, Lua generators, baked lighting |
