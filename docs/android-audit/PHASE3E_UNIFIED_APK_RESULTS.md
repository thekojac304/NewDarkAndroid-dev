# Phase 3E: one APK for SCP Beta 4 and SCP Beta 8, and the crouch fix

2026-10-18. Branch `android-port`, from `43dd5f8` (clean). Galaxy S26 Ultra (SM-S948U1, Android 17 / API 37). Commits:
`c2652bc` (A, crouch), `2cc24fd` (B, module selection), `d4ff6cf` (C, app and migration), and this report (D).
Details: [PHASE3E_CROUCH_FIX.md](PHASE3E_CROUCH_FIX.md), [PHASE3E_MODULE_SELECTION.md](PHASE3E_MODULE_SELECTION.md).
Evidence: `phase3e/`.

## Summary

| Success criterion | Result |
| --- | --- |
| Crouching works with the touchscreen | **Verified on the phone** (HID touchscreen, both installations): tap → crouched, tap → standing. Root cause: the legacy `user.bnd` binds Shift to *creep*; CROUCH now presses the installation's own crouch key |
| One APK supports both SCP Beta 4 and Beta 8 | **Verified.** `local.newdarkandroid.ss2` 0.9.1-unified holds both NVScript builds |
| Each installation loads the matching NVScript | **Verified.** By the file's identity (size, ImageBase, entry point, code CRC), never by label. Bad, foreign, not-PE and missing files are refused with a diagnostic, never substituted |
| Both mod configurations intact | **Verified.** Legacy: 17 mods + `patch_ext`, `osm+ubermod`; game folder 6,448 of 6,448 files identical to the validated copy. Beta 8 folder untouched |
| Saves separate, load correctly | **Verified.** `profiles/scp4` and `profiles/ss2scp`; legacy slots 0, 3, 8, 13, 14 load at the Phase 3C positions; save → reload works in both |
| Audio, touch, UI scaling functional | **Verified** (logs and Android audio state; touch through the real input path); audibility was already confirmed by the player |
| Legacy test app no longer needed for ordinary play | **Yes**, pending your acceptance (§11). It is still installed, unchanged, as the rollback |

## 1. Crouch: root cause and fix

The touch CROUCH button sent **Shift**, the stock 2.48 binding (`bind shift crouch`, Beta 8). Your legacy `user.bnd`
has `bind shift +creepon`, `bind c crouch` and `bind ctrl +crouchhold`, so in the legacy game CROUCH only made you
creep. Desktop check, standing still (`phase3e/A1-…jpg`): C toggles crouch, Ctrl crouches while held, Shift changes
nothing. Timing, hit-testing, input ownership and LOOK/CURSOR mode were not involved.

Fix (`touch_binds.c`, `touch_ss2_android.c`): the bridge reads `user.bnd` through the profile overlay (the copy the
game itself reads and writes) and presses the key bound to `crouch` (else `+crouchhold`), keeping Shift when Shift is
one of them. Re-read each time touch enters LOOK mode. No game or bind file is changed.

| On the phone (0.9.0-modsel, HID touchscreen, disposable write folder) | SCP Beta 4 | SCP Beta 8 |
| --- | --- | --- |
| Key chosen (log) | `'c' (crouch, toggle), DIK 0x2e`, from `profiles/scp4/user.bnd` | `'shift' (crouch, toggle), DIK 0x2a`, from the game folder's `user.bnd` |
| Tap 1 / tap 2 | crouched / standing (`phase3e/A2-…jpg`) | crouched / standing (`phase3e/A3-…jpg`) |
| With stick held and a look swipe (3 contacts) | `W` stays down while C goes down and up; look finger kept | — |

Host tests: **0 of 380** failed (345 earlier + 35 new: parser and crouch key). Log: `phase3e/A-device-crouch-touch-log.txt`.

## 2. Module-selection architecture

