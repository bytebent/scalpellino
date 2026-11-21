# Scalpellino — Log archive

Older log entries, moved out of [`LOG.md`](LOG.md), newest first. Nothing here is
needed to continue the work; it is the history of how each milestone was built.

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

### 2026-11-18 — v0.9.3: single pieces of a combination, Multi-Replicate as a window
- **Pieces on their own:** a plain click still selects the whole combination;
  **Option+click** selects just the piece under the cursor (in 2D the nearest outline
  wins, so a subtract piece hidden inside the shape can be picked). A piece already
  selected on its own (Option+click, or picked in Properties) stays that way when
  clicked, so dragging, rings and size handles act on it alone. `pickPrim` now also
  reports the hit piece. Status bar, Help → Controls and README say so.
- **Multi-Replicate is a window, not a modal dialog**: pan, zoom, orbit and fly in the
  views while the preview stays up. Return / Esc work from the views too; the window has
  Create / Cancel and a close box.
- **Fix found by the new test:** grabbing a size handle without moving it snapped an
  off-grid size (120 on grid 16 became 128). The change is snapped now, not the size.
- Script commands `replicateopen`, `expect replicating`; new UI script
  `v093_pieces_replicate` (17 scripts).
- **Tests:** core 4252 checks pass; all 17 UI scripts pass at 1x and 2x.
- Screenshots: `screenshots/m9-07-piece-moved.png` (the subtract sphere moved and resized
  alone), `screenshots/m9-08-replicate-window.png` (window up, 3D view turned to look at
  the preview).

