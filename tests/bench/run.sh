#!/bin/sh
# Prop benchmark: tests/bench/run.sh <path-to-Scalpellino-binary>
# Linux + Xvfb (software OpenGL): the numbers compare versions, they are not what a real
# GPU does. Prints one "bench props" line per size.
set -u
APP="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
cp -r "$HERE/../ui/assets" "$work/assets"
python3 "$HERE/make_model.py" "$work/rock.glb" 5000 >/dev/null
( cd "$work" && timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$APP" --script "$HERE/props.script" ) | grep "^bench"
code=$?
rm -rf "$work"
exit $code
