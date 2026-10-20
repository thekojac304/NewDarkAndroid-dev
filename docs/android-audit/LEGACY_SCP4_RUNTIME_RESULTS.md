# Phase 3C: legacy SCP Beta 4 runtime compatibility

2026-10-18. Branch `android-port`, started at `aa260c4`. Goal: run the user's original SCP Beta 4 campaign (NewDark 2.47
saves, 17 mods) on the existing recompiled NewDark 2.48 engine, without converting saves or upgrading mods. Follows
LEGACY_SCP4_FEASIBILITY.md §8–9.

Labels as in the feasibility audit: **Verified** (checked here, from files or runs), **Strongly supported**,
**Plausible but unverified**, **Unknown**.

## Assessment for the player

**Continuing the original campaign in the Android test app looks reasonably safe, with caveats below.** The original
NVScript 1.2.4 was recompiled, not replaced. It loads behind the unchanged identity check. On the phone, every save
tried loads in the original SCP Beta 4 environment with all 17 mods. Scripts run, and the "Mid-game activation"
warning from the Beta 8 attempt is gone. Saving and reloading works. A save made by the port keeps every quest
variable and every NVScript record of the original, apart from the timers that advance while you play.

What was **not** tested: changing decks (elevator or level transition), buying from a replicator, psi powers, and play
sessions longer than about a minute. Play these yourself before trusting a long session (checklist in §11). Keep
playing from the copies in the test app. The PC installation and the saves on the phone's Download folder stay the
master copies, and nothing here changed them. This is a tested, high-fidelity setup. It is not a guarantee of
100 % compatibility.

## Summary

| Success criterion | Result |
| --- | --- |
| 1. Original NVScript 1.2.4 runs under the recompiled 2.48 runtime | **Verified.** `abed4ee2…` lifted (4,288 functions), loaded at 30400000 with 78 imports, `DllMain -> 1`, about 4,700–9,300 engine calls per run. Same identity check as for every module |
| 2. Original SCP Beta 4 mod configuration loads | **Verified.** The 17-mod `mod_path` in the original order plus the original `patch_ext`. Gamesys DMLs applied as the data predicts (§6) |
| 3. Representative original saves load unchanged | **Verified.** All 15 on desktop, 8 representative ones on the phone (Hydroponics, Engineering, Operations, Recreation, Quicksave, Auto-save). The save copies stay byte-identical |
| 4. Scripts and gameplay interactions execute | **Verified** for what was exercised: NVScript and Squirrel run (call counts, live timers in saves), the Quest Note Notifier starts, an inventory item (medical hypo) works, an elevator door button works. Level changes, replicators and psi are **untested** (§11) |
| 5. No SCP version warning | **Verified.** Beta 4 has no version check (no `SCPVersion` anywhere in its scripts or strings), and none appears in any capture |
| 6. SCP Beta 8 setup intact | **Verified.** The accepted app is still installed (`2a5ad353…`) and its data is unchanged. A rebuild of the regular app packages a byte-identical `libss2game.so` |
| 7. Separate Android test environment | **Verified.** `local.newdarkandroid.ss2.legacytest` ("SS2 Legacy test") installs next to the accepted app, with its own game folder and saves |

## 1. Module binaries

