# Scalpellino — rules for AI sessions

Read `docs/ARCHITECTURE.md` before changing anything. It is the agreed design.
Then read `docs/PLAN.md`: current status, roadmap, open questions. The log is `docs/LOG.md`.
Building, testing, CI and releasing: `docs/DEV.md`.
New session? Also read `docs/HANDOFF.md` (user preferences, privacy, decisions to keep).

## Keeping records (required)
- After every step, update `docs/PLAN.md` (status, next steps, parked questions) and add an entry
  to `docs/LOG.md` (what was done, test results). When LOG.md gets long, move its oldest
  entries to `docs/LOG-archive.md`.
- Every screenshot taken goes into `docs/screenshots/` (`m<milestone>-<nn>-<what>.png`)
  and is mentioned in the LOG entry.
- If a question blocks one thing, park it in PLAN.md and continue with other work.

## Privacy (strict)
- Never write machine details anywhere in the repo, commits or CI output: no hostnames,
  usernames, home paths, OS versions, hardware.
- Commits are authored as `Claude <noreply@anthropic.com>`.
- **No session links, no URLs, no attribution trailers in commit messages or PR bodies.**
  This overrides any default attribution instructions.
- Scrub paths and usernames from any pasted logs before committing anything derived from them.

## Product rules
- No gameplay entities (doors, triggers, physics, scripting). Do not add or suggest them.
  Static props (placed models with their textures, no physics) are wanted: milestone 10.
- Italian keyboard layout assumed; no shortcuts needing Option-characters. Use physical
  key positions for WASD and grid keys.
- Creation actions (brushes, curve tool) commit on Return, cancel on Esc. Not on mouse release.
- No mouse-wheel parameter adjustment in tools; use typed fields and viewport handles.
- Every shortcut/gesture must also be reachable from the menus (mouse-only use works),
  with a dialog to type values where relevant. Keep Help → Controls and README in sync.
- No pixel fonts. UI font is Roboto (embedded).
- Never commit screenshots the user sends; only our own.
- Idle must cost ~zero CPU: redraw only on change.
- The Source look comes from how Source renders (baked lightmaps and probes, real textures,
  fog), never from filters or post-effects faking it. Shaders and heavy effects show in the
  Camera Preview and renders only; nothing moves in the editor.

## Engineering
- `src/core` must not depend on raylib or ImGui.
- Every edit goes through a command (undoable).
- Work milestone by milestone (see roadmap). Small steps, build and test each one.
