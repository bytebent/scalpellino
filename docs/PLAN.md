# Scalpellino — Working Plan & Log

**Purpose:** let any future session (human or AI) pick up work without outside context.
Update this file at the end of every step: what was done, test results, what's next,
open questions. The step-by-step log is in `LOG.md` (newest entries at the top).

Design: [`ARCHITECTURE.md`](ARCHITECTURE.md). Rules: [`../CLAUDE.md`](../CLAUDE.md).

---

## Current status

- **Released:** v0.1.0 … v0.9.4. Milestones 0–9 are done (see the milestone sections
  below and the log). **Milestone 10 — Props is built and pushed as v0.10.0** (static
  `.glb` models from Bottega's `models/`; `.mazz` waits for Mazzuola's final format).
  Report it to the user, then ask before the next milestone.
- **Next:** **Random Replicate** once the user says go. Bottega basics and the idle CPU
  test (`tests/ui/idle.sh`, see DEV.md) are done.
- **Roadmap:** Random Replicate → Sky Lab → cameras and Camera Preview →
  Render (Source look, with the material system) → water / cloud planes → camera effects →
  export. The renderer part is a
  proposal awaiting the user's confirmation. Details under "Roadmap" and "Direction".
- **Bottega** (shared folder for Mazzuola, Scalpellino and later tools): the basics are
  built (v0.9.4); models, shaders, presets and renders will use it as they arrive. See
  "Bottega" below.
- How the user likes to work: one release per step, short plain reports, ask before
  starting the next milestone (see `HANDOFF.md`).

## How to work in this repo

Build, tests, the UI script language, CLI flags, CI, releasing and the per-step checklist
are in [`DEV.md`](DEV.md). One-step-per-release: add `docs/releases/vX.Y.Z.md` and push to
`main`.

## Pinned dependencies

| Dep | Version |
|---|---|
| raylib | 6.2 |
| Dear ImGui | v1.93.0-docking |
| rlImGui | commit 5c17e70e98ae9f50ca94bdcad0e3b3536b46a203 |

## Open questions (parked)

- **Milestone 8, answered by the user (2026-11-16):** sky in the 3D view toggled from the
  View menu; a very rough light preview, not computed while off; fog on/off, off by
  default; the sun's direction separate from the sky; selected lights get xyz rotation
  rings.
- **World, decided without asking (confirm with the user):**
  - Lights are selected apart from brushes (a plain click on one drops the other);
    the select tool also picks lights; only lights move by dragging their marker.
  - Ring turns snap to 15 degrees (typed pitch/yaw for anything else).
  - The light preview cuts faces into ~64-unit triangles so lights in the middle of big
    faces show; brightness 200 = the texture as it is at the light.
  - Sky, preview, fog and figure toggles are editor settings (not saved in the map);
    the figure's position and the bookmarks are saved in the map.
  - Built-in skies: the three panoramas (the matching cube crosses are the same skies).
- **Math primitives, decided with the user (2026-11-18):** they sit beside brushes (low-
  poly shapes stay); objects like lights; brush tools don't touch them; Bryce-style
  combining wanted; saved maps keep them as primitives, only exports make meshes;
  per-axis size allowed; kinds: sphere, cube (rounded), cylinder, cone, torus, capsule,
  disc.
- **Math primitives, confirmed by the user (2026-11-18):** the choices below are fine.
- **Math primitives, decided without asking:**
  - A click selects the whole combination; Properties lists the pieces to pick one.
  - Size handles only on a single selected primitive; rings around any selection.
  - A combination can use up to 4 different textures (more fall back to the colour);
    16 pieces per combination are drawn.
  - The cut surface of a subtract piece shows that piece's own texture (as in Bryce).
  - New primitives get the current texture (or "flat colour" in Properties).
