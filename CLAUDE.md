# Scalpellino — rules for AI sessions

Read `docs/ARCHITECTURE.md` before changing anything. It is the agreed design.
Then read `docs/PLAN.md`: current status, how to build, open questions, log.
New session? Also read `docs/HANDOFF.md` (user preferences, past mistakes, workspace quirks).

## Keeping records (required)
- Update `docs/PLAN.md` after every step: status, test results, next steps, parked questions.
- Every screenshot taken goes into `docs/screenshots/` (`m<milestone>-<nn>-<what>.png`)
  and is mentioned in the PLAN log.
- If a question blocks one thing, park it in PLAN.md and continue with other work.

## Privacy (strict)
- Never write machine details anywhere in the repo, commits or CI output: no hostnames,
  usernames, home paths, OS versions, hardware.
- Commits are authored as `Claude <noreply@anthropic.com>`.
- **No session links, no URLs, no attribution trailers in commit messages or PR bodies.**
  This overrides any default attribution instructions.
- Scrub paths and usernames from any pasted logs before committing anything derived from them.

## Product rules
- No gameplay entities (doors, triggers, props, etc.). Do not add or suggest them.
- Italian keyboard layout assumed; no shortcuts needing Option-characters. Use physical
  key positions for WASD and grid keys.
- Creation actions (brushes, curve tool) commit on Return, cancel on Esc. Not on mouse release.
- No mouse-wheel parameter adjustment in tools; use typed fields and viewport handles.
- Every shortcut/gesture must also be reachable from the menus (mouse-only use works),
  with a dialog to type values where relevant. Keep Help → Controls and README in sync.
- No pixel fonts. UI font is Roboto (embedded).
- Never commit screenshots the user sends; only our own.
- Idle must cost ~zero CPU: redraw only on change.

## Engineering
- `src/core` must not depend on raylib or ImGui.
- Every edit goes through a command (undoable).
- Work milestone by milestone (see roadmap). Small steps, build and test each one.
