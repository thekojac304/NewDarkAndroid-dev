# Legacy SCP Beta 4 on the NewDark 2.48 Android port: feasibility audit

2026-10-18. Investigation only: no engine, launcher, APK, game folder, save or setting was changed. Branch
`android-port` at `094e55a`. The installed APK is `0.8.1-import-fix-play` (`2a5ad3535a2586b9e16f7c67c5f86d08e204e9513d6d71f8e8f502d9c988efb6`), and the selected game folder is `ss2scp`. Builds on
LEGACY_SAVE_COMPATIBILITY.md (2.47 saves under SCP Beta 8).

Labels: **Verified** (checked here, from files or runs), **Strongly supported** (several independent facts agree; not run),
**Plausible but unverified**, **Unknown**, **Incompatible**.

## 1. Executive verdict

**Feasible, with one native module to recompile. The original SCP Beta 4 environment is not runnable with
today's APK.**

- The original campaign's data is consistent with itself and complete. This is **Verified** statically for all 15
  saves. All 143,082 saved object→archetype links resolve in SCP Beta 4's own gamesys. Every script the saves and
  Beta 4 use comes from `allobjs.osm`, NVScript 1.2.4 or the installation's own Squirrel files (the exceptions behave
  the same as on the PC, §5.3). Every save declares exactly `allobjs`, `nvscript`, `squirrel`. Once the original Beta
  4 gamesys and mod set are used, the Beta 8 problems from the previous test disappear: the mid-game warning, the
  reused "Once Router" ID and the missing `crpsf_arm`/`pichappy` models.
- SCP Beta 4 states its own requirement as "NewDark **2.46** or higher" (**Verified**, its readme). NewDark's 2.48 notes
  say that older savegames should load and list no save-format change (**Verified**, release notes). The port already
  loaded these exact 2.47 saves (**Verified**, previous test).
- **The blocker:** SCP Beta 4 runs on **NVScript 1.2.4** (`abed4ee2…`), and the APK contains only SCP Beta 8's NVScript
  (`f15fc91b…`). The host refuses any other bytes (`port/core/modules.c:103`, `:120`), so with today's APK SCP Beta 4's
  own module gate stops the player (**Incompatible today**). NVScript 1.2.4 looks like an easy recompile. It was built
  with MSVC 2008 like the Squirrel module that is already recompiled. It has no TLS and no SSE instructions, and
  every import already has a host shim (**Verified** statically). The recompile itself has **not** been attempted.
- **Squirrel:** under 2.48 the right module is 2.48's own `squirrel.osm` 1.02, which is already recompiled, and not
  2.47's 1.01. NewDark 2.48 ships 1.02 with one documented fix that the legacy scripts do not use (**Strongly supported**).
  `allobjs.osm` and `lgvid.dll` are byte-identical in both installs (**Verified**).
- **Original saves can stay byte-for-byte unchanged** (**Strongly supported**). Nothing in this path needs a save edit,
  a quest-variable change or a warning suppression.

**Safest next step:** recompile NVScript 1.2.4 with the existing `--nvscript` build option, into a **separate,
side-by-side APK** (its own application ID and storage). Then run the validation in §9 against an isolated copy. The
accepted APK and the SCP Beta 8 setup stay untouched (§8).

## 2. Preservation checks (done first)

| Item | State |
| --- | --- |
| Git | `android-port`, HEAD `094e55a8e31d2ce1391b0b46dd0252ca92255496`. Untracked: the previous session's `LEGACY_SAVE_COMPATIBILITY.md` and `legacy-save/` (committed together with this report) |
| Installed APK | `0.8.1-import-fix-play`, `base.apk` sha256 `2a5ad353…efb6` = `build/android/baseline/import-fix/app-play-debug-import-fix.apk`; that folder's `SHA256SUMS` verify (both APKs OK) |
| Launcher state | `game` = `ss2scp`, `env` empty (restored after the previous test) |
| Phone source `Download/System Shock 2` | 6,624 files, all hashes equal to the list taken before the previous test (`build/android/legacy-save-test/source-full-tree.sha256`) |
| `profiles/ss2`, `profiles/ss2scp`, `touch-settings.cfg` | Unchanged against `app-data-before.sha256` (28 files). The backup tar is present (15.1 MB, 41 entries) |
| `games/ss2scp` | Hashed (`build/android/legacy-save-test/ss2scp-tree.sha256`, 1,583 files) |
| **PC original** `<LaunchBox install>` | Read only. **All 6,624 files are identical to the phone copy** (path and sha256; `pc-original-tree.sha256`), including the 161 save files |
| Test profile from the previous session | `profiles/legacy247-test` still exists. **Its `save_15` was written under SCP Beta 8 and must never be copied into a Beta 4 profile** |