`LoadLibrary(NVScript.osm)` → every built-in version of that name whose SizeOfImage, ImageBase and entry point equal the
file's is mapped at its own base, relocated, and CRC-checked; the first exact match loads. No match → nothing loads,
every identity is logged, error 126 to the engine. Each version has its own generated prefix and reserved guest range,
so both live in one `libss2game.so`. Labels never take part. Full description: PHASE3E_MODULE_SELECTION.md.

## 3. Supported NVScript identities (`port/nvscript_versions.json`)

| | SCP Beta 8 | NVScript 1.2.4 (SCP Beta 4) |
| --- | --- | --- |
| sha256 | `f15fc91b276e73ad52d5026a86a7d51b83e5644a20c3e58a53a10a4b52d4f11a` | `abed4ee229dda567553b7d805f219008b6390c2d2da37459629eb28ac9ee6a9a` |
| Bytes, SizeOfImage, ImageBase, entry | 704,512, `0xb2000`, `0x10000000`, `+0x5d989` | 662,016, `0xa8000`, `0x10000000`, `+0x5f1e9` |
| Prefix, lifted at, code CRC there | `nv`, `0x30400000`, `7426e52a` | `nv124`, `0x30500000`, `273bab35` |
| Found in | `games/ss2scp/DMM/SCP_beta8/` | `games/scp4/osm/` (loaded first) and `games/scp4/DMM/SCP_beta4/` (identical) |

## 4. Build and packaging changes

- `--nvscript` repeatable in `port/build.py` and `android/build_native.py`; files identified by sha256 in the manifest;
  stale NVScript lifts dropped; CMake compiles every `nv*/` folder. Command: PHASE3E_MODULE_SELECTION.md §Build.
- Same engine lift for everything (`SS2.exe` 2.48 `c7ff08c5…`, `allobjs` `096c3a1e…`, `Squirrel` 1.02 `d95b4415…`,
  `lgvid` `64d55311…`). No proprietary file, generated C or library is in git.
- `GAME_MODULES` lists both NVScript headers; the import check accepts either.
- Launcher: game-folder labels (§8.4). Version **0.9.1-unified** (code 14). Play flavour: no frame limit, no
  diagnostics; dev flavour keeps the diagnostics and conformance programs.
- The side-by-side build option (`-Pside`) is unchanged and no longer needed for SCP Beta 4.

## 5. APKs (`build/android/baseline/phase3e-unified/`, `SHA256SUMS`)

| Build | Bytes | sha256 |
| --- | --- | --- |
| **Play 0.9.1-unified (installed)** | 47,486,394 | `19aacb278b6a5fef29186428941a10872aa721a9a12c4d05f73b2bc6c5150bb5` |
| Dev 0.9.1-unified | 96,273,628 | `f8b60da3e42890d7382a5cf8f363a994b63ca0bed5add4c39eedc0887377ef90` |
| `libss2game.so` in the play APK | 45,956,704 | `f47a714a12ab0ae99a102e0f7f25b4e3ca9b1b0c97587aa01d73883d54bebf66` |
| `libSDL2.so` in the play APK | 1,420,256 | `741436830e92d588fa6e94728bd04287e1059876b192477b4b8e741614abe2e6` |

The second NVScript adds 3.8 MB to the game library (42,145,824 → 45,956,704 bytes). Kept for rollback:
regular 0.8.1 play `2a5ad353…` (`baseline/import-fix/`, and the pulled copy in `phase3e-preserve/apk/`), legacy
0.8.3 play `59d5f447…` (`baseline/legacytest-cursor/`). Test builds of this phase: `phase3e-apk-b/` (0.9.0-modsel,
used for §1 and §6–7; its play file is 93 MB because Gradle's incremental packaging left unused space in it; the content
is the same 47.5 MB) and `phase3e-apk-c/`. The final APKs were packaged from a clean Gradle build.

## 6. SCP Beta 4 (legacy) in the regular app

