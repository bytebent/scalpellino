#!/bin/sh
# Benchmarks: tests/bench/run.sh <path-to-Scalpellino-binary> [props|brushes]
# (default: both). Linux + Xvfb (software OpenGL): the numbers compare versions, they are
# not what a real GPU does. Prints one "bench ..." line per size.
set -u
APP="$1"
WHICH="${2:-props brushes}"
HERE="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
cp -r "$HERE/../ui/assets" "$work/assets"
python3 "$HERE/make_model.py" "$work/rock.glb" 5000 >/dev/null
code=0
for b in $WHICH; do
  ( cd "$work" && timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$APP" --script "$HERE/$b.script" ) | grep "^bench" || code=1
done
rm -rf "$work"
exit $code
