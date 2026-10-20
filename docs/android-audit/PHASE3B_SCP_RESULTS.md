# Phase 3B results: System Shock 2 Community Patch (SCP) Beta 8 on Android (Milestone C)

2026-10-18 (overnight session). Goal: run a properly configured SCP-modded installation through the Android port and
show that SCP's own content and scripts work, not just that its files load.

**Status: PARTIAL.** SCP Beta 8 runs on the S26 Ultra from a copied, DMM-style installation. Everything tested is
SCP's real behaviour: its missions, gamesys, objects, Squirrel and NVScript scripts, its module gate, both Earth grav
shafts, the UNN centre with SCP's doors and furniture, the training-sim teleport, quicksave and quickload. **Not
reached:** a level transition (Earth → Station), item pickup and use, and a measured SCP gameplay-rule change. Touch
input on the phone was held by another app's overlay all night (PHASE2B2_RESULTS.md), so all of this was driven by
the host's scripted input, with frame captures from the game itself. No crash, fault or guest error in any SCP run.

| Version tested | **SCP Beta 8** ("Beta 8 - December 25, 2025"), `<downloads>\SCP_beta8.7z`, 37,710,819 bytes, sha256 `bcf547c7bd3a38ccb26e6918d72d74e567dc69b138c1ffcb975ea6d165ddb3f6` |
| --- | --- |
| Base game | Steam System Shock 2, NewDark **2.48** ("System Shock 2 Patch Final 2.48"), `SS2.exe` sha256 `c7ff08c5…823e`, with the Steam release's community fixes (`patch_ext.crf`, `misdml\`) |
| APK | `0.7.0-phase3a` (sha256 `a59d7f92…7578042`, `build/android/baseline/phase3a/`), built with `--nvscript` from this SCP's `NVScript.osm` |

## 1. SCP Beta 8: what it is and needs (C1)

| | |
| --- | --- |
| Provenance | The user's download of the official package; Beta 6 (Remaster `.kpf`) and Beta 4 (old PC install) were found earlier but not used. Beta 8 supersedes them, as instructed |
| Contents | 1,407 files, 154.6 MB: `shockscp.gam` (`5c2d7f25…`), 24 missions (`earth.mis` … `shodan.mis`, `hydro2.dif`, `shodan.dif`), `sq_scripts\scpScript.nut` (`5d19b000…`), **`NVScript.osm`** (`f15fc91b…`, PHASE3A_NVSCRIPT_RESULTS.md), assets (`obj`, `mesh`, `fam`, `snd`, `snd2`, `song`, `strings`, `intrface`, `iface`, `bitmap`, `book`, `material`, `objicon`, `mschema`), `readme.txt`, `SCP_beta8_readme.pdf` |
| Requires (its readme) | Retail SS2 patched to **NewDark 2.48** or higher (SS2Tool recommended); install as a standard mod with the Dark Mod Manager at the lowest priority; a **new game**. "If any of the script modules required by SCP fail to load, this will be detected at new game start and you won't be allowed to proceed." |
| NVScript | Shipped in the archive (Nameless Voice, 2024, MSVC 2017). Recompiled in Milestone B |
| Squirrel | Not shipped: NewDark 2.48's own `osm\squirrel.osm` (already recompiled) runs `scpScript.nut` |
| Missing components | None. The Steam 2.48 folder plus this archive is a complete SCP installation |

## 2. Installation (no Android mod manager)

What DMM does, done by hand, keeping the Steam folder untouched:

