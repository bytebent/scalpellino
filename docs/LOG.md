# Scalpellino — Log

What was done, step by step, newest first. Recent work lives here; when this file gets
long, move the oldest entries to [`LOG-archive.md`](LOG-archive.md) (newest first there
too). Status, plan and open questions are in [`PLAN.md`](PLAN.md).

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

