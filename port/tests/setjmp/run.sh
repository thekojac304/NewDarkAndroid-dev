#!/bin/sh
# port/tests/setjmp/run.sh: builds the setjmp test (test.c) at -O0 and -O2 and runs it.
#   sh port/tests/setjmp/run.sh                    # host compiler ($CC, default cc; $LIBS, default -pthread)
#   sh port/tests/setjmp/run.sh android [SERIAL]   # arm64 Android: NDK clang ($ANDROID_NDK_ROOT, else the newest under
#                                                  # $ANDROID_HOME/ndk), run on the device with adb
set -e
D=$(dirname "$0"); W=${SETJMP_TEST_DIR:-build/setjmp_test}; mkdir -p $W
if [ "$1" = android ]; then
  export MSYS_NO_PATHCONV=1                        # Git Bash on Windows: keep device paths (/data/...) as they are
  NDK=${ANDROID_NDK_ROOT:-$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)}
  CC="$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/clang* | grep -E '/clang(\.exe)?$' | head -1) --target=aarch64-linux-android${ANDROID_API:-26}"
  ADB="${ADB:-adb}${2:+ -s $2}"; DEV=/data/local/tmp/ss2setjmp; $ADB shell "rm -rf $DEV && mkdir -p $DEV"
fi
for o in -O0 -O2; do
  ${CC:-cc} $o -g -fstack-protector-strong -I runtime $D/test.c -o $W/setjmp$o ${LIBS--pthread}
  echo "$o:"
  if [ -n "$ADB" ]; then $ADB push $W/setjmp$o $DEV/ >/dev/null; $ADB shell "chmod 755 $DEV/setjmp$o && $DEV/setjmp$o"; else $W/setjmp$o; fi
done