### 2026-11-18 — v0.9.2: units in cm, blending, Snap to Land, Multi-Replicate
- **Units:** 1 unit = 1 cm (user's choice). Scale figure 175 units; fly speed, terrain
  brush, light reach, fog and spiral-stair defaults in cm; far clip 2 km, sky at 1 km;
  docs (exports to metres ÷ 100, `.glb` import × 100). Older maps keep their numbers
  (Transform → scale by 2.54 converts an inch map). Mazzuola's docs still say inches:
  to change when its repo can be written (read-only here).
- **Blending primitives** (user's idea): `MathPrim::blend`, `primDistance`,
  `comboDistance`, `smoothMin/Max`, ray marching for blended combinations (core + shader),
  colours melt across joints; Properties field; saved as `blend`.
- **Snap to Land / Ground** (`core/landing`): primitives only (user), onto brushes,
  terrain (its surface, not the flat face) and other primitives; combinations as one;
  sunk objects come up. Transform menu, Cmd+J, Properties button; Snap to ground dialog.
- **Multi-Replicate** (`core/replicate`): brushes offset only (user), primitives turn and
  scale, lights turn; pivots: selection (travels), origin, point; groups and
  combinations copied as new ones; dialog with live preview; one command.
- **Fixed:** Edit → Duplicate / Delete were greyed out with only lights or primitives
  selected.
- **Script:** `snapland`, `snapground`, `replicate`, `replicatepreview`, `primedit blend`.
  New script `v092_blend_land_replicate`.
- **Tests:** core **4252 checks, 0 failed** (distance = 0 at every kind's exact hits,
  blended neck, soft rim, landing on flat / sunk / 45° slope / raised terrain / another
  primitive / as a combination / nothing below, replicate rows, a 12-ring, scaled steps,
  lights). UI: all 16 scripts pass at 1× and 2×. Idle with a blended combination: 0 CPU
  ticks over 10 s.
- **Screenshots:** `m9-05-blend-and-land.png`, `m9-06-replicated-ring.png`.

### 2026-11-18 — Fix: "conflicting ID" error in Properties (v0.9.1)
- **Reported by the user** on v0.9.0: Dear ImGui's "2 visible items with conflicting ID"
  error. Cause: with the Primitive tool (or the Light tool) active and objects selected,
  Properties shows the selected objects' fields above the "new" fields with the same
  labels (colour, texture repeats, brightness…). Each section now has its own ID scope.
- **Now caught automatically:** UI scripts fail when two widgets share an ID in a frame
  (Dear ImGui's item hooks, `IMGUI_ENABLE_TEST_ENGINE`, `src/ui/imgui_checks.cpp`, on in
  scripted runs only; ImGui itself only notices while the mouse hovers them). It found
  the light panel's duplicates too.
- **Tests:** core 3649 checks, 0 failed; all 15 UI scripts pass at 1× and 2× (m8_lights,
  m8_world and m9_primitives failed the new check before the fix).
- **Bryce additions (user's proposal file), discussed:** Multi-Replicate (brushes: offset
  only, no rotation), Snap to Land (props and primitives only), Random Replicate (after
  props), infinite ground/water/clouds planes, "Y2K preview" renamed "Nano-Preview".
  Not started; waiting for the user.
- **Order agreed with the user:** v0.9.1 this fix; v0.9.2 = units to 1 cm + blending
  primitives + Snap to Land / Multi-Replicate (one release, a commit per step, pushed
  once); v0.10.0 props (`.glb` first); Random Replicate; M11 renderer with infinite
  planes; M12 Nano-Preview; M13 export (`.glb` first, Godot package low priority).

### 2026-11-18 — Milestone 9: Math primitives (v0.9.0)
- **Design talk:** the user asked for Bryce-style math primitives and (for M10) static
  props, after looking at the Mazzuola design. Decisions are in "Open questions".
  CLAUDE.md product rule changed: static props allowed; still no doors, triggers,
  physics or scripting.
- **Core** (`core/mathprim`): `MathPrim` (kind, pos, rot, size, round, thickness, op,
  combo, texture, colour, repeats, kept keys), rotation matrices and angles (`R = Rz Ry Rx`,
  half turn reads 180), turning about world axes and a pivot, exact ray stretches per
  kind (quadrics, slab-clipped; rounded cube by sphere-traced exact distance; torus by
  sampling + bisection), combining stretches (add / subtract / intersect), picking,
  bounds, outlines. `Document`: primitives, primitive selection, `PrimChange` commands,
  `withCombos`. Lights and primitives keep creation order on undo.
- **File:** `scal_primitive` point entities; byte-identical round trip.
- **Drawing** (`render/mathprim_render`): one shader per combination over its box (back
  faces, so it works from inside the box), per-pixel stretches + combining, depth
  written, texture mapping by kind, editor shading or rough light preview, fog. 2D
  outlines by role; selected in red; 3D outline for selected ones.
- **Tools:** `ObjectEdit` replaces `LightEdit` (lights + primitives: pick, move, rings,
  size handles with Shift = evenly); new Primitive tool (Shift+M): drag a box / typed
  box, Return / Esc; Select, Light and Primitive tools share object editing.
- **Editor:** Primitives menu (tool, new kind ×7, create, combine, separate, piece
  mode), Properties panel (typed transform, rounding, thickness, texture / flat colour,
  colour, repeats, combination pieces, Combine / Separate), Delete / Cmd+D, Help →
  Controls, README.
- **Script:** `tool prim`, `primset`, `primbox`, `prim`, `primedit`, `primselect`,
  `combine`, `separate`, `expect prims|primsel|primpos|primrot|primsize|primop`. New
  script `m9_primitives`.
- **Found and fixed while testing:** undoing a delete put objects back at the end of the
  list (order changed in the saved file); Shift held only at the press didn't scale
  evenly; even scaling now follows the mouse (size grows twice the handle's move).
- **Tests:** core **3649 checks, 0 failed** (every kind's hits incl. rotated and
  stretched, rounded cube, torus with two stretches, disc from both sides, combining
  three ways, outlines on the surface, bounds, rotation round trips, document undo,
  file round trip). UI: all 15 scripts pass at 1× and 2×. Idle with primitives in the
  map: 0 CPU ticks over 10 s.
- **Screenshots:** `m9-01-sphere-rings-handles.png`, `m9-02-shapes-and-a-bite.png`,
  `m9-03-light-preview.png`, `m9-04-close-up.png`.

### 2026-11-16 — Milestone 8: World (v0.8.0)
- **Core** (`core/world`, `core/sky`, `core/document`):
  - `Light` (kind, pos, colour 0–255, brightness, reach, pitch/yaw, inner/outer, kept
    unknown keys), `lightDirection` / `directionAngles` / `rotateLight` (world axis,
    right-hand rule), `axisCircle`, `lightAt` (ambient + Lambert, smooth falloff, spot
    cone, suns everywhere).
  - `Environment` (sky, ambient, fog), `CameraBookmark`, scale figure position.
  - `Document`: lights + light selection, `LightChange` in commands (`makeAddLights`,
    `makeModifyLights`, `addLightDeletes`), environment commands, bookmarks/figure
    (not undone), `WorldData` on `reset`.
  - `skyLayout`, `crossToPanorama` (checked against the pack's matching panorama: mean
    squared error 1.2 on 0–255), `skyUV`.
- **File:** lights as `light` / `light_spot` / `light_environment` with Hammer keys;
  `_scal_sky`, `_scal_ambient`, `_scal_fog`, `_scal_cameras`, `_scal_figure` in
  worldspawn. Byte-identical round trip. Opening a map now carries lights etc. (the
  editor's open path rebuilt the document and dropped them at first; fixed).
- **Tools:** `LightEdit` (pick marker / ring, move in 2D, turn by ring), used by the
  Select tool and the new Light tool (place with click, Return / Esc).
- **Drawing:** markers, cones, arrows, reach, rings (2D and 3D; constant screen size via
  `Viewport::worldPerPixel`); sky sphere; rough light preview (`render/lighting`,
  subdivided faces, terrain corners); fog shader (GLSL 330, loaded on first use); scale
  figure (2D outlines, 3D see-through).
- **Editor:** World menu (Light tool, new point/spot/sun, create, Environment…, Camera
  bookmarks…, Scale figure position…), View toggles, Properties panel for lights
  (edits all selected), Delete / Cmd+D for lights, sky folders (settings file
  `sky_folders.txt`), Help → Controls and README.
- **Assets:** `assets/skies/Panorama_Sky_04|15|21.png` (CC0), test cross
  `tests/ui/assets/skies/Cubemap_Sky_21.png` (scaled down). `Grass_20`, `Tile_13`
  textures (previous entry).
- **Script:** `tool light`, `lightset`, `lightedit`, `expect lights|lightsel|lightpos|
  lightdir`, `viewset`, `environment`, `skydir`, `bookmark`, `figure`, `expect sky|
  bookmarks|camera`. New scripts `m8_lights`, `m8_world`.
- **Tests:** core **3520 checks, 0 failed** (directions, rings, lighting maths, light /
  environment undo, file round trip incl. unknown keys, sky layouts and mapping). UI:
  all 14 scripts pass at 1× and 2×. Idle with sky, fog and a light in the map: 0 CPU
  ticks over 10 s.
- **Screenshots:** `m8-01-lights-and-rings.png`, `m8-02-sky-day.png`,
  `m8-03-sunset-cross-fog.png`, `m8-04-night-light-preview.png`,
  `m8-05-preview-with-sun.png`, `m8-06-scale-figure.png`, `m8-07-sky-hidden.png`.

### 2026-11-16 — Two more built-in textures
- `assets/textures/Grass_20.png`, `Tile_13.png` (Screaming Brain Studios, CC0, from the
  owner; converted RGBA → RGB, fully opaque). Credited in `docs/THIRD_PARTY.md`.
- The owner also gave three skies (04 day, 21 sunset, 15 night) as panoramas (2:1) and
  cube crosses (4:3). Not added yet: they go in with milestone 8 once the sky questions
  below are answered.
- UI scripts: all 12 pass. No release for this alone (ships with the next one).

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
