# System Shock 2 Community Patch (SCP) on the portable host

Phase 0 audit, 2026-10-17. Nothing here has been tested on the portable host. **SCP is not claimed to work.**

## 1. SCP version examined

| Version | Status | Source |
| --- | --- | --- |
| Beta 8 | **Reported, not verified.** Search-engine summaries of the official systemshock.org topic list "Beta 8 – December 2025" (`SCP_beta8.7z`, about 37 MB) and say it incorporates "Olfred's Fixed Objects v15". The official page (systemshock.org topic 7116) and ttlg.com sit behind a Cloudflare check that this audit did not try to get past. | Unverified |
| Beta 7 | Released 2025-04-02 (confirmed). | Steam discussions, ModDB comments (`https://www.moddb.com/mods/system-shock2-community-patch`) |
| **Beta 6** (2024-08-11) | **Examined in detail.** A local copy exists inside the 25th Anniversary Remaster's `mods\scp.kpf`, read-only (`modinfo.json` "Beta 6"; `doc/readme.txt` "Beta 6 - August 11, 2024"). This is the Remaster packaging; its game data should match the NewDark release, but its native-module packaging differs (see §2). | Local, `<local path>` |
| Beta 4 (2017-10-31) | Present but inactive in the local NewDark install (`DMM\SCP_beta4`, with readme PDF). | Local |

**Action for the project owner:** download the current SCP release (Beta 7, or Beta 8 if it exists) from
systemshock.org in a normal browser and place it in the PC install. The component analysis below should then be
re-checked against that release's readme. The structure has been stable from Beta 4 to Beta 6.

## 2. SCP component inventory and actual dependencies (from local Beta 6 and Beta 4 files)

