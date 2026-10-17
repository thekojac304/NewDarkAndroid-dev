# Android app (`android/`)

This is a minimal Android shell around the portable host. It runs the game-free conformance program (Phase 1B) and,
when built with `--game`, the recompiled game from your own NewDark 2.48 install (Phase 1C). An APK built with `--game`
contains code generated from your game files: it is a personal build, never to be distributed.

| Part | What it is |
| --- | --- |
| `LauncherActivity` (main process) | The test screen, later the launcher. It prepares a run directory, starts `NewDarkActivity`, and shows the verdict, preflight checks and log once the run's process has ended. |
| `NewDarkActivity` (`:game` process) | An `SDLActivity`. SDL loads `libSDL2.so` and the library named by the `lib` extra, then calls its `SDL_main` (`port/backends/android/`) with the `args` extra. A log overlay shows progress. |
| `build_native.py` | Builds SDL 2 and the native libraries with the NDK, and stages them with their assets in `build/android/`. With `--game SS2.exe` it also lifts the game (default lift) and builds `libss2null.so` (headless) and `libss2game.so` (SDL2 + OpenGL ES). |
| Gradle | Packages only. Java plus SDL's Java sources, taken straight from the SDL tree. There is no `externalNativeBuild`: the recompiled game is lifted and built on the PC first. |

Every native run is a new `:game` process, because `port_exit` ends the process. The launcher survives exits and
crashes and reports each one.

## Build

Requirements:

- Android SDK with NDK `29.0.14206865`, CMake `3.22.1` and build-tools 34. `ANDROID_HOME` must be set.
- JDK 17.
- The venv from `port/tests/run_conformance.py`: Python with `pefile`, `capstone` and `ziglang`.
- SDL 2.32.10 source in `build/deps/SDL2`. Alternatively set `SDL2_SOURCE`, or pass `--sdl2` / `-Psdl2.dir`.

```sh
git clone -b release-2.32.10 https://github.com/libsdl-org/SDL build/deps/SDL2
python android/build_native.py          # about 1.5 min: SDL2, conformance.exe, two lifts, two libraries
python android/build_native.py --no-rebuild-guest --game "/path/to/SS2.exe"   # + the game: ~8 min the first time (lifts), then seconds
cd android && ./gradlew assembleDebug   # app/build/outputs/apk/debug/app-debug.apk
```

## Run

```sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun all   # or default, cr; neg = negative tests
adb logcat -s NewDarkLauncher ss2port                                                 # RESULT mode=... verdict=PASS ...
adb shell run-as local.newdarkandroid.ss2 cat files/results/history.txt
```

You can also tap the buttons on the screen. Each run's files are in `files/runs/<mode>/`: `run.log`, `status` and the
working files.

### The game (personal builds with `--game`)

```sh
D=/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games
adb push "<SS2 folder>/." $D/ss2/                     # or each top-level entry; Bonus Content is not needed
adb shell chmod -R a+rX,go-w $D                        # adb push creates folders the app cannot enter (2770, owner shell)
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game-null   # headless, 600 frames
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game        # window, 3600 frames
#   --ei frames N changes the frame limit (0: none)
```

The game folder stays read only (the app's uid has no write permission on it). Saves, configuration and `SS2.log` go
to `files/profiles/ss2/` through `SS2PORT_WRITE_DIR`. The game window is landscape and fullscreen, with no overlay; a
physical keyboard works through SDL. There are no touch controls yet.

## Storage layout

| Folder | Holds |
| --- | --- |
| `/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games/<name>/` | Read-only game folders, copied with `adb push` |
| `files/profiles/<name>/` (internal storage) | The write folder (`SS2PORT_WRITE_DIR`): saves, configuration changes, logs |
| `files/` | App settings |

Uninstalling the app removes both.
