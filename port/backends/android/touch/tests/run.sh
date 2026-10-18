#!/bin/sh
# Host tests of the touch controls' System Shock 2 bridge: touch_ss2.c over the imported Shockolate core.
#   sh port/backends/android/touch/tests/run.sh            (CC defaults to cc; e.g. CC="python -m ziglang cc")
set -e
D=$(cd "$(dirname "$0")/.." && pwd); P=$(cd "$D/../../.." && pwd)
OUT=${TMPDIR:-/tmp}/touch_ss2_test$$
${CC:-cc} -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter -I"$P/include" -I"$D" -o "$OUT" \
    "$D/tests/touch_ss2_test.c" "$D/touch_ss2.c" "$D/touch_controls.c" "$D/touch_editor.c" "$D/touch_radial.c" "$D/touch_settings.c" "$D/touch_binds.c" -lm
"$OUT"; r=$?; rm -f "$OUT" "$OUT.exe"; exit $r