| Component | Content | Evidence |
| --- | --- | --- |
| Own gamesys | `shockscp.gam`. SCP missions name it as their gamesys. | Beta 6 `.mis` files |
| Missions | Replacement `earth.mis` … `shodan.mis`, plus `HYDRO2.DIF`, `SHODAN.DIF` | Package listing |
| Archetypes / properties | Inside `shockscp.gam` | — |
| Squirrel | `sq_scripts\scpScript.nut` (≈208 KB in Beta 6, dozens of `scp*` classes). Missions are converted to Squirrel control, e.g. "Converted decon shower to Squirrel script control". | Beta 6 readme, file size |
| **NVScript (native)** | `shockscp.gam` references **62 distinct `NV*` script classes**, e.g. `NVCreateAndLink` ×23, `NVRelayTrap` ×18, `NVCreateAndLinkLinkData` ×16, `NVRelayTrapTDest` ×11, `NVSchema`, `NVPhantomTrap`, `NVImplant`. `earth.mis` lists the script modules `allobjs`, `nvscript`, `squirrel`. Beta 4 shipped `NVScript.osm` 1.2.4.0. The Beta 6 Remaster package ships no `.osm`. For NewDark, the readme instead tells users to run SS2Tool in Update mode "to ensure you have the most current versions of all scripts". Inference: NewDark SCP Beta 6+ uses SS2Tool's `osm\NVScript.osm`. | String scan of local `shockscp.gam`/`earth.mis`; Beta 6 readme lines 68-72 |
| Assets | `obj` (813 files), `fam`, `mesh`, `snd`, `snd2`, `song`, `strings`, `intrface`, `iface`, `bitmap`, `book`, `material`, `objicon`, `mschema` (with its own `motiondb.bin`) | Package listing |
| DML | SCP itself works mainly through gamesys and missions. SS2Tool's `patch_ext\gamesys.dml` loads `#script "NVScript"` and `#script "Squirrel"` and skips itself when SCP is present (`FINGERPRINT { !NAME -4755 SCP }`). SS2Tool's `misdml\*.mis.dml` also uses NVScript scripts. | Local install |
| Config | Optional `user.cfg` variables `lower_spawn_min`, `raise_spawn_rand`, `mult_spawn_max`, `mult_spawn_period`. SCP sets qvar `scplevel=1` for other mods' fingerprinting. | Beta 4/6 readmes |
| Required NewDark | "patched to version **2.48** (NewDark) or higher … use SS2Tool 6.1 or higher" (Beta 6 readme l. 68-72). Beta 4 needed only 2.46. | Local readme |
| Install method | A standard mod through the Dark Mod Manager (or Blue's Mod Manager), at the lowest priority. | Beta 6 readme l. 74-78 |
| Incompatible | "Major gameplay overhaul mods (SecMod, System Shock Infinite, etc)". Probably compatible with graphics, sound and small gameplay tweaks. Handles SHTUP load order and the latest Vurt flora. | Beta 6 readme l. 80-82 |
| Self-check | "If any of the script modules required by SCP fail to load, this will be detected at game start and you won't be allowed to proceed". Concretely, a message appears in front of the first gravshaft on Earth and the player is blocked. | Beta 6 readme l. 88-89 and release notes l. 1037-1039 |

### Local PC prerequisites found

- The only classic install (`<LaunchBox install>`, GOG + SS2Tool 6.1.1.x + Blue's Mod
  Manager) runs **NewDark 2.47** (`ss2.log`: "System Shock 2 Patch Final 2.47"; `ss2.exe` SHA-256 `4199aac1…5a55`).
  NewDarkRecompiled is built and verified for **2.48** only, and SCP Beta 6+ requires 2.48. **A 2.48 install is a
  prerequisite for both the vanilla baseline and SCP.** Where to obtain an updated classic 2.48 build (GOG or Steam)
  is the owner's call. Per PCGamingWiki, the classic version is now sold only bundled with the 25th Anniversary
  Remaster.
- All mods in that install are currently inactive (`ss2bmm.log`, 2026-10-13). A stale SCP save (`save_14`) exists;
  `ss2.log` shows the engine warning when it is loaded without SCP. No game files were changed by this audit.

## 3. Compatibility assessment per component

Categories: **(1)** confirmed compatible from evidence, **(2)** likely compatible, untested, **(3)** needs a
portable-host fix, **(4)** needs a larger implementation, **(5)** unsupported.

| SCP component | Category | Reasoning |
| --- | --- | --- |
| `shockscp.gam`, missions, `.DIF` | **(2)** | Loaded by recompiled engine code through the VFS (MOD_COMPATIBILITY.md §2). |
| Assets incl. `mschema\motiondb.bin` | **(2)** | Engine resource path. |
| `scpScript.nut` | **(2)** | Recompiled `Squirrel.osm`. Needs arm64 setjmp fix B1 for Squirrel compile errors. SS2Tool may install a newer `squirrel.osm` than upstream verified. The build lifts the user's own file, so identity matches, but lifter verification of that exact version is not established. |
| **NVScript.osm** | **(4) — confirmed blocker for SCP today** | Not in the recompiled-module list (`port/build.py:35-36`). `LoadLibrary` fails (`port/win32/kernel32.c:322-327`), so 62 script classes are missing and SCP's own check blocks progress on Earth. Fix: lift and verify NVScript.osm as a fourth recompiled module (MOD_COMPATIBILITY.md §3). The PE header looks favourable: MSVC 2008, static CRT, relocatable, same profile as `squirrel.osm`. |
| SS2Tool `patch_ext` DMLs | **(2)**, and also need NVScript | They reference NVScript scripts, so a vanilla-plus-SS2Tool baseline has the same dependency. Gamesys DML loading is confirmed on the portable host (Vita, `port/backends/vita/PERF.md`). |
| `user.cfg` spawn variables | **(2)** | Engine config. Writes go to the write directory. |
| DMM/BMM layout (`DMM\SCP_*`, relative `mod_path`) | **(2)** | Relative paths only (MOD_COMPATIBILITY.md §4). Check the copied `cam_mod.ini` for absolute paths. |
| Performance | Unknown | The readme notes "more resource-intensive scripting" and improved lighting. The Vita found script and AI work is not the bottleneck there (`PERF.md`). This needs measurement on the Pixel. |

**Overall: SCP is likely to work with the portable host once NVScript.osm is recompiled. Until then it is blocked by
design** (SCP's own module check). There is no indication that SCP needs any other native module, a modified
executable, or a non-Indeo cutscene format.

## 4. Save-game considerations

- SCP: "SCP will not work with games started in any previous version of SS2, including previous versions of SCP.
  You will need to start a new game" (Beta 6 readme l. 85-87). Activating it mid-game shows a warning.
- The engine itself detects a gamesys mismatch. The local `ss2.log` shows "loading a savegame made with
  'shockscp.gam' but current gamesys is 'shock2.gam'".
- **Requirement for the port:** start a **fresh SCP game** for SCP testing. Never load vanilla saves with SCP active,
  or the reverse.
- Saves made on the Windows PC with the *same* engine, SCP version and module set are in principle the same format
  (same recompiled engine code). Moving them is **untested** and is not part of the plan. No conversion or repair is
  proposed.
- Keep saves for different mod sets in separate write directories (one Android "profile" per installed game
  folder). This is cheap with `SS2PORT_WRITE_DIR`.

## 5. Targeted test plan

Run after the vanilla baseline (IMPLEMENTATION_PLAN.md Phase 2) is stable.

1. **Vanilla baseline:** an unmodded 2.48 install (no SS2Tool `patch_ext`, or NVScript recompiled). Main menu, new
   game, Earth → Station → MedSci 1, one save/load, one level transition.
2. **Clean SCP install on the PC:** SCP at the lowest priority via DMM/BMM, SS2Tool Update mode, verified playable
   on Windows. Then rebuild the APK **from that exact install** and copy the folder to the phone.
3. **Fresh SCP game.**
4. **Positive SCP checks (behaviour, not just start-up):**
   - **Script-module gate:** you can pass the first gravshaft on Earth. A block message means NVScript or Squirrel
     failed to load.
   - Earth: items cannot be carried out of the training booths. The UNN recruitment centre has front doors and
     furniture. The Happy Noodle has props and a seated customer. Keypad 266 no longer opens with "00000". A message
     shows when the secret area is found.
   - Station: the "Career Advisor" computer (obj 763) and its floor path are present. The mission-gate doors in the
     training niches open automatically. The training droid salutes.
   - MedSci 1: the radar-dish impact shakes the player, flashes and burns out lights 947/878. A force-field sound
     plays in the first room. There is a broken door in the cryo-pod room. Help text appears in the PDA.
   - Systemic: a cigarette gives +1 PSI and makes a coughing sound (Beta 6 readme l. 620, 901). Med beds clear
     toxins/radiation at full health (l. 567). The pistol and assault rifle do less damage (l. 617). Compare the
     numbers against a vanilla run.
5. **Resource loading:** `SS2.log` shows no missing `obj`/`mesh`/`snd` errors. The port log shows no
   `not found (...)` lines for SCP paths (`port_miss`, `host.c:120`).
6. **Scripting:** `SS2.log` shows `Loaded script module "nvscript.osm"`, `allobjs.osm` and `squirrel.osm`, plus no
   Squirrel compile errors (`scpScript.nut`). The port log shows `NVScript.osm: loaded the recompiled module`
   (`modules.c:96`).
7. **Inventory and item interactions:** pick up, combine, use and drop. Research an item. Use a hypo. Hack a keypad.
8. **Save and reload:** quicksave and full save, reload in the same session and after an app restart.
9. **Level transitions:** Earth→Station→MedSci 1, and back to a previous deck (state persistence).
10. **Logs:** review `SS2.log`, the port log (logcat plus file) and `--verbose` output for `LoadLibraryA(...): not
    available`, `ABI VIOLATION` (if `--abi-check`) and guest faults.

## 6. Blockers, uncertainties and priorities

| # | Item | Type | Priority |
| --- | --- | --- | --- |
| 1 | NVScript.osm is not recompiled | Confirmed blocker (implementation) | **Highest for SCP** (Phase 4, first task) |
| 2 | The local install is 2.47; 2.48 is required | Prerequisite (owner action) | Before Phase 1 lifting |
| 3 | arm64 setjmp (B1) affects Squirrel error handling | Confirmed toolchain blocker | Phase 1 |
| 4 | Current SCP release (Beta 7/8) not inspected | Uncertainty | Before Phase 4 |
| 5 | SS2Tool may install a newer `squirrel.osm` than upstream verified | Uncertainty | Phase 4 (run the upstream harness on it) |
| 6 | Absolute paths in `cam_mod.ini` | Possible host fix | Only if seen |
| 7 | SCP performance on the Pixel | Unknown | Phase 5 |
