#!/bin/sh
# Benchmarks: tests/bench/run.sh <path-to-Scalpellino-binary> [props|brushes|render]
# (default: props and brushes; render saves its pictures in $SCAL_BENCH_OUT if set). Linux + Xvfb (software OpenGL): the numbers compare versions, they are
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
  if [ "$b" = render ]; then
    ( cd "$work" && timeout 3600 xvfb-run -a -s "-screen 0 1600x1000x24" "$APP" --script "$HERE/render.script" ) || code=1
    [ -n "${SCAL_BENCH_OUT:-}" ] && mkdir -p "$SCAL_BENCH_OUT" && cp "$work"/render-*.png "$SCAL_BENCH_OUT"/ 2>/dev/null
    for f in "$work"/studio/renders/*.log; do grep -h "luxels\|patches\|props:\|prop light\|cubemaps built\|Building cubemaps\|Drawing mirrors\|Done in" "$f"; done
  else
    ( cd "$work" && timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$APP" --script "$HERE/$b.script" ) | grep "^bench" || code=1
  fi
done
rm -rf "$work"
exit $code
