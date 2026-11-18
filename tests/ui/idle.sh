#!/bin/sh
# Idle CPU check: the app must cost ~zero CPU when nothing happens, with a map open.
# Usage: tests/ui/idle.sh <absolute path to Scalpellino> [seconds] [--control]
#
# Starts the app on a virtual screen with tests/ui/idle/rich_map.script, which builds a
# map with terrain, brushes, primitives, lights, sky, fog and the View previews on, saves
# and re-opens it, and does NOT quit, so the app goes idle. Once the script's screenshot
# appears, the harness waits a few seconds, then reads the process's CPU time (user +
# system, all threads) twice, <seconds> apart (default 10). Idle must add 0 ticks
# (1 tick = 10 ms); IDLE_MAX_TICKS raises the allowance.
# --control runs a script that keeps the app busy and expects CPU use, to prove the
# measurement can see it. Linux only (/proc, Xvfb).
set -u
APP="$1"
SECS="${2:-10}"
MODE="idle"
[ "${3:-}" = "--control" ] && MODE="control"
[ "${2:-}" = "--control" ] && { MODE="control"; SECS=5; }
MAX="${IDLE_MAX_TICKS:-0}"
HERE="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
cp -r "$HERE/assets" "$work/assets"
cleanup() { kill "$APID" "$XPID" 2>/dev/null; wait 2>/dev/null; rm -rf "$work"; }
APID=0; XPID=0
trap cleanup EXIT

if [ "$MODE" = "control" ]; then
  script="$work/busy.script"
  { echo "shot idle-ready.png"; i=0; while [ $i -lt 20000 ]; do echo "view 1 0 0"; i=$((i+1)); done; } > "$script"
else
  script="$HERE/idle/rich_map.script"
fi

# Own virtual screen, so the app's own process id is known (xvfb-run would hide it).
Xvfb -displayfd 3 -screen 0 1600x1000x24 -nolisten tcp 3>"$work/display" >/dev/null 2>&1 &
XPID=$!
n=0; while [ ! -s "$work/display" ] && [ $n -lt 100 ]; do sleep 0.1; n=$((n+1)); done
[ -s "$work/display" ] || { echo "FAIL idle: no virtual screen"; exit 1; }
export DISPLAY=":$(head -n1 "$work/display")"

( cd "$work" && exec "$APP" --script "$script" 2>"$work/app.log" ) &
APID=$!

# Ready = the script's screenshot exists (the app is then on its last frames).
n=0; while [ ! -e "$work/idle-ready.png" ] && [ $n -lt 1200 ]; do
  kill -0 "$APID" 2>/dev/null || { echo "FAIL $MODE: the app exited before it was ready"; exit 1; }
  sleep 0.1; n=$((n+1))
done
[ -e "$work/idle-ready.png" ] || { echo "FAIL $MODE: not ready after 120 s"; exit 1; }
sleep 3
# Errors the app printed while building the scene (failed expects, bad commands, ...).
if grep -qE 'FAIL|script:' "$work/app.log"; then echo "FAIL $MODE: the scene script reported errors:"; head -n 10 "$work/app.log"; exit 1; fi

ticks() { sed 's/.*) //' "/proc/$APID/stat" | awk '{print $12 + $13}'; }
t0="$(ticks)"; sleep "$SECS"; t1="$(ticks)"
kill -0 "$APID" 2>/dev/null || { echo "FAIL $MODE: the app died during the measurement"; exit 1; }
d=$((t1 - t0))
if [ "$MODE" = "control" ]; then
  if [ "$d" -ge 10 ]; then echo "PASS idle-control (busy app used $d ticks in $SECS s)"; exit 0; fi
  echo "FAIL idle-control: a busy app used only $d ticks in $SECS s; the measurement can't be trusted"; exit 1
fi
if [ "$d" -le "$MAX" ]; then echo "PASS idle ($d ticks over $SECS s, allowed $MAX)"; exit 0; fi
echo "FAIL idle: $d ticks over $SECS s (allowed $MAX)"; exit 1
