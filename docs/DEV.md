# Scalpellino — Dev tools, build and testing

Everything needed to build, test and release. Rules are in [`../CLAUDE.md`](../CLAUDE.md),
design in [`ARCHITECTURE.md`](ARCHITECTURE.md), status in [`PLAN.md`](PLAN.md), history in
[`LOG.md`](LOG.md).

## Build

- **Mac:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
  (CMake 3.24+, Apple command line tools). The app is `build/Scalpellino.app`.
- **Linux** (CI and the AI workspace): needs `libxrandr-dev libxinerama-dev libxcursor-dev
  libxi-dev libgl1-mesa-dev xvfb` (CI also installs `libgl1-mesa-dri libxkbcommon-dev`).
  The binary is `build/Scalpellino`.
- **Dependencies** (raylib, Dear ImGui, rlImGui) are fetched by CMake at pinned versions
  and checked against their SHA-256 (`URL_HASH`), see `cmake/Deps.cmake` and the table in
  `PLAN.md`. To update one, change the URL and the hash and note it in the LOG.
- **AI workspace:** GitHub archive downloads are blocked, so FetchContent fails. Clone the
  three deps with git at the pinned refs into a scratch folder and configure with
  `-DFETCHCONTENT_SOURCE_DIR_RAYLIB=… -DFETCHCONTENT_SOURCE_DIR_IMGUI=…
  -DFETCHCONTENT_SOURCE_DIR_RLIMGUI=…`.

## Core tests

- `ctest --test-dir build --output-on-failure`, or run `./build/scal_core_tests` directly
  (`tests/test_core.cpp`; it prints the number of checks and failures).
- **Probes:** to reproduce a bug in numbers, write a small `.cpp` in a `mktemp -d` folder
  and link it against `build/libscal_core.a`. Fix, then keep a permanent test.
- Every generated shape has a no-overlap test; keep that for new shapes.

## Scripted UI tests

- Scripts are `tests/ui/*.script`, run on a virtual screen (Linux + Xvfb) by
  `tests/ui/run.sh <absolute path to build/Scalpellino> [screenshot-dir]`. Each script runs
  in a fresh temp folder with a copy of `tests/ui/assets/`. A failed `expect` makes the
  script, and the run, fail.
- Run them at 1× and at Retina-like scale: `SCAL_VIEW_SCALE=2 tests/ui/run.sh …`.
  CI does both. GitHub's Mac VMs have no OpenGL, so UI tests only run on Linux.
