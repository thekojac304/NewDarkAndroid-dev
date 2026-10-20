# Game import fix: "cannot create …/games/system-shock-2.importing"

2026-10-18. The user tested `0.8.0-phase4-play` on the Galaxy S26 Ultra (serial <serial>, Android 17, API 37).
**Import a game folder...** failed with:

```
Import failed: cannot create /storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games/system-shock-2.importing
Nothing was changed.
```

**Status: FIXED and validated on the phone** (`0.8.1-import-fix`). Imports now go to a new, app-owned `imported/`
folder. The importer scans first and asks before writing, brings saves into the new game's own write folder, and
survives interruptions. The user's own installation (`Download/System Shock 2`) now imports completely and
byte-for-byte, **but it cannot be played with this APK**: it is **NewDark 2.47**, and the APK was recompiled from the
NewDark 2.48 `SS2.exe` (§2). The importer and the Play button now say so before anything is copied or started.

| | |
| --- | --- |
| Root cause | `files/games/` was created by `adb push` as the shell account (uid 2000, `drwxrwsr-x`, group `ext_data_rw`). The app (uid 10034) is not in that group, so it only has "other" `r-x`: `mkdir` fails with EACCES (§1) |
| Fix | Imports go to `files/imported/`, which the app creates itself. `games/` and everything in it are left alone (§3) |
| Second blocker | The user's installation is NewDark 2.47 (`ss2.exe` 5,116,416 bytes, linked 2018-06-03). The port runs only the 2.48 `SS2.exe` it was generated from (`host.c:402`). Now detected at import and at Play (§2) |
| Saves | Top-level `save_N/` folders go to the new `profiles/NAME/`, never into the game folder, and only if the player ticks the box. `current/` is not copied (§4) |
| APKs | `0.8.1-import-fix`: play `2a5ad3535a2586b9e16f7c67c5f86d08e204e9513d6d71f8e8f502d9c988efb6` (installed), dev `6cb82c4b74bdc714f824ac2922c5a8080622869dba8d66358da614b0ac08c5b7`, in `build/android/baseline/import-fix/` |
| Previous APK | `0.8.0-phase4` play `3b4ed73d…f2c49b9f1c` and dev `89f28ce4…2681f5b3db`: unchanged in `build/android/baseline/phase4/` (verified with its `SHA256SUMS`). This was the APK installed on the phone before the fix (hash of the installed `base.apk`) |
| Native code | Unchanged. The packaged `libss2game.so` is byte-identical in both play APKs (`da855353…`); only the Java launcher changed |

## 1. Investigation

### 1.1 The failure (logcat, the user's attempts at 11:08–11:09)

```
E NewDarkLauncher: java.io.IOException: cannot create …/files/games/system-shock-2.importing
E NewDarkLauncher:   at local.newdarkandroid.ss2.GameImport.copyDir(GameImport.java:100)
```

The old importer had already listed the picked folder through the Storage Access Framework and found `ss2.exe` in it
(`hasChild`). So the **SAF read grant worked**, and the failure was the first write: `File.mkdirs()` on the
destination. `mkdirs()` returns `false` without a reason, which is why the message gave none.

### 1.2 Ownership, permissions, SELinux

```
pm list packages -U   →  local.newdarkandroid.ss2 uid:10034
ls -lanZ  …/Android/data/local.newdarkandroid.ss2
  drwxrws---  10034 1078  u:object_r:fuse:s0   .            (created by Android for the app)
  drwxrws---  10034 1078  u:object_r:fuse:s0   files        (created by the app)
  drwxrwsr-x   2000 1078  u:object_r:fuse:s0   files/games  (created by adb push, owner shell)
  drwxr-sr-x   2000 1078  …                    files/games/ss2, files/games/ss2scp
```

The app's own process (`/proc/<pid>/status` of `local.newdarkandroid.ss2`):

```
Uid:    10034 10034 10034 10034
Groups: 9997 20034 50034                      (no 1078 ext_data_rw)
context u:r:untrusted_app_34:s0:c34,c256,c512,c768
```