| Module | Source | sha256 | Use |
| --- | --- | --- | --- |
| `NVScript.osm` **1.2.4** | Original install, `osm\` and `DMM\SCP_beta4\` (identical) | `abed4ee229dda567553b7d805f219008b6390c2d2da37459629eb28ac9ee6a9a` (662,016 bytes) | **Recompiled in this phase**, unmodified |
| `squirrel.osm` 1.02 | Steam 2.48 install | `d95b441565365525c3a208fc7f7ae5d513daac624e85d9a87703de2b5f763dc4` | Already recompiled; replaces 1.01 in the game copy, as a 2.48 update does |
| `squirrel.osm` 1.01 | Original install | `50f569ad495129c3c4734ddd1e886943eb9a212ff0a081d0d7a4a6a038aa26f0` | Not used (§5) |
| `SS2.exe` 2.48 | Steam install | `c7ff08c5a1837942410cc2328b4a6579ddb1d5a2678b8274db8b0347ce19823e` | Already recompiled; replaces the 2.47 `ss2.exe` in the game copy |
| `ss2.exe` 2.47 | Original install | `4199aac1d9af1e41385ba83d63470ca8b68c6f9ab684616c73fd6b94fd7a5a55` | Not used, not run |
| `allobjs.osm`, `lgvid.dll` | Both installs (identical) | `096c3a1e…a645`, `64d55311…4eec` | Unchanged |
| `NVScript.osm` (Beta 8) | `SCP_beta8.7z` | `f15fc91b…f11a` | Stays in the accepted app only |

All hashes: `legacy-scp4-runtime/hashes.txt`.

## 2. Recompilation findings

`lift.py --iat-indirect --rebase 0x30400000` (via `build_native.py --nvscript`): **4,288 functions, 175,962
instructions, 11 s**. No lifter or runtime change was needed (`legacy-scp4-runtime/nvscript-1.2.4-lift.txt`).

| Check | Result |
| --- | --- |
| Instructions left as run-time faults | 34 in all: 18 `mov m16, sreg` and 16 `int` (none SSE, none in script code). **Squirrel 1.02, which has shipped since Phase 1C, has exactly the same 18 + 16.** They are the same MSVC 2008 static-CRT routines: the crash-report context capture (`mov [x], ss/cs/ds/…`) and the legacy Pentium-FDIV workaround table (`_adj_fdiv_*`), whose `int 6` bytes sit between `fdiv`/`fdivr` fragments that are reached only through a dispatch table. 1,449 of 1,920 bytes around that block are identical in both modules, which is consistent with the same CRT code linked at different addresses (the differing bytes were not classified one by one) |
| SSE / x87 | No XMM instruction (as the audit found). x87 code goes through the existing x87 translation |
| Static TLS | None (Beta 8's module needed the Phase 3A TLS support; 1.2.4 does not) |
| Imports | 78 (KERNEL32 77, USER32 1), all bound to host shims at load: `78 imports bound` |
| C++ exceptions | `RaiseException` is called from 2 CRT sites. `_CxxThrowException` is reached from many functions; the busiest are helpers that fetch an engine script service by interface ID and throw only if the engine refuses it (the `SService` pattern of the LG script headers). None was triggered in any run. The host still ends the game with a clear message on a `throw` (unchanged) |
| Identity | Descriptor `"NVScript.osm", base 0x30400000, size 0xa8000, entry 0x3045f1e9, file base 0x10000000, code CRC 0x83eb1ef4`. The loader's size, base and CRC checks are unchanged. A different NVScript is still refused |

Not run for this module: the upstream `harness/difftest.py`/`insnfuzz.py`. The lifter is unchanged since Phase 3A, where
the SSE test passed 435 of 435 forms, and NVScript 1.2.4 uses only instruction forms the engine and Squirrel lifts
already use.

## 3. Changes

Code (committed):

| File | Change |
| --- | --- |
| `android/app/build.gradle` | `-Pside=NAME` (`-Pside.label=…`): `applicationIdSuffix ".NAME"`, `versionNameSuffix "-NAME"`, launcher label `SS2 <label>`, `BuildConfig.SIDE_LABEL`. Without `-Pside` the build is unchanged |
| `AndroidManifest.xml` | Label from the `appLabel` placeholder (`NewDark SS2` by default) |
| `LauncherActivity.java` | A side-by-side build shows "<label> build (<package>): separate app, separate games and saves" under the title and logs it at start |
| `android/README.md` | How to build a side-by-side app |

No change to the lifter, the runtime, the host, the module loader or the identity checks. The engine lift is reused:
`build/android-scp4/game/gen/{nd,ao,sq,lv}` were copied from the regular build, and `build_native.py` accepted them
only after checking their source hashes (`already lifted`). JNI names follow the Java namespace, which did not change,
so the native code needs nothing for the new application ID.

Build (not in git, under `build/`):

```sh
python android/build_native.py --build build/android-scp4 --game "<Steam>/SS2.exe" --game-backend all \
    --nvscript build/scp4/modules/nvscript-1.2.4/NVScript.osm          # a copy of the original, named NVScript.osm
