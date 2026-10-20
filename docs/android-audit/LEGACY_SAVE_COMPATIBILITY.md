# Legacy save compatibility: NewDark 2.47 / SCP Beta 4 saves on the 2.48 / SCP Beta 8 port

2026-10-18. Galaxy S26 Ultra (serial <serial>), installed APK `0.8.1-import-fix-play` (`2a5ad353…`), game folder
`games/ss2scp` (NewDark 2.48 `SS2.exe` + SCP Beta 8). No engine, launcher or APK change was made for this test.

**Verdict: loadable and playable in short tests, but not supported by SCP.** All 15 of the user's saves are listed,
and every save tried (6 of 15) loads into the game. The player can move, use the inventory and use world objects.
Scripts run, and a new save reloads exactly. But every save shows SCP Beta 8's own warning, "Mid-game
activation of SCP detected. Not supported. You must start a new game." The offline analysis (§4) also finds three
Operations trigger objects that change meaning under Beta 8, and one model from a mod that is not installed.
Long play-throughs, level changes and the rest of the game are **not verified**.

| Check | Result |
| --- | --- |
| Save appears in the load menu | **Yes, all 15** (13 named slots, Quicksave, Auto-save; `legacy-save/1-…jpg`) |
| Save actually loads | **Yes, 6 of 6 tried**: slots 0, 7, 8, 13, 14 and the new slot 15. The other 9 were not loaded (§3) |
| Player can move | **Yes**. `PLAYER POS` changes with forward, turn and back on every slot tried (`legacy-save/player-pos.txt`) |
| Player can interact | **Yes**. Inventory opens with the saved items on slots 0, 8, 13 and 14. USE on the Hydroponics replicator opens its interface ("[broken]", REPAIR) |
| Required scripts initialize | **Yes**. `allobjs`, `nvscript` and `squirrel` load, and all three are called continuously (`modstats-slot14.txt`). SCP's own Squirrel script runs (it is what shows the warning). The monolog shows no script errors (positive control in §3) |
| Save/reload keeps working | **Yes**. New save from the menu into the empty slot 15. In a new process it reloads at exactly the saved position (−57.7136, −169.435, 6.01358) with the same inventory |
| Crashes | **None**. 13 runs, all `verdict=PASS`, `exit 0`, `fatal=no` |
| Longer-term compatibility | **Unverified**: level transitions, unvisited Beta 8 levels, quests, the Operations triggers in §4.3, play sessions longer than about a minute |

## 1. What was checked first

| Item | State |
| --- | --- |
| APK | `local.newdarkandroid.ss2` `0.8.1-import-fix-play` (code 10), installed `base.apk` sha256 `2a5ad3535a2586b9e16f7c67c5f86d08e204e9513d6d71f8e8f502d9c988efb6`. It is the play APK in `build/android/baseline/import-fix/`, whose `SHA256SUMS` verify (both APKs OK) |
| Source | `/storage/emulated/0/Download/System Shock 2/`: NewDark 2.47 `ss2.exe`. `cam_mod.ini` `mod_path` = SCP_beta4 + 16 more DMM mods + patch_ext (§4.4). 15 save folders `save_0`…`save_14`, 161 files, 139 MB |
| SCP Beta 8 | `games/ss2scp`: 2.48 `SS2.exe` (5,121,024 bytes), `mod_path DMM\SCP_beta8+patch_ext`, `shockscp.gam` sha256 `5c2d7f25…`. Its profile `profiles/ss2scp` holds `save_0`, `save_14`, `current/`, `cam.cfg`, `user.cfg` |
| Overlay | `mCurrentFocus` was the launcher, so the Black Screen overlay was not up. Input was scripted anyway (`SS2PORT_INPUT_SCRIPT`, frame captures with `SS2PORT_SHOT_DIR`), so the runs can be repeated |

## 2. Isolation and preservation

