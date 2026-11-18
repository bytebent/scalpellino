# Scalpellino — Log

What was done, step by step, newest first. Recent work lives here; when this file gets
long, move the oldest entries to [`LOG-archive.md`](LOG-archive.md) (newest first there
too). Status, plan and open questions are in [`PLAN.md`](PLAN.md).

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