So for `games/` the app falls into "other", which has `r-x`. It can read and enter the folder, which is why the
adb-pushed games run, but it cannot create anything in it. The SELinux label (`fuse`) is the same on the
app-owned `files/` and the shell-owned `games/`, so SELinux is not the cause. A free-space or name problem would
not give this result either: there was 22 GB free, and no `system-shock-2*` entry existed.

**A trap when checking this:** `run-as local.newdarkandroid.ss2 mkdir …/games/x` *succeeds*. `run-as` keeps the
adb shell's supplementary groups (its `id` lists `1078(ext_data_rw)`), so it is not the app's real permission set.
I created and removed one probe folder this way (`games/probe.tmp`). It left nothing behind, but `games/`'s
modification time is now 11:13.

The fixed build logs the check from inside the app process, before every import:

```
I NewDarkLauncher: import: scanning "ndtest-small" (…); destination …/files/imported; games/ is uid 2000 (the adb
                   shell account), mode 2775, the app is uid 10034
```

### 1.3 Storage Access Framework

`ACTION_OPEN_DOCUMENT_TREE` → `content://com.android.externalstorage.documents/tree/primary%3ADownload%2F…`. Listing,
`openInputStream` on 6,448 files, and reading `ss2.exe` and the OSMs for the version checks all worked without a
persistable grant. Android refuses the `Download` root itself ("Can't use this folder"), but subfolders are fine.

### 1.4 Saves and configuration

The port keeps the game folder read only. `SS2PORT_WRITE_DIR=files/profiles/NAME` (internal storage) overlays
it **file by file**: reads look in the write folder first, and writes always go there (`port/core/vfs.c`, `vfs_map`).
The existing profiles hold `save_N/`, `current/`, `cam.cfg`, `user.cfg`, `user.bnd` and `SS2.log`. Touch settings are
global, in `files/touch-settings.cfg`.

The user's installation holds `save_0` … `save_14` (161 files, 139 MB) and `current/` (15 files). If those
stayed in a read-only game folder, the overlay would merge them with the profile file by file. A profile `save_3`
would then show stale `.mis` files from the PC's `save_3`, and a stale `current/medsci2.MIS` would be picked up at a
level change. So saves must not stay in the game folder.

## 2. Second finding: the user's installation is NewDark 2.47

| | APK built from (`games/ss2`) | `Download/System Shock 2` |
| --- | --- | --- |
| `SS2.exe` | 5,121,024 bytes, CRC32 `c8f2d1e4`, sha256 `c7ff08c5…`, linked 2019-06-15, ProductVersion **2.48** | `ss2.exe` 5,116,416 bytes, CRC32 `14274867`, sha256 `4199aac1…`, linked 2018-06-03, ProductVersion **2.47** |
| `NVScript.osm` | SCP Beta 8's copy, SizeOfImage `0xb2000` | 1.2.4 (2017), `0xa8000`, in `osm/` and `DMM/SCP_beta4/` |
| `Squirrel.osm` | `0x2a0000` | 1.01 (2018), `0x29f000` |
| SCP | Beta 8 | **Beta 4**, plus 17 more DMM mods |

The two executables differ almost everywhere (4.5 M differing bytes; this is not a Large Address Aware header
flag, even though `EnableLAA.exe` is in the folder). The port refuses any other executable (`host.c:402`, size and
CRC32). It also skips any module of another version (`modules.c`), so this installation would die at launch. A
working importer alone would not have made it playable.