cd android && ./gradlew -Pside=legacytest "-Pside.label=Legacy test" -Pnative.dir=<repo>/build/android-scp4 \
    assemblePlayDebug assembleDevDebug
```

The descriptor name comes from the file name, so the copy has to be called `NVScript.osm`. Desktop hosts:
`build/desktop-win-{null,sdl2}-scp4/ss2port.exe` (CMake with `PORT_GENERATED=build/android-scp4/game/gen` and the Zig
toolchain wrappers of earlier phases).

## 4. Isolated legacy installation

| | |
| --- | --- |
| PC copy | `build/scp4/game-scp4`: the original install minus `save_*/` and `current/` (6,448 files, 1.6 GB). Then exactly **2 files replaced**: `ss2.exe` ← 2.48 `SS2.exe` (the name is kept), `osm\squirrel.osm` ← 1.02. All other 6,446 files are byte-identical to the original (manifest diff in `hashes.txt`) |
| Phone copy | `Android/data/local.newdarkandroid.ss2.legacytest/files/games/scp4`: `adb push` of the PC copy, **6,448 of 6,448 hashes equal** |
| Profile (phone) | `files/profiles/scp4/`: `save_0`…`save_14` copied from the phone's `Download/System Shock 2` (161 files, hashes equal to the PC originals), `cam.cfg` = the original with only `game_screen_size 2560 1440` → `1280 720`, `user.cfg` = the original `User.cfg`, seeded by the launcher, plus its two Interface-size lines |
| Profile (desktop) | `build/scp4/pc-write/`: the same saves and configuration files |
| Storage | 22 GB free before; the copy needs 1.6 GB |

`cam_mod.ini` against the Blue Mod Manager list: **no discrepancy.** `mod_path` names the 17 mods in exactly the
listed order, then `.\patch_ext`. `NoGunDegrading` is in `DMM\` but not in `mod_path`. `uber_mod_path osm+ubermod`
puts the original `osm\NVScript.osm` (1.2.4) first. The original configuration files (`cam_ext.cfg` with
`phys_freq 60`, HDR, bloom and post-processing; `install.cfg`; `User.cfg`) are used unchanged.

## 5. Squirrel

2.48's Squirrel 1.02 was used, as planned. Every run logged `Loaded script module "squirrel.osm"` and
`OSM: SQUIRREL> Quest Note Notifier initialized`. The original 2.47 PC log has the same line. The monolog stayed
empty in all runs. As a positive control, `script_dump_files` wrote `'allobjs' 'nvscript' 'squirrel'`, so the monolog
was being written. Saves keep SCP's Squirrel state (`scpCameraHelper`, `scpElevatorHelper`, `TransluceInOut`,
`zbQuestNotifier`, …) and it changes only as play goes on (§7). **No incompatibility was found, so Squirrel 1.01 was
not recompiled.**

## 6. Legacy mod loading (Verified, `legacy-scp4-runtime/desktop-dbmod.log-slot0.txt`)

With `dbmod_log 3`, slot 0 on desktop:

| Gamesys DML | Result | Expected (feasibility §3–4) |
| --- | --- | --- |
| `patch_ext\gamesys.dml` (SS2Tool, `99a2d2c7…`) | queued, **applied** ("patch done"), with warnings for properties SCP Beta 4 already changed | Applied: its guard archetype −4755 does not exist in Beta 4 |
| `DMM\obj_fixes_v12\gamesys.dml` | applied | applied |
| `DMM\ss2_quest_note_notifier\gamesys.dml` | applied | applied |
| `DMM\Vurt's SS2 Flora Overhaul 1.0k\gamesys.dml` | applied | applied |
| `DMM\SS2_Rebirth_v05a\gamesys.dml` | "patch rejected by fingerprint identification" | rejected (fingerprinted for another mission set) |

The engine log shows the original `mod_path` and `uber_mod_path` unchanged. SCP Beta 4's gamesys (`shockscp.gam`
`430b1793…`) resolves **every** object→archetype link in all 24 port-written saves (0 unresolved, §7). The texture,
model and music replacement mods are found through the same `mod_path`. They leave nothing in a save, and they were
not checked one by one on screen.

## 7. Save compatibility

