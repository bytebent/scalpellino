# Scalpellino — Log

What was done, step by step, newest first. Recent work lives here; when this file gets
long, move the oldest entries to [`LOG-archive.md`](LOG-archive.md) (newest first there
too). Status, plan and open questions are in [`PLAN.md`](PLAN.md).

### 2026-11-20 — The user tried the cameras; Camera Preview decisions (docs only)
- The user noticed the preview is the 3D view seen through the camera, not a render
  (as agreed for M11 until the renderer exists), and that it follows some View menu
  options. Checked: sky and fog already ignore the View menu (a script with the sky
  toggled off and on draws the same preview); the rough light preview, Show primitives /
  Show props and hidden things are followed. The preview also isn't marked out of date
  when only View options change, which can look like it follows them.
- Decided with the user: the preview is a **draft render** (M12's renderer at lower
  settings); preview and render **ignore the View menu** and **include hidden things**;
  fix the current preview in M12, not now. Recorded in PLAN.md (Roadmap item 5),
  ARCHITECTURE §9, HANDOFF.md. No code changed.
- The user will start a new chat for the next milestones.

### 2026-11-20 — Built-in Sky Lab skies, plain sky bottoms out of memory (v0.11.2)
- Asked by the user as an aside before M12 (they also said the v0.11.1 choices seem good;
  they haven't tried the cameras yet). They asked for the skies Sky Lab was tested with
  as built-in presets, and agreed to dropping the plain grey bottom of skies from memory,
  "a bit after the fade just to be sure".
- **Built-in presets** (`core/sky_presets`): 13 skies made by running the settings of the
  Sky Lab test scripts (v104 / v105 / v106) through Sky Lab and saving them: Clear day,
  Sunset, Darker sunset, Painted dusk, Plain night, Clouds, Sunset clouds, Round clouds,
  Moonlit night, Comets, Solar eclipse, Lunar eclipse, Rainbow and halo. "Fog in the sky"
  is left out: its fog belongs to the map, not to the saved sky. Listed under "Built-in"
  in Load a saved sky (works without a Bottega).
- **Memory:** `skyRowsToKeep` (`core/sky`) finds the plain block at the bottom of a
  panorama (every pixel within 2 of the bottom colour) and keeps the rows above it plus 5
  degrees (never above the horizon; only if 5% or more goes). `SkyLibrary` crops the
  texture and keeps `cover` / `below`; the sky sphere draws the textured part over
  `cover` and the rest in the plain colour (3D view and Camera Preview). Files unchanged.
- **Tests:** core 4660 checks, 0 failed (presets read back to their own text; the cut on a
  made-up picture, tolerance, the margin, all-one-colour, and on four real Sky Lab skies:
  everything dropped is the plain colour, 30–45% of the rows go). UI 28 of 28 at 1x and 2x
  (new `v112_sky_presets`: built-in presets baked, the kept part, looking past the edge
  of the floor at the fade (no seam), without a Bottega, a photo sky kept whole). Idle 0
  ticks at 1x and 2x.
- **Screenshots:** `m11-07-builtin-sunset-clouds.png`, `m11-08-sky-cut-horizon.png`,
  `m11-09-builtin-moonlit-night.png`.
- **Next:** report; then Render (M12) when the user says go.

### 2026-11-20 — Cameras, step 2: Camera Preview and presets (v0.11.1)
- The user confirmed the v0.11.0 choices ("seems sensible") and said carry on.
- **Presets:** `cameraPresetToString` / `applyCameraPreset` in `core/camera` (settings
  only, "key value" lines, partial and bad files handled); Save / Load in Properties
  (selected camera, one undo step; else the next camera's settings); `.camera` files in
  the Bottega's `presets/camera_presets`.
- **Camera Preview:** docked panel under Properties (dock layout v6: the right column is
  now Textures / Properties / Camera Preview), only while the map has a camera; drawn
  only on Cmd+K, its Update button or World > Update Camera Preview; an "out of date"
  line when the map or the active camera changed since. `Viewport::setLens`: a camera
  object as the 3D camera (field of view across the longer side, near / far as clip
  planes, orthographic), the sky sphere kept between near and far, no grid / axes /
  editor outlines / markers. The primitives' shader got parallel rays for orthographic
  cameras.
- **Bugs found while testing:** primitives through an orthographic camera came out as
  ragged specks: the rays started 1 km away and the sphere / torus maths lost precision in
  floats; they now start on the camera's plane. (Also a typo in the new script made a
  flat sphere; fixed the script.)
- **Tests:** core 4629 checks, 0 failed (presets: round trip, what is kept, partial and
  bad files). UI 27 of 27 at 1x and 2x (new `v111_camera_preview`: nothing without a
  camera, drawn on request only, out of date after edits and after switching the active
  camera, presets save / load / undo, a preset into the next camera, file-name cleaning,
  portrait, orthographic, near cutting, delete all cameras and undo). Idle 0 ticks at 1x
  and 2x with the preview drawn (`rich_map` now draws it once).
- **Screenshots:** `m11-03-camera-preview.png`, `m11-04-camera-preview-portrait.png`,
  `m11-05-camera-preview-ortho.png`, `m11-06-camera-preview-near.png`.
- **Milestone 11 is done.** Next: report to the user; then Render (M12), when they say go.

### 2026-11-20 — Cameras, step 1: camera objects (v0.11.0)
- **Core** (`src/core/camera.*`): the camera (name, position, pitch / yaw / roll, field of
  view across the longer side, lens mm on a 36 mm sensor and back, near / far, picture
  size, orthographic and its width, exposure, marker colour, active), lens and size
  presets, axes and angles (`cameraBasis` / `setCameraBasis` through the primitives'
  `Mat3`), ring turns, roll, aim at a point, the picture frame at a distance, the lines
  that draw it, clamping. Document: cameras, their selection, `CameraChange`, add /
  modify / delete / activate commands that keep exactly one active camera. `.map`:
  `scal_camera` point entity (angles Hammer-style), a file with none or several active
  opens with one. `dirCircle` (a ring round any direction) in `world.hpp`.
- **Editor:** Camera tool (Shift+K; World and Tools menus, tools strip); drawing in all
  views (pyramid / box, "up" mark, diamond marker, white dot on the active one, rings plus
  the yellow roll ring); picking, moving, turning and rolling in `object_edit`; Properties
  (`editor_cameras.cpp`: name, active, position, angles, lens, presets, near / far,
  picture size, orthographic, colour, exposure, aim at a typed point, aim at a clicked
  point, the camera tool's list of cameras with the active dot); World menu: Make selected
  camera active, Aim selected cameras at a clicked point; Delete, Cmd+D (new name, never
  active), select tool, 3D rectangle select, Esc. Help → Controls and README.
- **Script commands:** `tool camera`, `cameraset`, `cameraedit`, `cameraselect`,
  `cameraactive`, `cameraaim x y z | click`; `expect cameras`, `camerasel`, `cameraactive`,
  `cam <key>` (DEV.md).
- **Bug found while testing:** the roll ring's "seen edge-on" test was inverted (it hid
  the ring exactly where it is a full circle); fixed before the release.
- **Tests:** core 4622 checks, 0 failed (new `testCameras`: lens and angle conversions,
  axes, read back of angles, ring turns, roll, aim, frame and portrait sizes, orthographic,
  clamping, one active camera through add / activate / modify / delete / undo /
  duplicate, selection, `.map` round trip with unknown keys, files with several or no
  active cameras). UI 26 of 26 at 1x and 2x (new `v110_cameras`: place two, undo, the
  Z ring, the roll ring in the side view, aim clicked and typed, active switching with
  undo, delete the active one, duplicate, typed options, select tool, drag, save and
  open). Idle 0 ticks at 1x and 2x with two cameras in the map (one orthographic);
  control 885 ticks.
- **Screenshots:** `docs/screenshots/m11-01-cameras.png`, `m11-02-camera-properties.png`.
- **Next:** v0.11.1, saved camera presets and the Camera Preview.

### 2026-11-20 — Milestone 11 decided with the user (docs only)
- The user confirmed the roadmap order (cameras → Render with materials → water / cloud
  planes → camera effects); **export is not confirmed**.
- Milestone 11 choices: the Camera Preview shows the editor's 3D look through the camera
  until the renderer replaces it (M12); it lives in a **docked panel**; no extras beyond
  the agreed list; two releases, v0.11.0 (camera objects and options) and v0.11.1
  (presets and the preview). Recorded in PLAN.md (Roadmap item 4), ARCHITECTURE §9 and
  §15, HANDOFF.md, before starting (user's request).
- **Next:** v0.11.0.

### 2026-11-20 — Sky Lab, step 3: night sky and effects (v0.10.6)
- **Core:** the moon (free place, phase with the lit side toward the sun, size, intensity,
  glow, earthshine, softness, colour, texture: a 2:1 map on a ball or a square photo,
  brightened to a bright grey average); solar eclipse (cover from circle overlap, the sky
  darkened by what is left of the sun, a corona when nearly total, the moon's face dark);
  lunar eclipse (Earth's shadow across, eclipse colour); halo rings round sun and moon
  (red inside, blue outside, secondary); stars (Earth's sky from the Yale Bright Star
  Catalogue compiled in, or a random field with the real sky's brightnesses; latitude and
  star time; placed once, drawn row by row; only where the sky is dark); comets (head and
  a slightly curved tail away from the sun); the rainbow (42 and 51 degrees, colours in
  order, only while the sun is up); Blend with Sun for haze and fog; fog in the sky from
  the map's fog with base height and thickness (`SkyExtras`); scene effects stored. The
  moon lights clouds and the night sky. `solarEclipseCover`. Fog in the map: two more
  numbers, written only when set. Skies of v0.10.4 / v0.10.5 come out byte for byte the
  same (checked against the v0.10.5 code on 7 skies).
- **Bugs found while testing:** stars near the zenith came out as long bars (the pixel
  direction was clamped, breaking the distance) — fixed; a moon exactly centred on the sun
  gave 99.99996% cover (acos precision) — the angle now comes from the chord. **JPEG and
  TGA never loaded** (raylib leaves them off): `.jpg` / `.tga` textures and skies and JPEG
  textures inside models were silently empty. Both turned on in `cmake/Deps.cmake`.
- **Editor:** the Sun & Moon tab in sections (Sun, Moon with its own compass, "Move moon
  onto sun" and the NASA credit, Eclipses with the cover shown, Stars and comets, Rings,
  Scene lighting); Atmosphere in sections (Haze, Fog shared with World > Environment,
  Rainbow, Blend with Sun, Scene effects); tabs scroll inside a fixed height; saved skies
  (Load, Save sky) and Randomize sky; the moon image loaded once; the built-in moon
  bundled with the app; moon images kept out of the sky list; out of date also when the
  map's fog changes; World > Environment gained fog base height and thickness. Script
  commands `skylabsection`, `skylabpreset`, `skylabrandom`, fog in `skylabset`,
  `environment fogheight`, `expect fog`.
- **Tests:** core 4572 checks, 0 failed (new `testSkyLabNight`: text form and clamping,
  old maps, fog in the map, eclipse cover, total / partial eclipse darkness, moon phases
  and its lit side, a square moon texture, lunar eclipse red, stars at night and not by
  day, Sirius in its place, comets, rings, rainbow colour order and none at night, fog in
  the sky, Blend with Sun). UI 25 of 25 at 1x and 2x (new `v106_night`: the night sky,
  comets, eclipses, rainbow, fog, saved skies, Randomize, fog out of date and Rebake, undo,
  save and open). Idle 0 ticks at 1x and 2x with moon, stars, rings and rainbow in the open
  Sky Lab window; control 908 ticks. Bake 2048 with moon, stars and clouds: 0.5 s on 2
  threads here; 8192: 6.9 s.
- **Screenshots:** `docs/screenshots/m10c-11-moon-stars.png`, `m10c-12-comets.png`,
  `m10c-13-solar-eclipse.png`, `m10c-14-lunar-eclipse.png`, `m10c-15-rainbow.png`,
  `m10c-16-fog-in-sky.png`.
- **Next:** report; then cameras and Camera Preview when the user says go.

### 2026-11-20 — Sky Lab, step 2: clouds (v0.10.5)
- **Moon credit** fixed as the user asked: "NASA's Scientific Visualization Studio"
  (THIRD_PARTY.md, new `assets/moon/CREDIT.txt`, PLAN.md; separate commit).
- **Core:** stratus and cumulus layers (on, cover, frequency, amplitude, turbulence,
  complexity each), cloud height (also widens the haze), cumulus colour, spherical clouds,
  pattern seed, cloud shadows (stored). Stored as `stratus=`/`cumulus=` (six numbers) and
  five more fields; maps from v0.10.4 have no clouds. Gradient noise with a 16-direction
  table, fBm with octaves faded below a pixel, domain warp for turbulence, cells for
  spherical clouds, stratus streaked; layers on curved shells, faded far away; lit side
  from the sun's light, shaded side blue-grey, a silver lining toward the sun, dark at
  night. `rowsParallel` (threads; the same bytes). Speed: first version 2048 in 1.1 s and
  8192 in 17 s on 2 threads; the gradient table and skipping invisible far clouds brought
  that to 0.4 s and 5.5 s.
- **Editor:** the Clouds tab (both layers, cumulus colour, height, spherical, seed with New
  pattern, cloud shadows); the bake runs on all threads; `skylabtab` script command.
- **Tests:** core 4538 checks, 0 failed (new `testSkyLabClouds`: text form, bad and old
  input, no cover = no clouds, more cover = more cloud, nothing below the horizon, seeds,
  threads = rows, white by day / warm at sunset / dark at night). UI 24 of 24 at 1x and
  2x (new `v105_clouds`). Idle 0 ticks at 1x and 2x with clouds in the open Sky Lab
  window; control 895 ticks.
- **Screenshots:** `docs/screenshots/m10c-07-clouds-tab.png`, `m10c-08-sunset-clouds.png`,
  `m10c-09-spherical.png`, `m10c-10-baked-clouds.png`.
- **Next:** report; v0.10.6 night sky and effects.

### 2026-11-19 — Sky Lab, step 1 (v0.10.4)
- **Design agreed with the user** from the Bryce 7 guide's Sky Lab chapter (recorded in
  PLAN.md Roadmap item 3 and ARCHITECTURE §6 "Sky Lab"; commit before the code).
- **Core** (`src/core/skylab.*`): all v0.10.4 settings plus the scene-lighting ones stored
  for the renderer; `_scal_skylab` text form (unknown fields kept, bad numbers keep the
  default, values clamped); the sun lock's angle conversions; Randomize (seeded); the
  generator: a table of the sky from single scattering (Rayleigh + Mie) per sky, a colour
  grade per mode, Custom (three colours) and Off (one colour), the sun's disk (size,
  horizon illusion, rim, reddening low down), the haze band, the ground below the horizon,
  a small dither. Row by row so a bake can show progress. 2048x1024 in 0.16 s, 8192x4096
  in 2.2 s here (first version: 0.6 s and 9.7 s; per-row and per-column work moved out of
  the pixel loop, an sRGB table).
- **Editor** (`src/app/editor_skylab.cpp`): World > Sky Lab... (a window that doesn't block),
  the panorama strip, tabs, a sun compass, sliders with typed fields; the 3D view shows the
  preview while open (256 wide while dragging, 1024 on release, nothing while idle); Apply /
  OK bake over several frames with a progress bar into the Bottega's `skies/skylab/`, then
  one undo step (environment + the sun light when locked); no Bottega: a message and a
  button to set one; World > Rebake Sky Lab sky when the locked light has turned or the PNG
  is missing; a missing PNG is baked again when the map is opened.
- **Tests:** core 4521 checks, 0 failed (was 4482; new `testSkyLab`: text form, map round
  trip, lock angles, sun at the brightest pixel, ground colour, night, Darker, Off exact,
  Custom colours, disk size, seeds). UI 23 of 23 at 1x and 2x: new `v104_skylab` (preview
  with the View sky off, Apply / Cancel / OK, undo steps, lock turning the light, out of
  date, Rebake, undo / redo, save and open, a missing PNG made again, no Bottega); four
  wrong expectations checked to fail. Idle 0 ticks at 1x and 2x with the Sky Lab window
  open (added to `rich_map.script`); control 900 ticks.
- **Screenshots:** `docs/screenshots/m10c-01-skylab-default.png`, `m10c-02-sunset-preview.png`,
  `m10c-03-darker-baked.png`, `m10c-04-custom.png`, `m10c-05-atmosphere-off.png`,
  `m10c-06-no-bottega.png`.
- **Also:** the user's NASA moon map added as `assets/moon/moon_lroc_color.jpg` (1024x512,
  for v0.10.6); THIRD_PARTY.md updated (and its split table joined).
- **Next:** report; v0.10.5 clouds.

### 2026-11-19 — Editor split, prop benchmark, faster props (v0.10.3)
- **Asked by the user** before Sky Lab: split `editor.cpp` (both steps), a prop benchmark
  for our own use, target hundreds to thousands of props.
- **Split, step 1:** the 110 Editor functions moved unchanged by a script into nine files
  by topic (the largest now ~750 lines); every original line checked present exactly
  once; shared helpers in `editor_util.hpp`, member templates in `editor_templates.hpp`.
- **Split, step 2:** the 666-line `stepScript` if-chain (127 branches) became tables:
  `editor_script_commands.cpp` (91 commands) and `editor_script_expects.cpp` (36 checks,
  keyed by the word after "expect"; all 402 expect lines in the scripts reach the same
  check as before; a must-fail script still fails). Selection landing moved into the
  core as `landSelection` with its own test.
- **Benchmark** (`tests/bench/`): found Snap to Land testing every obstacle in the map (50
  props among 5000: 56 s), 2D views drawing everything off screen, and one 3D draw call
  per prop. **Fixed:** landing only tests what is under the footprint (56 s -> 90 ms);
  2D views skip what is off screen (43 -> 5 ms); 3D frustum culling and GPU instancing
  (5000 crates: 31 -> 15 ms, 4909 draw calls -> 1). Table in DEV.md.
- **Checked:** screenshots with culling off are identical; instanced vs one-call-per-prop
  differ in 1–6 edge pixels (rounding); 2D views identical to v0.10.2's screenshots.
- **Tests:** core 4482 checks, 0 failed. UI 22 of 22 at 1x and 2x, and 22 of 22 with the
  fallback drawing path (`SCAL_PROPS_PLAIN=1`). Idle 0 ticks at 1x and 2x.
- **Next:** report; Sky Lab once the user says go.

### 2026-11-19 — Landing fixes, 3D rectangle select, show / hide (v0.10.2)
- **Asked by the user:** Snap to Land went wrong with things on top of each other; a
  rectangle select in the 3D view; a View option to hide all props and all primitives;
  check review notes from another AI.
- **Snap to Land:** before, an object was first lifted above everything whose box
  overlapped its footprint, so it climbed onto a neighbour it overlapped, a shelf above it,
  or a selected copy not landed yet. Now only surfaces under its middle height count
  (per ray, per obstacle), upward rays start only from top surfaces, and several selected
  objects land on each other only if one started entirely above the other (editor). Props
  rest on mesh points (vertices, the 64 lowest, axis extremes, points across faces and
  edges for low-poly models), not their box corners (the review's point).
- **Rectangle select** (Select tool, 3D view): wholly-inside rule, Cmd adds, Esc restores;
  drawn by the viewport from `Scene::selectRect`.
- **Show primitives / Show props** (View menu): `Document::setPrimsShown` /
  `setPropsShown`; respected by the scene, picking, the prop tool's surface ray and Snap
  to Land; Show all and new objects show them again.
- **Review notes checked:** NaN / inf in `.map` (fixed: brushes and terrain data refuse
  the file, entity keys keep their default); landing on box corners (fixed, above);
  unsaved after undo (fixed: commands get serials, `dirty()` compares the history top);
  model name clashes (fixed: listed in the Props panel); version 0.0.1 (fixed: CMake reads
  the newest `docs/releases` file); editor.cpp size and prop instancing: parked in PLAN.md.
- **Tests:** core 4475 checks, 0 failed (was 4412). New core tests failed on the old code
  first: landing (5 failures), map numbers (11). UI 22 of 22 at 1x and 2x: new
  `v102_landing` (stack stays, overlapping crates reach the floor, shelf, wall) and
  `v102_rect_select` (rectangle, Cmd adds, Esc, hidden never picked, Show all);
  `v101_random_replicate` now checks all 40 scattered crates rest on the floor after
  Cmd+J (16 didn't before the editor change). Idle 0 ticks at 1x and 2x.
- **Screenshots:** `docs/screenshots/m10-04-rectangle-select.png`,
  `docs/screenshots/m10-05-props-and-primitives-hidden.png`.
- **Next:** report; Sky Lab once the user says go.

### 2026-11-19 — Random Replicate (v0.10.1)
- **Before:** checked the v0.10.0 commit (made by another model by mistake): builds, core
  4397 / 0 failed, UI 19 / 19 at 1x and 2x, idle 0 ticks, privacy rules kept. The user
  confirmed M10's choices; fixed a doubled "Sky Lab" in HANDOFF.md.
- **What:** `core/replicate`: `RandomReplicateSpec` and `randomReplicate()` (graphics-free):
  per copy a random offset, turn and size inside ranges, from a seed, through our own
  SplitMix64 so every machine gets the same copies; copy i depends only on the seed and i,
  nine draws per copy in a fixed order. Editor: the Random Replicate window (Cmd+Shift+R,
  Edit menu; non-modal, live preview, Return / Esc from the views, New seed), sharing the
  preview and the one-undo-step commit with Multi-Replicate (`replicateSource`,
  `commitCopies`); only one of the two windows open at a time. Brushes move by whole grid
  steps while snapping is on. Script commands `randreplicate`, `randreplicateopen`,
  `randreplicatepreview`, `expect randomizing`. Help → Controls, README and DEV.md updated;
  Multi-Replicate's window text now mentions props.
- **Tests:** core 4412 checks, 0 failed (was 4397): ranges kept, whole range used, even
  sizes, same seed same copies, fewer copies keep the first ones, pinned numbers for seed
  1, per-axis sizes, brushes on the grid, selection turns as one piece, lights turn. UI 20
  of 20 at 1x and 2x, including the new `v101_random_replicate` (props scattered and
  landed, undo / redo, window Esc / Return, one window at a time, uneven primitives,
  brushes on the grid). Idle: `rich_map.script` now also leaves the Random Replicate window
  open with 30 copies previewed: 0 ticks at 1x and 2x; control busy (868 ticks).
- **Screenshots:** `docs/screenshots/m10-02-random-replicate-window.png`,
  `docs/screenshots/m10-03-random-replicate-landed.png`.
- **Next:** report v0.10.1; Sky Lab once the user says go.

### 2026-11-19 — Milestone 10: Props (v0.10.0)
- **What:** static models placed in the map. Core (graphics-free): `model.*` (meshes,
  materials, bounds, ray tests), `prop.*` (a prop, its box, surface points, ray tests),
  `landing` generalised to any object, `replicate` carries props; io: `glb_loader.*` (cgltf:
  node transforms, triangles, base colour and textures, cutout, no draco / meshopt),
  `model_store.*` (Bottega `models/` scan, lazy load), `scal_prop` entity in `map_format`.
  Render: `model_library.*` (one upload per model, one draw per prop, own shader with editor
  shading, cutout and fog). Tools: `prop_tool.*`; `object_edit` picks, moves, turns and
  scales props with the shared handles (scaling is even by default, Shift = one axis, also
  for primitives). Editor: Props panel (search), Properties for props, Props menu,
  Shift+P, Snap to Land / Ground, Duplicate, Delete, Multi-Replicate preview and copies,
  script commands `tool props`, `propset`, `prop`, `propselect`, `propedit`,
  `expect props|propsel|proppos|proprot|propscale|propmodel|hasmodel`.
  cgltf's global symbols are renamed (raylib bundles its own cgltf).
- **Tests:** core 4397 checks, 0 failed (was 4298). UI suite 19 of 19 at 1x and 2x,
  including the new `v100_props` (place with a 2D click, Return / Esc, typed values, select,
  drag, edit, Snap to Land, duplicate, replicate, delete, undo, save and open, Bottega off
  keeps the prop as a box). `m9_primitives` and `v093_pieces_replicate` were adjusted for
  the even-by-default handles. Idle test: rich map now has three props (one missing from
  the Bottega, two selected): 0 ticks at 1x and 2x, control run busy (878 ticks).
- **Test assets:** `tests/ui/assets/models/crate.glb` and `post.glb`, generated by a short
  script (a textured box each; embedded PNG).
- **Screenshot:** `docs/screenshots/m10-01-props-placed.png`.
- **Next:** Random Replicate, once the user says go.

### 2026-11-19 — Bottega basics (v0.9.4)
- **What:** the shared folder. `src/core/bottega.*` (graphics-free): check a folder
  (ready, empty, has files but no marker, missing, newer version, damaged), make it
  (sub-folders plus `bottega.txt` with `bottega 1`, never overwrites or deletes), name
  keys (`textures/walls/Brick_Red`: relative path, no extension, spaces as `_`). Texture
  and sky libraries scan the Bottega first, then extra folders, then built-ins; the
  built-ins keep plain names. Editor: File > Bottega folder... dialog (path, Browse,
  status, confirm before making sub-folders), setting kept per user, File > Rescan
  Bottega and Cmd+R, Textures panel Rescan button; Controls and README updated. Script
  commands `bottega`, `rescan`, `bottegadialog`, `copy`, `expect bottega|hastexture|hassky`.
- **Tests:** core 4298 checks, 0 failed. UI suite 18 of 18 at 1x and 2x, including
  `v094_bottega` (dialog, states, nothing rescans by itself, Rescan finds a new file, a
  Bottega sky). Idle test now builds a Bottega and uses its texture: 0 ticks at 1x and
  2x, control run busy.
- **Screenshot:** `docs/screenshots/m9-09-bottega-dialog.png`.
- **Next:** milestone 10, Props.

### 2026-11-18 — Idle CPU test
- **What:** `tests/ui/idle.sh` + `tests/ui/idle/rich_map.script`. The script builds a map
  with terrain (raised and painted), brushes, a dome, a blended primitive combination, a
  torus, a cube, three lights, sky, ambient, fog, a bookmark and the figure; saves it and
  **re-opens the saved map**; switches on sky, light preview, fog and figure; selects the
  primitives; ends without `quit`. The harness waits for the script's screenshot, then
  reads the process's CPU time twice, 10 s apart. Pass = 0 ticks.
- **Finding:** no new app command was needed: a scripted run is busy only while commands
  are left, so a script without `quit` leaves the app genuinely idle.
- **Guards:** the script `expect`s its contents and the harness fails on any error the app
  printed (checked: a wrong expect fails the test); `--control` runs a busy script and
  must see >= 10 ticks.
- **Result:** idle 0 ticks over 10 s at 1x and at 2x; control 891 ticks in 5 s. Core tests
  4252 checks, 0 failed; all 17 UI scripts pass (1x baseline before the change; the app's
  code is unchanged).
- CI: the `linux` job runs the control, 1x and 2x. DEV.md: Idle check section rewritten.
- Also this session: built the app in the workspace (deps cloned at the pinned refs).

### 2026-11-18 — Camera presets live in Bottega (docs only)
- User: camera presets are saved in Bottega, `presets/camera_presets`. Recorded in PLAN
  (Bottega layout, camera options) and ARCHITECTURE §7. No code changed.

### 2026-11-18 — Camera options settled (docs only)
- The camera's option list, agreed with the user (lens, roll, near / far, image sizes,
  frame guide, aim at a point, orthographic mode, depth of field later, exposure later,
  own presets). Recorded in PLAN and ARCHITECTURE §7. Open: where presets are saved.
  No code changed.

### 2026-11-18 — Which Source features the renderer includes (docs only)
- From the Valve wiki's Source feature list, the user picked: radiosity lightmaps with
  bounces, a baked light grid for primitives and props, reflection probes, water
  (reflection / refraction / Fresnel), rich materials, alpha cutouts, fog, rim lighting,
  HDR later; bloom and colour correction optional. Recorded in PLAN and ARCHITECTURE §9.
  No code changed.

### 2026-11-18 — No filters for the Source look; materials belong to the renderer (docs only)
- User: the Source look must not be made with filters; and materials should not come after
  the render. Recorded: the look comes from how Source renders (baked lightmaps and
  probes, real textures, fog); the material system is part of the Render milestone, with
  water / cloud / endless planes as a step after. "Chunky period looks" wording removed
  from ARCHITECTURE §9; the rule added to CLAUDE.md. No code changed.

### 2026-11-18 — One renderer; docs rewritten around renders and cameras (docs only)
- User: no automatic probe placement, probe quality as presets, and the ray tracer is only
  a maybe-future expansion (the Source renderer is the one). Recorded in PLAN.
- **Rewritten to match everything decided today:** ARCHITECTURE intro, §7 (cameras,
  reflection probes, bookmarks are not cameras), §9 Rendering (editor shows nothing
  moving; Camera Preview on request; Render window and terminal log; Source look;
  reflections; materials), §10 (pictures are the output; `.glb` optional and last),
  roadmap table; PLAN roadmap and status; README intro.
- **Order of the renderer part is a proposal** (cameras and preview, render, materials,
  camera effects, export), waiting for the user's confirmation. No code changed.

### 2026-11-18 — Source renderer first; baked reflections (docs only)
- User confirmed the Source renderer comes first, and reminded that Hammer / Source 2 bake
  reflections (cubemaps / probes). Recorded in PLAN with the probe proposal marked as
  unconfirmed. No code changed.

### 2026-11-18 — Reflections: all three levels, two renderers (docs only)
- User: wants environment, mirror and full reflections depending on material and render
  options, and two renderers (a Source-look one, not ray traced, and a ray tracer).
  Recorded in PLAN with the proposed consequences marked as unconfirmed. No code changed.

### 2026-11-18 — The preview approximates the heavy stuff, shaders included (docs only)
- User: the Camera Preview should show an approximation of the heavy effects, including
  shaders (corrects the earlier "no shaders" assumption). Reflections to be discussed
  next. Recorded in PLAN. No code changed.

### 2026-11-18 — Camera options and the active camera (docs only)
- User: cameras have many options with presets (field of view, image size...); the size
  is set on the camera and in the render window that opens on Render; a map can hold
  several cameras with exactly one active, the only one used by the preview and renders.
  Recorded in PLAN ("Direction"). No code changed.

### 2026-11-18 — Camera is not a bookmark; the preview updates on request (docs only)
- User: the camera object has nothing to do with the bookmarks (both stay); the Camera
  Preview is an approximation of the final picture that refreshes only on a keybind or
  button. Recorded in PLAN ("Direction"). No code changed.

### 2026-11-18 — How Render and the camera work, in the user's words (docs only)
- Recorded in PLAN ("Direction"): the camera is an object in the map; the small preview
  shows its view and only exists with a camera; Render runs as a terminal-style job
  (including baking the lights) and saves an image; nothing moves in the editor. The old
  M11 / M12 / ARCHITECTURE §9 wording is flagged as out of date, to be rewritten once the
  open points are answered. No code changed.

### 2026-11-18 — Direction corrected: renders are the goal (docs only)
- User: shaders work at render time and show in the final image; nothing animates in the
  editor; the point is exporting pictures, `.glb` and other editors don't matter now.
  Recorded in PLAN ("Direction") and HANDOFF. The roadmap order and what the renderer is
  stay open until discussed; the shader format agreement is marked as dependent on that.
  No code changed.

### 2026-11-18 — Shader format agreed; material shaders come first (docs only)
- Shader format agreed (GLSL with a settings header). User: material shaders, water and
  clouds matter far more than the Camera Preview effects; recorded as a priority, with the
  roadmap order left open until discussed. No code changed.

### 2026-11-18 — Bottega decided, except the shader format (docs only)
- Recorded in PLAN ("Bottega"): normal folder set once per tool; maps store paths relative
  to the root; model folders self-contained; **no automatic rescanning, a Rescan command
  (menu, button, shortcut)**; a marker file with a version; existing texture and sky
  folders stay as extras; `shaders/` is a drop-in folder for any shader.
- Knock-on: props reload via Rescan (was: on window focus); props live in `models/`.
- Open: the shader file format (proposal in PLAN). No code changed.

### 2026-11-18 — Props decisions and the idle test, recorded (docs only)
- Discussed with the user and recorded in PLAN ("Open questions"): props are instanced,
  v1 lighting = editor shading + fog, uniform scale by default (also for primitives, a
  change to come), Shift+P and a Props panel, placing like a primitive with Snap to
  Land, cgltf as the loader. File placement waits for Bottega, to be settled first.
- Idle CPU test agreed: it must run with a map open; plan in PLAN.
- Not built yet; no code changed.

### 2026-11-18 — DEV.md and a map of the docs (docs only)
- **`docs/DEV.md`** (new): build (Mac, Linux, AI workspace), core tests and probes, UI
  scripts (running, 1× / 2×, duplicate-ID check, writing tips), the **script language
  reference** (moved out of the comment above `Editor::loadScript`, which now points to
  DEV.md), command line, idle check, CI, releasing, per-step checklist, workspace quirks.
- **Moved, not copied:** PLAN's "How to work in this repo" and HANDOFF's "Workspace
  quirks" and "Habits that worked" are now one-line pointers to DEV.md.
- **Docs map:** one sentence per doc in HANDOFF's reading order and in the README;
  CLAUDE.md points to DEV.md; HANDOFF's intro no longer mentions "mistakes".
- **Found:** the idle CPU check ("0 ticks over 10 s") is a manual probe; nothing automates
  it and the method wasn't written down. Noted in DEV.md as an open item.
- No build run: the only code change is a comment in `src/app/editor.cpp`.

### 2026-11-18 — Docs cleanup (docs only)
- **Log split:** the log moved out of `PLAN.md` into `LOG.md` (recent) and
  `LOG-archive.md` (2026-11-16 and older, from Milestone 8 back to the skeleton). PLAN keeps
  a pointer; `CLAUDE.md` and `HANDOFF.md` now say to log in `LOG.md` and to move old
  entries to the archive when it gets long.
- **Props rule:** ARCHITECTURE rule 7 no longer bans props (static props are wanted;
  still no doors, triggers, physics or scripting).
- **Godot:** README and ARCHITECTURE intro say `.glb` export instead of Godot; Godot-only
  wording removed from the primitives, file-format and roadmap passages. The export tiers
  stay as ideas.
- **Names:** repo layout says camera preview (not y2k pipeline).
- **HANDOFF:** the "mistakes already made" list is gone (the rules it repeated are in
  `CLAUDE.md`); a short Privacy section stays; the discuss-first / write-it-down-the-same-day
  habit moved into "The user".

### 2026-11-18 — Plan brought up to date (docs only)
- User pointed out the plan sections lagged behind the log. Rewrote "Current status",
  added Milestone 8 / 9 sections, replaced the old roadmap (9 Renderer, 10 Y2K, 11
  Export) with the agreed one, added a "Bottega" section with the user's answers
  (no `heightmaps/`; palettes decided with Mazzuola; `renders/` = Scalpellino's renders)
  and the open `shaders/` proposal. ARCHITECTURE: roadmap table and "Y2K preview" →
  "Nano-Preview".
- User: "you forgot about the skylab tool". It was agreed in a separate Bryce chat and
  never written down. Discussed, then agreed: baked to a panorama with live preview;
  everything Bryce's Sky Lab has; a "lock to sun light" tick box when the map has a sun
  light. After Random Replicate, before M11.
- User: Godot will probably never be used with Scalpellino, so it must not weigh on
  choices. "Nano-Preview" is now **Camera Preview**.
