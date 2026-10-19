#!/bin/sh
# Shockolate's six accepted touch-core suites (system-shock-android, branch touch-controls, commit 7502370) built against
# THIS repository's core (port/backends/android/touch): touch_controls, alpha11, alpha2, alpha3, alpha4, ammo, with
# Shockolate's own flags (scripts/test-touch-linux.sh: -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS), plus -UNDEBUG: the suites are
# assert()s, and some compilers (zig cc) define NDEBUG at -O2.
# The Shockolate repository is only read (git show); its working tree is never touched.
#   sh docs/android-audit/phase3f/run_shockolate_suites.sh [SHOCKOLATE_REPO]      (CC defaults to cc)
set -e
SRC=${1:-/c/Dev/system-shock-android}
HERE=$(cd "$(dirname "$0")/../../.." && pwd); T=$HERE/port/backends/android/touch
OUT=${TMPDIR:-/tmp}/shockolate_suites$$; mkdir -p "$OUT"
fail=0
for suite in touch_controls touch_alpha11 touch_alpha2 touch_alpha3 touch_alpha4 touch_ammo; do
    git -C "$SRC" show "7502370:AndroidProject/app/jni/touch/tests/${suite}_test.c" > "$OUT/${suite}_test.c"
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O2 -UNDEBUG -DTOUCH_HEADLESS -DTOUCH_SHOCKOLATE_LAYOUT -I"$T" "$T/touch_controls.c" "$T/touch_settings.c" \
        "$T/touch_editor.c" "$T/touch_radial.c" "$OUT/${suite}_test.c" -lm -o "$OUT/${suite}_test"
    if (cd "$OUT" && ./${suite}_test "$OUT/${suite}-settings.cfg" > "$OUT/${suite}.log" 2>&1); then echo "$suite: PASS"; else echo "$suite: FAIL"; cat "$OUT/${suite}.log"; fail=1; fi
done
rm -rf "$OUT"
exit $fail