| | PC (`build/scp/game-scp/`) | Phone (`files/games/ss2scp/`) |
| --- | --- | --- |
| Game folder | a copy of the Steam folder | a copy of the vanilla game folder (`cp -r games/ss2 games/ss2scp`) |
| SCP | `DMM\SCP_beta8\` (the extracted archive) | `adb push` of the same folder |
| `cam_mod.ini` | the Steam file with one line changed: `mod_path DMM\SCP_beta8+patch_ext` | the same file |
| Write folder (saves, `cam.cfg`, logs) | `build/scp/pc-write/` | `files/profiles/ss2scp/` (separate from the player's `profiles/ss2`; a copy of the player's `cam.cfg` for the 1280×720 resolution) |
| Permissions | | `chmod -R a+rX,go-w` after copying with adb (as for the vanilla folder) |

The launcher chooses the folder with **Game folder** or `--es game ss2scp`, and every folder gets its own write
folder, so SCP and vanilla saves can never mix. The vanilla folder and profile were not changed by any SCP run.

## 3. Mod loading (C2)

| What | Result | Evidence |
| --- | --- | --- |
| `cam_mod.ini` | ✓ The engine reads `mod_path DMM\SCP_beta8+patch_ext`, `uber_mod_path osm+ubermod` | `SS2.log` config dump |
| Search order | ✓ SCP's missions and gamesys win over `Data\` (SCP-only objects and the SCP gate exist in the loaded Earth); Steam's `patch_ext` stays below SCP | captures; `dbmod.log` |
| Modified `.mis` | ✓ SCP `earth.mis`: its payphone, front doors, lobby bench and poster, the module gate | captures |
| Modified `.gam` | ✓ `shockscp.gam` (named by SCP's missions): the patch DML is applied against it (`RenderAlpha` warnings for archetypes SCP changed) | `phase3b/dbmod.log-earth.txt` |
| `.crf` archives | ✓ `gamesys.dml` "found … in path `patch_ext\`" (Steam's `patch_ext.crf`) | `dbmod.log` |
| DML | ✓ Gamesys DML applied. Mission DML `misdml\earth.mis.dml` (Steam community fixes, `!FINGERPRINT { QVAR "scplevel" == 1 }`): **"patch rejected by fingerprint identification"**, as designed for SCP | `dbmod.log` (`dbmod_log 3`) |
| Squirrel scripts | ✓ `squirrel.osm` loads and runs SCP's `scpScript.nut`: SCP's Squirrel gate (`scpSelfDestruct`, an object that only a running Squirrel removes) did not block; the engine calls Squirrel ~400–3,300 times / 20 s | run logs |
| NVScript scripts | ✓ The NVScript gate opens only with the recompiled module (PHASE3A §5); the engine calls NVScript ~4,000–7,400 times / 20 s; the monolog lists the mission's modules `'allobjs' 'nvscript' 'squirrel'` (`script_dump_files`) | `phase3a/`, `phase3b/monolog-script_dump_files.txt` |
| Case-insensitive paths | ✓ Mixed-case folders and files resolve; of 296 distinct SCP paths the engine probed and did not find, none exists in SCP (they are its extension and folder probes) | §5 |

## 4. Gameplay (C3), S26 Ultra, `ss2scp`, new game on Normal

| Check | Result | Evidence |
| --- | --- | --- |
| Main menu, New Game, difficulty screen, Start Game | ✓ | `phase3b/1-new-game-difficulty.jpg` |
| Opening cutscene, skip | ✓ | captures |
| Earth tram station | ✓ SCP's **payphone** ("Added payphone 1000") and signage; caption "4 Years Earlier / Ramsey Recruitment Ctr." | `2-earth-tram-payphone.jpg` |
| SCP module gate in front of the first grav shaft | ✓ Not shown (both modules ran); shown and blocking without NVScript | `3-grav-shafts.jpg`, `phase3a/scp-gate-with-and-without-nvscript.jpg` |
| Lift grav shaft | ✓ z 5.0 → 18 → **52.8** at the top (vanilla Phase 1E: 52.8–53.6) | `SS2.log` |
| Descent grav shaft (jump) | ✓ z 5.50 → apex **17.54** → 5.50 (vanilla Phase 1E: 17.59 / 17.54) | `SS2.log` |
| Street level, UNN recruitment centre | ✓ SCP's added **front doors** (the player is stopped by the closed leaf, walks through the open side) | `4-street-level-quicksave.jpg` |
| UNN lobby | ✓ SCP's furniture: bench, "UNN STARTS WITH U" poster, Marines poster; Basic/Advanced Training signs | `5-unn-lobby.jpg`, `6-unn-lobby-bench-poster.jpg` |
| Basic training | ✓ Teleport into the cyberspace sim (y 162 → 593), HUD appears | `7-basic-training-sim.jpg` |
| Quicksave, quickload | ✓ Saved at (−24.63, 48.18, 52.51), walked to (−24.63, 85.36, 57.52), quickload restored (−24.63, 48.18, 52.51); all three modules reloaded; NVScript calls continue after the load | `SS2.log` |
| Load from the main menu | ✓ The lobby quicksave loaded through Load Game → slot → Load | run |
| Crash-free | ✓ The launcher's history for the session (SCP and vanilla test runs, 1–2 minutes each): **35 PASS** (exit 0 at the script's `quit`); 3 early probes without a `quit` that were ended with `am force-stop` (recorded as killed, `status="start …"`); 1 refused before start (a copied folder the app could not read yet). Every history line has `fatal=no` (the launcher scans each run log for `FATAL` and ABI violations) | `files/results/history.txt` |
| Resource loading | ✓ `SS2.log` errors: only `failed to load psapi.dll` (vanilla has it too) | logs |
| **Level transition** (Earth → Station) | **Not reached.** The shuttle hallways that lead to Station were not found by blind scripted navigation in the time given | §6 |
| **Inventory / item use** under SCP | **Not verified.** Early Earth has no inventory interface; a summoned item landed off the crosshair | §6 |
| **A measured SCP rule change** | **Not verified.** Planned: SCP's cigarettes (`scpCancerStick`: +1 PSI, −1 HP); the item did not reach the inventory in the sim | §6 |

## 5. Compatibility matrix

| SCP component | Status | Notes |
| --- | --- | --- |
| Missions (`*.mis`), `.dif` | **Works** (Earth) | Other levels not reached |
| Gamesys `shockscp.gam` | **Works** | |
| Assets (objects, textures, sounds, strings, interface) | **Works** where seen | Earth only; no missing-file errors |
| `scpScript.nut` (Squirrel) | **Works** | Gate, ongoing calls |
| `NVScript.osm` | **Works** | Recompiled (Milestone B); gate, ongoing calls |
| SCP + Steam community fixes (`patch_ext`, `misdml`) | **Works as designed** | misdml skipped by fingerprint; `patch_ext` gamesys DML applied with harmless warnings |
| Saves | **Works** (quicksave/quickload, menu load) | Fresh SCP game only, as SCP requires; vanilla saves were not loaded with SCP |
| Level transitions | **Untested** | |
| Touch controls with SCP | **Untested by touch** (overlay); the overlay renders and the touch mode follows the game in SCP (LOOK in play, CURSOR in menus) | captures |
| Performance | Not measured with SCP. The runs played at the normal pace of the scripts | |

## 6. Open items and observations

- **Earth → Station transition:** blocked by navigation, not by a known defect. Basic training was entered; inside
  the sim the player stopped at (−23.0, 593.5). **The same happens in vanilla** with the same route
  (`ss2test`, new game): it is not SCP-specific, and whether it is the training sim's design (waiting for its
  instructions) or a pre-existing port issue needs a person to play it.
- **Item pickup/use under SCP and the cigarette rule** need a short manual check (or a touch session).
- **Recommended manual SCP check** (about 15 minutes, once touch works): Play with **Game folder: ss2scp** → New
  Game → Earth → both shafts → UNN centre → finish or skip basic training → choose a service → **Station loads** →
  pick up and use an item → save, quit, restart, load.

## 7. Reproduce

```sh
# phone: SCP folder from the vanilla one, plus the mod and the edited cam_mod.ini
D=/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games
adb shell cp -r $D/ss2 $D/ss2scp
adb push build/scp/SCP_beta8 $D/ss2scp/DMM/          # -> DMM/SCP_beta8
adb push build/scp/overlay/cam_mod.ini $D/ss2scp/cam_mod.ini
adb shell chmod -R a+rX,go-w $D/ss2scp
# scripted runs with frame captures (docs/android-audit/phase3b/scp_script.py writes the scripts):
python docs/android-audit/phase3b/scp_script.py lift1.txt wait:5000 esc wait:1500 esc wait:1500 esc wait:2000 newgame wait:2500 \
  menu:648:333 wait:8000 esc wait:12000 pos w:7000 pos s:900 pos d:350 pos shot w:1500 pos shot ...
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game --es game ss2scp \
  --es env SS2PORT_INPUT_SCRIPT=<script>,SS2PORT_SHOT_DIR=<dir>,SS2PORT_MODSTATS=1
adb exec-out run-as local.newdarkandroid.ss2 cat files/profiles/ss2scp/SS2.log | grep "PLAYER POS"
```

The test profile `profiles/ss2scp/user.cfg` had `dbmod_log` and `monolog` added for the DML/monolog evidence; they
were removed afterwards.