1. **Hashes of the originals.** All 161 save files were hashed on the phone, with a per-slot digest in
   `legacy-save/source-saves.per-slot.sha256.txt`. The whole source tree was also hashed (6,624 files). The full lists are in
   `build/android/legacy-save-test/` (`source-saves.sha256`, `source-full-tree.sha256`; not in git).
2. **Backup.** `files/profiles/` (both profiles), `files/touch-settings.cfg` and `shared_prefs/` were saved before any run
   to `build/android/legacy-save-test/backup-app-data-before.tar` (41 entries), with per-file hashes in
   `app-data-before.sha256`.
3. **Test profile.** `files/profiles/legacy247-test/` is new. It holds `user.cfg` and `cam.cfg` copied from
   `profiles/ss2scp` (SCP Beta 8's configuration: 1280×720, Interface size 640×480) and `save_0`…`save_14` copied
   from the source with `cp`. All 161 copied files hash identical to the source. `current/` was not copied. In
   `save_14`, the user's own `save_14.zip` came along unchanged; the game ignores it.
4. **How the test profile was used.** The launcher always passes `SS2PORT_WRITE_DIR=profiles/<game>`. The test runs
   also passed `--es env SS2PORT_WRITE_DIR=…/legacy247-test`. The native side applies `--android-env` entries in order
   with `setenv(…, 1)`, so the later value wins, and `run.log` shows both lines. Saves, `current/`, `SS2.log` and the
   monolog therefore went to the test profile. The game folder is still `games/ss2scp`, read only, with SCP Beta 8's
   `cam_mod.ini` and mod paths. No launcher change was needed.
5. **After the tests** (all verified by hash):
   - Source tree: **6,624 of 6,624 files unchanged** (same paths, same sha256).
   - `profiles/ss2`, `profiles/ss2scp`, `touch-settings.cfg`: **unchanged** (28 files).
   - `shared_prefs/runs.xml` still held the test `env` (it is kept for later starts), so it was restored from the
     backup with the app stopped. It is byte-identical to before (`d5244716…`): game `ss2scp`, `env` empty.
     Android's own `ActivityThread.IDS.xml` changed, as in earlier phases.
   - The test copies of `save_0`…`save_14` are still identical to the source. Loading does not write to a slot.
   - Removed: `files/legacy-shots/`, `files/legacy-script.txt` and the `/data/local/tmp` staging copies.

## 3. Runs

Every run starts a new `:game` process with **Load Game** from the main menu. Then: slot → **Load** → about 20 s
→ captures, `log_player_pos`, input → `quit`.

| Slot | Name in menu | Last level | Loaded | Moved | Interacted | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Hydroponics B/C 3:48:59 | Hydro2 | ✓ | ✓ (−92.04 → −69.95, 6.00) | ✓ inventory; USE on replicator opens its interface | First save tried. SCP warning (§4.1). `2-…`, `3-…`, `4-…jpg` |
| 15 (new) | (Quick Save) | Hydro2 | ✓ in a new process | ✓ | ✓ inventory | Made from slot 0 via **Save Game** → empty slot → **Save** (no name typed). Reload position = saved position exactly. `5-…`, `6-…jpg` |
| 14 | Auto-save | Rec1 | ✓ | ✓ (14.75, −322.75 → 12.42, −316.96) | ✓ inventory | Newest save (2024). Monolog positive control here (below). `7-…jpg` |
| 7 | Recreation A 5:55:14 | Rec1 | ✓ | ✓ | (inventory opened, not inspected) | |
| 8 | Engineering A 2:23:32 | eng1 | ✓ | ✓ (−10.80, −433.00 → −7.65, −444.62) | ✓ inventory | Earliest progress. `8-…jpg` |
| 13 | Quicksave | ops2 | ✓ | ✓ (−176.05, 289.70 → −180.20, 273.33) | ✓ inventory | Shotgun equipped, ammo HUD. `9-…jpg` |

Slots 1–6 and 9–12 were not loaded. The offline analysis (§4.3) puts each of them in the same group as a slot that
was tried, by the levels it contains and the findings it has.

**Script evidence.** `SS2.log` after each load has only the three `Loaded script module` lines and `failed to load
psapi.dll`, which vanilla also has. For the monolog, the test profile's `user.cfg` got `monolog monolog.txt`. Through
every load and about 15 s of play it stayed **empty**. As a positive control, slot 14 then ran the console command
`script_dump_files`, and the monolog filled with `'nvscript' 'allobjs' 'squirrel'`
(`monolog-slot14-script_dump_files.txt`). So the monolog was being written, and Squirrel/NVScript reported no
errors. Module call counts over 60 s (`SS2PORT_MODSTATS=1`): allobjs 21,558, NVScript 14,276, Squirrel 3,265.

## 4. Why SCP warns, and what else differs

### 4.1 The warning is SCP's version check, not a load failure

`DMM/SCP_beta8/sq_scripts/scpScript.nut`:

```
const MIDGAME_ACT_CHECK = true;   // check for activating or updating SCP mid-game
const SCP_VERSION = 8;            // (must match value set by earth qb trap 15)
function OnCheckMidgame() {
    if (Quest.Get("SCPVersion") != SCP_VERSION && scp.MapName() != "earth" && !IsEditor()) {
        ShockGame.TlucTextAdd("SCPMidgame", "misc", 0);  Sound.PlaySchemaAmbient(self, "hack_critical"); } }
```

SCP Beta 4's `scpScript.nut` has no `SCPVersion` at all, and none of the 15 saves' `QUEST_CMP` contains it. So Beta 8
reads 0 and shows the message (with the `hack_critical` sound) on **every** load of an old save, outside Earth. It
does nothing else: the game keeps running. Since a Beta 4 save can never have `SCPVersion = 8`, the message will come
back on every load. It means the SCP authors do not support this, not that the port failed.

### 4.2 How a save depends on the gamesys

Each `save_N/*.MIS` names its gamesys (`GAM_FILE` = `shockscp.gam`) and stores **only concrete objects**. It holds
no archetypes, and links point at archetype IDs. On this install those IDs are looked up in **Beta 8's**
`shockscp.gam`, with Beta 8's DMLs applied. Comparing the two gamesys files (`P$SymName`):

| | Beta 4 (`430b1793…`) | Beta 8 (`5c2d7f25…`) |
| --- | --- | --- |
| Named archetypes | 4,819 | 5,015 |
| Same ID and name in both | 4,815 | |
| ID now means something else | 4: −4592 Security Camera Scripts → ImmolateStimSrc, **−4590 Once Router → MatSnow**, −4059 (whitespace only), −889 Anti-ManyBoss → Broken Door | |

So SCP kept almost every archetype ID between betas, which is why the saves load at all.

### 4.3 Per-save findings (`legacy-save/save_analysis.py`, all 15 saves)

| Saves | Archetype links checked | IDs now meaning something else | Missing models |
| --- | --- | --- | --- |
| 0, 1, 2, 8, 9, 10, 11, 12 | 6,395–9,075 each | none | `crpsf_arm` (eng1, eng2), `pichappy` (earth) |
| 3, 4, 13 | ~10,200 | ops2 obj 1647 (Once Router) | same |
| 5, 6, 7, 14 | 11,066–13,235 | ops2 obj 1647; ops3 objs 1469, 1478 (Once Router) | same |

- **Once Router → MatSnow** (Operations, only once the player has been there). In ops2 the chain is
  `Ecology → Once Router 1647 → Sound Trap 1651`. In ops3 it is `Once Router 1469 → Swarmer Floor Pod 636`
  (1478 has no links left). Under Beta 8 those objects become a material archetype with no router script, so
  these chains no longer pass their signal. If they have not fired yet, the effect is predicted to be a missing
  one-shot sound in Ops 2, and a swarmer floor pod that a trigger does not start in Ops 3. This is **predicted from
  the data, not observed**, and it matters only when returning to Operations. Beta 8 moved Once Router to −4603.
- **Missing models.** 99 of the 102 model names in the saves resolve in the Beta 8 install (SCP obj/mesh, vanilla
  `OBJ.CRF`/`MESH.CRF`, `patch/`, `patch_ext.crf`). `fx_particle` is the engine's particle model, not a file. Two names
  have no file:
  - `crpsf_arm`: an **SS2 Rebirth** corpse model, on objects in Engineering 1 and 2.
  - `pichappy`: a **Beta 4** Earth model that Beta 8 no longer ships. Earth cannot be revisited.

  Objects with a missing model draw as nothing or as a placeholder. This was not looked at in game.

### 4.4 The other 16 mods are not installed

The PC setup's `mod_path` is SCP_beta4, SS2_Rebirth_v05a, SHTUP-ND_beta1, thelovebat-SS2-altMusic,
Vurt_Env_map_for_SS2_water, Vurt-Goo, Vurt's SS2 Water v5.1, Vurt's SS2 Flora Overhaul 1.0k, Vurt's Pool Water, Vurt's
Hi-Res Space, SS2_Vaxquis_VintageSongRemake, ss2_quest_note_notifier, SS2 Black Egg 1.02, obj_fixes_v12,
Kolya-Crosshair, Tacticool_complete_v1.05, AccFam_20, patch_ext. `ss2scp` has only SCP Beta 8 + patch_ext. With the
old saves this means:

- **Art, sound and music mods** (Rebirth, SHTUP, Vurt's, Tacticool, Black Egg, AccFam, music, crosshair) are replaced
  by SCP/vanilla assets, apart from `crpsf_arm` above. Nothing in the saves needs them.
- **Gamesys DMLs** (obj_fixes, Flora, quest notifier) are not applied. They change archetypes, so the archetypes now
  behave as in Beta 8. No saved object points at an archetype that only a DML created: every ID resolves in Beta 8.
  SS2 Rebirth's `gamesys.dml` is fingerprinted to Christine's FMs only, so it was never applied on the PC either.
- **ss2_quest_note_notifier** puts `zbQuestNotifier` on "The Player" through the gamesys. That script is not loaded
  now, so there are no quest pop-ups. No error results.
- `NoGunDegrading` is in `DMM/` but was not in the `mod_path`.

### 4.5 NewDark 2.47 → 2.48

The save format itself is fine. The recompiled 2.48 engine reads the 2.47 saves without any conversion, and it writes
new saves that it reads back. No save was converted, repaired or rewritten. Slot 15 is a new save made by 2.48.

## 5. Not verified

- Level transitions (elevators, Rec → later decks), and the levels these saves have not visited yet (they load
  from Beta 8's `.mis` with Beta 4-era quest state). This is where SCP's "not supported" is most likely to show.
- Quest progress, keycards, audio-log/email state, and AI behaviour over time.
- The Operations router chains (§4.3) and the `crpsf_arm` corpse in Engineering, in game.
- Slots 1–6 and 9–12 in game (analysed offline only).
- Visual parity with the PC setup (Rebirth/Vurt art is gone by design). The Recreation plants look very dark in
  the captures, but there is no PC reference to compare with.
- Touch-only play. All input here was scripted.
- Sessions longer than about a minute per load. The 13 runs total about 11 minutes of process time, menus included.

## 6. Recommendations (each is the user's decision; none was done)

1. **To keep playing the old campaign on the phone now:** use the saves as they are, with the warning, accepting
   the unknowns in §5. No code change is needed to make this a normal game folder. Copy `games/ss2scp` to a second
   adb game folder, for example `games/ss2scp-legacy` (664 MB, `adb shell cp -r` + `chmod -R a+rX,go-w` as in
   PHASE3B §7), and rename `profiles/legacy247-test` to `profiles/ss2scp-legacy`. **Game folder** then switches between
   it and `ss2scp`, and the two saves never mix. Remove the `monolog` line from that `user.cfg` first.
   Save often into new slots, and keep the PC originals as the master copy.
2. **For an SCP-supported game:** start a new game in `ss2scp` (SCP's own requirement since it added the version
   check).
3. **To reproduce the PC setup exactly** (SCP Beta 4 + the 16 mods) on Android, the port would have to run Beta 4's
   NVScript 1.2.4 and Squirrel 1.01. Today it runs only the modules it was recompiled from (`modules.c`), so that
   means recompiling those two modules: a development task, not a configuration change. The 2.47 engine itself is
   not needed (§4.5).
4. A cheap, low-risk launcher addition, if wanted: let a game folder use a different profile name, so option 1
   needs no 664 MB copy. Not started.

## 7. Phone state afterwards

- `files/profiles/legacy247-test/` **is kept** for inspection. It holds the 15 copied slots (unchanged), the new
  `save_15`, `current/`, `SS2.log`, `monolog.txt`, `cam.cfg`, and `user.cfg` (+ `monolog monolog.txt`). It is not a
  game folder, so the launcher does not list it. Remove it with
  `adb shell run-as local.newdarkandroid.ss2 rm -rf files/profiles/legacy247-test`.
- Everything else is as before (§2.5): installed APK `2a5ad353…`, `ss2scp` selected, `env` empty, both profiles and
  touch settings unchanged, `Download/System Shock 2` unchanged. `files/results/history.txt` gained the 13 test
  runs.

## 8. Reproduce

```sh
P=local.newdarkandroid.ss2; D=/data/user/0/$P/files
# test profile: SCP Beta 8 config + verbatim copies of the PC saves
adb shell "mkdir /data/local/tmp/ls && cp -r '/storage/emulated/0/Download/System Shock 2'/save_* /data/local/tmp/ls/ && chmod -R a+rX /data/local/tmp/ls"
adb shell "run-as $P sh -c 'mkdir files/profiles/legacy247-test && cp files/profiles/ss2scp/user.cfg files/profiles/ss2scp/cam.cfg files/profiles/legacy247-test/ && cp -r /data/local/tmp/ls/save_* files/profiles/legacy247-test/'"
# scripted run (half-size capture coordinates; slot rows at y = 68 + 20.3*N, Load at 870,216)
python docs/android-audit/phase3b/scp_script.py s.txt wait:5000 esc wait:1500 esc wait:1500 esc wait:2000 loadgame wait:1500 \
  menu:600:68 wait:500 menu:870:216 wait:20000 pos shot w:2500 pos shot frob wait:2000 shot tap:tab wait:1000 shot quit
adb push s.txt /data/local/tmp/s.txt && adb shell "chmod 644 /data/local/tmp/s.txt; run-as $P cp /data/local/tmp/s.txt files/s.txt; run-as $P mkdir -p files/shots"
adb shell am start -n $P/.LauncherActivity --es autorun game --es game ss2scp --ei frames 0 \
  --es env "SS2PORT_WRITE_DIR=$D/profiles/legacy247-test,SS2PORT_INPUT_SCRIPT=$D/s.txt,SS2PORT_SHOT_DIR=$D/shots"
# afterwards: clear the kept env, or restore shared_prefs/runs.xml from a backup
# offline analysis (reads only)
python -I docs/android-audit/legacy-save/save_analysis.py <SCP_beta4>/shockscp.gam <SCP_beta8>/shockscp.gam build/scp/game-scp <folder with save_N>
```

Files in `docs/android-audit/legacy-save/`: captures `1-…` to `9-…jpg`, `player-pos.txt`, `modstats-slot14.txt`,
`monolog-slot14-script_dump_files.txt`, `source-saves.per-slot.sha256.txt`, `save_analysis.py` and its output
`save_analysis-output.txt`.
