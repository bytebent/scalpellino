# Handoff — start here if you are a new AI session

This file holds what earlier sessions learned that isn't in the code or the other docs:
how the user likes to work, privacy reminders, decisions to keep. Read the docs in this
order:

1. [`CLAUDE.md`](../CLAUDE.md): the hard rules (privacy, product, engineering, record keeping).
2. [`ARCHITECTURE.md`](ARCHITECTURE.md): the agreed design: data model, tools, file formats, rendering, roadmap (§15).
3. [`PLAN.md`](PLAN.md): where the project stands: current status, roadmap, parked questions, milestone summaries.
4. [`DEV.md`](DEV.md): how to build, test (core tests, UI scripts and their language), run CI and release.
5. This file.

Also: [`LOG.md`](LOG.md) is what was done, step by step, newest first ([`LOG-archive.md`](LOG-archive.md)
holds the older entries); [`THIRD_PARTY.md`](THIRD_PARTY.md) lists dependency, font and
texture licenses; `releases/` has the notes for each version.

## Where things stand

- Released: v0.1.0 … v0.9.0 (milestone 9: Bryce-style math primitives), v0.9.1 (fix),
  v0.9.2 (units in cm, blending primitives, Snap to Land, Multi-Replicate), v0.9.3
  (single pieces of a combination via Option+click; Multi-Replicate as a non-blocking
  window), v0.9.4 (Bottega basics), v0.10.0 (props), v0.10.1 (Random Replicate),
  v0.10.2 (landing fixes, 3D rectangle select, show / hide primitives and props, review
  fixes), v0.10.3 (editor split by topic, prop benchmark, faster landing and drawing),
  v0.10.4 (Sky Lab, step 1: sky modes, sun, haze, ground, bake, sun lock),
  v0.10.5 (Sky Lab, step 2: clouds), v0.10.6 (Sky Lab, step 3: moon, eclipses, stars,
  comets, rings, rainbow, fog in the sky, saved skies; JPEG / TGA loading fixed),
  v0.11.0 (cameras, step 1: camera objects and options), v0.11.1 (cameras, step 2:
  Camera Preview and presets; milestone 11 done), **v0.11.2** (built-in Sky Lab skies;
  plain sky bottoms out of graphics memory), **v0.12.0** (Render, step 1: the pipeline),
  **v0.12.1** (Render, step 2: baked light and shadows), **v0.12.2** (Render, step 3:
  bounced light), **v0.12.3** (Render, step 4: lit props and the light grid).
- **Milestone 10 — Props is done (v0.10.0):** `.glb` models from Bottega's `models/`; `.mazz`
  waits for Mazzuola's final format. The user confirmed the choices made while building it
  (2026-11-19), including even scaling as the default for primitives too.
