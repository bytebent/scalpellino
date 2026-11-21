# Scalpellino — Log

What was done, step by step, newest first. Recent work lives here; when this file gets
long, move the oldest entries to [`LOG-archive.md`](LOG-archive.md) (newest first there
too). Status, plan and open questions are in [`PLAN.md`](PLAN.md).

### 2026-11-22 — v0.12.4c: VRAD's code checked (texlight scale, bump correction)
- Read VRAD's code (Source SDK 2013, `utils/vrad`; the user said go). Not kept in the repo.
- **Bump maps:** VRAD bakes each bump map as cosine light (`GatherSampleLightAt4Points`);
  `LinearToBumpedLightmap` ("matches the engine's conversion") scales a luxel's 3 bump
  values so their average is the flat value; the shader (`lightmappedgeneric_ps2_3_x.h`)
  blends with squared weights over their sum, no other scale. Added the same correction at
  the end of the bake (faces, terrain, per-vertex props). The seam is gone
  (`m12-35-bump-seam.png`: 128 vs 128 of 255, was 115 vs 128).
- **Texlights:** `CreateDirectLights` multiplies by `DIRECT_SCALE` (100 x 100); added. The
  materials test lamp is now `%texlight 255 210 150 6` (was 60000): the same picture
  (`m12-25-materials.png` identical).
- The bake key carries the bake's version (old cached bakes are made again).
- **Tests:** core 5010 checks, 0 failed (`testBumpBake` checks the correction); all 34 UI
  scripts pass at 1x and 2x; idle 0 ticks at 1x and 2x.
- Screenshots updated: `m12-23-material-panel.png`, `m12-24-materials-log.png`,
  `m12-31-bump-render.png`, `m12-32-bump-log.png`, `m12-33-bump-model-style.png`,
  `m12-34-exposure-sky.png`, `m12-35-bump-seam.png`.

### 2026-11-21 — v0.12.4b: bump maps, Phong and rim light
- **Bake** (`core/bake.*`): bumped faces, terrain and per-vertex props get the light along
  Source's 3 bump directions (VRAD's `GetBumpNormals` from the texture axes): direct and sky
  light share their shadow rays across the 4 normals (`Gather::atN`); patches keep 3 more
  totals and each transfer is weighed by `max(0, b.dir) / n.dir`; the bounce reaches the
  bump maps. `FaceLightmap::bump`, `PropLighting::bumped` (4 values a vertex),
  `BakeInput::bumpedModels`; the bake key includes them. Every prop now gets its ambient
  cube and 2 lights (Phong and rim light need them).
- **Drawing:** the lightmap packer puts the 3 bump maps beside the flat one (or below);
  the World prelude has the texture frame from screen derivatives, `bakedLightBumped`
  (squared weights over their sum) and `bakedLightSSBump`; the Model prelude the prop's
  lights (`modelLightAt`) and its 4 vertex values. LightmappedGeneric: `$bumpmap`,
  `$ssbump`, `%flipgreen`, `$normalmapalphaenvmapmask`; VertexLitGeneric: `$bumpmap`,
  `%flipgreen`, Phong and rim light keys. Shader binding takes a texture finder, so props
  bind their glTF normal map and the textures their `.vmt` names (`io/model_store`:
  `modelMaterialSettings`). The glTF loader reads the normal texture. The sky behind the
  scene takes the exposure. Material panel: long keys no longer overlap their fields.
- **Found while checking:** with the agreed SDK 2013 blend a flat bump map is darker than no
  bump map (0.58 x under direct light); a seam shows (`m12-35`). Built as agreed and parked
  in PLAN.md with options, to check in VRAD's code with item 9.
- **Tests:** core `testBumpBake` (the flat map unchanged by a bump map, each bump map =
  flat x b.L / n.L, the basis along s and t): 5009 checks, 0 failed. New UI script
  `v124b_bump.script` (helper maps, flip-green undo, bumped walls, SSBump floor, glTF orbs
  with Phong and rim, like-a-Source-model orb, exposure, the seam check): all 34 scripts
  pass at 1x and 2x; idle 0 ticks at 1x and 2x. The bump test scene bakes in under 0.5 s
  (Normal). Busy maps (`tests/bench/run.sh ... render`, Normal, 2 threads, no bump maps in
  them): rich 1.2 s, 1000 boxes 8.7 s, 4000 boxes 56.7 s, as after the speed-up.
