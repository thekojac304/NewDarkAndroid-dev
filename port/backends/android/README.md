# Android entry (`backends/android/`)

The Android app (`android/`) loads the portable host as a shared library and calls its `SDL_main`. This directory
holds the Android-only part of that library. The rest is the usual `PORT_SYSTEM` (posix) plus `PORT_BACKEND` (`null` or
`sdl2`), built unchanged.

`port/CMakeLists.txt` builds it when `ANDROID` is set (the NDK toolchain file):

```sh
cmake -S port -B <dir> -G Ninja -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-26 -DPORT_GENERATED=<lifter output> -DPORT_BACKEND=null -DPORT_ANDROID_LIB=main
cmake --build <dir> --target main          # lib<PORT_ANDROID_LIB>.so; the ss2port executable stays for adb tests
```

On Android everything is compiled with `-fvisibility=hidden`. The only exported symbol is `SDL_main`.

## `android_main.c`

- **Arguments.** `SDL_main(argc, argv)` gets `argv[1]` = run directory and `argv[2..]` = `port_main`'s arguments.
  The run directory becomes the working directory.
- **Logging.** `plat_log_write` is wrapped (`-Wl,--wrap=plat_log_write`). Lines go to logcat (tag `ss2port`) and to
  `<run dir>/run.log`. Stray stdout and stderr also go to the file.
- **Exit status.** `<run dir>/status` gets `start <pid>`, then either `exit <code>` or `signal <n> code <c> addr <a>`.
  The launcher reads it from its own process.
- **Ending the process.** `exit` is wrapped (`-Wl,--wrap=exit`). `port_exit` therefore records the status and calls
  `_exit`, so no atexit handler or library destructor runs while ART and the guest threads are alive.
- **One run per process.** The host's state (guest address space, thunks, guest lock) is set up once per process.
  The app runs every program in a fresh `:game` process for this reason.
- **Native crashes.** A crash is recorded in the status file and log, then handed to the previous handler (debuggerd:
  tombstone), so it is never lost and never reported as success.
- **Preflight checks.** These run in the app sandbox before `port_main`:
  - **App process:** uid ≥ 10000 and the `untrusted_app` SELinux domain.
  - **4 GB guest reservation:** reserved through `plat_mem_reserve`, written and read back across the range,
    discarded, then released.
  - **Deep stack:** 48 MB of recursion on a thread with the game thread's 64 MB stack.
  - **Threads and locks:** the `plat_*` mutex, condition variable and timed wait.
- **Game thread.** `port_main` runs on a thread named `ss2-game` with a 64 MB stack. SDL's own `SDLThread` has the
  Java default of about 1 MB.
- **Crash test.** `--android-crash-test` (test only) makes the game thread crash, to check crash reporting.
- **Environment.** `--android-env NAME=value` (any number) sets an environment variable before `port_main`. The app
  uses it for `SS2PORT_WRITE_DIR`, so saves and configuration go to internal storage and the game folder stays read only.
- **SDL2 backend.** When the library is built with `PORT_BACKEND=sdl2`, the game thread runs `plat_sdl2_main`
  (`backends/sdl2/plat_sdl2.c`: SDL set-up, then `port_main`) instead of `port_main`. It sets `SS2PORT_GLES=1`,
  `SS2PORT_FULLSCREEN=1` and landscape orientations unless they are already set, and clears `SS2PORT_RENDER_THREAD`:
  the GL context and SDL's event pumping both stay on the game thread. `plat_sdl2.c` does not call
  `SDL_StartTextInput` on Android (it would open the on-screen keyboard).