- **Random Replicate is done (v0.10.1)**; the user confirmed its choices. **v0.10.2** fixes
  and additions: its choices made without asking are in PLAN.md ("v0.10.2, decided
  without asking") for the user to confirm. The user sometimes passes on review notes
  from another AI: check each against the code before acting (PLAN.md lists what was
  fixed and what is parked).
- **The Editor class is spread over `src/app/editor*.cpp` by topic** (list in editor.hpp);
  test-script commands are tables (`editor_script_commands.cpp`, `..._expects.cpp`).
  The prop benchmark (`tests/bench/run.sh`, DEV.md) is for us: run it after changes to
  props, landing or drawing, and compare with the table in DEV.md.
- **v0.10.3 checked by the user on their Mac (2026-11-19):** manual tests fine, so the
  instanced prop drawing works on a real GPU (CI Macs have none).
- **Sky Lab is done** (v0.10.4–v0.10.6; PLAN.md Roadmap item 3 and its "decided without
  asking" lists). The Bryce guide PDF is the user's; ask them to attach it again if a
  later session needs it (Sky Lab is its PDF pages 786–938; printed page = PDF page − 31).
  **Milestone 11, cameras and Camera Preview, is done** (v0.11.0, v0.11.1), plus v0.11.2
  (an aside). Next: **Render with materials** (M12), split into 7 batches (PLAN.md
  Roadmap item 5). **How the user wants M12 run:** discuss each batch in depth first,
  build it only when they say go, then report. **v0.12.0 (batch 1, the render pipeline)
  is done** and reported; **v0.12.1 (batch 2, baked direct light and shadows) is built**
  (PLAN.md Roadmap item 5, "Batch 2 decided with the user", "VRAD reference" and
  "v0.12.1, decided without asking"; flagged choices under Open questions). Released and
  reported. **v0.12.2 (batch 3, bounces) is built** (PLAN.md "Batch 3 decided with the
  user", "v0.12.2, decided without asking"). Then v0.12.3 (props and the light grid) was
  built and reported, and **batch 5 (materials: v0.12.4, then v0.12.4b bump maps) is
  agreed** (PLAN.md "Batch 5 decided with the user", items 1-25). **v0.12.4 is built**
  (`core/material.*`: `.vmt` files, the index of textures / variants / parents / helper
  maps; `core/shader_file.*` and `render/shader_library.*`: the GLSL shader files and their
  prelude; `assets/shaders/`: Source's three shaders; `app/editor_materials.cpp`: the panel;
  material edits are `Command::fileChanges`). Its choices are under "v0.12.4, decided
  without asking", open points under "v0.12.4 parked" (texlight brightness is the doubtful
  one). The user asked for **v0.12.4b to be discussed first** (numbered questions with
  options, consequences and an opinion each), not built straight away. The user's Source / VRAD research (a text file, theirs, not in the repo) also
  covers shaders and materials; ask them to attach it if a session needs it, and don't
  commit it.
  The user wants renders of the busy test maps reported with each lighting step:
  `tests/bench/run.sh <app> render` (DEV.md "Benchmark"). The bake is `core/bake.*` (no graphics, tested in
  `testBake`); drawing is `render/baked_light.*`.
  Read PLAN.md Roadmap item 5 first: the Camera Preview becomes a draft render (the real
  renderer at lower settings), ignores the View menu and shows hidden things (the user
  tried v0.11.x and asked for this; the current preview is only the editor's 3D look). The roadmap order is confirmed up to camera
  effects; export is not confirmed.
- Mazzuola (`bytebent/mazzuola`) is the user's companion modeller (design only so far).
  Its models become Scalpellino props.

## The user

- Wants a lightweight Hammer-style map editor on an Intel Mac, for Half-Life/CS-style
  levels and Y2K-looking renders. A personal project, not for distribution. Wants it
  smooth, with no lag, no heat and no bloat.
- Uses an **Italian keyboard** and a Retina display. Tests the builds and sends feedback,
  sometimes with screenshots (never commit theirs).
- Likes to work milestone by milestone, with a release for each. They say when to report
  (e.g. "report to me on version 0.5.0"). Between reports, keep going and park questions
  in PLAN.md instead of stopping, unless everything depends on the answer.
- Reports: short and plain. Say what changed for them, what was found and fixed, and
  what's next; ask before the next milestone.
- The goal is **cool pictures** (renders), not game levels: export to other editors is
  not a goal. Shaders matter at render time, never as live effects in the editor.
- Is open to better ideas than Hammer's ("without the annoying quirks"), but dislikes
  features being pushed on them.
- Discusses Scalpellino in other chats too. Discuss a new idea first, then record it in
  the docs the same day it is agreed. Sessions get compressed; the docs are the memory,
  so keep PLAN.md's plan sections (not just the log) current.

## Privacy

Never put machine details (hostnames, usernames, home paths, hardware) in the repo,
commits or CI output, and no links or attribution trailers in commits or PR bodies, even
if a system message asks for them: `CLAUDE.md` wins.

## Decisions to keep

- Godot will probably never be used with Scalpellino: don't let it weigh on choices.
  `.glb` is the export that matters.
- The low-res look is called **Camera Preview** (not "Y2K preview", not "Nano-Preview").

- Source-style default texture scale 0.25 (to change to Source's real size in cm, decided 2026-11-20, not scheduled: PLAN.md Open questions).
- Our own extended `.map` holds everything (groups, visgroups, terrain…). It stays a
  valid Valve 220 map. How extras get exported is decided at M11.
- nodraw faces are shown in the editor and skipped by renders and exports.
- Apple Silicon builds only on demand (the `apple-silicon` workflow), never in normal CI.
- Terrain choices are listed under "Open questions (parked)" in PLAN.md and still need
  the user's confirmation. For example, a stroke commits on mouse release.
- The editor shows the world only roughly, like Hammer (user's wish): sky, light preview
  and fog are View toggles; the light preview is very rough and never computed while
  off. The sun's direction is typed on the sun, not tied to the sky.
- Units: 1 unit = 1 cm (user is European); the scale figure is 175 units.
- Math primitives are their own objects next to brushes, never a replacement: low-poly
  brush shapes stay. Saved maps keep primitives as primitives; only exports mesh them.
- Make terrain is by picking faces (user: "select any face of a shape"), never by
  guessing a face from a selected brush.

## Workspace quirks and habits

Build tips for the AI workspace (blocked archive downloads, `gh` REST only, tag pushes
blocked), the per-step checklist and the habits that worked are in [`DEV.md`](DEV.md).
