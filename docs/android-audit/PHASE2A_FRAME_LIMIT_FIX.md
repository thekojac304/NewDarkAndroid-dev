# Phase 2A fix: normal gameplay ended after about 40 seconds

2026-10-17. Hands-on play of the Phase 2A APK (`0.4.0-phase2a`) on the Galaxy S26 Ultra ended after about 40 s.
Everything else, including the touch controls, worked.

## Cause: the launcher's default frame limit (not a crash or Android)

The launcher gave the windowed game a default limit of **3600 frames** (`LauncherActivity.defaultFrames`: 600 for
`game-null`, 3600 for anything else). Its buttons reset the `frames` setting to -1, which means "the mode's default", so
**Game: SDL2 window** always started the game with `--frames 3600`. At the game's 90 to 100 fps that is 36 to 41 s. The
Phase 2A device tests all used `--es autorun game --ei frames 0` from adb, which removed the limit, so they did not hit it.
The Phase 2A checklist told a person to use the button.

The native side has no default limit (`port/core/host.c`: `max_frames` is 0 unless `--frames` is given) and no
watchdog on Android (the hang watchdog is Vita-only). `NewDarkActivity` does not finish itself.

Evidence from the run that ended at 21:48:33 (pid 15951, the button):

| Source | Line |
| --- | --- |
| `files/runs/game/run.log` line 1 | `port arguments: --verbose --android-env SS2PORT_WRITE_DIR=… --frames 3600 …/SS2.exe` |
| `run.log` line 365 | `[port] stopping after 3600 frames` |
| `run.log` line 384, logcat `ss2port` | `[android] process exit(0) after 38.415 s` |
| `files/runs/game/status` | `start 15951`, then `exit 0` (no `signal`) |
| logcat `Zygote` | `Process 15951 exited cleanly (0)` |
| logcat `ActivityManager` | `Process local.newdarkandroid.ss2:game (pid 15951) has died: fg TOP`: in the foreground, not killed by Android |
| `files/results/history.txt` | `frames=3600 … status="exit 0" … seconds=40.600`, `40.862`, `38.415` for the three button runs of the day |

A crash would show `signal N` in `status` and a tombstone. A kill by Android or the low-memory killer would show
`status` stuck at `start` and an `ActivityManager` kill reason. Neither appears. This was a clean exit at the frame
limit, reported as PASS by the launcher because a game run with a limit is meant to end that way.

## Fix

`android/app/src/main/java/local/newdarkandroid/ss2/LauncherActivity.java`:

- `defaultFrames`: the windowed `game` mode now has **no limit** (0). It runs until the player quits or the process is
  stopped. `game-null` keeps 600, since nobody can quit a headless run.
- An explicit `--ei frames N` still sets a limit, so bounded test runs work as before (`--ei frames 3600` gives the old
  behaviour).
- The button reads **Game: SDL2 window (play, no frame limit)**.
- The launcher logs the limit it uses: `starting game: … , frame limit none` or `frame limit 600`.

The limit was removed from normal play, not raised. Also changed: `android/README.md` (frame-limit notes), version
`0.4.1-phase2a` (versionCode 5). Native code, Shockolate, SCP and the game installation were not changed.

| Start | Frame limit |
| --- | --- |
| Launcher icon, then **Game: SDL2 window** | none |
| `--es autorun game` | none |
| `--es autorun game --ei frames N` | N |
| **Game: headless** or `--es autorun game-null` | 600 (or N with `--ei frames N`) |

## Verification on the S26 Ultra

APK `build/android/baseline/phase2a-fix/app-debug-phase2a-fix.apk`, sha256
`38fa5fcb010662ebc2126e96c0beac347e7f8950efbfabc78c8d86b397ab8a51`, installed with `adb install -r` (saves kept).
The native libraries are byte-identical to Phase 2A's; only `classes2.dex` and the manifest differ.

**Normal play, from the launcher icon and the button.** `am force-stop`, then the launcher icon (`monkey -c
android.intent.category.LAUNCHER`, `autorun=null`), then a tap on the button:

- `starting game: libss2game.so in …/runs/game, frame limit none`; `run.log` has no `--frames`.
- With the HID test touchscreen (`uhid_input -t`): **II** skipped the intro, **Load Game** → **medsci** → **Load**.
  In MedSci at about 61 s. Stick forward and a look swipe moved and turned the player ("An unbreakable window.").
- **II** → **Save Game** → first empty slot → **Save**: `save_2` written (`CAMPAIGN`, `medsci1.MIS`, 1.5 MB). `save_0`
  and `save_1` unchanged. **Continue** returned to play; the stick moved the player again.
- Checks at 94, 114 and 135 s: same pid, CPU time rising (about 69 ticks/s), no `stopping after` in `run.log`.
  Still playing at 220 s.
- Ended on purpose at about 229 s with `am force-stop`: `ActivityManager: Killing 17367 … stop local.newdarkandroid.ss2`,
  `Zygote: Process 17367 exited due to signal 9`. The launcher reports such a run as FAIL ("ended without an exit
  status"), as before for unlimited runs that do not quit through the game.

**Bounded test runs (adb).**

| Command | Result |
| --- | --- |
| `--es autorun game --ei frames 600` | `frame limit 600`, `--frames 600` in the arguments, `stopping after 600 frames`, PASS, exit 0 after 19.9 s |
| `--es autorun game-null` | `frame limit 600`, `stopping after 600 frames`, PASS, exit 0 after 19.8 s |
| `--es autorun game` | `frame limit none`, no `--frames` (stopped by hand) |
| `--es autorun all` | default 0/511, `--cache-regs --abi-check` 0/511, 0 ABI violations, both PASS |
| Touch bridge suite (`touch_ss2_test.c`, built with the NDK clang and run on the phone; no host C compiler on this PC) | 0 of 76 checks failed |

## Preserved

- Phase 2A APK unchanged: `build/android/baseline/phase2a/app-debug-phase2a.apk`, `1f6f1092…60e6d1e2` (matches its
  `SHA256SUMS`). It is larger on disk (117.9 MB vs 80.1 MB) only because of Gradle's incremental packaging; the entries
  are the same.
- The save `save_2` (medsci) written by this test stays in `files/profiles/ss2/`.
