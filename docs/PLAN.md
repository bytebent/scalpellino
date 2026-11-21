# Scalpellino — Working Plan & Log

**Purpose:** let any future session (human or AI) pick up work without outside context.
Update this file at the end of every step: what was done, test results, what's next,
open questions. The step-by-step log is in `LOG.md` (newest entries at the top).

Design: [`ARCHITECTURE.md`](ARCHITECTURE.md). Rules: [`../CLAUDE.md`](../CLAUDE.md).

---

## Current status

- **Released:** v0.1.0 … v0.12.1. Milestones 0–11, Random Replicate and Sky Lab are done (see the
  milestone sections below and the log); the user confirmed the choices of M10, Random
  Replicate and v0.10.2 (2026-11-19). **v0.10.3 is built and pushed** (asked by the user
  before Sky Lab): `editor.cpp` split by topic (step 1: code moved unchanged; step 2:
  script commands as tables, selection landing in the core), a prop benchmark (DEV.md
  "Benchmark"), and what it showed: Snap to Land among many props, 2D views drawing
  everything, the 3D view's draw call per prop. Reported; the user tested it on their Mac
  and it is fine (2026-11-19). Next session: Sky Lab.
- **Sky Lab is done** (milestone 10c, design agreed 2026-11-19, Roadmap item 3): v0.10.4
  (window, live 3D preview, bake, sky modes, sun, haze band, ground, sun lock, Rebake),
  v0.10.5 (clouds) and **v0.10.6 (moon, eclipses, stars, comets, rings, rainbow, fog in
  the sky, Blend with Sun, saved skies, Randomize Sky, scene effects stored) are built and
  pushed**; the choices made without asking are listed under "Sky Lab v0.10.4 / v0.10.5 /
  v0.10.6, decided without asking" for the user to confirm. v0.10.6 also fixed JPEG and
  TGA images not loading at all (raylib had them off).
- **Milestone 11, cameras and Camera Preview, is done** (v0.11.0 camera objects, v0.11.1
  Camera Preview and presets; built and pushed). v0.11.0's choices were confirmed by the
  user; v0.11.1's are under "v0.11.1, decided without asking". **v0.11.2** (an aside the
  user asked for): built-in Sky Lab skies and plain sky bottoms kept out of graphics
  memory (choices under "v0.11.2, decided without asking"). Next: **report**, then
  **Render, the Source look, with materials** (Roadmap item 5) when the user says go; its
  open point is the reflection probes' details. The user tried the cameras (2026-11-20):
  the preview must be a draft render ignoring the View menu (Roadmap item 5); they will
  start a **new chat** for the next milestones.
- **M12 is split into 7 batches (v0.12.0 … v0.12.6), agreed 2026-11-20** (Roadmap item 5).
  **Batch 1, the render pipeline, is built and pushed (v0.12.0)**: Render button / menu /
  Cmd+Shift+K, terminal window, PNG and log in the Bottega's `renders/`, render options,
  the Camera Preview as a draft render, height fog in renders. Its choices made without
  asking are under "v0.12.0, decided without asking". Reported; the user tried it ("seems
  to work"). **Batch 2 (v0.12.1, baked direct light and shadows) was discussed in depth
  and agreed (2026-11-20)**: VRAD's methods, checked against Valve's SDK 2013 source; see
  "Batch 2 decided with the user" under Roadmap item 5 and the choices flagged "might
  change" under Open questions. **v0.12.1 is built and pushed** (the user said go,
  2026-11-21): baked direct light and shadows (`core/bake.*`, `render/baked_light.*`); its
  choices made without asking are under "v0.12.1, decided without asking" (**the user
  confirmed them, 2026-11-21**). Released (CI green). **Batch 3 (v0.12.2, bounces) was
  discussed and agreed (2026-11-21)**: see "Batch 3 decided with the user" under Roadmap
  item 5. **v0.12.2 is built and pushed** (the user said go, 2026-11-21): bounced light
  (VRAD's radiosity in `core/bake.*`); its choices made without asking are under "v0.12.2,
  decided without asking". Renders of the busy test maps (`tests/bench/render.script`) are
  in `docs/screenshots/m12-13` to `m12-16`. Reported (CI green, released); the user is
  fine with the slow bakes. **Batch 4 (v0.12.3, prop lighting and the light
  grid) was discussed and agreed (2026-11-21)**: see "Batch 4 decided with the user" under
  Roadmap item 5. **v0.12.3 is built and pushed** (the user
  said go, 2026-11-21): props lit by the bake and the light grid; its choices made without
  asking are under "v0.12.3, decided without asking"; the busy maps rendered again
  (`docs/screenshots/m12-20` to `m12-22`). **The 1000-rocks map didn't finish within the
  benchmark's hour** (2.6 million prop vertices at full quality, about 325 rays each, on 2
  threads). Reported; **the user wants an option for fewer rays for props, in the next
  batch** (see Roadmap item 5, batch 5). Next: **discuss batch 5 (v0.12.4, the material
  system)** in depth.
- **Review fixes (2026-11-20), merged into `main`** (fast-forward from
  `fix/review-findings` after the user's go-ahead; no new version, so only "Latest build"
  is refreshed): version releases are
  built from the commit that added their notes; scenes share the brush list instead of
  copying it per view; edits rebuild only the touched brushes (editor, light preview,
  renderer); 64 MB undo history limit; strict numbers in terrain rows, terrain face /
  size, visgroup and combo; terrain size checked before use; dependency SHA-256 checks.
  A brush and terrain benchmark was added (DEV.md "Benchmark"). Next: watch the first
  version release made with the new release logic. See the LOG entry.
- **Roadmap:** Sky Lab → cameras and Camera Preview →
  Render (Source look, with the material system) → water / cloud planes → camera effects →
  export. **Confirmed by the user (2026-11-20)** up to camera effects; **export is not
  confirmed** (it stays last and optional). Details under "Roadmap" and "Direction".
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

Each archive's SHA-256 is in `cmake/Deps.cmake` (checked at configure time).

## Open questions (parked)

- **Light grid "for now" choices (user, 2026-11-21):** a regular HL2-style grid (not
  per-leaf samples) and props reading the nearest sample (no blending); might change.
- **Prop lighting origin (maybe, user 2026-11-21):** Source's `lightingorigin` key (measure a
  prop's light at another point) if the bounding box centre turns out wrong for some props.
- **Batch 2 (v0.12.1) choices flagged "might change" (user, 2026-11-20).** Agreed for
  now (see "Batch 2 decided with the user" under Roadmap item 5); look at them again
  after the user has tried the bake:
  - **Rough light preview keeps its old fade** (`(1 - d/reach)^2`, ambient everywhere)
    while renders use VRAD's falloff and sky light. Check later whether the editor feels
    off next to the renders; if so, give the preview the same formulas (still no shadows).
  - **Sky Lab's lighting fields stay unused in the Source render:** soft shadows /
    shadow softness (the sun's spread is the sun light's own field, as in Hammer),
    shadow strength (would fake the light) and sky dome. Sun diffuse is used (a
    brightness multiplier on the sun). Revisit if something is missed.
  - **Primitive lightmap density:** one lightmap scale per primitive, shared by a
    combination; very thin parts may leak light. Might change once tried.
  - **Primitives and props don't bounce light** (as Source: only brush faces are bounce
    patches). **Changed in batch 3 (user, 2026-11-21): they do reflect light onto other
    things** (a deliberate difference from Source); see "Batch 3 decided with the user".
  - **Default texture scale (decided, not scheduled):** the user will change the default
    for new faces to Source's real size (0.25 in Hammer units = about 0.64 in cm), since
    the lighting now converts Source's numbers to cm. Existing maps unchanged. When:
    open (not part of v0.12.1 unless the user says so).
  - The earlier proposal recorded here (5 questions, from another chat the same evening)
    is superseded by the in-depth discussion: shadow detail = lightmap scale per face
    (Q1), sun spread on the sun light, no size on point / spot lights (Q2, Hammer has
    none), sky light (Q3, yes), primitives lit already in v0.12.1 (Q4), linear light
    (Q5: brighter than the texture is allowed; no fixed 2x overbright).