### 7.1 Method

For every slot: load the original save, walk, wait about 10–15 s, then **Save Game into the empty 16th slot**. The new
save is then compared with the original by `legacy-scp4-runtime/save_roundtrip.py`, which parses the script-data
records (`SCRIPTSTATE`: object, script class, key, value), the quest variables (`QUEST_DB`, `QUEST_CMP`) and the
object→archetype links (`L$MetaProp`, resolved in Beta 4's gamesys). The original slots were never written: their 161
files hash the same after all runs, on both the PC and the phone.

### 7.2 Results (`legacy-scp4-runtime/roundtrip-output.txt`, `runs-summary.txt`)

| | Desktop (x86-64), slots 0–14 | Phone (arm64), slots 0, 3, 5, 7, 8, 12, 13, 14 |
| --- | --- | --- |
| Loads, plays, saves, exits 0 | 15 / 15 | 8 / 8 (`verdict=PASS`, `fatal=no`) |
| Script modules loaded | 3 / 3 every run | 3 / 3 every run |
| Monolog (script errors) | empty | empty |
| Faults, `RaiseException`, ABI violations | 0 | 0 |
| Archetype links unresolved | 0 (of about 7,000–11,000 per save) | 0 |
| Mission quest variables changed (`QUEST_DB`) | 0 | 0 |
| Campaign quest variables changed (`QUEST_CMP`, 88–132 per save) | 0, except 2 research timers (slot 4 `Arach._Organ_ResearchTime`, slot 10 `INT_Boost_ResearchTime`) still running | 0 |
| NVScript records (642–2,008 per save) | all levels not entered: identical; current level: only timers changed and records of objects that spawned or died | same |

What changed in the current level, and why it is expected:

- **Timers:** `ecotimer`, `HackTimer`, `StdDoor.Timer`, `StatBoostImplant.timer`, and `cScr_NVRelayTrap.m_hTimerHandle`
  (a repeating NVScript relay re-arming its timer, which shows NVScript running and saving its state).
- **Objects created or removed by the game:** shotgun hybrids spawned by the ecology and firing (`OG-Shotgun`, `Grunt
  Shotgun Slug`, muzzle flash and shell eject, each with its NVScript records), corpses removed, SCP fade-out
  effects finishing (`TransluceInOut`). **The same objects with the same archetypes appear on desktop and on the
  phone** for the same slot and input; in one case the object IDs differ by 2 (allocation order).
- **Slot 8** (2023-04-07, before the Quest Note Notifier was installed): `zbQuestNotifier.noteState` is new. This is the
  notifier mod starting on an older save, as the feasibility audit predicted, and as it would on the PC.

**Operations archetype references** (the Beta 8 problem): in the phone's saves from slots 3, 5 and 13, ops2 obj 1647 and
ops3 objs 1469 and 1478 are **"Once Router"** before and after (under Beta 8 they became "MatSnow"). Their trigger chain
partners are 1646 "Ecology", 1651 "Sound Trap" and 636 "Swarmer Floor Pod". Unchanged.

### 7.3 Interaction, save, reload (phone)

| Test | Result |
| --- | --- |
| Load menu | All 15 original names listed in the original order, plus `<EMPTY>` (`1-…jpg`) |
| Movement | Position changes on every slot. **Right after loading, positions are identical on desktop and phone** for all 8 slots. After 1.5 s of walking they are identical on 4 and within about 0.1 units on 4 (key-hold timing) |
| Inventory | Opens with the saved items (`3-…jpg`) |
| Item use | Slot 0: right-click on the medical hypo stack: tooltip "A medical hypo", stack **20 → 19**, HP **48 → 50** (`4-…jpg`). The save confirms the player (obj 40) went from 48/50 to 50/50 |
| World interaction | Slot 14: USE on the elevator door button opens the doors, and the player walks in (x 14.75 → −2.39; `9-…jpg`) |
| New save in a new slot, reload in a new process | The new "(Quick Save)" in slot 16 reloads at exactly the saved position (−92.0434, −169.932, 7.00275) with HP 50 and 19 hypos (`5-…jpg`) |
| SCP warning | None in any capture (`2-…`, `6-…`, `7-…`, `8-…jpg`) |

## 8. Desktop (host-side) validation

Done before anything was installed on the phone. The Windows `ss2port.exe` (null and SDL2 backends) was built from the
same lift and run on `build/scp4/game-scp4` with the desktop profile. First run (slot 0): NVScript 1.2.4 loaded
(`78 imports bound`, `DllMain -> 1`, entry `3045f1e9` = descriptor), all three modules were logged in `SS2.log`, the
notifier started, the player moved, and the monolog stayed empty. A 15-slot loop then followed (§7.2). The SDL2 run
captured the screens (`10-…jpg`).

## 9. Android results and regressions

| Test | Result |
| --- | --- |
| Conformance, phone (`run_conformance.py --target android`), default | **0 / 511** failed |
| … `--cache-regs --abi-check` | **0 / 511**, 0 ABI violations |
| … `--write-dir` | **0 / 113**, game folder unchanged |
| Touch controls, Interface size, keyboard button | Present and unchanged in the legacy app (overlay buttons, LOOK/CURSOR modes, Interface size 640×480; `11-…jpg`). Driven by scripted input here, not by hand |
| Android lifecycle | Every run is its own `:game` process. The launcher's history holds 13 legacy-app runs (dev and play variants), all `verdict=PASS`, exit 0, `fatal=no` |
| Module cleanup and reload | Each run loads the modules again in a new process. Load → save → new process → load worked. Within one process, a save-then-load loop was not run on this build (Phase 3B covered it for Beta 8's NVScript) |
| Regular app (SCP Beta 8) | Still installed, `base.apk` `2a5ad353…`, never replaced. `profiles/ss2`, `profiles/ss2scp`, `touch-settings.cfg` and `shared_prefs` are byte-identical before and after (30 files). A rebuild of the regular play APK without `-Pside` gives package `local.newdarkandroid.ss2`, label "NewDark SS2", the Beta 8 NVScript identity (`NVScript.osm:b2000`), and a **byte-identical** packaged `libss2game.so` (`da855353…`). It was not installed |

## 10. Comparison with the original PC setup

No new PC run of 2.47 was made. The original executable was not launched, and scripted input cannot drive it. The
reference is the original install's own `ss2.log` from its last session (read only):

| Item | PC 2.47 | Port (2.48, desktop and phone) | Class |
| --- | --- | --- | --- |
| Modules | nvscript 1.2.4, allobjs, squirrel **1.01** | nvscript 1.2.4 (recompiled), allobjs, squirrel **1.02** | Expected engine-version change (the 2.48 update replaces Squirrel) |
| Module line | `[FileVer=1.2.4.0 ; …]` | `[]` (the host does not supply version resources) | Cosmetic log difference |
| Notifier | "Quest Note Notifier initialized" | same | Same |
| Monolog | empty | empty | Same |
| Load positions | — | identical across x86-64 and arm64 | No difference found |
| Rendering | 2560×1440, D3D9, 8× MSAA | 1280×720, the port's translated renderer | Visual difference |
| Input | mouse and keyboard | touch overlay or scripted input | Input difference |
| Quest state, archetypes, NVScript data | (in the saves) | unchanged apart from play (§7) | No regression found |

**No gameplay regression and no save-state difference beyond normal play was found.** Rare script paths, other decks
and long sessions were not compared.

## 11. Remaining risks, unresolved items, and the player's checklist

| Item | Status |
| --- | --- |
| Level transition / elevator deck change | **Untested.** The door opened, but the deck panel was not found by scripted input. **Check by hand first** |
| Replicator purchase, recycler, upgrade units (NVUpgradeCheatStopper) | Untested (NVScript classes present and their state is carried, but they were not triggered) |
| Psi powers | Untested |
| Long sessions (an hour or more), late-game decks | Untested |
| C++ exceptions in NVScript 1.2.4 | None seen. A `throw` would end the game with a message rather than corrupt state |
| Mission DMLs for decks not yet visited | Same files and DML engine as the PC. Applying them was not observed, because no new deck was entered |
| 2.47 → 2.48 engine fixes (save loading, level transitions) | Intended behaviour changes in the engine; can differ from what was seen on 2.47 |
| Performance with the original rendering extras (HDR, bloom, post-processing) | Not measured. Runs played at normal pace. Visual only |
| `build/android/baseline/phase4/libss2game.so` | **Found at the start, not caused here:** one bit differs from its recorded hash (offset 230,293,672: 0x25 vs 0x2D). The phase4 APKs verify, and the same library in `phase3a/` verifies. Possibly storage bit-rot. Left as found |

**Checklist for the player** (about 30 minutes, in "SS2 Legacy test"):

1. Play → Load your latest slot. Check the inventory, health and location against your memory or the PC.
2. Take an elevator to another deck and back. The deck you left should be as you left it.
3. Buy something from a replicator, use a psi power, and use an upgrade unit if one is near.
4. Save into a new slot, quit with Back/home, start again and load it.
5. Play 30–60 minutes, then save into a new slot. If anything looks wrong, stop and keep the PC as master.

## 12. Recommendation: Beta 4 and Beta 8 in one app

**Keep them as two apps for now.** The side-by-side build needs no host change and keeps the accepted Beta 8 app
untouched. Two NVScript builds in one APK would need identity-based module selection in `modules.c`: among the
descriptors with a file's name, take the one whose size, base and code CRC match the file. Each would need its own
prefix and base (for example 1.2.4 at 0x30500000), and it would add about 36 MB of generated C. That keeps every identity
check, but it touches the loader for all games. It is worth doing only after the checklist above has passed and the
legacy setup is wanted long term. Then both SCP setups and the conformance tests have to be run again.

## 13. Phone and PC state afterwards

| Item | State |
| --- | --- |
| Accepted app `local.newdarkandroid.ss2` | Installed, `2a5ad353…`, data unchanged |
| Legacy app `local.newdarkandroid.ss2.legacytest` | Installed: **play variant `0.8.1-import-fix-legacytest-play`, `192824dc…`**. Game folder `scp4` selected, profile `scp4` with the 15 original slots (hashes equal to the PC originals). The test save (slot 16) was removed after it was pulled for analysis |
| APKs | `build/android/baseline/legacytest/`: play `192824dc8eb31d5781d17c7a8d7a0c7109e6cdd85532bfd83899f8fc3e5d2543`, dev `8d60f4d3006f8f72224e25007be6aea190e4494c83ef56f361b0ed1da5a03d58` (`SHA256SUMS`). All earlier baselines kept |
| PC original install | **6,624 of 6,624 files unchanged** (hashed at the start and end). Not launched |
| Steam install | Only read (no file newer than the morning's baseline) |
| Phone `Download/System Shock 2` | 6,624 files unchanged |
| Shockolate | Not touched |
| Evidence not in git | `build/scp4/` (game copy, desktop profile, all runs and saves), `build/android/legacy-scp4-runtime/` (hash manifests before and after) |

## 14. Reproduce

```sh
# desktop 15-slot round trip (null backend; writes only build/scp4/pc-write/save_15, then moves it out)
build/venv/Scripts/python.exe docs/android-audit/legacy-scp4-runtime/desk_roundtrip.py 0 1 2 ... 14
# one phone run in the legacy app (scripted input + captures; pulls logs, captures and any new slot)
build/venv/Scripts/python.exe docs/android-audit/legacy-scp4-runtime/phone_run.py NAME wait:5000 esc ... quit
# compare saves
python docs/android-audit/legacy-scp4-runtime/save_roundtrip.py --gam <SCP_beta4>/shockscp.gam \
    --watch ops2.mis:1647,ops3.mis:1469 build/scp4/pc-write/save_5 build/scp4/phone/slot5-rt/save_15
```

The two drivers contain this machine's paths (`<repo>`, the phone serial). Files in
`legacy-scp4-runtime/`: captures `1-…` to `11-…jpg`, `roundtrip-output.txt`, `runs-summary.txt`, `hashes.txt`,
`nvscript-1.2.4-lift.txt`, `desktop-dbmod.log-slot0.txt`, `desktop-monolog-script_dump_files.txt`,
`{desktop,phone}-run.log-slot0.txt`, `phone-SS2.log-slot0.txt`, `phone-history.txt` (the legacy app's run history), and the scripts.