- **Props (M10), from the user:** a prop is a model with its textures; Scalpellino opens
  both `.glb` and `.mazz`. `.mazz` reading follows Mazzuola's file format, which is final
  at its milestone 1 (Mazzuola's plan still lists "Scalpellino reading .mazz or .glb
  only?" as open: the answer is both).
- **Props (M10), decided with the user (2026-11-18):**
  - **Instancing:** each model is loaded once; every copy is drawn from that one mesh, so
    scattering hundreds (Random Replicate) costs little.
  - **Lighting:** v1 props get the editor shading plus fog; the rough light preview on
    props comes later.
  - **Scale:** per-axis sizes are possible (typed fields and handles), but **uniform by
    default**, for props **and for primitives** (a change to how primitive handles work
    now: today Shift scales evenly; to be reversed so even scaling is the default, with a
    modifier for per-axis; details to settle when building it).
  - **Shortcut and panel:** Props tool on **Shift+P**; a Props panel next to Textures,
    searchable by name (thumbnails later).
  - **Placing:** like a primitive: it is moved around the same way and can be snapped to
    a surface (Snap to Land), Return creates, Esc cancels.
  - **Loader:** cgltf (single header, MIT, pinned) in `src/io`, not raylib's `LoadModel`:
    the core stays graphics-free and headless-testable, and picking and landing need the
    triangles on the CPU. The user had no preference (technical choice). Also used for the
    M13 export.
  - **Where models live:** in Bottega's `models/`, one self-contained folder per model;
    a prop stores its relative name (see "Bottega").
- **Props (M10), decided without asking (confirm with the user):** static meshes only
  (animations, skins, cameras, lights ignored); base colour plus optional texture,
  embedded or external, several materials per model, grey where a texture is missing;
  alpha opaque and cutout only; metres x 100, Y-up to Z-up, the model's origin is its
  pivot; saved as a `scal_prop` point entity (path, origin, rotation, scale; unknown keys
  kept); a missing file shows as a box with a warning and is kept on save; models reload
  through Bottega's Rescan command (no watching, not automatic); 2D views show a rotated bounding
  box, 3D picking uses triangles; Snap to Land works on props (landing generalised to any
  object); hiding / visgroups as for primitives (not yet); `.mazz` after `.glb`, behind a
  loader interface; a small generated `.glb` as test asset; a triangle-budget warning.
- **Props (M10), built (v0.10.0). Decided without asking while building (confirm with the user):**
  - **`.mazz` is not read yet**: it waits for Mazzuola's final file format; the loader sits
    behind `ModelStore`, so it is an extra reader later.
  - **Model names:** a folder with one model file is that folder's model (`models/tree`,
    whatever the file is called); with several files the one named like the folder is the
    model, else each file is its own model; loose `.glb` files in `models/` are models too.
    Spaces become `_`.
  - **Handles:** even scaling is now the default for primitives too, **Shift = one axis**
    (the two old UI scripts were adjusted).
  - **Depth in 2D views:** a click in the top view puts the prop on top of the last box
    drawn (else z = 0); in the front and side views the depth is the middle of it. Type
    the exact place in Properties.
  - Props are deselected by every other object tool, as lights and primitives are; Snap to
    Land / Ground, Duplicate (offset by the grid size), Delete and Multi-Replicate (turns
    and scales props about the pivot) work on them. Hiding and visgroups do not cover props yet.
  - cgltf 1.15 is vendored in `third_party/cgltf` (archive downloads are blocked here) with
    its global symbols renamed (`src/io/cgltf_rename.h`), because raylib bundles its own copy.
  - 3D drawing: each model is uploaded once (mesh chunks of at most 65535 vertices) and
    every prop is drawn from it with its own matrix; backface culling is off for props;
    editor shading, alpha cutout and fog in one shader; light preview on props later.
  - Triangle budget per model: 200000; a prop whose model is above it shows a "heavy
    model" warning in Properties (it is still drawn).
- **Idle CPU test (agreed 2026-11-18; done):** `tests/ui/idle.sh` runs with a map open
  (terrain, primitives, lights, sky, fog, light preview on, map saved and re-opened) and
  expects 0 CPU ticks over 10 s; a busy control proves the measurement; in CI at 1x and 2x.
  See DEV.md. (No new script command was needed: a script without `quit` leaves the app
  idle.)
- **v0.9.2, decided without asking (confirm with the user):** blend is set per piece
  (selecting the whole combination sets it for all); textures melt over the widest blend
  in the combination; Multi-Replicate's default offset is 4 grid steps along X; Snap to
  Land handles selected objects lowest first, so stacked ones land on their new spots.
- **Bottega:** decided with the user; see its section.
- **Primitives extras, later:** "align to slope" for Snap to Land; hiding primitives / visgroups; the Transform dialog on
  objects; Bryce's other shapes (pyramid, endless ground plane) if wanted.
- **World extras, later:** hiding lights / lights in visgroups; moving lights together
  with brushes in one drag; aiming a light at a clicked point; lights in the Transform
  dialog.

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
- **Native dialogs are untested on a real Mac by the AI** (CI compiles them; no GUI there).
  Ask the user to try Cmd+O / Cmd+Shift+S / Texture → Add folder → Browse.

- **Vertex tool extras:** edge handles (drag an edge = both its corners), marquee
  (drag-box) vertex selection, corner handles in the 3D view. Not in v0.2.0.
