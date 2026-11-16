#!/bin/sh
# Runs every tests/ui/*.script against the built app on a virtual screen (Linux + Xvfb).
# Usage: tests/ui/run.sh <path-to-Scalpellino-binary> [screenshot-output-dir]
set -u
APP="$1"
OUT="${2:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
fail=0
for script in "$HERE"/*.script; do
  name="$(basename "$script" .script)"
  work="$(mktemp -d)"
  ( cd "$work" && timeout 120 xvfb-run -a -s "-screen 0 1600x1000x24" "$APP" --script "$script" )
  code=$?
  if [ $code -eq 0 ]; then echo "PASS $name"; else echo "FAIL $name (exit $code)"; fail=1; fi
  if [ -n "$OUT" ]; then mkdir -p "$OUT" && cp "$work"/*.png "$OUT"/ 2>/dev/null; fi
  rm -rf "$work"
done
exit $fail