What the user can do (not done here; each is the user's decision):

- Update the PC installation to NewDark 2.48 (and SCP Beta 4 → Beta 8, as in PHASE3B), then import it again. SCP
  itself does not promise that saves carry over between betas (PHASE3B §5: fresh SCP game only).
- Or build an APK from the 2.47 executable (`build_native.py --game <2.47 ss2.exe>`). This is untested: every phase
  so far lifted 2.48.

## 3. The fix

### 3.1 Where imports go (`GameFolders.java`)

| Folder | Owner | Used for |
| --- | --- | --- |
| `files/games/NAME/` | adb shell (existing) | adb copies, exactly as before. Never written, moved, renamed or chmod'ed by the app |
| `files/imported/NAME/` | **the app** (created with `Os.mkdir`, `drwxrws---  10034 1078`) | imports. adb can still read and write it, because the shell is in group 1078 |
| `files/profiles/NAME/` | the app (internal) | write folder, as before |

- **Least invasive correction for the adb-owned `games/`: none.** Nothing was chmod'ed or chown'ed. The shell cannot
  chown to the app's uid. `chmod o+w games/` would make it world-writable. Migrating the existing games was not
  wanted. A fresh install still creates `games/` itself (then it belongs to the app, and `adb push` into it still
  works).
- `GameFolders.dir(NAME)` uses `games/NAME` if it has an `SS2.exe`, else `imported/NAME`. The list and Game
  folder button show both roots, each name once. A new name must be unused in `games/`, `imported/` and `profiles/`
  (case-insensitively, including `.importing`/`.commit`), so a new game never inherits old saves.
- `SS2.exe` is found in any letter case (the user's copy is `ss2.exe`), as is `user.cfg`.

### 3.2 Import flow (`GameImport.java`)

1. **Scan** (worker thread, nothing written). The whole tree is listed with the sizes the provider reports. This
   checks names (`..`, separators, NUL), a depth limit of 32, and entries that **differ only in letter case** in one
   folder, which are refused with both names. It finds `SS2.exe`, reads it whole, and compares size and CRC32 with
   `BuildConfig.GAME_EXE`. It reads the PE headers of every `allobjs.osm`/`Squirrel.osm`/`lgvid.dll`/`NVScript.osm`
   against `BuildConfig.GAME_MODULES` and makes one note per module. It sorts top-level `save_N/` folders (saves)
   and `current/` (skipped) from the rest (the game), lists the settings files, picks a free name, and checks free
   space: game + saves + 256 MB must fit on the emptier of the two volumes.
2. **The player chooses** (a dialog; see `import-fix/1-…png`, `2-…png`). It shows the file count and size, the
   new folder name, that the source is only read and that the other folders, saves and touch settings stay
   unchanged, the saves (slots, size, where they go), the settings that come along, what is not copied, version
   warnings, and free space. A checkbox, ticked by default, says **Also import the N saved games**. The button reads
   **Import**, or **Import anyway** if the `SS2.exe` is wrong. There is no import button at all when space is
   short. **Cancel** writes nothing.
3. **Copy.** The game goes to `imported/NAME.importing/`, the saves to `profiles/NAME.importing/`. Files stream in
   1 MB blocks, each is `fsync`ed, and its size is checked against the scan. The copy then checks that `SS2.exe`
   arrived and that the file count is complete. The button turns into **Cancel import**, and the screen stays on.
4. **Commit.** The app writes the journal `imported/NAME.commit`, renames both staging folders into place, then
   deletes the journal. On any error or cancel, both staging folders are deleted.
5. **Recovery** (`GameFolders.recover`, run when a new launcher process starts). With a journal, it finishes the
   renames; without one, it deletes `*.importing` in `imported/` and `profiles/`. It reports what it did above the
   game folder list. `games/` is never touched.

Errors say what happened and what to do. Out of space (`ENOSPC`, at any point) gives the copied amount, free space
and the shortfall. EACCES on a folder gives the owner of its parent ("uid 2000 (the adb shell account)") and the
app's uid. A revoked SAF grant says to pick the folder again. A file vanishing during the copy and a short read
are named. Everything ends "Nothing was changed: the partial copy was removed…".

### 3.3 Play

- `startNext` checks the game folder's `SS2.exe` (CRC cached by path, size and mtime). A wrong one is refused
  before any native code runs, with the reason, right under the buttons (`import-fix/3-…png`). It used to be a
  `port_die` in the `:game` process. The Game folder button reads "(its SS2.exe is not supported)".
- A profile without a `user.cfg` now starts from the game folder's `user.cfg` (any case) before the Interface size
  line is added. Before, the launcher's one-line `user.cfg` hid the installation's own (the overlay reads the
  profile first), which silently dropped an imported mod setup's settings. Existing profiles already have a
  `user.cfg` and are not affected.

### 3.4 Smaller changes

- `build.gradle`: `BuildConfig.GAME_EXE` / `GAME_MODULES`, read from `build/android/game/gen/` (`nd_hostdata.c`,
  `*_moddata.c`), with a warning and no check if they are missing. Version `0.8.1-import-fix` (code 10).
- The import progress poll only touches the views when the text changed. Before, it kept the UI from ever going
  idle, and `uiautomator` could not read the screen.
- adb test settings (both builds, kept until cleared; logged as warnings): `--es import_test
  "free=BYTES,enospc=BYTES,kill=copy|commit"`; `--es import_test ""` clears them.

## 4. Save import strategy

- Only the top-level `save_N` folders of the picked folder are saves. They go to `profiles/NAME/save_N/` of the
  **new** game, so they belong to the game folder they came with, and vanilla and SCP saves never mix. That
  profile is new and empty, so **nothing can be overwritten**. Importing saves into an *existing* game folder is
  not offered, which also means no overwrite needs confirming.
- If the box is unticked, the saves are not copied, and the result says so ("Its 2 saved games were not imported;
  they are still in the original folder"). They are never silently dropped, and never left in the game folder,
  where the overlay would mix them with new saves (§1.4).
- `current/` (the PC's working state from its last session) is not copied, and the dialog says so. The game
  creates a fresh one.
- Configuration (`cam_mod.ini`, `user.cfg`, `user.bnd`, `cam.cfg`, `cam_ext.cfg`, `install.cfg`) stays in the game
  folder, where the game reads it through the overlay. `user.cfg` is copied into the profile at first Play (§3.3).
  Touch settings (`files/touch-settings.cfg`) and other games' profiles are not touched.

## 5. Validation (S26 Ultra, play builds of this change)

All imports went through the real system folder picker (DocumentsUI), driven by `uiautomator`/`input tap`, and the
SAF "Allow … to access folder?" grant. The fixtures were disposable folders in `Download/`, removed afterwards.
The tests ran on successive play builds of this change. Each later build changed only dialog wording, the UI idle
fix, how module notes are grouped, or where the Play refusal is shown, and none touched the copy, commit or
recovery code. Tests 4, 8 and 9 and the Play refusal ran on the final APK `2a5ad353…`, the real-installation import
on `dce43fd7…`, and tests 1–3 and 5–7 on `39af2942…`/`2699ea00…`.

| # | Test | Result |
| --- | --- | --- |
| 1 | Small fixture `ndtest-small`: a 2.48 `SS2.exe`, `User.cfg`, `cam_mod.ini`, `Data/sub/deeper/deepest/`, `DMM/Mod With Spaces/Readme Mixed.TXT`, a 3 MB file, an empty folder, `save_0`, `save_3`, `current/` | **PASS.** 6 game files byte-identical (sha256), empty folder kept, both saves in `profiles/ndtest-small/`, `current/` not copied, the game selected |
| 2 | Nested and mod files: `ndtest-scp`, a copy of `games/ss2scp` (SCP Beta 8 through DMM, 1,583 files, 664 MB). Also the user's real installation (§2: 18 DMM mods, 6,448 files, 1,525 MB, `ss2.exe` lower case) | **PASS.** Both trees are byte-identical to their sources (`sort | diff` of sha256 lists: 1,583 and 6,448 files; 289 folders). The real one took 26 s. The source was re-hashed afterwards: all 6,624 files unchanged |
| 3 | Saves without overwriting | **PASS.** `ndtest-scp`'s `save_0`/`save_14` (real SCP saves) are identical to the `ss2scp` originals. The real installation's 15 saves (161 files) are identical to the source. Every import made a new profile; `profiles/ss2` and `profiles/ss2scp` are unchanged (test 9) |
| 4 | Another installation already present | **PASS.** Re-importing `ndtest-small` with four game folders present → `ndtest-small-2`, `ndtest-small` untouched. Saves unticked → no profile made, and the message says so |
| 5 | Interrupted import | **PASS.** `kill=copy`: process killed at 336 MB. On restart, "An earlier import did not finish: removed the interrupted import imported/ndtest-scp.importing", and it was never listed as a game. `kill=commit`: killed after the journal. On restart, both renames were finished and the game and its saves were complete and identical |
| 6 | Insufficient storage | **PASS.** `free=100 MB`: the dialog says "needs 927 MB (664 MB for the game, 6 MB for its saves, and 256 MB left over for Android), but only 100 MB is free. Free up 827 MB", offers only Cancel, and writes nothing. `enospc=200 MB` (a simulated ENOSPC write error): "the phone ran out of storage space after 199 MB of 671 MB had been copied …", and both staging folders were removed |
| 7 | Launch an imported game | **PASS.** `ndtest-scp` from `imported/`: RESULT PASS, exit 0. Its Load Game list shows the imported saves (slot 0 and slot 14 "Autosave"). Loading slot 0 → in MedSci with HUD and touch controls (`import-fix/4-…png`), `modules=4` (NVScript etc. loaded), `PLAYER POS` logged, exit 0. The real 2.47 installation is refused at Play with the reason (`3-…png`); no native start |
| 8 | Switch vanilla ↔ SCP | **PASS.** `ss2` → `ss2scp` → `ss2`, 900 frames each: PASS, exit 0, game folder as selected |
| 9 | Keep saves and touch settings | **PASS.** Every file under `profiles/ss2`, `profiles/ss2scp` and `touch-settings.cfg` is byte-identical to the snapshot taken before the new APK was installed (28 files). Only `runs.xml` (selection, last-run fields) and Android's `ActivityThread.IDS.xml` changed. All 1,759 files in `games/` are identical |

Also checked: cancelling at the dialog writes nothing. The real installation's dialog names the 2.47 problem and
the NVScript/Squirrel version notes, 15 saves (139 MB), `current/` (15 files), and six settings files
(`2-…png`). `games/` stays `2000:1078 drwxrwsr-x`.

**Not tested on the phone:** a case-only name clash. Android's casefolded storage cannot hold such a fixture, and
the scan refuses one before writing. Also untested: a SAF grant revoked mid-copy, and the activity being recreated
during an import (the import is a static job that the new activity picks up). The `dev` APK was built but not
installed; it is the same Java code.

### Phone state afterwards

The test imports (`ndtest-small`, `ndtest-small-2`, `ndtest-scp`, `system-shock-2`), their profiles, and the
`Download/ndtest-*` fixtures were deleted. The user's `Download/System Shock 2` was only read. `ss2scp` is selected
again, as before. The test settings are cleared. The installed APK is the play build `2a5ad353…`.

The import of the user's 2.47 installation was **not kept**: it cannot run with this APK. Importing it again takes
about 30 s (`Download/System Shock 2` → Import anyway, saves ticked).

## 6. Limitations

- The import runs in the launcher process, not a foreground service. If Android kills the app during a long
  import (for example after switching away), the next start removes the half copy, and the player imports again.
- Recovery after a crash during the commit finishes the import, but does not select the new game folder.
- The module check compares SizeOfImage and ImageBase only. A same-sized module with different code is still
  caught when the game loads it (`modules.c` code CRC), but only in `SS2.log`.
- There is no in-app way to delete a game folder. Remove one with
  `adb shell rm -rf …/files/imported/NAME` and `adb shell run-as local.newdarkandroid.ss2 rm -rf files/profiles/NAME`.
- Unrelated to this fix, but found while checking: `build/android/baseline/phase4/libss2game.so` (05:20) does not
  match the `6c319481…` in that folder's `SHA256SUMS`. That hash is the staged `build/android/libs/` copy (05:23).
  The Phase 4 APKs themselves verify.

## 7. Reproduce

```sh
cd android && ./gradlew assembleDebug             # BuildConfig.GAME_EXE "5121024:c8f2d1e4"
adb install -r app/build/outputs/apk/play/debug/app-play-debug.apk
adb logcat -s NewDarkLauncher                       # import: scanning … / scan of … / copying … / Imported …
# test settings (cleared with ""):
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es import_test "free=104857600"
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es import_test "enospc=209715200"
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es import_test "kill=copy"   # or kill=commit
```