- **Grid-friendly angles:** user asked for "rotate by exact dimensions"; delivered as typed
  angles + Pythagorean angles. Note for the user: slopes like 1:2 can't keep rotated
  corners on whole numbers (irrational sin/cos) — build such slopes with the vertex tool.

- **Fly-mode up/down keys:** Q/E for now. User hasn't specified.
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

## Milestone 8 — World (done, v0.8.0)

Delivered: point / spot / sun lights with rotation rings, sky picker (panoramas and cube
crosses, a setting rather than a sealed box), ambient, fog, rough editor preview with
View-menu toggles (sky, light preview, fog), camera bookmarks, 175 cm scale figure.

## Milestone 9 — Math primitives (done, v0.9.0 … v0.9.3)

Delivered: Bryce-style sphere, rounded cube, cylinder, cone, torus, capsule, disc drawn
exactly per pixel; combining (add / subtract / intersect) and blending (v0.9.2); object
handles shared with lights (move, rings, size handles); units changed to 1 unit = 1 cm,
Snap to Land / Ground, Multi-Replicate (v0.9.2); ImGui ID fix (v0.9.1); single pieces of
a combination and a non-blocking Multi-Replicate window (v0.9.3).

## Roadmap (revised 2026-11-18; steps 4 to 8 are a proposal, to confirm)

1. **M10 Props (done, v0.10.0)** — static models with their textures, no physics. `.glb`
   first; `.mazz` once Mazzuola's file format is final. Placed, turned and sized like primitives (shared
   object handles), Snap to Land works on them, reload when the file changes. Texture
   filtering follows Scalpellino's Crisp / Smooth switch.
2. **Random Replicate** (Bryce) — scatter copies with random offset / turn / size ranges,
   for props and primitives (brushes: offset only, as with Multi-Replicate).