Nothing expected was missing. Every file this audit used was read from the phone with `adb pull`, read from `E:`, or
taken from `build/scp/`.

## 3. The original installation

### 3.1 Engine and configuration (Verified)

| | Value | Source |
| --- | --- | --- |
| Engine | NewDark **2.47**, `ss2.exe` 5,116,416 bytes, `4199aac1d9af1e41…` | `ss2.log` "System Shock 2 Patch Final 2.47" |
| Search paths | `resname_base .\+.\Data+.\Data\res`, `load_path .\+.\misdml+.\Data`, `script_module_path .\+.\Data`, `uber_mod_path osm+ubermod` (no `ubermod` folder) | `install.cfg`, `cam_mod.ini`, `ss2.log` |
| Object ID space | `obj_min -6144`, `obj_max 8000`, `max_refs 32768`. These are the same in the 2.48 install, and the saves' `ObjVec` is −6144…8000 | `cam_ext.cfg`, saves |
| Gameplay-relevant extras | `phys_freq 60` (fixed 60 Hz physics; 2.48 default 0 = off), `new_mantle`, `shock_quicksave_slot 13`, `shock_autosave_slot 14` | `cam_ext.cfg` |
| Rendering extras (visual only) | `postprocess 1`, `d3d_disp_enable_hdr 32`, bloom settings, `multisampletype 8`, `d3d_disp_sw_cc`, ATOC 2. `cam.cfg`: 2560×1440 full screen | `cam_ext.cfg`, `cam.cfg` |
| `User.cfg` | `safe_texture_manager`, `check_rooms`, `no_endgame`, `monolog`, editor lines | file |

### 3.2 Enabled mods and load order (Verified)