- Screenshots: `m12-30-bump-material.png`, `m12-31-bump-render.png`,
  `m12-32-bump-log.png`, `m12-33-bump-model-style.png`, `m12-34-exposure-sky.png`,
  `m12-35-bump-seam.png`.
- Pushed; CI green (Linux, macOS, release); release `v0.12.4b` published.

### 2026-11-21 — Faster bakes, the same light (before v0.12.4b)
- The user asked for bake speed-ups that keep the look (items 1-4 of the proposal).
  Profiling a test scene (floor, 300 boxes, 100 props of 5000 triangles, Normal, 2 threads)
  showed 82% of the time in finding which patch of a face a point is on: three loops
  looked through every patch of the face per luxel or ray. A grid per face in lightmap space
  (`StGrid`) answers the same: 122 -> 22 s. Then the tracer, moved to `core/tracer.*`:
  binned SAH tree, nearer child first, single-precision boxes and triangles (the triangle
  test stays in double so edge cases fall as before: a first all-float version turned a
  luxel on a wall's edge dark under a sun straight down), a multiply-add box test, prop
  models stored once and placed per prop (instances): 22 -> 15.6 s. Testing 4 rays at once
  was not done (see PLAN.md). A single-precision pre-test of triangles gained nothing and was
  taken out.
- Same light: the test scene's lightmaps and prop light differ by at most 0.00005; the busy
  renders match the v0.12.4 ones (`m12-26` to `m12-28`) within 2 / 255 per pixel (so no new
  screenshots). Busy maps: rich 1.2 s (same), 1000 boxes 21.6 -> 9.0 s, 4000 boxes 133.9 ->
  56.0 s.
- Tests: new core `testTracer` (placed models answer like the same triangles in the world,
  2000 random rays): 4994 checks, 0 failed. All 33 UI scripts pass at 1x and 2x; idle 0
  ticks at 1x and 2x. Not released on its own: it goes out with v0.12.4b.

### 2026-11-21 — v0.12.4b agreed; benchmark without the rocks
- The user said yes to all twelve v0.12.4b recommendations (PLAN.md, current status) and to
  dropping the 1000 rocks from the busy-map render benchmark (`tests/bench/render.script`,
  DEV.md). Bake speed-ups that keep the look were proposed; no code changed.

### 2026-11-21 — v0.12.4 busy-map renders
- Released (CI green). `tests/bench/run.sh ... render`, Normal quality, 2 threads (v0.12.3 in
  brackets): the rich map 1.2 s (1.4), 1000 boxes 21.6 s (25), 4000 boxes 133.9 s (159), 1000
  rocks with Render > Prop light > Quick 3353 s, about 56 minutes (v0.12.3, full: stopped by
  the hour limit). Of the rocks' time, props per vertex took 2886 s (2.6 million vertices),
  transfers 204 s, direct light 33 s: even a quarter of the rays leaves the props the slow
  part. A first run was stopped part-way by an interruption; this is the second.
- Screenshots: `m12-26-busy-rich.png`, `m12-27-busy-1000-boxes.png`,
  `m12-28-busy-4000-boxes.png`, `m12-29-busy-1000-rocks.png`.

### 2026-11-21 — v0.12.4: materials (M12 batch 5)
- Built as agreed (PLAN.md "Batch 5 decided with the user", items 1-5, 9-14, 16, 17, 19-25);
  choices made while building are under "v0.12.4, decided without asking", open points
  under "v0.12.4 parked" (texlight brightness, materials on props and primitives, exposure
  on reflections, textures with alpha and no `.vmt`).
- **Core:** `core/material.*` (Source's KeyValues read / written, `.vmt` files, `patch`
  parents with insert / replace, Source vectors, texlight values; `MaterialIndex`: textures,
  settings beside them, variants, parents, helper maps by name, where files go);
  `core/shader_file.*` (the shader header); `Command::fileChanges` (material edits are undo
  steps that don't mark the map changed). The bake: texlights (VRAD's surface lights per
  patch), cut-out shadows (ray tests read the picture's alpha), see-through faces block no
  light, `$reflectivity`, quick prop light (a quarter of the rays).
- **Render:** `render/shader_library.*` compiles the GLSL files (prelude per geometry: World,
  Model); `assets/shaders/` has Source's LightmappedGeneric, UnlitGeneric, VertexLitGeneric.
  Brushes and terrain are drawn by their materials (see-through ones after the props, far
  to near); props by VertexLitGeneric; reflections read a small copy of the sky. The texture
  library sits on the material index (variants show their base texture in the editor).
- **UI:** the Material panel (Texture tool, Properties), multi-select in the Textures panel
  (Cmd / Shift, Select all shown, a hint line), Texture menu entries, Make a variant,
  parents, helper maps toggle, Render > Prop light. Help > Controls and the README updated.
- **Tests:** core `testMaterials` (KeyValues, values, patch rules, the index with variants /
  helpers / parents, the shipped shader headers, file undo leaving the map saved) and
  `testBakeMaterials` (cut-out shadows, see-through faces, a texlight's colour and VRAD's
  repeats scaling, the keys): 4984 checks, 0 failed. New UI script `v124_materials.script`;
  the idle map now has a material with a `.vmt`. All 33 UI scripts pass at 1x and 2x; idle
  0 ticks at 1x and 2x (control 892 ticks).
- **Screenshots:** `m12-23-material-panel.png` (the panel on a material with a parent),
  `m12-24-materials-log.png` (the render log), `m12-25-materials.png` (cut-out shadows of a
  fence, a texlight under an overhang, cheap glass, a wet-metal variant with cheap chrome).
- The SDK's VRAD code couldn't be read this session (fetching the page needed the user's
  approval, who was away): the texlight scaling is from memory (parked).

### 2026-11-21 — Batch 5 additions: batch editing, parent materials (docs only)
- Variants start as a plain copy (the user chose this over presets).
- For big imports: batch editing of materials (mixed values shown, only the changed field
  written, one undo), parent materials (Source's `patch` include, asked by the user),
  multi-select in the texture browser with the controls written in its window, "Select all
  shown" after a filter and "Select the textures of the selected faces", helper maps
  (`_normal`, `_ssbump`, `_mask`) paired by name and hidden. No batch variants (the user:
  avoid making hundreds of files by accident). All in v0.12.4 (bump maps in v0.12.4b).
  Recorded as items 2 and 19-24 of "Batch 5 decided with the user".
- `.vmt` files live next to their texture in `textures/`, not in a `materials/` folder
  (item 25; the user agreed).
- No code changed. Next: build v0.12.4 when the user says go.

### 2026-11-21 — Batch 5 (v0.12.4 materials, v0.12.4b bump maps) agreed (docs only)
- 18 numbered questions on the material system; the user agreed with the recommendations,
  with changes: material variants (a "wet" copy of a texture's material, with presets),
  cheap and full versions (cheap chrome now, chrome with probes; cheap glass now, glass
  with mirrors; cheap water and water in milestone 6), and decals / overlays added as their
  own step. The steps after materials reordered: v0.12.4b bump maps, v0.12.5 decals,
  v0.12.6 probes, v0.12.7 mirrors and glass. The fewer-rays option for props goes in the
  Render menu. Recorded as "Batch 5 decided with the user".
- The user corrected an old note: they never called the sky-only reflection "the Y2K
  chrome look" (fixed in PLAN.md).
- No code changed. Next: build v0.12.4 when the user says go.

### 2026-11-21 — v0.12.3 reported
- The user wants an option for fewer rays for props (the rocks map's per-vertex bake),
  recorded for batch 5. Next: the batch 5 (materials) discussion.

### 2026-11-21 — v0.12.3 busy-map renders
- `tests/bench/run.sh ... render`: the rich map 1.4 s, 1000 boxes 25 s, 4000 boxes 159 s
  (screenshots `m12-20-busy-rich.png`, `m12-21-busy-1000-boxes.png`,
  `m12-22-busy-4000-boxes.png`). The 1000 rocks were stopped by the benchmark's one-hour
  limit while baking their props per vertex (about 50 minutes of it, after 8 minutes of
  direct light and transfers): 2.6 million vertices x about 325 rays on 2 threads. To ask
  the user.

### 2026-11-21 — v0.12.3: lit props and the light grid (M12 batch 4)
- Built as agreed (PLAN.md "Batch 4 decided with the user"); choices made while building
  are under "v0.12.3, decided without asking".
- **Core (`core/bake.*`):** after the bounces, Half-Life 2's light grid (48 x 48 x 96 Hammer
  units; 162 rays per sample into 6 sides: the hit patch's light x its reflectivity, or the
  sky; samples inside brushes / primitives left out) and the props: per vertex (direct
  light with shadows, sky light, bounce gathered from the patches; props that cast no
  shadows shadow themselves through their own ray test) or like a Source model (the grid's
  nearest cube, the 2 strongest visible lights, the others folded into the cube). Props got
  `modelLighting` (Source's `disablevertexlighting`).
- **Render:** `ModelLibrary` draws props in renders with a baked shader: vertex light from a
  float texture (gl_VertexID), or the ambient cube + 2 lights with Half-Lambert and VRAD's
  falloff; linear x exposure, sRGB, fog. View > 3D view: light grid draws the last bake's
  samples as small cubes (editor only). Prop Properties: lighting. Render log: props and
  grid lines.
- **Tests:** core `testPropLight` (Source's cube formula; per-vertex sun / shadow on a cube
  prop, a slab shadowing half its top; a model-style prop with the sun as its light and
  sky in its cube; the grid's spacing, sky above; a prop that casts no shadows leaves the
  floor lit but is lit itself; the key; the map key): 4888 checks, 0 failed. New UI script
  `v123_props.script`. All UI scripts pass at 1x and 2x; idle 0 ticks.
- **Screenshots:** `m12-17-props-baked.png` (crates baked per vertex, one like a Source
  model), `m12-18-props-log.png`, `m12-19-light-grid.png` (the grid in the 3D view).
- **Busy maps:** the rich map 1.4 s (grid 616 samples), 1000 boxes 25 s (grid 8280, 1.7 s),
  4000 boxes 159 s (grid 26 928, 16 s). The 1000 rocks (2.6 million prop vertices, each
  with about 325 rays) were still baking after 40 minutes on 2 threads (see the next entry).

### 2026-11-21 — Batch 4 (v0.12.3, prop lighting and the light grid) agreed (docs only)
- 13 numbered questions on lighting props; all answered. Recorded as "Batch 4 decided with
  the user" (PLAN.md Roadmap item 5); the batch is renamed "prop lighting". The user asked
  whether reflections need the light grid: no (Source's come from env_cubemap probes). I
  first recorded the grid as parked; that was a misreading: the user wants the real grid
  built now. Four follow-up questions (layout, how props read it, saving, a view toggle)
  answered and recorded as 4a-4d; the lighting origin is a "maybe".
- No code changed. Next: build v0.12.3 when the user says go.

### 2026-11-21 — v0.12.2 reported
- Released (CI green). Reported with the busy maps' renders and bake times; the user is
  fine with the slow bakes for now (no speed-up or cutoff). Next: discuss batch 4.

### 2026-11-21 — v0.12.2: bounced light (M12 batch 3)
- Built as agreed (PLAN.md "Batch 3 decided with the user"); choices made while building
  are under "v0.12.2, decided without asking".
- **Core (`core/bake.*`):** VRAD's radiosity after the direct light: patches on brush faces
  (VRAD's split, 4 luxels), terrain blocks, primitives' surface cells and props'
  triangles; direct light per patch from its luxels; transfers with VRAD's form factors
  (point-to-point, polygon-to-point inside the "5 times rule"), a ray per facing pair,
  normalized to pi; Jacobi gathering with VRAD's stop rule and `Bounce #n added RGB(...)`
  lines; bounced light blended onto the luxels, bilinear on terrain, gathered by
  primitives' cells (162 rays to the hit patch). The ray tracer now also finds the nearest
  hit and what it belongs to. Bounce strength in Environment (`_scal_bake` "bounce").
- **Render:** textures' average colours (`TextureLibrary::reflectivity`) and props' models'
  colours go into the bake; the render log shows patches, transfers, where the time went
  and the bounce lines.
- **Tests:** core `testBounce` (no bounce without bounces; the red floor's bounce on the
  wall is red, weaker higher up, less than the floor; the floor gets a little back;
  strength 0; Normal stops before 100; a sphere gathers the red bounce; a sunlit sphere
  reflects onto the wall; the key follows reflectivity). The v0.12.1 direct-light tests
  bake without bounces. 4866 checks, 0 failed. New UI script `v122_bounces.script`. All UI
  scripts pass at 1x and 2x; idle 0 ticks.
- **Busy maps** (`tests/bench/render.script`, new; `tests/bench/run.sh ... render`): the rich
  map (as the idle test's), 1000 boxes + 8 terrains, 4000 boxes + 16 terrains, 1000 rocks
  of 5000 triangles. Bake times on 2 threads: 1.5 s, 23 s, 137 s, 287 s (transfers 0.1,
  7, 76, 234 s); transfers now looked at once per pair and only within reach (about twice
  as quick as the first version: 4000 boxes 182 s -> 137 s).
- **Screenshots:** `m12-10-bounce.png`, `m12-11-bounce-log.png`, `m12-12-no-bounce.png` (UI
  script: a room lit by a sunbeam, with and without bounces), `m12-13-busy-rich.png`,
  `m12-14-busy-1000-boxes.png`, `m12-15-busy-4000-boxes.png`, `m12-16-busy-1000-rocks.png`
  (the busy maps).
- Next: report; then discuss batch 4 (light grid).

### 2026-11-21 — Batch 3 (v0.12.2, bounces) agreed (docs only)
- v0.12.1 released (CI green); reported. The user confirmed the v0.12.1 choices made
  without asking.
- 16 numbered questions on bounces (radiosity) with options, consequences and a
  recommendation; all answered. Recorded as "Batch 3 decided with the user" (PLAN.md
  Roadmap item 5). One change from Source by the user's choice: primitives and props
  reflect light onto other things (they get patches of their own). The flagged item in
  Open questions is updated.
- No code changed. Next: build v0.12.2 (one release) when the user says go.

### 2026-11-21 — v0.12.1: baked light and shadows (M12 batch 2)
- Built as agreed (PLAN.md Roadmap item 5, "Batch 2 decided with the user"); choices made
  while building are under "v0.12.1, decided without asking".
- **Core (`core/bake.*`):** VRAD's direct light, no graphics: luxel grids along each face's
  texture axes (`Face::lightmapScale`), terrain lightmaps over the grid, 3D lightmaps around
  primitives (cells near the surface lit at the nearest surface point, the rest filled from
  neighbours); point / spot / sun / sky light with VRAD's formulas (`_light` ^2.2, 100-unit
  normalization, 50% / 0% solve with `SolveInverseQuadraticMonotonic` ported, hard
  falloff's quintic fade, cap distance, spot cosine and exponent, 162 Halton sky directions
  cosine-weighted, sun jitter by `sin(SunSpreadAngle)`); Source's inches converted (cm / 2.54);
  shadows by ray tests against a BVH of brush faces (nodraw too), terrain cells and props'
  triangles plus the primitives' own interval tests; smooth normals under 45 degrees;
  supersampling where neighbours differ; all threads, progress and cancel; a hash key.
- **Data:** lights got falloff mode, 50% distance, hard falloff, c / l / q, spot exponent,
  sun spread (saved as Hammer's keys; lights without them read as inverse-square); faces a
  lightmap scale (`//@ lm` line after the faces, only when not default); primitives
  `lightmapscale`; props `disableshadows`; Environment sky light %, minimum light,
  smoothing angle (`_scal_bake`).
- **Render (`render/baked_light.*`):** float atlas pages, a lightmap shader for brushes and
  terrain layers (texture to linear x light x exposure, back to sRGB, fog), primitives' 3D
  lightmaps read in their shader (trilinear over slices). The render job bakes on other
  threads with VRAD's 0...10 progress line, Esc cancels; bakes kept per quality while the
  key is the same; the Camera Preview bakes at Draft. Sky pictures are read small and
  linear for the sky light (`SkyLibrary::picture`).
- **UI:** light Properties (falloff, 50% distance, hard falloff, c / l / q, exponent,
  spread), Environment (sky light %, minimum light, smoothing angle), Face panel lightmap
  scale, primitive lightmap scale, prop "casts shadows". README and Help > Controls.
- **Tests:** core `testBake` (falloff numbers against VRAD's, spot cone, Halton
  directions, a shadow under a slab, sky light open / covered, minimum light, cancelling, the
  sun and a sphere's 3D lightmap, soft sun edge, sky picture orientation, map round trip of
  every new setting, old lights as inverse-square): 4849 checks, 0 failed. New UI script
  `v121_baked_light.script` (bake log and progress, kept bake, rendersame, props' shadows,
  sky light / minimum light, face lightmap scale, Esc, preview, save / open). All UI
  scripts pass at 1x and 2x; idle 0 ticks at 1x and 2x. A 400-box map with 9 lights bakes
  in 0.3 s (Draft), 3.6 s (Normal), 14 s (High) on 2 threads.
- **Screenshots:** `m12-04-baked-light.png`, `m12-05-bake-log.png`,
  `m12-06-preview-baked.png` (UI script), `m12-07-room-render.png` (a room lit by a lamp
  and the sun through a doorway), `m12-08-terrain-spot-render.png` (terrain, a spot, a
  smooth cylinder), `m12-09-sun-shadows-render.png` (sun shadows of a sphere and a wall).
- Next: report; then discuss batch 3 (bounces).

### 2026-11-20 — Batch 2 (v0.12.1, baked direct light and shadows) agreed (docs only)
- The user asked whether Scalpellino bakes light like Source / Hammer: not yet (v0.12.0
  renders use the rough light preview: no shadows, no bounce, flat ambient). Discussed
  what would make it as close to VRAD as possible while also lighting primitives.
- The user shared their research on Source 1 lighting (VRAD, radiosity normal mapping,
  ambient cubes). Its unconfirmed points were checked in Valve's Source SDK 2013 code
  and settled: the 50% / 0% falloff fit, the 100-unit normalization, spot falloff (with
  an extra cosine the research missed), 162 sky directions, ColorRGBExp32 decoding,
  squared and normalized bump weights, the bounce stop rule, how ambient cubes are
  gathered, smoothing at 45 degrees by default. Written down as "VRAD reference" in
  PLAN.md (Roadmap item 5).
- 26 numbered questions with options, consequences and a recommendation; all answered.
  Recorded as "Batch 2 decided with the user" (Roadmap item 5); the batch list updated
  (primitives get their own 3D lightmaps already in v0.12.1; v0.12.3 is ambient cubes and
  prop vertex light). Choices the user flagged "might change" are under Open questions,
  with the default texture scale change (decided, not scheduled).
- Replaces the "Batch 2 under discussion" proposal (5 questions) recorded earlier the
  same evening from another chat; its questions are answered by this discussion (noted
  in Open questions).
- No code changed. Next: build v0.12.1 (one release) when the user says go.

### 2026-11-20 — Review fixes (branch `fix/review-findings`, not released)
- The user passed on a review (another AI's) with six findings; each checked against the
  code first, all six confirmed and fixed. On a branch, no `main` push, no release.
- **Releases from the wrong commit** (`build.yml`): a version release took the zips of
  whichever successful `main` build first saw a notes file without a release. Now it is
  only published from a build of the commit that added the notes file: this run's own
  zips if it is that commit, otherwise the notes commit is tagged and the workflow is
  started on the tag (GITHUB_TOKEN tag pushes start no runs; `workflow_dispatch` does),
  which builds and publishes exactly that commit. "Latest" is still only from the newest
  `main` commit, but its check no longer skips the version releases. Checked locally by
  running the publish step against a scratch repo with a stand-in `gh`: notes commit's
  own run, later commit without tag, tag already there (warn), tag at another commit
  (error, no release), already released.
- **Measured first:** new brush / terrain benchmark (`bench brushes`, DEV.md). Before:
  building one view's scene copied every brush (6 ms with 4000 boxes and 16 terrains,
  four times per redraw); an edit of one brush took 154 ms to rebuild the caches.
- **Scenes share the brush list** (`SceneBrushes` in `render/scene.hpp`): a scene keeps
  the cache's list by `shared_ptr` and swaps in the few brushes a tool replaces (terrain
  being sculpted) instead of copying all of them. Scene build 6 ms → 0.00 ms.
- **Only touched brushes rebuild:** `Document::brushStamp(id)` changes in `setBrush`
  (apply, undo, redo) and on open; `lightingStamp()` when lights or the environment
  change. `SceneCache` (editor; and the renderer's, with hidden brushes kept and nodraw
  left out) and `LightingCache` keep each brush's data with its stamp and rebuild only
  those whose stamp changed (lighting: all when a light or the environment changed). The
  old list stays alive until the next update, so a `ToolEnv` reference taken before an
  edit never points at freed memory. Edit of 1 brush: 154 ms → 9 ms (4000 boxes + 16
  terrains), 28 → 1.4 ms (1000 boxes).
- **Undo history limit** (`Document::kHistoryBytes`, 64 MB, as Mazzuola): each step's
  size is estimated (`commandBytes`: brushes with terrain and paint, objects, selections);
  the oldest steps are dropped past the limit, the newest always stays. Undoing all that
  is left reports unsaved changes (`historyTop` falls back to the newest dropped step).
- **Strict numbers in .map** (as Mazzuola's reader): terrain height and blend rows
  (`abc`, `1.5x` now refuse the file with the line and the bad text), terrain `face` and
  `power`, brush `visgroup` refuse the file; `combo` and the other object key values
  (`numOr`) ignore a bad value as before, but no longer read `1.5x` as 1.5; the visgroup
  list drops entries with a bad id. Curve generator keys were not in the review (parked).
- **Terrain size checked before use:** `power` is range-checked before `(1 << power) + 1`.
- **Dependency fingerprints:** `URL_HASH SHA256=…` for raylib, ImGui, rlImGui in
  `cmake/Deps.cmake`. The workspace can't download the archives, so a throwaway workflow
  on the branch printed each hash (two downloads each, identical); removed again.
- **Tests:** core 4791 checks, 0 failed (new `testBrushCaches`, `testHistoryLimit`,
  `testMapStrictNumbers`; the caches are compiled into the test binary, they only use the
  core). UI 29 of 29 at 1x and 2x. Idle 0 ticks at 1x. No screenshots (nothing visible
  changed).
- **Second check before merging** (the user asked): the notes commit is now found along
  `main` only (`git log --first-parent`), so after a merge the release is built from the
  merge commit, not the branch commit behind it (which lacks what landed on `main`
  meanwhile); checked with a scratch repo holding a merge. Also: a tag run uses the old
  commit's workflow file, so the rebuild route can't redo v0.3.1 (its workflow only
  releases on pushes); the earlier PLAN note saying it could was wrong and is corrected
  (manual steps there). Every notes file up to v0.12.0 has a release, so merging starts
  no version builds.
- **App check, branch vs `main`:** both built and run through the same script (terrain,
  a shot mid sculpt stroke, paint, undo / redo, move, ambient change, hide, nodraw,
  render, Camera Preview, save and reopen): 13 of 15 screenshots pixel-identical, the
  render included; the other two differ only in the open render log's times and file
  date. Render time the same (0.21–0.23 s, three runs each).
- **Merged** into `main` (fast-forward) after the user's go-ahead. No new notes file, so
  no version release; "Latest build" is refreshed.
- **Next:** watch the first version release made with the new release logic.

### 2026-11-20 — Docs check against the chat; batch 2 proposal recorded (docs only)
- The user asked whether anything decided in the chat was missing from the docs (the repo
  had changed: the review fixes were merged). Checked after updating to `main`: the batch
  split, batch 1's decisions and the way of working were in. Missing / fixed: the batch 2
  proposal and its 5 open questions (now in PLAN.md "Batch 2 under discussion"), the
  status line (report done, batch 2 being discussed), and two older PLAN lines that still
  put the picture size in the render window (now point to the decision: the camera's).

### 2026-11-20 — The user tried v0.12.0 (docs only)
- "Seems to work! so far!" The v0.12.0 choices are not explicitly confirmed yet (still
  listed in PLAN.md). Next: discuss batch 2 (baked direct light and shadows).

### 2026-11-20 — Render, step 1: the pipeline (v0.12.0)
- The user said go for batch 1 (decided earlier today, entry below).
- **Core** (`core/render_job`): render options (quality, anti-aliasing, filtering) and their
  settings text, `fitAA` (the drawn picture at most 16384 a side and 50 M pixels),
  `downscaleRGB` (averaging in linear light), render file names.
- **Renderer** (`render/renderer`): the picture's scene from the map as it is (every
  brush, hidden ones too, nodraw faces dropped; primitives, props, the map's sky and fog,
  lights; the View menu ignored), the rough lighting always on, drawn through a lens
  `Viewport` at any size with its own texture filtering, read back top row first.
- **Height fog** in the brush, primitive and prop shaders (`fogAmount`: the stretch of the
  line of sight inside the layer, as in the baked sky); renders pass the map's base and
  thickness, the editor passes 0 (distance only, unchanged).
- **Render job** (`app/editor_render.cpp`): Render button at the left of the status bar,
  new Render menu (Render..., Update Camera Preview moved from World, Quality,
  Anti-aliasing, Texture filtering; options in the user's `render.txt`), Cmd+Shift+K. A
  modal terminal window, one step per frame, Esc stops between steps; PNG plus `.log` in
  the Bottega's `renders/`; no Bottega: the Bottega dialog with a message.
- **Camera Preview** = the renderer at Draft settings (replaces the editor-look preview).
- **Tests:** core 4682 checks, 0 failed (new `testRenderJob`). UI 29 of 29 at 1x and 2x (new
  `v120_render`: no camera, no Bottega, a saved render with its log and size, the View
  menu and hiding don't change the picture, nodraw left out, Draft / 4x / crisp, Esc
  stops and saves nothing, the preview). Idle 0 ticks at 1x and 2x, control 887.
- **Screenshots:** `m12-01-render.png` (a render, 640 x 360, Normal), `m12-02-render-log.png`
  (the terminal window), `m12-03-preview-draft-render.png` (the preview with the 3D view's
  sky off).
- Seen: drawing 1280 x 720 with a primitive takes about 4 s here (software OpenGL); a real
  graphics card is far faster.
- **Next:** report; then discuss batch 2 (baked direct light and shadows).

### 2026-11-20 — M12 split into batches; batch 1 decided (docs only)
- New chat. Agreed with the user: M12 in 7 batches (v0.12.0 pipeline, v0.12.1 direct
  light and shadows, v0.12.2 bounces, v0.12.3 light grid, v0.12.4 materials, v0.12.5
  probes, v0.12.6 mirrors), each discussed first, built on the user's go, then reported.
- Batch 1 decided: the editor waits during a render; Render button at the left of the
  status bar plus a Render menu, Cmd+Shift+K; picture size always the camera's (no size
  field in the window); presets Draft / Normal / High, anti-aliasing, texture filtering
  as render options; saved to the Bottega's `renders/` (asks for a Bottega if none); the
  window is only the terminal, Esc stops. Recorded in PLAN.md (Roadmap item 5),
  ARCHITECTURE §9 / §15, HANDOFF.md. No code changed.

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