3. **Sky Lab** (Bryce; agreed with the user in a separate chat, missed here until they
   pointed it out) — make skies instead of only loading images. **Everything Bryce's Sky
   Lab has** (user: "it is amazing"): sun and moon (position, size, glow, colour, moon
   phase, halos / rings), stars and comets, cloud layers (cumulus / stratus, height,
   cover, frequency, amplitude, colours), atmosphere (sky colours, haze, fog, colour
   perspective, rainbow), sky modes (soft / darker / custom / off). The Bryce guide (the
   user's PDF) is the reference: ask the user to attach it again before building.
   - **Baked:** a live preview while tweaking; the result is a panorama for the existing
     sky loader (no cost when idle). The settings are saved in the map, so Sky Lab can be
     reopened and tweaked.
   - **Sun lock:** if the map has a sun light, Sky Lab shows a tick box that locks the
     sky's sun position to that light. No sun light, no tick box.
   - Generated skies go to Bottega's `skies/` once Bottega exists.
   - Comes after Random Replicate, before the renderer, so renders get good skies.
4. **Cameras and Camera Preview** (proposed order, confirm) — camera objects (many, one
   active), options and presets (field of view, image size...), and the small preview: an
   approximation of the final picture, shaders included, updated only on a keybind or
   button, shown only while the map has a camera.
5. **Render, the Source look, with materials** (proposed) — the render window (image size,
   options) that opens on Render, a terminal-style log (+ log file), baking the lights
   (shadows), reflection probes, mirror reflections on flat surfaces, a PNG saved to
   `renders/` (Bottega's once it exists). Only what the camera sees is drawn (frustum,
   back faces, nodraw). **The material system is part of this milestone** (a surface's
   look is its material): GLSL shaders from Bottega's `shaders/`, reflection kind per
   material. The Source look comes from how Source renders, never from filters.
6. **Water, cloud and endless planes** (proposed) — materials made with the system above.
7. **Camera effects** (proposed, lowest priority per the user) — dithering, palette,
   bloom and similar.
8. **Export** — `.glb`, optional and last; the user probably will never use it elsewhere.
   Godot not a goal.
- **Later / maybe:** a ray-tracing renderer (Bryce look; real reflections and bending
  glass), `.wad` textures, Lua generators, texture tags.

## Bottega (shared folder) — basics built in v0.9.4 (folder setting, Rescan, marker, textures and skies)

User's idea (2026-11-18): one folder that Mazzuola, Scalpellino and later texture tools
read and write. Agreed so far:
- Layout: `textures/` (the library), `models/` (one folder per model), `maps/`,
  `palettes/`, `skies/`, `shaders/`, `renders/`, `presets/` (camera presets in
  `presets/camera_presets`). **No `heightmaps/`** (user: a mistake).
- `palettes/`: format decided when Mazzuola gets there.
- `renders/`: the renders Scalpellino makes (M11).
- `shaders/`: a **drop-in folder**: any shader file put there becomes available, so a
  future tool that makes shaders, or one that reuses them, just reads and writes it.
  (User, 2026-11-18: the point is easy import, so this is not only "shared look
  shaders".) Scalpellino's own internal shaders (primitive drawing, fog, light preview)
  stay inside the app. **Open:** the shader file format, see below.
- **Decided with the user (2026-11-18):**
  - Bottega is a normal folder anywhere; each tool asks once, in its settings, where it
    is; an empty folder is offered the sub-folders.
  - Maps store paths relative to the Bottega root (`textures/walls/Brick_Red`,
    `models/tree`), so moving Bottega doesn't break maps. **Only for what lives in the
    Bottega** (user, 2026-11-19): the built-in (shipped, dev) textures keep plain names
    like `Bricks_24`, and so do the user's extra folders.
  - A model folder keeps its own textures (self-contained) and may also use the shared
    `textures/` library.
  - **No automatic or constant rescanning** (not even on window focus): a **Rescan**
    command, as a menu entry, a button and a shortcut. Idle stays free.
  - A small marker file at the root with a format version.
  - The user's existing texture and sky folders keep working as extra folders beside
    Bottega.
- **Built in v0.9.4 (core in `src/core/bottega.*`, tests in `tests/test_core.cpp` and
  `tests/ui/v094_bottega.script`):** File > Bottega folder... (typed path, Browse, status
  line, make the sub-folders; the path is kept in the user's settings, `bottega.txt`);
  marker file `bottega.txt` holding `bottega 1` (user: fine); a folder with files but no
  marker can be made a Bottega, files kept (user: yes); File > Rescan Bottega, **Cmd+R**
  (user: fine) and the Textures panel's Rescan do one full re-read (textures, skies;
  models, shaders and presets join when they are built); search order Bottega, extra
  folders, built-ins, first match wins; Bottega skies are named by path too (decided
  without asking, same rule as textures). A name that can't be found stays in the map.
  Folders for models, shaders, palettes, renders and presets are made but nothing reads
  them yet. Cmd+R itself is not scripted (scripts call the same `rescan` function).
- **Shader format, agreed with the user (2026-11-18):** GLSL files, one per shader, with a
  few comment lines at the top giving its name, its kind and its settings, so a tool can
  list a shader and show its settings without running it. Fits the Source renderer (see
  "Direction"). Priority (user): material shaders, water and clouds matter far more than
  the camera effects.
- **Direction, from the user (2026-11-18), corrects earlier assumptions:**
  - **The goal is exporting cool pictures** (renders), not game levels. The user will
    probably never open the `.glb` in another editor; export is not important now. A tool
    to walk through a map would be a separate tool in the far future.
  - **Shaders work when the render is made and show in the final image.** Nothing moves or
    animates in Scalpellino's editor; no "Animate" toggle. (Earlier docs treated shaders
    as live viewport effects; that was a misunderstanding.)
  - **Material shaders, water, clouds and the like matter far more than the Camera
    Preview effects.** Material shaders apply wherever a texture can go: brush faces,
    primitives, props and the endless water / cloud planes.
  - **How Scalpellino is used (user, 2026-11-18):** the user builds a small 3D scene and
    renders it to an image from where the **camera** is. The camera is a real **object in
    the map**, placed and aimed like a spot light. The **small preview** shows the
    camera's view and exists **only if the map has a camera**. (This is the "Camera
    Preview".)
  - **What Render does (user):** a terminal-style window shows the work as it goes
    ("baking lights... 1 ... 2 ... 10", then the next job, and so on); it takes as long as
    it needs; the result is a still image saved to `renders/`. The **light baking
    (shadows) is part of the render**, not a separate step. The look is Source /
    Half-Life (Hammer era); Bryce is where the tools (primitives, replicate, sky lab,
    water and cloud planes) come from.
  - **Camera object vs bookmarks (user):** the camera is an object in the map, nothing
    to do with the camera bookmarks (editor-only saved views of the 3D view for moving
    around); both stay.
  - **Camera Preview (user):** an approximation of the final picture, in between a full
    render and the normal editor view. It updates **only when asked** (a keybind or a
    button), never live, and shows only while the map has a camera. **It approximates the
    heavy stuff too, shaders included** (user, 2026-11-18). Exactly what is approximated
    (lights, reflections, water) is settled with the reflections discussion. The normal
    editor view shows materials as a simple placeholder (colour or texture from the
    shader's header) (decided without asking, confirm).
  - **Docs still to rewrite to match:** ARCHITECTURE §9 and the roadmap rows for M11 and
    M12 (they describe a viewport mode and a "low-res look"). Not done yet.
  - **Camera settings (user):** the camera has many options, with presets: field of view
    and, among others, the image size. The user decides the image size, both on the
    camera and in the **render window**, which opens when Render is clicked.
  - **One active camera (user):** a map can hold several cameras, but only one is
    **active**; the Camera Preview and Render use only that one. Making another camera
    active deactivates the old one.
  - **Reflections (user, 2026-11-18):** the user wants several levels of reflection,
    chosen per material and by the render options: environment only (sky; the Y2K chrome
    look), baked reflections of the surroundings, and mirror reflections on flat surfaces
    (water, floors; scene included). Real reflections and bending glass on any shape
    (the Bryce look) need a ray tracer.
  - **No filters (user, firm):** the Source look must come from **how Source renders**
    (baked lightmaps, baked probes, real textures, fog), **never from filters or
    post-effects laid over the picture to imitate it**. Camera effects (dither, palette,
    bloom) are optional extras and not the look.
  - **One renderer (user):** the **Source-style renderer** (fast, on the graphics card,
    baked lighting) is the important one and comes first. The user loves the Source look
    and does not want it made with a ray tracer. A **ray-tracing renderer** (Bryce look)
    is only a **maybe-future expansion, not pressing**; the Source renderer may well be
    enough.
  - **Baked reflections (user, remembered from Hammer / Source 2):** shiny surfaces can
    reflect a baked picture of their surroundings, like Hammer's `env_cubemap` and Source
    2's reflection probes. Reflection **probes are placed by hand** (user: no automatic
    placement in rooms). During Render a "building cubemaps" step captures the views from
    each probe; with no probe a shiny surface reflects the sky. Probe quality is a preset
    (draft / normal / high) in the render options (user: yes). Mirror reflections on flat
    surfaces are drawn live in the render. Level of detail beyond this is open.
  - **Shaders:** GLSL files (agreed earlier) fit the Source renderer. They are
    **drop-in files in Bottega's `shaders/`**, run at render time and in the Camera
    Preview, and are not live in the editor. A material can choose its reflection kind.
    Glass that bends the view (refraction) is only approximate in a Source-style
    renderer; to discuss when materials are planned.
  - **Source features to include (user, 2026-11-18; reference: the Valve Developer
    Community page "Source Engine Features", a feature list, not a method):**
    1. radiosity lightmaps with several bounces (the render log's "baking lights 1..10");
    2. a baked light grid so primitives and props pick up the baked light (they are not
       flat faces, so they can't carry lightmaps; Source does the same for its models);
    3. reflection probes (cubemaps); 4. water with reflection, refraction and Fresnel;
    5. materials with specular, emissive, detail textures and multi-texture blending;
    6. alpha cutouts (foliage, fences); 7. fog; 8. HDR lighting (a later refinement);
    9. **rim lighting** (the user added it); 10. bloom and colour correction only as
    optional extras. Skipped: dynamic shadows, particles, motion blur, wrinkle maps (nothing
    moves in Scalpellino) and physical material properties (gameplay).
  - **Camera options (user, 2026-11-18: all of it):** aimed like a spot light (rings and
    typed values), everything typeable.
    - *Lens and framing:* field of view as degrees or lens length in mm (they convert);
      lens presets (ultra-wide 14, wide 24, normal 50, portrait 85, telephoto 135 mm);
      **roll** (a third ring); **near and far distance** (near cuts away what is closer,
      to look into a room through its wall).
    - *Image:* size presets (1280x720, 1920x1080, 3840x2160, square, portrait 4:5, 3:2,
      4:3) plus any typed width x height, set on the camera and in the render window; a
      **frame guide** showing the picture edges in the Camera Preview.
    - *Placing:* **aim at a point** (typed or clicked); name, marker colour, active
      checkbox (activating one deactivates the others); drawn in the views as a little
      pyramid with its field of view.
    - *Extras:* **orthographic mode** (no perspective; isometric-style pictures);
      **depth of field** (focus distance and blur, a camera effect, so with the optional
      ones, later); **exposure** for when HDR arrives.
    - *Presets:* the user can **save their own presets** (for example a favourite lens
      plus size). They live in Bottega, in **`presets/camera_presets`** (user), so other
      tools share them.
  - **Open:** probe details; the order of the
    renderer part of the roadmap (proposed below, to confirm).

## Milestone 1 plan — Brushes (done)

## Log

The step-by-step log is in [`LOG.md`](LOG.md) (recent) and [`LOG-archive.md`](LOG-archive.md)
(older), newest first.