| Test | Result |
| --- | --- |
| NVScript selected | `identified as recompiled version nv124 … one of 2 built in`, 78 imports, `DllMain -> 1`, every run |
| Mods | `mod_path` = the 17 mods in the original order + `patch_ext`; `uber_mod_path osm+ubermod` |
| Original saves | Slots 0, 3, 8, 13, 14 load; positions equal to Phase 3C's (e.g. slot 0 −92.0434 −169.932 7.00275). Slot 14 is your own auto-save from 18:08 (§8.3) |
| Scripts | Quest Note Notifier initialized; monolog empty; no SCP version warning in any capture (`phase3e/B1-…jpg`) |
| Save → new process → load | Saved into the empty 16th slot at (−66.9667, −169.566, 6.00385), reloaded there; the test slot removed |
| Audio | `sfx_device` already 1; DirectSound 44.1 kHz stereo; Android player `started` |
| Normal Play button | game folder `scp4`, frame limit none, write folder `profiles/scp4`, cursor relative (your setting) |
| Lifecycle | Home → icon: the same game process comes back to the front; audio player `started` |

## 7. SCP Beta 8 in the same app

| Test | Result |
| --- | --- |
| NVScript selected | `identified as recompiled version nv …`, 77 imports, static TLS slot, `DllMain -> 1` |
| Load | slot 0 loads (101.757 50.4182 −11.996), no warning |
| Earth grav shaft | new SCP game, Phase 3B's script: z 5.76 → 18.06 → 40.11 → **52.87** at the top (Phase 1E/3B: 52.8) |
| Save → new process → load | (103.005, 42.6953, −11.9984) both times; test slot removed |
| CROUCH | Shift, unchanged behaviour (§1) |
| Level transition | Not tested (Phase 3B could not reach the shuttle by scripted input either) |

Switching between installations: 17 runs of the two installations, interleaved, in this app; each run loaded its own
NVScript version; no reinstall in between (the APK was only updated between milestones).

## 8. Data migration

Script: `phase3e/migrate_legacy.py check | run` (backups first, staging names, every file hashed, never overwrites,
refuses a profile copy while the legacy game runs). Log of the final run: `phase3e/C-migrate-run.txt`.

### 8.1 Backups (`build/android/phase3e-preserve/`, 935 MB)

Both apps' internal data as tar (verified against on-device hashes: 218 and 182 files), both installed APKs, hash
manifests of all three game folders (176, 1,583 and 6,448 files). Phone storage: 18.6 GB free after the migration.

### 8.2 Game folder

`games/scp4` copied on the phone from the legacy app: **6,448 files, manifest digest `fd79e4a1…`**, every file identical
to the legacy app's copy and to the PC copy `build/scp4/game-scp4` (2.48 `ss2.exe`, Squirrel 1.02, NVScript 1.2.4, all
mods). The 2.47 executable is not used.

### 8.3 Profile

`profiles/scp4`: **181 files** (manifest `c89ab659…`, `phase3e/C-migrated-profile-scp4.sha256`): **15 slots, 160 save
files** (digest `8ad4cb67…`), `cam.cfg` (with `sfx_device 1`), `cam.cfg.openal`, `user.cfg`, `user.bnd`, `current/`,
logs. Slots 0–13 are byte-identical to the PC originals. **Slot 14 (Auto-save) is newer than the PC's**: your own
session in the legacy app at 18:04–18:08 rewrote it (CAMPAIGN, Rec1, MedSci1, ops2 changed). It loads. That legacy game
process was still running in the background when the profile was copied (it was not touched); anything you did in it
after the 18:08 auto-save is only in the legacy app.

### 8.4 Touch settings and labels

- The regular app's `touch-settings.cfg` (`b0ce8f1a…`) held no choice of yours: the never-moved Phase 2A layout,
  defaults, Direct cursor. It is kept as `touch-settings.cfg.before-3e`. The legacy app's file (`d2102308…`: current
  default layout + **Relative cursor**, speed 1) is now the regular app's. One settings file serves all game folders.
- Labels: `scp4` "System Shock 2 — SCP Beta 4 (Legacy)", `ss2scp` "System Shock 2 — SCP Beta 8", `ss2` "System Shock 2
  (no mods)". Hold **Game folder** to rename. The selected folder is now `scp4`.

### 8.5 Checks after all tests