- **Duplicate IDs:** on every scripted frame a check (`src/ui/imgui_checks.cpp`, using
  Dear ImGui's item hooks, `IMGUI_ENABLE_TEST_ENGINE`) fails the run if two widgets share
  an ID. ImGui itself only notices while the mouse hovers them. Wrap each Properties
  section in its own `PushID`.
- **Writing scripts:** use world coordinates for 2D input (`wpress`, `wdrag`, `release`)
  and `press3d` / `camera` for the 3D view, so scripts don't depend on the window layout.
  Default 2D zoom is 0.5 px/unit. Snapped drags move brushes by up to half a grid cell, so
  compute brush edges from snapped values before aiming clicks near edges. `printviews`
  prints each view's origin and size. Deselect before screenshots so the red selection
  tint doesn't hide textures.
- **The script language** (one command per frame; this is the reference, the code in
  `Editor::loadScript` is the implementation):

```
One command per frame. Views: 0=3d 1=top 2=front 3=side. Coordinates are pixels
inside the view. Lines starting with # are comments.
  refused <command ...>   (the command must be refused: its failure is expected, not counted)
  tool select|block       press <view> <x> <y> [shift] [cmd] [alt]
  drag <x> <y>            release
  key Return|Esc|Delete   undo | redo | selectall
  grid <n>                save <path> | open <path>
  transform move x y z | transform rotate x|y|z <deg> [origin] | transform scale sx sy sz
  rotate90 x|y|z <+1|-1>  flip x|y|z  duplicate  dialog transform|controls  closedialogs
  tool vertex   vertexmove dx dy dz   snapgrid   microgrid on|off   rotsnap slopes|15
  pivot center|origin|<x y z>   expect ongrid   expect faces <n>
  view <1|2|3> <u> <v> [zoom]   (centre a 2D view; zoom = pixels per unit)
  layout classic|default   (classic = no right-hand panels; older scripts use it)
  wpress <view> <u> <v> [mods] / wdrag <u> <v>   (2D input in world coordinates)
  press3d <dx> <dy> [mods]   (3D view click relative to its centre)   drag3d <dx> <dy>
  tool texture   texturedir <path>   texture <name>   applytexture   applynodraw   wraparound
  rendermode flat|textured
  texscale <s>   facefit   facerotate <deg>   facealign world|face
  facejustify left|right|top|bottom|center
  expect texture <name>   expect pickedfaces <n>   expect current <name>
  tool clip   clippoints x y z  x y z  x y z   clipkeep both|front|back   (key Return clips)
  carve   hollow <thickness>   expect volume <v>   (sum of all brush volumes, +-0.5)
  curveshape arch|ring|curved_wall|spiral_stairs|carve_arch|dome|helix_ramp|straight_stairs
  curveset <key> <value>
  (keys as stored in the .map: segments, thickness, box, depth, sweep, flat_top...)
  editcurve   bakecurve   expect groups <n>
  blockshape box|wedge|cylinder|cone|pyramid|sphere [sides]
  tool terrain   terrain pick | make <cells> | remove | stitch | heightmap <path> <low> <high>
                 (pick = Terrain > Make terrain: clicks then pick faces; make uses the picked
                  faces, else changes the grid size of the selected terrain)
  vertexraise <d> | vertexheight <h>   (selected terrain points, vertex tool)
  expect terrainpoints <n>   expect paintlayers <n>
  tool light   lightset|lightedit kind point|spot|sun | color r g b | brightness|range|pitch|yaw|inner|outer <v>
               | pos x y z   (lightset = the next light, pos places it; lightedit = selected lights)
  expect lights <n>   expect lightsel <n>   expect lightpos x y z   expect lightdir <pitch> <yaw>
  viewset sky|preview|fog|figure|prims|props on|off   skydir <path>   figure x y z
  tool prim   primset kind <k>|round|thickness|flat on/off|color r g b|texscale   primbox c s [axis]
  prim <kind> x y z sx sy sz [rx ry rz]   primedit <key> <value...>   primselect <index...>
  snapland   snapground <z>   replicate <n> ox oy oz [rot x y z] [scale x y z] [scaleoffset] [pivot selection|origin|x y z]
  replicatepreview on|off   replicateopen <n> ox oy oz   expect replicating on|off
  randreplicate <n> sx sy sz [turn x y z] [size <min%> <max%>] [uneven] [seed <n>]
    (offsets up to +-s per axis)   randreplicateopen <same arguments> (opens the window)
  randreplicatepreview on|off   expect randomizing on|off
  expect bottoms <z>   (every selected primitive object and prop rests with its lowest point at z)
  press3d / drag3d / release also draw the 3D rectangle select (Select tool)
  tool props   propset model <name>|rot x y z|scale s   prop <model> x y z [rx ry rz] [scale]
  propselect all|none|<index...>   propedit pos x y z|rot x y z|scale s|scale x y z|model <name>
  expect props|propsel <n>   expect proppos|proprot|propscale x y z   expect propmodel <name>
  expect hasmodel [not] <name>
  combine   separate   expect prims|primsel <n>   expect primpos|primrot|primsize x y z   expect primop <op>
  environment sky <name|none> | ambient r g b | fog on|off | fogcolor r g b | fogrange <start> <end>
  skylab open|apply|ok|cancel|rebake   (apply / ok bake; the next command waits for the bake)
  skylabtab sun|clouds|atmosphere   (opens that tab of the Sky Lab window)
  skylabsection <title start>   (opens that section of the tab and scrolls to it: Moon, Stars...)
  skylabset fog on|off | fog_color r g b | fog_range <start> <end> | fog_height <base> <thickness>
                                  (the window's fog, applied to the map with the sky)
  skylabpreset save|load <name>   (a saved sky in the Bottega's presets/skies)
  skylabpreset builtin <name>     (a built-in sky, core/sky_presets)
  expect skycover <min> <max>     (how much of the map's sky picture is kept in memory, 1 = all)
  skylabrandom [<seed>]           (Randomize Sky)
  environment ... fogheight <base> <thickness>   expect fog on|off [<base> <thickness>]
  skylabset <field> <value...>   (a Sky Lab field as saved in the map: mode darker, sun_altitude 6,
                                  sun_color 255 200 150, lock_sun 1...; ambient r g b)
  expect skylab <field> <value...>   (the map's Sky Lab settings)   expect skyoutofdate yes|no
  expect skylabwindow open|closed [<preview width>]   expect undos <n>   (undo steps)
  tool camera   cameraset|cameraedit name <n> | pos x y z | angles pitch yaw roll | fov <deg> | lens <mm>
               | near|far <d> | size <w> <h> | ortho 0|1 | orthosize <u> | exposure <s> | color r g b
               (cameraset = the next camera, pos places it; cameraedit = the selected cameras, one
                undo step; cameraedit active 1 makes the first selected one active)
  cameraselect all|none|<index...>   cameraactive <index>   cameraaim x y z | click
  camerapreview   (Cmd+K: draws the Camera Preview with the next frame)
  camerapreset save|load <name>   (from / into the selected camera, else the next camera)
  expect camerapreview <times drawn> [stale 0|1]   expect camerapresets <n>
  tool probe   probeset size <n> | pos x y z   (the next probe: Hammer's cubemapsize; pos places it, Return creates)
  probe x y z [size]   probeselect all|none|<index...>   probeedit pos x y z | size <n>
  expect probes|probesel <n>   expect probepos x y z | probesize <n>   (the first selected probe)
  expect probeatlas <n>   (probes in the last built cubemaps)   viewset probeballs|probeuse on|off
  expect previewmirrors <n>   (mirror planes drawn for the last Camera Preview, v0.12.7)
  plane water|ground|clouds [z] | plane current [z] | plane material <name> <z>   (World > Endless ... plane, v0.13.0)
  planeselect all|none|<index...>   planeedit z <h> | material <name> | scale <u> <v>   (the selected planes, one undo step)
  expect planes|planesel <n>   expect plane z <h> | material <name> | kind water|ground|clouds   (the first selected plane)
  render   (Cmd+Shift+K; the next command waits until it has finished)   renderstart (doesn't wait)
  renderset quality draft|normal|high | aa <1..4> | filter smooth|crisp | props full|quick   (Render menu)
  matselect <name> [cmd|shift] | shown | faces | none   (Textures panel clicks: the materials the panel edits)
  texsearch <text>   texhelpers on|off   (the Textures panel's search, helper maps shown)
  matset <key> <value...> ("" = empty)   matreset <key>   matshader <name>   matvariant <name>
  matparent <name>|none|new <name>   mateditparent   matback   (the Material panel; each one undo step)
  expect matsel <n>   expect mat <name> <key> <value...|none>   expect matfile <name> yes|no
  expect matkind <name> texture|variant|helper|parent   expect matshader <name> <shader>
  expect matparent <name> <parent|none>
  rendercopy <file.png>   (the last saved render, copied here: screenshots)
  expect render saved|stopped|failed|none   expect renders <n>   (saved, each on disk with its log)
  expect renderlog [not] <text>   expect rendersize <w> <h>   expect renderwindow open|closed
  expect rendersame yes|no   (the last two saved renders: the same picture, or clearly not)
               (click: the next press aims the selected cameras, as World > Aim ... at a clicked point)
  expect cameras|camerasel <n>   expect cameraactive <index>   (the only active one)
  expect cam pos|angles|fov|lens|near|far|size|ortho|orthosize|exposure|color <values> | name <n>
               (the first selected camera, +-0.01)
  bookmark add <name> | go <index>   expect sky <name>   expect bookmarks <n>   expect camera x y z
  bottega none | <path> | make <path>   rescan   bottegadialog   (in memory only; make = create + use)
  expect bottega ready|empty|notbottega|missing|newer|damaged|none
  expect hastexture [not] <name>   expect hassky [not] <name>
  copy <from> <to...>   (tests only: add a file, e.g. to prove nothing rescans by itself)
  terrainset mode <raise|lower|smooth|flatten|noise|paint|erase> | radius|amount|strength <n>
             | falloff soft|linear|hard      whover <view> <u> <v> (hover, no button)
  camera <x> <y> <z> <yaw> <pitch>   (3D view camera, degrees)
  expect terrains <n>   expect terrainheight <x> <y> <z>  (surface below x,y; +-0.5)
  hide   hideunselected   showall   visgroup new|show|hide <name>   expect visible <n>
  findoverlaps   nextoverlap   expect overlaps <n>   toorigin
  block x0 y0 z0 x1 y1 z1 (test setup)   expect bounds minx miny minz maxx maxy maxz
  shot <file.png>         quit
  expect brushes|selected <n>  |  expect size <x> <y> <z>  (selection bounds, +-0.5)
  expect edges   (every brush/preview edge in the 2D views is visible on screen)
  A failed expect prints FAIL and makes the exit code 1.
```

## Command line

- `--open <map>`: open a map. `--script <file>`: run a script. `--screenshot <png>`:
  render a few frames, save a PNG in the current folder, exit.
- Automated runs never read or write the user's layout settings.
- Linux headless: `xvfb-run -a -s "-screen 0 1600x1000x24" build/Scalpellino --screenshot x.png`.
- Environment: `SCAL_VIEW_SCALE=<n>` overrides the display scale (tests only).

## Idle check

The app must cost ~zero CPU when nothing happens (`src/app/main.cpp` describes the idle
policy), and it must hold **with a map open**, not only with an empty app.

- `tests/ui/idle.sh <absolute path to Scalpellino> [seconds]` (Linux, needs Xvfb):
  starts the app on its own virtual screen with `tests/ui/idle/rich_map.script`, which
  builds a map with a Bottega (one texture in it, found by Rescan, used on the tower), terrain (raised and painted), brushes, endless water and cloud planes (v0.13.0), a dome, a blended primitive
  combination, a torus and a cube, three lights, sky, ambient, fog, a bookmark and the
  scale figure, saves it, **re-opens the saved map**, switches on the sky, light preview,
  fog and figure, selects the primitives and props, and leaves the Random Replicate window
  open with its preview and the Sky Lab window open with its live preview. The script has no `quit`, so the app goes
  idle (a scripted run is only "busy" while commands are left). The screenshot at the end
  is the "ready" signal; the harness waits 3 s more, then reads the process's CPU time
  (user + system, all threads, from `/proc`) twice, 10 s apart.
- **Pass = 0 ticks** (1 tick = 10 ms). `IDLE_MAX_TICKS=<n>` raises the allowance if a CI
  runner proves noisy. Run it at 2x too: `SCAL_VIEW_SCALE=2 tests/ui/idle.sh …`.
- **The scene is checked:** the script `expect`s its contents, and the harness fails if the
  app printed any error while building it, so a broken script can't pass vacuously.
- **Control:** `tests/ui/idle.sh <app> --control` runs a script that keeps the app busy and
  expects plenty of CPU time (it must see at least 10 ticks), proving the measurement can
  see CPU use.
- CI runs all three (control, 1x, 2x) in the `linux` job. Not part of `tests/ui/run.sh`,
  whose glob only takes scripts directly in `tests/ui/`.
- To add something heavy to the map (a new object kind, a new preview), add it to
  `rich_map.script`: a new feature that makes idle cost CPU fails this test.

## Benchmark

A development tool, not a test: `tests/bench/run.sh <absolute path to Scalpellino> [props|brushes|render]`
(Linux + Xvfb; props and brushes by default). Three benchmarks:

**Render** (`tests/bench/render.script`, several minutes): busy maps rendered at Normal
quality with the baked light and (v0.12.6) reflection probes on a shiny floor (3, 16 and 36
probes; the log's "cubemaps built in" line; v0.12.7: a mirror on the rich map's wall; v0.13.0: an
endless water plane and a cloud layer around the rich map): the rich map (as the idle test's), 1000 boxes + 8 terrains,
4000 boxes + 16 terrains. (The 1000 rocks were dropped on 2026-11-21: 56 minutes even with
quick prop light.) Prints each render log's bake lines; with
`SCAL_BENCH_OUT=<folder>` it also copies the pictures and render windows there. The user
wants these renders reported with each lighting step.

**Brushes and terrain** (`tests/bench/brushes.script`, about 10 s): a square of boxes,
optionally with terrain patches (32 x 32 cells, raised with noise and painted). Each line:
building one view's scene, a whole redraw of the four views as when flying the camera
(each view builds its scene), and an edit that moves one brush followed by the editor's,
the light preview's and the renderer's caches catching up (what every edit, undo and redo
costs). Script command: `bench brushes <count> [<terrains> [<power>]]`, `bench clearbrushes`.

| | 1000 boxes | 4000 boxes | 1000 + 8 terrains | 4000 + 16 terrains |
|---|---|---|---|---|
| scene, one view | 0.00 ms (0.7) | 0.00 ms (4.3) | 0.00 ms (1.9) | 0.00 ms (6.0) |
| edit of 1 brush, caches rebuilt | 1.4 ms (28) | 7.7 ms (125) | 1.9 ms (44) | 9.1 ms (154) |
| redraw, 4 views | 58 ms (48) | 111 ms (135) | 77 ms (89) | 155 ms (170) |

(Review fixes branch; before in brackets.) The redraw is almost all software rasterising
here and varies by ±15% between runs; the scene copy it no longer makes (4 per redraw)
weighs more on a real GPU, where drawing is fast.

**Props** (`tests/bench/props.script`, about 1.5 minutes). It makes a heavy model (`tests/bench/make_model.py`, a 5000-triangle
rock), runs `tests/bench/props.script` and prints one line per size: 500 / 2000 / 5000
crates and 300 / 1000 rocks. Each line has the time to build the scene, draw the 3D view
from above one corner (everything in view) and close up (most props behind or beside the
camera, so skipped), draw a 2D view, pick with 40 clicks, and Snap to Land 50 lifted props
among all the others, plus how many props were skipped and how many draw calls were made.

The OpenGL here is software (llvmpipe), so the numbers compare versions; they are not what
a real GPU does (the rock rows are almost all software rasterising). Script command:
`bench props <count> <model> [spacing]`, `bench clear`.

Switches for comparing drawing paths (both used to check v0.10.3's screenshots match):
`SCAL_PROPS_PLAIN=1` draws props one call each (the fallback), `SCAL_NO_CULL=1` draws every
prop even outside the 3D view.

Numbers (v0.10.3; before in brackets, v0.10.2):

| | 500 crates | 2000 crates | 5000 crates | 1000 rocks |
|---|---|---|---|---|
| Snap to Land, 50 props | 16 ms (5 s) | 40 ms (22 s) | 90 ms (56 s) | 46 ms (15 s) |
| 3D view, all in view | 5 ms (8) | 9 ms (15) | 15–18 ms (31) | 490 ms (490) |
| 3D view, close up | 4 ms | 7 ms | 10 ms | 260 ms |
| 2D view | 4 ms (8) | 4–7 ms (24) | 5 ms (43) | 3 ms (12) |
| pick (per click) | 0.04 ms | 0.2 ms | 0.4 ms | 0.1 ms |
| scene build | 0.02 ms | 0.1 ms | 0.4 ms | 0.05 ms |

## CI

`.github/workflows/build.yml`:
- **macos:** Intel (`macos-15-intel`): build, core tests, check the bundled textures, ad-hoc
  sign, zip `Scalpellino-intel.zip`.
- **linux:** build, core tests, UI scripts at 1× and 2×, the idle CPU check (control, 1×, 2×),
  screenshots as an artifact.
- **release:** after both pass, on pushes to `main`: replaces the "Latest build"
  pre-release (only from the newest commit on `main`), and makes a version release for
  every `docs/releases/vX.Y.Z.md` that has no release yet, **always from a build of the
  commit that added that notes file**. If this run is that commit's build, it publishes
  its own zips. If not (that build failed, or its release job was skipped or cancelled
  because main moved on), it tags the notes commit and starts this workflow on the tag;
  that run builds exactly that commit and publishes it. A later commit's build is never
  published under an older version.
  If the tag build fails, there is no release and the next pushes warn about it; fix
  and re-run that run (Actions → build → ref `vX.Y.Z`). Pushing a `vX.Y.Z` tag by hand
  also publishes that commit. "The commit that added the notes" is counted along
  `main` (`--first-parent`): with a merge, it is the merge commit. A tag run uses the
  workflow file as it was at that commit, so this only works for commits that have this
  release logic (not for versions up to v0.12.0, which are all released anyway).
- Apple Silicon builds are on demand only: Actions tab → **apple-silicon** → Run workflow.

## Releasing

Add `docs/releases/vX.Y.Z.md` (copy the Download and First launch part from the last one)
in the commit that holds the version's code, and push to `main`. CI makes the tag and
release from **that commit** after its builds pass (about 7–8 minutes), even if more
commits were pushed meanwhile (see CI above). So the notes go in with the code (or after
it), never before. Tag pushes are blocked in the AI workspace, so this file-driven route is the one
to use.
The app's version (in the Mac app's Info.plist) is taken from the newest of these files
by CMake, so there is nothing else to bump.

## Per-step checklist

1. Build, then core tests, then all UI scripts at 1× and 2×.
2. Idle CPU check (above): `tests/ui/idle.sh`, at 1× and 2×.
3. Screenshots into `docs/screenshots/` (`m<milestone>-<nn>-<what>.png`), mentioned in
   the LOG entry.
4. `docs/PLAN.md` status, a `docs/LOG.md` entry, release notes if releasing, README and
   Help → Controls kept in sync.
5. Commit as `Claude <noreply@anthropic.com>`, e.g.
   `git -c user.name=Claude -c user.email=noreply@anthropic.com commit …`. Push, then check
   CI (`gh api repos/bytebent/scalpellino/actions/runs`).

## Workspace quirks (AI cloud workspace)

- `gh` GraphQL commands (e.g. `gh release list`) are refused. Use REST:
  `gh api repos/bytebent/scalpellino/releases`,
  `gh api repos/bytebent/scalpellino/actions/runs`.
- Use `mktemp -d` for scratch work. A `cd …; rm -f *` command was blocked by a safety check.
- Measure before fixing: a probe reproduces the bug in numbers, the fix makes it zero, a
  permanent test keeps it zero.
