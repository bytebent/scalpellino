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
  v0.9.2 (units in cm, blending primitives, Snap to Land, Multi-Replicate), **v0.9.3**
  (single pieces of a combination via Option+click; Multi-Replicate as a non-blocking
  window).
- **Next: milestone 10 — Props** (static models from `.glb` and Mazzuola's `.mazz`, with
  textures, no physics). Wait for the user's go-ahead; they had just received the
  v0.9.3 report (which also gave an opinion on the Bottega shared folder) when this was
  written. Then Random Replicate, Sky Lab, M11 renderer, M12 Camera Preview, M13 export
  (see PLAN.md "Roadmap").
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

- Source-style default texture scale 0.25.
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