- **The v0.3.1 release holds the wrong code** (split / carve / hollow from after v0.3.1;
  the release bug fixed on `fix/review-findings`). Not changed: replacing a published
  release is the user's call. The automatic rebuild can't do it: started on the v0.3.1
  tag, GitHub runs that commit's old `build.yml`, whose release job only runs on pushes.
  By hand: delete the v0.3.1 release (keep the tag, it already points at the right
  commit), run the workflow on the tag (its `macos` job makes the zip), download the zip
  and attach it to a new v0.3.1 release made from the tag with its notes. Or just leave a
  note in v0.3.1's release text.
- **Curve generator parameters** in a `.map` (`core/curves.cpp curveFromKeys`) still read
  numbers loosely (`atoi`, `strtod`): not in the review's list; worth the same strict
  reading some time (a bad value there only changes a generated shape).

- **v0.12.3 (props and the light grid), decided without asking (confirm with the user):**
  - **Per-vertex samples** start 0.5 units off along the vertex normal (then the usual 0.3);
    the bounce at a vertex is gathered like the primitives' cells (162 rays to the patches,
    Draft 40).
  - **A prop that casts no shadows** shadows itself through a ray test of its own triangles.
    For props lit like a Source model, the lights' visibility ignores the prop's own
    triangles (it is measured from inside it).
  - **Like a Source model:** a light counts if a ray from the lighting origin reaches it
    (Source chooses by visibility data, without a ray); they are ranked by their strength
    at the origin; the 2 strongest are drawn per pixel with Half-Lambert and VRAD's falloff,
    the others are added to the cube by how much each side faces them.
  - **Lighting origin moved out of solids:** up in 16 steps of a 16th of the prop's height.
  - **The grid** covers everything (brushes, primitives, props), centred on it, at most 2
    million samples (else none, with a note). Samples inside primitives are skipped too.
    Each side of a cube averages the rays by how much they face it; rays that hit nothing
    take the sky (picture x sky light %), rays that hit a surface its patch's light x its
    reflectivity; with bounce strength 0 (no patches) the cubes hold only the sky.
  - **Drawing:** in renders props are drawn one call per prop (not instanced), their vertex
    light read from a float texture by vertex number. The editor's drawing is unchanged.
  - **The grid view:** 12-unit cubes, sRGB (clipped), in the editor's 3D view only; it shows
    the last bake made or reused (a render or a Camera Preview Update).
