# Android app (`android/`)

This is a minimal Android shell around the portable host. It runs the game-free conformance program (Phase 1B) and,
when built with `--game`, the recompiled game from your own NewDark 2.48 install (Phase 1C). An APK built with `--game`
contains code generated from your game files: it is a personal build, never to be distributed.

| Part | What it is |
| --- | --- |
| `LauncherActivity` (main process) | The test screen, later the launcher. It prepares a run directory, starts `NewDarkActivity`, and shows the verdict, preflight checks and log once the run's process has ended. |
| `NewDarkActivity` (`:game` process) | An `SDLActivity`. SDL loads `libSDL2.so` and the library named by the `lib` extra, then calls its `SDL_main` (`port/backends/android/`) with the `args` extra. A log overlay shows progress. |
| `build_native.py` | Builds SDL 2 and the native libraries with the NDK, and stages them with their assets in `build/android/`. With `--game SS2.exe` it also lifts the game (default lift) and builds `libss2null.so` (headless) and `libss2game.so` (SDL2 + OpenGL ES). |
| `tools/` | Test tools that run outside the app: `uhid_input.c`, a USB keyboard and mouse made from adb ([tools/README.md](tools/README.md)). |
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
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game        # window, no frame limit (play)
#   --ei frames N sets a frame limit for a bounded test run (0: none; game-null defaults to 600, game to none)
#   --es env SS2PORT_PROFILE=1,SS2PORT_INPUT_DEBUG=1   extra settings for the game process (comma or space separated)
#   --es portargs "--trace-only GetKeyState"           extra port arguments, before the executable
```

The game folder stays read only (the app's uid has no write permission on it). Saves, configuration and `SS2.log` go
to `files/profiles/ss2/` through `SS2PORT_WRITE_DIR`. A file placed there (for example a `user.cfg`) is read in
preference to the game folder's copy. The game window is landscape and fullscreen, with no overlay. There are no touch
controls yet.

**Touch controls.** The `game` mode has the Shockolate touch controls ([port/backends/android/touch/README.md](../port/backends/android/touch/README.md)):
- a floating stick in the left 45 % of the screen and swipe-to-look on the right;
- FIRE (drag to aim), USE, JUMP, CROUCH, RELOAD, AMMO, weapon `<` `>` and II (menu, skip);
- SET, the settings and layout editor, saved to `files/touch-settings.cfg`;
- a LOOK / CURSOR toggle that also switches the game between shoot and interface mode, and follows the game when it
  switches by itself.

In cursor mode a tap puts the game's own cursor there and clicks; a drag drags. `--ez touch false` starts a game without
them; touches then reach SDL as before. Touches never become SDL mouse events while the controls are on.

**Input.** A physical keyboard and mouse (USB or Bluetooth) work through SDL, alongside the touch controls. Neither can
release a key or button the other holds. Mouse look uses Android pointer capture. For scripted tests without hardware,
`tools/uhid_input.c` makes Android see a real USB keyboard and mouse, or (`-t`) a multitouch touchscreen
([tools/README.md](tools/README.md)).

**Diagnostics** (off by default, set with `--es env`):

| Setting | Logs |
| --- | --- |
| `SS2PORT_PROFILE=1` | Every 5 s: fps, frame-time mean/median/p95/p99/max, frames over 50 and 100 ms, time in SwapWindow, resident memory, audio callbacks (count, longest gap, peak level, silent buffers) |
| `SS2PORT_INPUT_DEBUG=1` | Every key, text, mouse-button, finger, window and app event SDL delivers |
| `SS2PORT_MOUSE_DEBUG=1` | Mouse capture changes and every motion event |
| `SS2PORT_TOUCH_DEBUG=1` | Touch controls: every finger, and the keys, buttons and cursor placements they send |
| `SS2PORT_INPUT_SCRIPT=<file>` | Not a log: plays scripted input at set times, in `game` or headless `game-null`. Put the file where the app can read it, for example `run-as local.newdarkandroid.ss2 cp /data/local/tmp/x.txt files/x.txt`, then `--es env SS2PORT_INPUT_SCRIPT=/data/user/0/local.newdarkandroid.ss2/files/x.txt` (see `port/README.md` and docs/android-audit/PHASE1E_RESULTS.md) |

**Lifecycle.** Home, then the app icon or Recents, resumes the running game where it was: same process, sound resumes
and mouse capture comes back. The launcher does not open a second copy on top. Back is passed to the game and does not
close it. If the game's activity is destroyed (for example cleared from its task), the game ends the process with
`exit 0`, after flushing its writes. It does not keep running without a window. A run killed with `am force-stop` is
reported as FAIL the next time the launcher starts a run.

## Storage layout

| Folder | Holds |
| --- | --- |
| `/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games/<name>/` | Read-only game folders, copied with `adb push` |
| `files/profiles/<name>/` (internal storage) | The write folder (`SS2PORT_WRITE_DIR`): saves, configuration changes, logs |
| `files/` | App settings |

Uninstalling the app removes both.