`mod_path`, highest priority first (`cam_mod.ini` = `ss2bmm.log` = `ss2.log`). "Enabled" means named in `mod_path`,
not merely present in `DMM\`:

| # | Mod | Kind | What it changes |
| --- | --- | --- | --- |
| 1 | **SCP_beta4** | **Gameplay + scripts** | `shockscp.gam` (`430b1793…`), 24 missions, `scpScript.nut` (`a8e6cce7…`), `NVScript.osm` 1.2.4, assets |
| 2 | **SS2_Rebirth_v05a** | Models + **mission DMLs** | `obj`/`mesh`; `earth/eng1/eng2/hydro1/ops1.mis.dml`; `gamesys.dml` is fingerprinted to Christine's FMs only (never applies) |
| 3 | SHTUP-ND_beta1 | Textures/interface | replacement only |
| 4 | thelovebat-SS2-altMusic | Music | replacement only |
| 5–7, 9, 10 | Vurt: water env map, Goo, Water 5.1, Pool Water, Hi-Res Space | Textures | replacement only |
| 8 | **Vurt's SS2 Flora Overhaul 1.0k** | Models + **gamesys and mission DMLs** | `gamesys.dml` removes `RenderAlpha` from 3 archetypes; 9 mission DMLs (some fingerprinted `QVAR "scplevel" == 1`) |
| 11 | SS2_Vaxquis_VintageSongRemake | Music | replacement only |
| 12 | **ss2_quest_note_notifier** | **Script** | `gamesys.dml` adds Squirrel script `zbQuestNotifier` to "The Player"; `questnot.nut` |
| 13 | SS2 Black Egg 1.02 | Models | replacement only |
| 14 | **obj_fixes_v12** | Models + **gamesys DML** | `RenderAlpha` on 4 bottles, pipe-debris rest axis, wedge-light render type |
| 15 | Kolya-Crosshair-Hazard-Amber | Interface | replacement only |
| 16 | Tacticool_complete_v1.05 | Weapon models | replacement only |
| 17 | AccFam_20 | Textures/models | replacement only |
| — | **patch_ext** (SS2Tool, base path) | **Gamesys DML + script** | 554-line `gamesys.dml` (`99a2d2c7…`) with `#script "NVScript"`/`"Squirrel"`; `sq_scripts/allobjs.nut`; models |
| — | `misdml\` (SS2Tool, in `load_path`) | Mission DMLs | 24 files, all fingerprinted by object positions |
| — | NoGunDegrading | — | **Not enabled** (in `DMM\`, not in `mod_path`; BMM: "Unrecognized") |

Hashes of 115 critical files (executables, modules, gamesys, scripts, DMLs, configs, SCP missions, `.crf`):
`legacy-scp4/original-install-critical-files.sha256`. Load order evidence: `legacy-scp4/mod-load-order.txt`.

### 3.3 Is this the set the saves were made with?

Blue's Mod Manager wrote this set on **2026-10-18 11:06**. The Phase 0 audit found all mods inactive on 2026-10-13.
So the current `mod_path` is a re-activation, not a record of 2023. The saves themselves show what was active when
they were made (`scp4_static_audit.py` §4):

| Evidence in the saves | Conclusion | Label |
| --- | --- | --- |
| All saves name `shockscp.gam`; every archetype link resolves in Beta 4's copy | SCP Beta 4 | Verified |
| Earth obj 885 is at (−108.25, 30.44, 62.95) in all 15 saves; Beta 4's `earth.mis` has (−108.05, 30.0, 61.95); Flora's `earth.mis.dml` sets the former | Vurt's Flora active from the start | Verified |
| Engineering obj 1582 uses `crpsf_arm` in all saves; Beta 4's `eng1.mis` sets no model; Rebirth's `eng1.mis.dml` sets `crpsf_arm` | SS2 Rebirth active when Engineering was first entered | Verified |
| `zbQuestNotifier` state appears in saves from **2023-04-09** on (slots 0–7, 12–14) but not in slots 8–11 (2023-04-07) | The Quest Note Notifier was **added mid-campaign**, between 2023-04-07 10:49 and 2023-04-09 22:39 | Strongly supported |
| The 2026-10-18 PC session (`ss2.log`): the three modules load, "OSM: SQUIRREL> Quest Note Notifier initialized", no gamesys-mismatch line | The current set loads at least one save without complaint on 2.47 | Verified (which save is unknown) |

Pure replacement mods leave no state in a save, so whether one of them was added or removed later is **Unknown**, and
also harmless for compatibility. Whether any gameplay mod was active in 2023 and later removed is **Unknown**. Nothing
in the saves points to one: every archetype and script resolves.

## 4. NewDark 2.47 → 2.48 and SCP Beta 4

| Question | Finding | Label |
| --- | --- | --- |
| SCP Beta 4's engine requirement | "patched to version 2.46 (NewDark) or higher … use SS2Tool 6.1 or higher"; needs NVScript and Squirrel, and checks them at game start (readme PDF, "Requirements and Installation") | Verified |
| SCP Beta 4 and saves | "will not work with games started in any previous version of SS2, including previous versions of SCP". Our saves were started under Beta 4 (Earth obj 885 and the gamesys show it), so this does not apply | Verified |
| Older savegames on 2.48 | "Generally loading savegames saved with an older version of the game should work" (2.48 `release_notes.txt`, Installation). The 2.48 change list has no save-format change. Its save-related items are fixes ("original bug in some loading code … when loading savegames or during level transitions in SS2"; wrong texture palette after loading) | Verified (documentation) |
| 2.47 saves on the 2.48 port | All 15 listed, 6 loaded and played, save/reload worked (with Beta 8 data) | Verified (LEGACY_SAVE_COMPATIBILITY.md) |
| Script API | 2.48: Squirrel `SendMessage` data3 fix; corrected docs; `ObjRaycast` parameter "backward compatible"; new functions only. Beta 4's only `SendMessage` (`scpScript.nut:808`) passes no data; `questnot.nut` and SS2Tool's `allobjs.nut` use only `PostMessage` | Strongly supported |
| DML parsing | 2.48 adds `Create`/`CreateArch`. None of the five legacy gamesys DMLs creates archetypes. The DML reference `dbmod-sample.dml` is byte-identical in both installs (`6687f31a…`), so fingerprint rules are unchanged | Verified (files); behaviour Strongly supported |
| DMLs and savegames | "mission dbmods are not loaded for savegames … concrete object data is retrieved from the savegame" (`dbmod-sample.dml:155`). Only **gamesys** DMLs affect a loaded save. Mission DMLs act on levels not yet visited | Verified (documentation) |
| SS2Tool's `patch_ext\gamesys.dml` with SCP Beta 4 | Its guard is `FINGERPRINT { !NAME -4755 SCP }`. Archetype −4755 is "SCP" in **Beta 8** but **does not exist in Beta 4**, so in the original setup this 554-line DML **was applied on top of SCP Beta 4**. A faithful reproduction must keep the legacy `patch_ext` | Verified (data); runtime outcome Strongly supported |
| Quest variables | Beta 4 has no `SCPVersion` check (none in its `scpScript.nut`), so Beta 8's mid-game warning cannot appear. Option A changes no quest variable | Verified |
| Configuration | The legacy `cam_ext.cfg` carries over. Only display settings need a phone value (resolution 2560×1440 → e.g. 1280×720, in the profile, as for `ss2scp`) | Strongly supported |
| Anything that runs only on 2.47 | None found | Strongly supported |

**Can Beta 4's content run unmodified under 2.48?** Yes as far as static evidence goes: no data file needs changing.
The only things that differ from the PC are the engine executable and Squirrel module (2.48's) and the
recompilation itself (§6). **Strongly supported; not yet run.**

## 5. Gamesys, scripts and saves

### 5.1 Which gamesys the saves expect (Verified)

Each `save_N/*.MIS` names `GAM_FILE shockscp.gam` and holds only concrete objects (no archetypes). The engine loads
`shockscp.gam` through the search path, applies the **gamesys** DMLs, and then applies the save on top.

| Gamesys used at load | Links checked (15 saves) | Unresolved | Meaning changed |
| --- | --- | --- | --- |
| **SCP Beta 4** `shockscp.gam` (`430b1793…`, 4,819 named archetypes) | 143,082 | **0** | — |
| SCP Beta 8 `shockscp.gam` (`5c2d7f25…`, 5,015) | 143,082 | 0 | **15** links (3 objects × slots 3–7, 13, 14): ops2 1647, ops3 1469/1478 "Once Router" → "MatSnow" |

**Restoring the original Beta 4 gamesys removes every known archetype mismatch** (Verified). The gamesys DMLs (obj_fixes,
Flora, quest notifier, SS2Tool) change only properties of existing archetypes and create none (Verified), so no saved
ID depends on DML ordering.

### 5.2 Models (Verified)

Under Beta 8 two saved model names were missing. Both exist in the original set: `crpsf_arm` is in
`DMM\SS2_Rebirth_v05a\obj\` and `pichappy` in `DMM\SCP_beta4\obj\`.

### 5.3 Scripts (Verified statically)

324 distinct script names are used by the saves, Beta 4's gamesys and missions, and the quest notifier. Matched
case-insensitively, as the engine does:

| Provider | Names |
| --- | --- |
| `allobjs.osm` (identical in both installs) | 249 (+2 also in SS2Tool's `allobjs.nut`, +1 also in NVScript, +2 also in Public Scripts) |
| NVScript 1.2.4 | 33 (+1) = **34 NV classes**, e.g. `NVCreateAndLink`, `NVRelayTrap`, `NVMetaTrap`, `NVUpgradeCheatStopper`, `NVGuaranteedLoot`, `NVImplant` |
| Squirrel: Beta 4 `scpScript.nut` | 26 |
| Squirrel: `questnot.nut` | 1 (`zbQuestNotifier`) |
| No loaded provider | 10. Six are also used by **vanilla** `shock2.gam` (BrokenCookie, Charm, CutsceneRoom, PullProjectile, TrapCutscene, TrapMedia). `AnimLight` and `TrapDestroy` exist only in Public Scripts `script.osm`, which the original never loads. `stdelevator` and `tweqtrap` exist nowhere. **All 10 are equally inert on the PC original**, so this is parity, not a gap |

**Declared modules:** 165 save/mission files list `allobjs, nvscript, squirrel`, and 3 (Beta 4's `rick2`, `rick3`,
`shodan`) list `allobjs, nvscript`. None lists Public Scripts or any other module. The PC log loads exactly these three.

**Script state.** Saves store per-object script data (`SCRIPTSTATE`). Save 14 alone has about 1,456 entries for
NVScript classes and 300 for SCP's Squirrel classes (top: `NVUpgradeCheatStopper` 510, `NVRelayTrap` 134,
`NVMetaTrap` 122). This state was written by NVScript 1.2.4. Reading it back with the **same** NVScript keeps the
data contract intact (Strongly supported). With Beta 8's NVScript it is **Unknown**: all 34 class names exist there
too, but its data keys and formats were not compared.

### 5.4 Save risks that remain even with the original mod set

| Risk | Label | Notes |
| --- | --- | --- |
| Recompiled NVScript 1.2.4 behaving differently from the native module | Unknown | The lifter's verification (difftest, conformance) covers the method, not this binary. This is the main technical risk |
| C++ exceptions inside NVScript 1.2.4 | Unknown | The host ends the game on `RaiseException`. Like Beta 8's build, the static CRT contains the exception machinery. Where script code can reach it is decided when the module is lifted (as in PHASE3A §3) |
| 2.48 engine vs 2.47 in the 2.48-fixed areas (save loading, level transitions, voices of destroyed AI) | Plausible but unverified | These are fixes. Behaviour can differ from what the player saw on 2.47, in the intended direction |
| x87/float and timing differences of the port vs native x86 | Plausible but unverified | `phys_freq 60` fixes the physics step, which helps. AI and physics are not bit-identical across platforms |
| Unknown 2023 mod changes beyond §3.3 | Unknown | Only the quest notifier change is visible. It adds a player script, which initialises harmlessly on older saves |
| SCP Beta 4's own known issues (its readme: e.g. objects dropped in the Rec↔Command elevator don't transition) | Verified (documented) | Same as on the PC |
| Rendering extras (HDR, bloom, post-process, 8× MSAA, sw colour correction) on Android GPUs | Plausible but unverified | Visual only. The port translates the game's pixel shaders (bloom matched native Direct3D on desktop, `port/README.md:217`) |
| Using `legacy247-test/save_15` | Incompatible | Written under Beta 8 data. Never mix it in |

## 6. Native modules

`scp4_static_audit.py --pe` and `pefile`/`capstone` scans:

| Module | Original (2.47 install) | In the APK (from the 2.48 + Beta 8 install) | Same? |
| --- | --- | --- | --- |
| `Data\allobjs.osm` | `096c3a1e…a645`, linker 4.20 (1999), base 0x10000000, 0x8B000 | same file | **Yes** (Verified) |
| `lgvid.dll` | `64d55311…4eec`, linker 9.0, 0x9000 | same file | **Yes** (Verified) |
| `osm\squirrel.osm` | **1.01**, `50f569ad…26f0`, 2018-04-06, MSVC 2008 (linker 9.0), static CRT, relocs, no TLS, 0x29F000, 112 imports | **1.02**, `d95b4415…3dc4`, 2018-10-30, same toolchain, 0x2A0000, **identical import list** | No. Use 1.02 (below) |
| `NVScript.osm` | **1.2.4**, `abed4ee2…6a9a`, 2017-09-12, "© 2011 Nameless Voice", **MSVC 2008 SP1** (linker 9.0; 5 of 6 Rich-header tool IDs equal Squirrel's), static CRT, relocs 36 KB, **no TLS**, load config, 0xA8000, 78 imports (KERNEL32 77, USER32 1), export `_ScriptModuleInit@20`, **0 instructions with XMM operands** | Beta 8 build, `f15fc91b…`, 2025-11-22, MSVC 2017, **static TLS**, SSE2 throughout (needed `lift_sse.py`), 0xB2000 | **No: the blocker** |
| `osm\script.osm` (Public Scripts 2.1) | MinGW, `msvcrt.dll` imports | not present | Not needed (never loaded, §5.3) |

What the scan shows (**Verified** unless marked):

- **Every one of NVScript 1.2.4's 78 imports already has a host shim** (parsed from the `ShimDef` tables in `port/`). Its
  extra imports compared with Beta 8's (`HeapCreate`, `VirtualAlloc`, `GetStringTypeA`, …) are already used by the
  recompiled Squirrel.
- Both Squirrel builds have 13 unshimmed WSOCK32 imports (the remote debugger). They are bound to thunks that fail
  only if called, as in today's working 1.02 (**Strongly supported** harmless).
- NVScript 1.2.4 needs none of Phase 3A's new machinery (TLS, SSE), although both are available. It has the same
  profile as the already-lifted Squirrel module. **Recompiling it with the existing pipeline is Strongly supported,
  not yet attempted.**
- **Class registrations:** 1.2.4 contains 519 `NV*` identifiers and Beta 8's build 541. All 34 classes the campaign uses
  are in both. That shows only that names match; data compatibility of the two NVScript versions is Unknown (§5.3).

**Squirrel 1.01 or 1.02?** On the PC, the NewDark 2.48 update replaces `osm\squirrel.osm` with 1.02. The 2.48 install
has 1.02, and 2.48's notes name its one fix, which the legacy scripts don't exercise (§4). Running 1.01 under 2.48 is a
pairing NewDark never shipped. So the faithful 2.48 environment is **1.02, already in the APK** (Strongly supported).
Lifting 1.01 as well is possible (same toolchain) but is not recommended unless a 1.02 difference is found.

**Several versions in one APK.** The loader takes the **first** descriptor whose name matches (`modules.c:20-23`),
then checks size and base (`:103`) and code CRC (`:120`). So one APK can hold only one `NVScript.osm` today. The build
chooses which one with `--nvscript FILE` (`port/build.py:37-44`, `android/build_native.py:88`). Supporting both:

| Mechanism | Change | Assessment |
| --- | --- | --- |
| **Build-time choice** (today) | none in the host: build with `--nvscript <1.2.4>` | Works now; one NVScript per APK |
| Identity-based selection | `modules.c`: among descriptors with the file's name, use the one whose size, base and code CRC match the file; unique prefixes (`nv`, `nv4`) and bases (e.g. 0x30500000, after Beta 8's NVScript ends at 0x304B2000; `mod_reserve_all` must find the range free) in `build.py`; `mod_handle_of` per loaded module | Keeps every identity check (it *chooses by* identity). Touches the loader for all games, so conformance and both SCP setups must be re-run. Medium |
| Runtime flag to pick a module | — | Not proposed: it would select code by configuration rather than by the file's identity |

Arbitrary x86 loading and emulation stay out of scope.

## 7. Approaches compared

| | **A: 2.48 + the complete original mod set** | **B: separate legacy build (side by side)** | **C: limited adjustments to the current setup** |
| --- | --- | --- | --- |
| What runs | 2.48 engine (already lifted), allobjs + lgvid (same), Squirrel 1.02 (already lifted), **NVScript 1.2.4 (new lift)**; all original data, DMLs, `patch_ext`, `misdml`, configs, saves | The same as A, packaged as its own APK | Beta 8's NVScript and data with legacy saves (today's state) |
| APK change | A new lift of NVScript 1.2.4 (`--nvscript`). No host code expected | A + a Gradle flavor with `applicationIdSuffix` (e.g. `.scp4`) so it installs **next to** the accepted APK with its own storage | None |
| Game-folder change | A **copy** of the original with the two files a NewDark 2.48 update replaces: `ss2.exe` → 2.48 `SS2.exe` (`c7ff08c5…`; the launcher refuses others, `GameFolders.exeProblem`) and `osm\squirrel.osm` → 1.02 (`d95b4415…`). All else byte-identical. Profile: saves copied unchanged, `cam.cfg` with a phone resolution | Same | — |
| Coexists with SCP Beta 8? | Only with B's packaging or identity-based selection. A single-NVScript APK built for 1.2.4 would make `ss2scp` fail its module gate | **Yes**: separate app, separate data; the accepted APK is never replaced | Yes |
| Fidelity | Highest available on 2.48 | Same as A | Low: Beta 8 gamesys and scripts, mid-game warning, Once Router remap, missing models (LEGACY_SAVE_COMPATIBILITY.md) |
| Risk to existing work | Medium if installed over the current APK | **Low** | None |
| Effort | Lift ~10 min; NVScript verification | A + small Gradle change + a second ~1.6 GB game copy on the phone | — |

**Option C, legitimately:** there is no configuration-only way to reach the Beta 4 environment with today's APK. Putting
Beta 8's NVScript (or other Beta 8 files) into the legacy folder would make it load, but that copies Beta 8 assets in
(excluded). It would also read 1.2.4-written script state with another NVScript (§5.3), so it is **not proposed**.
Harmless adjustments that do belong in any option: display settings in the profile (`cam.cfg` resolution), and if a GPU
has trouble, switching off HDR/post-processing in the profile's `user.cfg`. Both are visual only.

## 8. Recommended minimal implementation (not started)

1. **Build** from `094e55a` (or its successor) with
   `android/build_native.py --no-rebuild-guest --game <2.48 SS2.exe> --game-backend all --nvscript <original osm\NVScript.osm, abed4ee2…>`.
   This lifts NVScript 1.2.4 at 0x30400000 and generates its descriptor and `BuildConfig.GAME_MODULES`. Review the lift
   report: unsupported instructions, `RaiseException` reachability, imports.
2. **Package side by side:** add a `legacy` product flavor (or dimension) with `applicationIdSuffix ".scp4"` and its own
   version name, so it never replaces the accepted `local.newdarkandroid.ss2` and has its own `Android/data`.
   Keep the main build's `--nvscript` (Beta 8) unchanged, and keep the two lifts in separate `gen` folders.
3. **Host tests before the device:** conformance (`run_conformance.py`, default and `--cache-regs --abi-check`), and the
   upstream `harness/difftest.py`/`insnfuzz.py` on NVScript 1.2.4 as PHASE3A did for Beta 8's.
4. **Game folder:** on the PC, make a **copy** of `<local path>` (never the original). In the copy only, replace
   `ss2.exe` with the 2.48 `SS2.exe` (keep the name the launcher finds) and `osm\squirrel.osm` with 1.02. Hash both
   trees and record the diff (exactly 2 files). Push the copy to the legacy app's `games/` (or import it with the app's
   importer, which then reports the modules as matching). Saves go to that app's own profile, byte-for-byte, with the
   hashes checked.
5. **Profile config:** `cam.cfg` from the original with only `game_screen_size` changed for the phone (or the `ss2scp`
   profile's 1280×720). `user.cfg` comes from the original `User.cfg` plus the launcher's Interface-size line.

Only after step 3 passes is a device run meaningful. Later, if wanted, identity-based selection (§6) could merge the two
APKs. It is not needed to protect the campaign.

## 9. Validation procedure (proposed)

On an **isolated copy** (step 8.4) in the side-by-side app. The originals on the PC and the phone stay read only.

**A. Reference runs on the PC (original 2.47, read only).** For a chosen set of slots, run the PC game from a
**separate PC copy** (so the original's `current\` and logs are untouched). Use `monolog` and `log_player_pos`, record
the saves made after each scripted action, and keep `ss2.log`/`monolog.txt`. These are the baselines.

**B. Device runs** (scripted input + frame captures, as in the previous tests):

| # | Check | Pass criterion | Detects |
| --- | --- | --- | --- |
| 1 | Main menu, Load list | All 15 listed with the PC's names | path/config errors |
| 2 | Module load | `SS2.log`: `nvscript.osm` 1.2.4, `allobjs.osm`, `squirrel.osm` loaded; port log: `NVScript.osm: loaded the recompiled module` with the new descriptor; no `Failed to load script module` | identity/lift problems |
| 3 | SCP start-up | **No** module-gate message, **no** "Mid-game activation" text; monolog shows "Quest Note Notifier initialized" and **no** Squirrel errors (positive control: `script_dump_files`) | missing scripts, version mismatch |
| 4 | Representative loads | Slots 8 (Engineering, earliest, pre-notifier), 12 (Hydro A), 0 (Hydro B/C), 13 (Quicksave, Ops), 5 (Ops C, the Once Router objects), 7 and 14 (Recreation, newest) | per-level data |
| 5 | Inventory, equipment, hypo/medkit use (HP change), research | Same values as the PC baseline | item scripts (NVComestible, NVMedPatchScript, NVBattery) |
| 6 | Replicator (buy), recycler (NVRecycler), upgrade units (NVUpgradeCheatStopper) | Nanite and stat changes equal to the PC | NVScript logic |
| 7 | Psi: one power from the PSI amp | Effect and psi cost equal to the PC | psi scripts |
| 8 | Elevators and a level change (e.g. Rec → Ops, Ops → Rec) | Arrive, then the level's saved state persists after going back | transitions, `current\` handling |
| 9 | A not-yet-visited level from a late save | Mission DMLs that apply on the PC also apply here (`dbmod_log 3` on both) | fingerprint/DML parity |
| 10 | Ops 2 / Ops 3 trigger chains (objs 1646→1647→1651, 1469→636) | Same as on the PC | archetype references |
| 11 | Save → quit → new process → load | Exact position and inventory; then **compare the saves offline** | save writing |
| 12 | Offline diff vs PC (the strongest silent-difference test) | For the same slot and the same scripted action list, compare the device save with the PC save, using `save_analysis.py`/`scp4_static_audit.py` parsers: `QUEST_CMP` (all quest vars), inventory/stat properties, `SCRIPTSTATE` keys and values for NV/SCP classes, object counts per archetype. Expect equality except time-dependent fields (positions of moving AI, timers) | silent script/state divergence |

**Confidence levels:**

- *Low:* 1–4 pass.
- *Reasonable:* 1–11 pass on at least 5 slots across 4 decks, plus check 12 shows equal quest variables and NVScript
  state for at least 3 scripted scenarios.
- *Good:* the above, plus a play session of 1–2 hours through at least two level transitions with a mid-session
  save/reload, with no monolog errors.

**No amount of bounded testing proves 100% compatibility.** Rare script paths, late-game levels and long sessions stay
uncovered. The PC original stays the master copy.

## 10. Remaining unknowns and blockers

| Item | Type |
| --- | --- |
| NVScript 1.2.4 is not recompiled; the current APK cannot run SCP Beta 4 | **Blocker** (Incompatible today) |
| Whether 1.2.4 lifts cleanly (unsupported instructions, exception paths) | Unknown until lifted |
| Behavioural parity of recompiled NVScript 1.2.4 with the native module | Unknown; check 9.12 |
| Squirrel 1.02 instead of 1.01 for these scripts | Strongly supported equivalent; check 9.3/9.12 |
| Mod changes during 2023 other than the quest notifier | Unknown (none indicated) |
| Rendering extras on Android GPUs | Plausible, visual only |
| Phone storage for a second ~1.6 GB game copy and a second app | To check before step 8.4 (22 GB free at the import test) |

## Final answers

1. **Can the original SCP Beta 4 environment plausibly run under NewDark 2.48?** Yes. SCP Beta 4 requires 2.46+, every
   data dependency of the saves resolves in the original set, and the engine and two of the three script modules are
   already in place. The missing piece is a recompiled NVScript 1.2.4. *Strongly supported, not yet run.*
2. **Can we preserve the original save files unchanged?** Yes. No step needs a save, quest-variable or object-ID change.
   Saves are copied byte-for-byte into a separate profile, and the originals stay read only (PC and phone are identical
   today). *Strongly supported.*
3. **Which native modules must be rebuilt or replaced?** Recompile **NVScript.osm 1.2.4** (`abed4ee2…`). Use the
   already-recompiled **Squirrel 1.02** (the 2.48 module) in place of 1.01 in the game-folder **copy**, alongside 2.48's
   `SS2.exe`. `allobjs.osm` and `lgvid.dll` are identical and need nothing.
4. **Separate APK or one APK for both?** Today the loader allows one `NVScript.osm` per APK, so the safe answer is a
   **separate side-by-side APK** (own application ID). One APK for both is possible later with identity-based module
   selection (§6), a moderate host change that keeps every identity check.
5. **What remains uncertain?** NVScript 1.2.4's lift and its behavioural parity, exception paths, 2.48's fixed loading
   paths vs 2.47 behaviour, undocumented 2023 mod changes, long-session and late-game behaviour (§10).
6. **Safest next implementation step:** lift NVScript 1.2.4 with the existing `--nvscript` option into a side-by-side
   `.scp4` build. Run the host verification (conformance, difftest) **before** anything goes on the phone. Then run §9
   on an isolated game copy. The accepted APK, `ss2scp` and all originals stay untouched.

## Files

`docs/android-audit/legacy-scp4/`: `original-install-critical-files.sha256` (115 files), `legacy-saves.per-slot.sha256.txt`,
`mod-load-order.txt`, `scp4_static_audit.py` and its output `scp4_static_audit-output.txt`
(`python scp4_static_audit.py <original or copy> build/scp/game-scp --pe`; it uses the readers in
`legacy-save/save_analysis.py`). Full tree hashes (phone, PC, `ss2scp`) are in `build/android/legacy-save-test/`
(not in git).
