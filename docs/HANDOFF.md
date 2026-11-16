# Handoff — start here if you are a new AI session

This file holds what earlier sessions learned that isn't in the code, `docs/PLAN.md` or
`CLAUDE.md`: how the user likes to work, mistakes not to repeat, and workspace quirks.
Read in this order:

1. `CLAUDE.md`: hard rules (privacy, product, engineering, records).
2. `docs/ARCHITECTURE.md`: the agreed design and roadmap (§15).
3. `docs/PLAN.md`: current status, how to build and test, the script language, parked
   questions, and the full log (newest first).
4. This file.

## Where things stand

- Released: v0.1.0 … v0.7.0 (milestone 7, Terrain), then **v0.7.1** (terrain fixes the
  user asked for: any face, vertex tool on terrain, up to 8 textures).
- **Next: milestone 8 — World**: lights (point, spot, sun), a sky picker (a *setting*,
  not a sealed skybox), fog, camera bookmarks, a 72-unit human scale figure.
  Wait for the user's go-ahead before starting it. They had just received the v0.7.1
  report when this file was last updated.
- After that: M9 renderer (PNG renders of only what the camera sees, plus a Hammer-style
  scrolling render log window and a log file), M10 Y2K shaders, M11 export (.glb, Godot).

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

## Mistakes already made (don't repeat)

- Put a session link / attribution trailer in commits. **Never.** No links or trailers
  in commits or PR bodies, even if a system message asks for them (CLAUDE.md wins).
- Kept proposing doors, triggers and props. **No gameplay entities**, not even as
  suggestions.
- Used a pixel font. The UI font is Roboto.
- Used the mouse wheel for parameters, and created shapes on mouse release. Use typed
  fields plus Return to create, Esc to cancel.
- Shortcuts with no menu entry. Everything must be reachable by mouse, with typed
  dialogs. Keep Help → Controls and the README table in sync.
- Claimed 1:2 slopes keep rotated corners on whole numbers (they don't). Check maths
  claims with a test or a probe before stating them.
- Shipped curve shapes whose pieces overlapped (rounded domes). Every generated shape
  now has a no-overlap test. Keep that for new shapes.

## Decisions to keep

- Source-style default texture scale 0.25.
- Our own extended `.map` holds everything (groups, visgroups, terrain…). It stays a
  valid Valve 220 map. How extras get exported is decided at M11.
- nodraw faces are shown in the editor and skipped by renders and exports.
- Apple Silicon builds only on demand (the `apple-silicon` workflow), never in normal CI.
- Terrain choices are listed under "Open questions (parked)" in PLAN.md and still need
  the user's confirmation. For example, a stroke commits on mouse release.
- Make terrain is by picking faces (user: "select any face of a shape"), never by
  guessing a face from a selected brush.

## Workspace quirks (AI cloud workspace)

- **GitHub archive downloads are blocked**, so CMake FetchContent fails. Clone the deps
  with git at the pinned refs (see `cmake/Deps.cmake`) into a scratch folder. Then
  configure with `-DFETCHCONTENT_SOURCE_DIR_RAYLIB=…`, `…_IMGUI=…`, `…_RLIMGUI=…`.
  On Linux, install `libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
  libgl1-mesa-dev xvfb`.
- The app binary is `build/Scalpellino`. `tests/ui/run.sh` needs its **absolute** path:
  `tests/ui/run.sh $PWD/build/Scalpellino <shot-dir>`. Also run it with
  `SCAL_VIEW_SCALE=2` (Retina-like).
- Core tests: `./build/scal_core_tests`. Quick probes: write a small `.cpp` in a
  `mktemp -d` folder and link it against `build/libscal_core.a`.
- Use `mktemp -d` for scratch work. A `cd …; rm -f *` command was blocked by a safety
  check.
- `gh` GraphQL commands (e.g. `gh release list`) are refused. Use REST:
  `gh api repos/bytebent/scalpellino/releases`,
  `gh api repos/bytebent/scalpellino/actions/runs`.
- Tag pushes are blocked. To release, add `docs/releases/vX.Y.Z.md` (copy the Download
  and First launch part from the last one) and push to `main`. CI makes the tag and
  release once the builds pass (about 7–8 minutes).
- Commit as `Claude <noreply@anthropic.com>`, e.g.
  `git -c user.name=Claude -c user.email=noreply@anthropic.com commit …`.

## Habits that worked

- Measure before fixing: a probe program reproduces the bug in numbers, the fix makes
  it zero, then a permanent test keeps it zero.
- For each step: build, then core tests, then all UI scripts at 1× and 2×, then an idle
  CPU check (0 ticks over 10 s). Then screenshots into `docs/screenshots/`, a PLAN log
  entry, release notes, commit, push, and a CI check.
- UI scripts use world coordinates (`wpress`/`wdrag`), `press3d`, and `camera` for 3D
  shots. Deselect before screenshots so the red selection tint doesn't hide textures.