- **v0.12.2 (bounces), decided without asking (confirm with the user):**
  - **Face patches:** VRAD's split along the widest world axis while a piece is 4 luxels or
    more across (maxchop = minchop = 4); Draft's coarser lightmaps make them twice as big.
    A patch's direct light is the average of the luxels on it (VRAD's AddSampleToPatch),
    without the minimum light.
  - **Terrain patches:** blocks of cells about 4 luxels wide; their reflectivity is the face
    texture's, blended toward each painted layer by its average strength in the block.
  - **Primitive patches:** their surface cells (within half a cell of the surface) in blocks
    of 4 x 4 x 4 cells, split by facing (6 directions); area = cells x cell^2; colour of the
    nearest piece (texture average x its colour, linear).
  - **Prop patches:** triangles grouped by patch-sized cells (160 units) and 6 facings;
    their direct light is computed at the patch's centre (props have no lightmaps yet);
    colour = the model's materials (texture average x colour factor) by how many triangles
    use each. Only props that cast shadows reflect (the others aren't in the ray tests).
  - **Transfers:** both patches must face each other (VRAD leaves that to visibility); the
    polygon form factor is taken unsigned (either winding order); transfers under
    1e-5 x pi are dropped (VRAD: 1e-7), which keeps big maps quicker; rays start 1 unit off
    the patches. Normalized to pi per receiver as VRAD.
  - **Onto the lightmaps:** each luxel blends its face's own patches with weights falling to
    0 at 1.25 patch widths (an approximation of VRAD's radial blending; nearest patch if
    none is that close); terrain: bilinear between its blocks.
  - **Primitives' gather:** 162 rays (Draft 40) from each surface cell; the hit patch's
    direct + bounced light x its reflectivity, cosine-weighted and normalized like the sky
    light.
  - **Stop rule as VRAD:** the light added by a bounce, summed over all patches, under 1 in
    VRAD's 0..255 units in every channel. Bounce strength 0 = no radiosity at all (no
    bounce lines).
  - **Textures that can't be read** bounce 20% grey.
  - **The log** adds where the time went (direct light, transfers, bounces).
  - **Transfers are found pair by pair once (i < j, one ray for both directions)**, and only
    between patches near enough to pass the minimum (patches sorted by area into grids):
    the same transfers as checking every pair, about twice as quick.
  - **Measured (2 threads, cloud workspace, `tests/bench/run.sh ... render`):** the rich
    map 1.5 s; 1000 boxes + 8 terrains 23 s (direct 15, transfers 7); 4000 boxes + 16
    terrains 137 s (direct 59, transfers 76); 1000 rocks of 5000 triangles 287 s (direct
    39, transfers 234: 52 000 patches, mostly the rocks'). Transfers grow with the square
    of the patch count. Reported to the user before adding anything (agreed). **The user
    is fine with these bake times (2026-11-21): no speed-up or cutoff added.**
- **v0.12.1 (baked light and shadows), decided without asking (confirm with the user):**
  - **Default lightmap scale 40 cm** (Hammer's 16 units are 40.64; rounded), allowed 1 to
    512. Faces needing more than 512 luxels a side get a coarser lightmap (doubled until it
    fits; the log says how many).
  - **Sky light strength:** a new Environment field "sky light (%)", default 100 = the sky
    picture's colours as they are (linear). With no sky picture the sky's colour is the
    ambient colour. This replaces "the Environment ambient as a multiplier" (recorded
    before building): a percentage was clearer. Sky Lab's IBL tick box and intensity stay
    unused (the sky picture always lights).
  - **Minimum light** = the ambient colour turned linear (^2.2), added everywhere: looks like
    the old flat ambient.
  - **Primitive lightmaps:** at least 4 cells across the object's largest side, at most 128
    per side; cells within 1.75 cells of the surface are lit at the nearest surface point,
    the others take their neighbours' light (so reading between cells never mixes in black).
  - **Quality:** Draft = lightmaps 2x coarser, a quarter of the sky rays (40) and sun
    samples, no extra samples (VRAD -fast). Normal = 162 sky rays, 30 sun samples when
    spread, luxels that differ from a neighbour by more than 0.02 + 10% sampled again 3 x 3.
    High = 4 x 4 extra samples and 4x the sky rays. Extra samples use a quarter of the sky
    rays (quicker; the sky light changes slowly).
  - **Samples:** luxels outside their face are moved onto it (a little inside its edge);
    rays start 0.3 cm off the surface; terrain edge luxels sit 0.2% inside.
  - **Smoothing** joins faces of different brushes too (as VRAD smooths all world faces);
    coplanar faces count once.
  - **The log notes the inverse-square lights once** ("N lights fade with the inverse
    square...") instead of per light: every light has a reach, so a per-light note would
    list them all.
  - **Spot exponent default 1** (Hammer's FGD default); 0 or 1 = straight fade.
  - **Sun brightness:** Sky Lab's sun diffuse multiplies it only when the map has Sky Lab
    settings.
  - **Exposure** (stops, on the camera) = a multiplier of 2^stops on the light.
  - **The bake key** hashes everything that lights the map (brushes, terrain heights,
    lights, primitives, props and their models, environment, the sky picture), so moving
    the camera or a bookmark keeps the bake.
  - **Camera Preview** bakes on Update, on the editor's thread (Draft; all threads), so the
    editor waits a moment on big maps. Kept like a render's.
  - **Lightmaps on the graphics card** are 32-bit float RGB (atlas pages 2048 wide, a
    one-luxel border copied from the edge); a primitive's 3D lightmap is one texture with
    its slices side by side, read trilinearly.
  - **Switching a light to 50% / 0%** starts the 50% distance at a quarter of the reach.
  - **Known gaps:** props aren't lit by the bake yet (v0.12.3); the render log can't show
    "light bake stopped" if Esc comes before the bake starts (it then just stops).
- **v0.12.0 (Render, the pipeline), decided without asking (confirm with the user):**
  - **A new Render menu** (between World and View): Render..., Update Camera Preview
    (moved there from the World menu), Quality, Anti-aliasing, Texture filtering. The
    status bar button is orange and greyed out while the map has no camera.
  - **The options are the user's settings** (`render.txt` next to the layout), not saved in
    the map. Defaults: Normal, 2x, smooth.
  - **Quality** sets the anti-aliasing for now: Draft off, Normal 2x, High 3x; the
    anti-aliasing menu also has 4x. Choosing a quality overrides the anti-aliasing chosen
    before. Too large to draw (over 16384 pixels a side or 50 million pixels, e.g. 4K at
    3x): it uses less and the log says so.
  - **Anti-aliasing** averages in linear light (as light adds up), not in the stored values.
  - **Smooth filtering** = trilinear with mipmaps; crisp = nearest pixel. The preview uses
    the render option too, not the editor's Texture > Filtering.
  - **File names:** an unsaved map is `untitled`; letters, digits, - and _ kept, the rest
    becomes _ (`camera 1` -> `camera_1`); local time; two renders in the same second get
    `_2`. Only a saved render writes its log; stopped or failed ones leave no files.
  - **The window:** modal, the log with a timestamp per line (seconds since the start),
    warnings in orange; Close (or Esc / Return) once finished; it keeps the last log until
    the next render. No camera: the window says so. One step per frame (map, lighting,
    drawing, scaling down, saving), so Esc stops between steps (not in the middle of the
    drawing).
  - **What a picture holds:** missing models are left out (a warning), missing textures are
    drawn as the checker (a warning), a map without lights gets a note (only the ambient).
    While Sky Lab is open, renders and the preview use the map's baked sky, not the
    window's live one (the old preview used the live one).
  - **Height fog in renders:** the stretch of the line of sight inside the fog layer, from
    the camera's real height (the baked sky assumes an eye at height 0).

- **v0.11.2 (built-in skies, sky memory), decided without asking (confirm with the user):**
  - The built-in skies are the 13 distinct skies of the Sky Lab test scripts, named
    by what they show; "fog in the sky" left out (the fog is the map's, not the sky's).
  - They are part of the app (no files to install), listed under "Built-in" below the
    user's own saved skies; they can't be changed or removed (save a changed one under
    your own name).
  - **The cut:** "plain" means every pixel of the row within 2 (of 255) of the bottom
    colour; the margin past the fade is **5 degrees**; never above the horizon; only when
    it saves 5% or more; any sky picture, not only Sky Lab's; Sky Lab's own live preview
    (while its window is open) isn't cut (it is small and short-lived).

- **v0.11.1 (Camera Preview, presets), decided without asking: the user said the choices
  "seem good so far" (2026-11-20; not tried in the app yet).** Kept for reference:
  - **Cmd+K** draws the preview (K is the camera key; also the panel's Update button and
    World > Update Camera Preview).
  - **Layout:** the right column is now Textures (top half) / Properties / Camera Preview
    (the lower 42% of the bottom half). The saved layout resets once to this (dock
    layout version bumped). Without a camera the preview's space goes to Properties.
  - **What it shows now:** textures always; the map's sky and fog whatever the View menu
    toggles say; the rough light preview only when it is on (it costs time); hidden
    brushes, and primitives / props hidden with the View toggles, stay out; Sky Lab's
    live sky while Sky Lab is open (as the 3D view). **Overruled by the user
    (2026-11-20):** the preview must ignore every View option and show hidden things;
    to fix in M12 (see Roadmap item 5).
  - **Size:** drawn as large as the panel at the picture's shape, at screen resolution
    (not the picture's pixel size); resizing the panel scales the last picture until the
    next update. "Out of date" shows when the map (anything undoable) or the active
    camera changed since; View toggles and Sky Lab's live edits don't count.
  - **Near** cuts through solid brushes as they are (you see through, nothing is capped);
    looking into a hollowed room through its wall works.
  - **Orthographic** cameras see the sky as almost one colour (every ray is parallel).
  - **Presets** hold the settings only (lens, near / far, size, orthographic and its
    width, exposure), not position, angles, name or colour. Names are cleaned for files
    ("Portrait/50" becomes "Portrait_50"); saving under an existing name replaces it;
    deleting a preset is deleting its file in the Bottega (no button). Presets need the
    Bottega, as saved skies do.

- **v0.11.0 (cameras), decided without asking: confirmed by the user (2026-11-20, "seems
  sensible").** Kept here for reference:
  - **Shortcut Shift+K** (K is free; C is the curve tool); the tool is in the World and
    Tools menus and the tools strip.
  - **Defaults:** 50 mm lens (39.6 degrees), 1920 x 1080, near 4 units, far 100000 (1 km),
    orthographic width 1024 units, amber marker. The field of view is measured across the
    picture's **longer** side and mm are on a **36 mm (full-frame) sensor** across that
    side, so a portrait picture keeps the lens's angle along its height.
  - **Active camera:** the first camera made is active; new and duplicated cameras never
    take over; deleting the active one makes the first one left (map order) active; a
    file with none or several active opens with the first. Unticking "active" isn't
    possible (make another one active instead).
  - **Names** `camera 1`, `camera 2`... (the lowest free number); a duplicate gets the next
    free name, offset by one grid step like other duplicates.
  - **Roll:** positive tips the camera to the right (clockwise seen from behind it). The
    roll ring is yellow, 1.35 x the other rings, round the view direction; in a 2D view it
    shows only when the view direction points enough toward or away from you (else it
    would be a line you can't turn by dragging; type the roll).
  - **Rings** turn a camera about its own position (like a light), keeping its frame (the
    world rings also change roll when the camera is tilted, as turning a real camera does).
  - **Aim at a clicked point:** in a 2D view the point is taken at the camera's own depth
    (the top view aims level); in 3D the first brush or terrain surface under the cursor
    (primitives and props aren't hit yet), else 10 m along the ray. The roll is kept.
  - **Drawing:** pyramid 56 px long on screen toward the picture frame (an orthographic
    camera: a box as wide as its picture, which can be large), a small triangle on the
    top edge for "up"; marker a diamond in the camera's colour, a white dot on the active
    one; inactive cameras dimmer.
  - **Properties** edits one camera at a time (several selected: the count, the rings and
    aim work on all); the camera tool lists the map's cameras with a dot to make one
    active and a click to select it.
  - **Saved** as `scal_camera` (keys listed in ARCHITECTURE §6 "Cameras"); angles
    Hammer-style (pitch positive down), like lights' `angles`.
  - **Not for cameras (like lights):** Multi- / Random Replicate, Snap to Land, hiding and
    visgroups, Cmd+E. The 3D rectangle select includes them. Exposure is stored only;
    depth of field waits for the camera effects.

- **Sky Lab v0.10.4, decided without asking (confirm with the user):**
  - **Lock on by default:** the first time Sky Lab opens in a map with a sun light, the
    lock is ticked and the sky's sun starts on the light (untick to separate them).
  - **Names of baked skies:** a saved map's sky is `skies/skylab/<map name>`; an unsaved
    map gets the first free `skies/skylab/untitled_<n>` and keeps it, so two unsaved maps
    never share a file. Applying again overwrites the map's own file.
  - **Directions:** azimuth is measured like a light's yaw (0 = +X, counterclockwise seen
    from above); the compass disc is drawn like the top view (+Y up); the inner ring is
    the horizon, the edge 30 degrees below it.
  - **Units:** disk size in degrees across (default 2.5; Bryce has a 0..100 slider),
    horizon illusion in % larger at the horizon; the other values 0..100 as in Bryce.
  - **Fields:** a slider and a typed field side by side for every value (like Bryce); no
    shortcut, World menu only (World > Sky Lab..., World > Rebake Sky Lab sky).
  - **Atmosphere off** is one plain colour all round (no ground colour), as Bryce uses it
    for plain backgrounds; haze still shows if left on (Bryce: set haze to 0).
  - **Ambient** can also be edited in Sky Lab (Scene lighting); it is the same value as
    World > Environment's.
  - **A missing PNG** (another machine, a new Bottega) is baked again when the map is
    opened, if a Bottega is set; that is no undo step and doesn't mark the map changed.
  - **Sky colours:** Soft and Darker come from a small light-scattering model of the air
    (a table per sky), then a colour grade toward Bryce's clean blue (Soft paler, Darker
    deeper). A tiny fixed dither stops banding in the 8-bit PNG.
  - **Presets and Randomize Sky** were agreed but not scheduled: they come with v0.10.6.
  - The NASA moon map is in `assets/moon/` (1024x512) already, used from v0.10.6. **Credit
    it as "NASA's Scientific Visualization Studio"** (user, 2026-11-20): THIRD_PARTY.md,
    `assets/moon/CREDIT.txt`, and next to the built-in moon in Sky Lab when it is used.
- **Sky Lab v0.10.5 (clouds), decided without asking (confirm with the user):**
  - **Each layer has its own** cover, frequency, amplitude, turbulence and complexity
    (Bryce moves both layers with one set of sliders). Turbulence and complexity are
    sliders (Bryce: +/- buttons on the cloud texture; no Deep Texture Editor, as agreed).
  - **One cloud height** for both: cumulus from 0.6 to 4.6 km, stratus two and a half
    times higher. As in Bryce it also widens the haze band; the default (35) leaves
    v0.10.4 skies as they were. Layers follow the planet's curve and fade toward the
    horizon.
  - **Pattern seed** with a New pattern button (not in Bryce; Bryce changes the pattern
    through its texture library).
  - **Colours:** the lit side takes the sun's light (white at midday, the sunset's colours
    low down), the shaded side is the cumulus colour times the sky's colour (blue-grey),
    stratus are thin and bright with a touch of the sky dome colour. Stratus are streaked
    along one direction set by the seed. **At night clouds are dark shapes** until the
    moon lights them (v0.10.6).
  - Cloud shadows and the darker scene under heavy cover are stored for the renderer.
  - **Bakes use all of the machine's threads** (the core now links threads). 2048 wide
    with both layers: 0.4 s on 2 threads here; 8192: 5.5 s.
- **Sky Lab v0.10.6 (night sky and effects), decided without asking (confirm):**
  - **Moon phase** 0..100 is the month (0 new, 50 full); **the lit side always faces the
    sun**, as in the real sky (so 25 and 75 look alike). The moon has its own size,
    intensity and glow (Bryce shares the sun's); horizon illusion is shared.
  - **Own moon images** go in the Bottega's `skies/moons/` (left out of the sky list);
    2:1 = a map wrapped on the ball, square = a photo of the face; bigger than 1024 wide
    is scaled down. The built-in moon shows "NASA's Scientific Visualization Studio".
  - **Stars:** the random field has real-sky brightnesses and colours (Bryce: all the same
    brightness); comets work with both fields (Bryce: random only). Instead of Bryce's
    trackball, **latitude and star time** (the sidereal hour) place the sky; north is +Y,
    east +X. Stars show only where the sky is dark (night, a total eclipse).
  - **Rings** round both the sun and the moon; radius 50 = 22 degrees, the secondary at
    about 46.
  - **Rainbow:** "radius" is the bow's width, as Bryce's guide says (the bow itself stays
    at 42 degrees, the secondary at 51).
  - **Fog in the sky:** the eye is at height 0; thickness 0 (all older maps) keeps the fog
    out of the sky; the sky is out of date when the map's fog changes (Rebake).
  - **Blend with Sun** works on Soft and Darker skies; its defaults (on, colour 60,
    luminance 100) give exactly the v0.10.4 / v0.10.5 skies (checked byte for byte).
  - **Saved skies** are named files in the Bottega's `presets/skies` (not Bryce's seven
    memory dots); loading one keeps this map's sun lock and baked size. Randomize keeps
    the lock, the baked size and the moon texture.
  - Stored for the renderer: Volumetric World, colour perspective (default 0 1 88, from the
    guide's example), IBL with an intensity.
  - **Found and fixed:** raylib was built without JPEG and TGA, so `.jpg` / `.tga`
    textures, skies and JPEG textures inside models silently loaded as nothing. Both are
    on now.
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
- **Props (M10), built (v0.10.0). Decided while building, confirmed by the user (2026-11-19):**
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
- **v0.10.3 (asked by the user: split, both steps; benchmark for my own use; target
  hundreds to thousands of props), decided without asking:**
  - The script interpreter became tables of commands inside the Editor (not a separate
    class): the commands use the editor's internals, so a separate class would have meant
    rewriting every line for no gain. Equivalence checked: all 402 `expect` lines in the
    scripts reach the same check as before.
  - Drawing props: GPU instancing, frustum culling, 2D culling. Screenshots match the old
    path (culling: identical; instancing: 1–6 edge pixels differ from rounding).
  - Not done: building the scene once per redraw instead of per view (0.4 ms x 4 at 5000
    props: not worth it yet); distance / LOD for heavy models (the software GPU here can't
    say whether a real one needs it).
- **v0.10.2, decided without asking; confirmed by the user (2026-11-19):**
  - **Snap to Land, what counts as below:** surfaces under the object's middle height.
    So it ignores a shelf over it and a neighbour it overlaps, and comes up out of what
    it is sunk into only up to half its height (sunk deeper, it goes down).
  - **Several selected objects:** one lands on another selected object only if it started
    entirely above it (stacks stay stacks; scattered copies all reach the ground).
  - **Rectangle select:** 3D view only (the 2D views could get it too: ask); selects what is
    *wholly* inside (so the floor isn't swept up); Cmd+drag adds; a drag that starts on a
    selected object's ring or size handle still turns / sizes it.
  - **Show primitives / props:** View menu tick boxes, no shortcut; not saved in the map;
    Cmd+U and adding a new one show them again.
  - **Model name clashes:** the first file in name order keeps the name, the others are
    listed in the Props panel (keys are not changed, so existing maps still find their models).
- **Outside review (another AI, at v0.10.1), checked 2026-11-19:**
  - Fixed in v0.10.2 (each with tests that failed before): NaN / infinity accepted in
    `.map` numbers; props landing on their box corners; "unsaved" after undoing back to the
    saved state; model name clashes silently hiding a file; project version 0.0.1 (now taken
    from the newest release notes).
  - **Done in v0.10.3 (were parked):** split `src/app/editor.cpp` (~4,300 lines) into smaller
    modules step by step; measure drawing of many props (each prop is one draw call per
    mesh; meshes are shared) on the Mac before considering GPU instancing. Ask the user
    when to do these.
- **Random Replicate (v0.10.1), decided without asking; confirmed by the user (2026-11-19):**
  - **Shortcut Cmd+Shift+R** (Edit menu); Cmd+R stays Rescan.
  - **The selection is one piece:** each copy of the whole selection gets one offset, turn
    and size, about the selection's centre (as in Bryce), not each object on its own.
  - **Ranges:** offset and turn are -x .. +x per axis (defaults: 400 units in X and Y, 0 in
    Z; turn 0 / 0 / 180 degrees); size from / to in % (default 80–120), the same on all
    axes unless "each axis its own size" is ticked. Up to 2000 copies.
  - **Seed:** a typed number plus a **New seed** button; the same seed gives the same
    copies on every machine. The settings are remembered while the app runs, not saved
    in the map.
  - **Brushes only move**, rounded to whole grid steps while grid snapping is on.
  - **No landing built in:** the copies stay selected, so Cmd+J (Snap to Land) drops them
    onto the land; the window says so.
  - Multi-Replicate and Random Replicate windows never stay open together.
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

## Roadmap (revised 2026-11-18; steps 4 to 7 confirmed by the user 2026-11-20, step 8 not)

1. **M10 Props (done, v0.10.0)** — static models with their textures, no physics. `.glb`
   first; `.mazz` once Mazzuola's file format is final. Placed, turned and sized like primitives (shared
   object handles), Snap to Land works on them, reload when the file changes. Texture
   filtering follows Scalpellino's Crisp / Smooth switch.
2. **Random Replicate (done, v0.10.1)** (Bryce) — scatter copies with random offset / turn /
   size ranges, for props and primitives (brushes: offset only, as with Multi-Replicate).
3. **Sky Lab** — **done (v0.10.4 to v0.10.6)**. (Bryce; agreed with the user in a separate chat, missed here until they
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
   - **Design agreed with the user (2026-11-19)**, from the Bryce 7 Artist Guide's Sky Lab
     chapter (Sun & Moon, Cloud Cover, Atmosphere, IBL tabs; the Sky & Fog palette's sky
     modes, memory dots and Randomize):
     - **The split:** Sky Lab has every control and all of them are saved in the map.
       What is part of the sky *picture* is baked now: sky modes (Soft, Darker, Custom,
       Atmosphere Off) and sun-dependent colours; sun and moon (position, disk, glow, size,
       horizon illusion, colour, rings); moon phase, earthshine, edge softness, texture;
       stars (random with a seed, or Earth's sky from the Yale Bright Star Catalogue),
       comets; cumulus and stratus clouds (cover, height, frequency, amplitude, colour,
       turbulence, complexity, spherical); the haze band, fog in the sky, rainbow, Blend
       with Sun. What lights the *scene* is stored now and used by the renderer (M12):
       shadow strength and softness, sun diffuse and specular, sky dome colour, ambient
       (the existing setting), cloud shadows, colour perspective, Volumetric World, fog
       and haze on objects, IBL (the baked sky lighting the scene).
     - **Left out:** cloud motion (speed, direction, turbulence over time), Link Clouds to
       View, Fixed Cloud Plane (animation-only or meaningless for one baked picture). Link
       Sun to View is parked until cameras (M11). No Deep Texture Editor: clouds get typed
       noise settings.
     - **Window:** World > Sky Lab..., a window that doesn't block the editor (like
       Multi-Replicate), tabs Sun & Moon / Clouds / Atmosphere, every value typeable, a
       small compass disc to drag the sun, a flat strip of the whole panorama. **The 3D
       view is the live preview** while the window is open (its sky shows even if the View
       toggle is off, and goes back when the window closes): computed at about 256x128
       while a value is dragged, once at about 1024x512 on release, nothing while idle.
       Apply bakes and is one undo step; OK applies and closes; Cancel drops the changes.
     - **Generation** in `src/core` on the CPU (testable headless, the same everywhere).
       Bake 2048x1024 by default, 4096 and 8192 as options, with a progress bar. 8-bit
       PNG; HDR waits for the renderer. Sky colours: a simple scattering model tuned
       toward Bryce's soft blue look (not physically exact).
     - **Saving:** the settings as the worldspawn key `_scal_skylab`; the PNG in Bottega's
       `skies/`. The settings decide the picture, so a missing PNG is rebaked from the map.
       **No Bottega set: Sky Lab asks for one** (user).
     - **Below the horizon:** the haze colour fades to a ground colour the user picks.
     - **Sun lock** (only if the map has a sun light): with it ticked, moving the sun in
       Sky Lab turns the sun light too (one undo step); turning the sun light later marks
       the sky "out of date" with a **Rebake** button (Sky Lab and World menu), never an
       automatic rebake.
     - **The moon moves freely** (not Bryce's always-opposite-the-sun); its phase is a
       slider. **Eclipses** wanted (user); see below.
     - **Fog:** one fog shared by Environment and Sky Lab: today's distance fog plus Bryce's
       base height and thickness (fog lying in valleys); the editor's rough fog stays
       distance-only.
     - **Presets:** Bryce's saved-sky slots and Randomize Sky, presets in Bottega's
       `presets/skies`.
     - **Moon texture:** the user's NASA colour moon map (LROC, 2:1), shipped at 1024x512 as
       the built-in moon, wrapped on a ball so phases fall on the real shape. The user's own
       image: square = a photo of the face, 2:1 = a map; bigger is scaled to 1024 wide.
     - **Releases:** v0.10.4 window, preview, bake, sky modes, sun, haze band, sun lock;
       v0.10.5 clouds; v0.10.6 moon, stars, comets, rings, rainbow, Blend with Sun, fog in
       the sky, eclipses.
   - **Eclipses, decided without asking (told the user, confirm):** solar: the moon is
     always drawn in front of the sun; the covered share darkens the sky and the glow; fully
     covered gives a corona (strength, size) and lets stars show; a smaller moon leaves a
     ring; a "Move moon onto sun" button; the moon's face is dark (earthshine only) during
     a solar eclipse whatever the phase. Lunar: an amount slider (0..1) and a colour (copper
     red), Earth's round shadow moving across the disk.
4. **Cameras and Camera Preview** (M11; done: v0.11.0, v0.11.1) — camera objects (many, one
   active), options and presets (field of view, image size...), and the small preview: an
   approximation of the final picture, shaders included, updated only on a keybind or
   button, shown only while the map has a camera. Full option list under "Direction" →
   "Camera options".
   - **Decided with the user (2026-11-20):**
     - **The preview before the renderer exists:** in M11 the Camera Preview shows the
       camera's view drawn the way the editor's 3D view draws it (textures, sky, fog, the
       rough light preview when it is on), at the image's shape (its width : height). The
       renderer replaces it in M12 with the real approximation (shaders and the rest).
     - **Where:** a **docked panel**, not a floating window or a viewport.
     - **No extras beyond the agreed list:** no "camera from the 3D view", no "look
       through the camera" in the 3D view (both offered, user: neither).
     - **Releases:** two steps. **v0.11.0** camera objects and their options (place, aim
       with rings, roll, lens, near / far, image size, orthographic, aim at a point, name,
       colour, active); **v0.11.1** saved camera presets (Bottega's
       `presets/camera_presets`) and the Camera Preview. Docs updated before starting
       (user).
     - Depth of field stays with the camera effects (item 7); exposure is stored for HDR.
5. **Render, the Source look, with materials** (confirmed; next) — the render window (the
   terminal; options in the Render menu, the size is the camera's, see "Batch 1 decided") that opens on Render, a terminal-style log (+ log file), baking the lights
   (shadows), reflection probes, mirror reflections on flat surfaces, a PNG saved to
   `renders/` (Bottega's once it exists). Only what the camera sees is drawn (frustum,
   back faces, nodraw). **The material system is part of this milestone** (a surface's
   look is its material): GLSL shaders from Bottega's `shaders/`, reflection kind per
   material. The Source look comes from how Source renders, never from filters.
   - **Decided with the user (2026-11-20), after trying the cameras:**
     - **The Camera Preview is a draft render:** the M12 renderer itself at lower
       settings (smaller picture, coarser baked light, fewer bounces...), not a separate
       cheaper imitation, so it can't disagree with the final render. It runs only on
       Update / Cmd+K, so taking a few seconds is fine. This replaces v0.11.1's editor
       look in the preview.
     - **The preview and renders ignore the View menu entirely** (it is for the editor):
       sky, fog, lights / light preview, primitives, props, textured / flat all as the map
       has them, whatever is switched off in the editor. Today's preview follows some of
       these (rough light preview, Show primitives / props, hidden things): fix it **in
       M12**, not before (user: they are only testing the app for now, nothing to be
       misled by yet).
     - **Hidden things are in the preview and the render:** Hide selected / hidden
       visgroups are an editing aid only (as Hammer still compiles hidden visgroups). A
       "leave out of the render" setting could come later if wanted (not asked for).
   - **Split into batches, agreed with the user (2026-11-20).** One release each; each
     batch is discussed in depth before it starts and built only when the user says go,
     then reported:
     1. **v0.12.0 the render pipeline:** render window, terminal log + log file, PNG to
        Bottega `renders/`; the Camera Preview becomes a draft render (same code, Draft
        settings), ignoring the View menu and including hidden things. Lighting is the
        rough light preview's (always on) until batch 2.
     2. **v0.12.1 baked direct light and shadows:** lightmaps on brush faces and terrain
        (point, spot, sun, sky light); primitives and props cast shadows; **primitives
        also receive baked light already** (their own 3D lightmaps; agreed 2026-11-20).
     3. **v0.12.2 bounces:** radiosity ("Bounce #n added RGB(...)"), bounce colour from
        the textures.
     4. **v0.12.3 prop lighting and the light grid (built):** baked per-vertex light for props
        (VRAD's `-StaticPropLighting`), or per prop "like a Source model" (the nearest
        sample of a real 3D grid of ambient cubes + its 2 strongest lights); primitives
        already have their lightmaps from v0.12.1.
     5. **v0.12.4 material system:** (also: **an option for fewer rays for props**, user
        2026-11-21, after the 1000-rocks map took over an hour; where it goes is asked in
        the batch 5 discussion) material files, GLSL shaders in Bottega `shaders/`,
        specular, emissive, detail textures, alpha cutout, rim lighting; editor
        placeholder.
     6. **v0.12.5 reflection probes:** placed by hand, "building cubemaps", reflection
        kind per material, probe quality in the render options.
     7. **v0.12.6 mirror reflections** on flat surfaces.
     HDR stays a later refinement. Batches 5–7 have open design points (material file
     format, probe details), settled in their own discussions.
   - **Batch 1 decided with the user (2026-11-20):**
     - **The editor waits while rendering** (it doesn't have to stay usable); only the
       terminal window updates. The render uses the map as it is when Render is clicked.
     - **Where:** a **Render button at the left end of the status bar** and a new
       **Render menu** (Render..., Update Camera Preview); shortcut **Cmd+Shift+K**.
     - **Picture size = the active camera's size**; the render window has no size field
       (change it on the camera). This replaces "image size in the render window" below.
     - **Render options:** quality presets Draft / Normal / High (later also the light
       bake and probe quality), anti-aliasing (drawn larger, scaled down), texture
       filtering (a render option, smooth with mipmaps by default; not the editor's Crisp
       / Smooth switch).
     - **Saved in the Bottega's `renders/`** as `<map>_<camera>_<date-time>.png` with the
       log next to it (`.log`); no Bottega set: it asks for one (as Sky Lab).
     - **The window** is just the terminal showing the process; **Esc stops** a render,
       Close when done. No other buttons.
     - Draws what the map has: textures (nodraw skipped), primitives, props, hidden
       things, the map's sky, distance fog and Sky Lab's height fog.
     - **Built (v0.12.0)**; choices made while building are under "v0.12.0, decided
       without asking".
   - **Batch 2 decided with the user (2026-11-20), v0.12.1 baked direct light and
     shadows.** Goal: as close to Source's VRAD as possible while also lighting
     primitives. The user's research (Source / VRAD write-up) was checked against Valve's
     Source SDK 2013 code (`utils/vrad/lightmap.cpp`, `vrad.cpp`,
     `leaf_ambient_lighting.cpp`, `mathlib/color_conversion.cpp`, stdshaders); the
     formulas are under "VRAD reference" below. Numbered as asked:
     1. **Units: Source's numbers are converted to cm (x2.54)** (Hammer units are
        inches): the 100-unit brightness reference becomes 254 cm, the default lightmap
        scale 16 becomes about 40 cm. Only constants inside the bake; fields and files
        stay in cm, nothing built changes size (user: yes, as long as it doesn't break).
     2. **Default texture scale** will change to Source's real size (see Open questions;
        not part of this batch unless asked).
     3. **Lightmap scale per face** (Face panel, applies to all picked faces), default
        16 Hammer units (about 40 cm), 1 to 128; saved in the brush's `//@` line. The
        lightmap axes follow the face's texture axes, as Hammer's `lightmapVecs`.
     4. **Falloff of a new light: Hammer's default, inverse-square** (light at its
        brightness at 254 cm), cut off only where VRAD's minimum light makes it
        negligible. Slower bakes are fine (user).
     5. **Falloff modes in Properties:** Inverse-square (Hammer) / 50% - 0% distance
        (with hard falloff) / Custom constant - linear - quadratic. Saved as Hammer's
        keys (`_fifty_percent_distance`, `_zero_percent_distance`, `_hardfalloff`,
        `_constant_attn`, `_linear_attn`, `_quadratic_attn`).
     6. **Lights in existing maps are read as Hammer reads them:** today's reach is
        `_zero_percent_distance`, which does nothing without a 50% distance, so old lights
        become inverse-square and need retuning (only test maps exist). The bake log
        notes each light with a 0% but no 50% distance.
     7. **The rough light preview is not changed** (flagged, see Open questions).
     8. **VRAD's quirks followed:** `_light` colour to linear with ^2.2, spot cones
        capped at 90 degrees (log warning), the spot's extra cosine to its axis,
        `_exponent` between the cones.
     9. **Sun spread as in Hammer:** a spread-angle field on the sun light
        (`SunSpreadAngle`, 0 = hard). Sky Lab's soft shadows don't drive it (flagged).
     10. **Sky Lab's Bryce lighting fields:** sun diffuse = a brightness multiplier on the
         sun; shadow strength and sky dome unused (flagged).
     11. **Sky light read from the sky picture:** each of the 162 sky rays that escapes
         takes the map's sky colour in that direction (the baked Sky Lab panorama or the
         sky image). VRAD's method, the sky's colours. How bright: a "sky light (%)"
         field in Environment, default 100 (built so in v0.12.1; see its "decided without
         asking").
     12. **No flat ambient by default:** a "minimum light" option (today's ambient
         everywhere) exists, off. Rooms without sky are dark until bounces (v0.12.2).
     13. **Lightmaps kept in memory only**, one cache per quality, reused while the map
         doesn't change. A disk cache only if bakes prove slow.
     14. **Quality presets:** Draft (also the Camera Preview) like VRAD `-fast`: lightmap
         pixels 2x coarser, a quarter of the sky rays and sun samples, no extra samples;
         Normal like VRAD's defaults (extra samples where neighbours differ); High like
         `-final`. No separate fields.
     15. **Smoothing:** faces less than 45 degrees apart are lit smoothly (VRAD's
         default; a map setting for the angle). Smoothing groups only if ever needed.
     16. **Bilinear lightmap filtering** in renders (Source's soft, slightly blocky look).
     17. **nodraw faces block light** (in Source nodraw faces aren't in the light
         compile, hence Valve's `toolsblocklight`; a deliberate
         difference, so hidden faces never leak).
     18. **Terrain:** one lightmap over the terrain face, like a Source displacement,
         smoothed with the terrain's normals.
     19. **Primitives receive baked light in v0.12.1:** each primitive (a combination:
         one for all its pieces) has a small 3D lightmap around it; each cell is lit at
         the nearest surface point (found with `comboDistance`), with the same light code
         as the brushes; the shader reads it at the hit point.
     20. **Primitive lightmap density:** a lightmap scale per primitive, brush default
         (flagged: thin parts may leak).
     21. **Props cast shadows from their real triangles** (like VRAD `-StaticPropPolys`),
         with a "casts shadows" tick box (Source's `disableshadows`); they stay lit the
         rough way until v0.12.3.
     22. **Primitives and props don't bounce light** (as Source; flagged).
     23. **Linear light:** baked and stored as linear floats, texture x light, then
         sRGB (Source's HDR path without tone mapping); light may exceed the texture.
     24. **Camera exposure** is used now as a plain multiplier (no auto exposure).
     25. **Esc stops the bake** like the rest of the render; the log shows VRAD-style
         steps.
     26. **One release** (v0.12.1). If primitive lightmaps stall while the brush bake
         works, tell the user and ship brushes / terrain first.
     - **Built (v0.12.1, 2026-11-21)** as one release; choices made while building are under
       "v0.12.1, decided without asking" (confirmed by the user, 2026-11-21).
   - **Batch 3 decided with the user (2026-11-21), v0.12.2 bounces (radiosity).** As VRAD
     (see "VRAD reference" below), numbered as asked:
     1. **VRAD's radiosity:** surfaces cut into patches; each patch's direct light is passed
        between patches that see each other (form factors, Jacobi iterations), then blended
        onto the lightmaps. Not a per-luxel ray gather.
     2. **Patch size: VRAD's default**, 4 luxels (about 160 cm at the default lightmap
        scale); Draft twice as coarse; no map setting for now.
     3. **Patches onto luxels as VRAD:** blended smoothly (VRAD's radial blending), so the
        bounce stays soft and doesn't follow the luxel grid.
     4. **All direct light bounces** (sun at full strength, VRAD's `indirect_sun` 1; sky
        light; point and spot lights). The minimum light doesn't.
     5. **Reflectivity = the texture's average colour, linear, capped at 0.99** (VRAD); a
        per-material override (`$reflectivity`) comes with materials.
     6. **A map-wide bounce strength** in Environment, like VRAD's `-reflectivityScale`
        (default 1; saved in `_scal_bake`).
     7. **Terrain bounces and receives** (VRAD makes patches on displacements).
     8. **Primitives receive bounce light now:** each cell of a primitive's 3D lightmap
        gathers what the patches around it give off (VRAD's ambient-cube gather: rays in
        the 162 directions, each taking the hit patch's light times its reflectivity).
     9. **Primitives and props also reflect light onto other things** (user: "we can
        diverge from Source in this case"). They get patches of their own (primitives from
        their surface, props from their triangles) that take part in the bounce like brush
        faces. Their reflectivity: a primitive's texture average or flat colour, a prop's
        model textures' average (decided while building if unclear; flagged in Open
        questions then). Props still aren't lit by the bake themselves until v0.12.3.
     10. **nodraw faces** block light and bounce nothing.
     11. **Bounces:** Normal and High as VRAD (up to 100, stop when a bounce adds less than
         1/255 in every channel); Draft and the Camera Preview stop at 10.
     12. **No visibility data:** every patch pair gets a ray test, pairs too far apart to
         matter are skipped (as VRAD drops tiny transfers). Measure; if a big map bakes too
         slowly, tell the user before adding anything (e.g. a cutoff distance).
     13. **The log as VRAD:** `Bounce #n added RGB(r, g, b)` for each bounce, then the
         patch and transfer counts.
     14. **Glowing textures** (texlights, `lights.rad`) come with materials (v0.12.4).
     15. **One release** (v0.12.2).
     16. The v0.12.1 choices made without asking stand (sky light %, lightmap scale 40,
         at least 4 cells across a primitive, the Camera Preview baking while the editor
         waits).
     - **Built (v0.12.2, 2026-11-21)** as one release; choices made while building are under
       "v0.12.2, decided without asking". The user also asked for renders of the busy test
       maps: `tests/bench/render.script`.
   - **Batch 4 decided with the user (2026-11-21), v0.12.3 prop lighting.** Numbered as
     asked:
     1. **Props are baked per vertex** (VRAD's `-StaticPropLighting`): direct light with
        shadows (their own too) at every vertex. Not per-texel prop lightmaps (models would
        need a second UV set).
     2. **A per-prop "lighting" choice in Properties:** "baked per vertex" (default) or "like
        a Source model" (ambient cube + the 2 strongest lights, Half-Lambert, no shadows on
        itself).
     3. **Bounce light on props:** each vertex gathers from the patches around it (as the
        primitives' cells do).
     4. **A real light grid, now** (user, after a first reading of "keep it for later" was
        wrong: the user wants the grid built). Reflections don't need it (Source's come from
        env_cubemap probes, v0.12.5; mirrors redraw the lit scene), but a future tool to walk
        around these maps might, and the code will be there.
        - **4a. Layout:** a regular grid as Half-Life 2 (one sample every 4 x 4 x 8 feet =
          48 x 48 x 96 Hammer units, about 122 x 122 x 244 cm) over the map's bounds;
          samples inside solid brushes skipped. "For now": Source's later per-leaf random
          samples need BSP leaves, which Scalpellino doesn't have.
        - **4b. A "like a Source model" prop reads the nearest sample** to its lighting
          origin, no blending (as Source). "For now".
        - **4c. Memory only**, like the lightmaps (a walk-around tool would probably rebake
          on its own; the code exists).
        - **4d. View menu toggle** (off by default): each sample drawn as a small cube
          coloured by its 6 directions in the 3D view, after a render or a Camera Preview
          bake. Static (nothing moves).
     5. **Where a prop's light is measured:** its bounding box centre, moved out of brushes
        if inside (as Source). **Maybe:** Source's `lightingorigin` (another point) later.
     6. **The ambient cube holds what VRAD's does:** light reflected off surfaces plus sky
        light (162 rays), no direct light from lights; the 2 strongest lights are added when
        drawing, the others folded into the cube (as Source).
     7. **Vertex samples** move a little along the vertex normal (as VRAD); fully buried
        vertices go dark (authentic).
     8. **Props that don't cast shadows are still lit** (their shadows on themselves count;
        only their shadows on other things are off).
     9. **Full quality** for props, however heavy (measure in the busy-map renders).
     10. **Shading as the brushes:** texture (linear) x the prop's baked light x exposure,
         back to sRGB; the editor and its rough preview unchanged.
     11. **Primitives don't get the "like a Source model" option** (their 3D lightmaps do
         better).
     12. **Camera Preview** bakes props too, at Draft (a quarter of the rays).
     13. **One release** (v0.12.3).
     - **Built (v0.12.3, 2026-11-21)**; choices made while building are under "v0.12.3,
       decided without asking".
     - **Also agreed:** sun and sky light count a ray that hits nothing as sky (there are
       no sky brushes); the bake runs on the processor with all threads, in the core
       (testable without a graphics card), with ray tests against brushes, terrain,
       primitives (`rayHitPrims` / `comboIntervals`) and props (`rayHitModel`, needs an
       acceleration structure); the editor stays as it is.
   - **VRAD reference (from the SDK 2013 code, for building batches 2 to 4):**
     - Colour: `_light "r g b brightness"`: each channel `(c/255)^2.2 * 255`, times
       brightness / 255.
     - Classic falloff `I / (c + l*d + q*d^2)`; all zero means c = 1; intensity is
       multiplied by `c + 100 l + 10000 q` (full brightness at 100 units).
     - 50% / 0%: a monotonic inverse quadratic through 1 at 0, 2 at d50 and 256 at d0
       (`SolveInverseQuadraticMonotonic`), rescaled so it is exactly 2 at d50 (half
       light). d0 < d50: warning, d0 = 2 d50. `_hardfalloff`: smoothstep to 0 from
       `0.75 d0 + 0.25 d50` to d0. Otherwise, if the quadratic would turn brighter,
       capped at that distance and faded to 0 by 10x it.
     - Spot: `_inner_cone` default 10, `_cone` at least the inner; both capped at 90;
       180 / 180 = point light. No light outside the outer cone; falloff also times the
       cosine to the axis; between the cones `t = (dot - cosOuter) / (cosInner -
       cosOuter)`, then `t^_exponent` (when not 0 or 1).
     - Sun: `N.L` times the share of sample rays that reach the sky; with a spread,
       rays jittered within `sin(SunSpreadAngle)`.
     - Sky ambient: 162 directions (`NUMVERTEXNORMALS`), weighted by cosine and divided
       by the summed cosine, so open sky gives the full `_ambient`; `_ambient` missing =
       half the sun's intensity. Draft: a quarter of the rays.
     - Smoothing: faces without smoothing groups are smoothed when under 45 degrees.
     - Patches (batch 3): a binary split in lightmap-pixel units, `maxchop = minchop = 4`
       (about 64 Hammer units at scale 16); reflectivity clamped to 0.99; bounce until 100
       or until a bounce adds less than 1 in every channel, logged as `Bounce #n added
       RGB(r, g, b)`; patch-to-patch form factor, the exact polygon one when patches are
       close ("5 times rule"); sky patches receive nothing.
     - Ambient cubes (batch 4): 162 rays per sample; a ray hitting a surface takes its
       lighting times its reflectivity, one reaching the sky takes the sky ambient;
       projected onto 6 directions; point / spot / sun light is left out (models get it at
       runtime: the 2 strongest lights, Half-Lambert `(0.5 N.L + 0.5)^2`). Read with
       `C(n) = nx^2 A[x+/-] + ny^2 A[y+/-] + nz^2 A[z+/-]`.
     - Stored lightmaps: ColorRGBExp32, decoded `c * 2^e / 255` (we keep floats).
     - Bump-mapped faces (batch 5, leave room now): 3 more lightmaps along the basis
       `(sqrt(2/3), 0, 1/sqrt3)`, `(-1/sqrt6, 1/sqrt2, 1/sqrt3)`, `(-1/sqrt6, -1/sqrt2,
       1/sqrt3)`; SDK 2013 blends them with squared, saturated dots divided by their sum.
     - Static props: per-vertex light baked by `-StaticPropLighting`; SDK 2013's
       multiplayer branch also has per-texel prop lightmaps (a precedent for the
       primitives' lightmaps).
6. **Water, cloud and endless planes** (confirmed) — materials made with the system above.
7. **Camera effects** (confirmed, lowest priority per the user) — dithering, palette,
   bloom and similar.
8. **Export** (not confirmed by the user) — `.glb`, optional and last; the user probably will never use it elsewhere.
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
    and, among others, the image size. The user decides the image size on the camera.
    (Earlier also in the render window; **replaced 2026-11-20: the render always uses the
    camera's size**, see Roadmap item 5, "Batch 1 decided".)
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
  - **Open:** probe details. (The order of the renderer part of the roadmap was confirmed
    by the user on 2026-11-20.)

## Milestone 1 plan — Brushes (done)

## Log

The step-by-step log is in [`LOG.md`](LOG.md) (recent) and [`LOG-archive.md`](LOG-archive.md)
(older), newest first.