| Item | Result |
| --- | --- |
| Legacy app (`…legacytest` 0.8.3) | All 182 internal files identical to the 18:45 backup (end of phase); its 6,448 game files equal to the regular app's copy (migration check, 19:19); not reinstalled |
| Regular app, `profiles/scp4` saves | 160 of 160 equal to the migrated copy and to the legacy app's |
| Regular app, `profiles/ss2`, `profiles/legacy247-test` | unchanged |
| Regular app, `profiles/ss2scp` | only `SS2.log` changed (test runs); saves unchanged |
| PC LaunchBox and Steam installations, Shockolate | not accessed in this phase (all inputs were copies under `build/`) |

## 9. Save and profile isolation

Each game folder has its own write folder `profiles/NAME` (unchanged design): `scp4` and `ss2scp` never share a slot,
`current/` or configuration. The migration used a new name (`scp4` was free in `games/`, `imported/` and `profiles/`)
and did not merge into an existing profile. The legacy `user.bnd` and `cam.cfg` stay in `profiles/scp4` and do not
affect Beta 8. Touch settings are shared on purpose (one player, one layout).

## 10. Regression suite

| Test | Result |
| --- | --- |
| Conformance on the phone, default lift | **0 / 511** failed |
| … `--cache-regs --abi-check` | **0 / 511**, 0 ABI violations |
| … `--write-dir` (VFS, write folder) | **0 / 113**, game folder unchanged |
| Touch bridge (direct and relative cursor, crouch, multitouch) | **0 / 380** failed |
| Module selection and refusals | desktop: all cases (PHASE3E_MODULE_SELECTION.md) |
| Beta 4 / Beta 8 scripts, save/load, audio, UI scale (640×480 interface), lifecycle | §6–7, on the final installed APK (native library identical to the tested one) |
| Not re-run here (code unchanged since Phase 3D) | Shockolate core suites, `SoundDeviceTest`, the relative-cursor HID gestures |

## 11. Remaining limitations

- **Not tested by hand**: everything here used scripted or HID input. Long sessions, replicators, psi, and a deck
  change you start yourself are still the checklist items from Phase 3C.
- Crouching while walking was checked by key edges on the phone and by position on desktop; the phone capture ended
  against a wall, so it does not show the stance.
- CROUCH follows a single-key binding only; a chord (e.g. `c+shift`) or a `keybind_path` setting falls back to Shift.
- A new NVScript version needs an entry in `port/nvscript_versions.json` and a rebuild; until then it is refused.
- The Windows host (`host/win_host.c`) still builds one NVScript.
- The legacy app's game process from your 18:04 session was still running in the background; it was left alone.
- A third-party floating padlock button (a system overlay) shows on the phone's screen in captures; it is not part of
  the app.
- The PS Vita low-address build accepts one NVScript, as before.

## 12. Your acceptance checklist (about 20 minutes, app "NewDark SS2")

1. Open the app: **Game folder** shows "System Shock 2 — SCP Beta 4 (Legacy) (scp4)". Tap it: it cycles to "(no
   mods)" and "SCP Beta 8"; back to Beta 4.
2. **Play** → Load: your 15 saves are listed (the top of the list is Hydroponics, the last two are Quicksave and
   Auto-save). Load your latest.
3. **Crouch**: tap CROUCH → the view drops; tap again → you stand. Also while walking with the stick.
4. Cursor: your Relative (trackpad) setting is active (SET → MORE shows it).
5. Sound, inventory, a door, an elevator to another deck and back.
6. Save into a **new** slot, leave with Home, come back with the icon, keep playing; quit and Play → Load that slot.
7. Switch **Game folder** to SCP Beta 8 → Play → load or start a game; CROUCH works (Shift binding); quit.
8. Back to Beta 4: your saves are still there and unchanged.
9. Only when 1–8 pass: the "SS2 Legacy test" app can be uninstalled by you (it is your rollback until then; nothing in
   it was changed). If you played in it after 18:08, re-run `phase3e/migrate_legacy.py check` first: it will report that
   the saves differ instead of copying over them.
