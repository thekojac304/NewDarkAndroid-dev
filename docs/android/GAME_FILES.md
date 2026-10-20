# Getting your game onto the phone

The APK contains no game data. You need the game folder of **System Shock 2 with NewDark 2.48**, the same `SS2.exe` the
APK was built from (see [BUILDING.md](BUILDING.md#2-your-game-files)). The app checks this before copying or starting
and tells you if it does not match.

## Option A: import in the app (recommended)

1. Copy your System Shock 2 folder to the phone (USB, cloud, network share). Modded installs (SCP etc.) work too; the
   layout is kept.
2. Launcher, then **Import a game folder...**, then pick the folder with Android's folder picker (no storage permission
   needed).
3. The app scans first (`SS2.exe` check, free space) and asks before writing. It can also bring the installation's
   saves into the game's own save folder (a checkbox).
4. Imports go to `Android/data/local.newdarkandroid.ss2/files/imported/<name>/`. Interrupted imports leave nothing
   visible and are cleaned up. The app never replaces an existing game folder, save or setting.

Allow about the size of your install in free space.

## Option B: adb

```sh
D=/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games
adb push "<your SS2 folder>/." $D/ss2/
adb shell chmod -R a+rX,go-w $D
```

Use a different folder name per installation (for example `ss2`, `scp4`, `ss2scp`); each gets its own save and config
profile. In Git Bash on Windows run `export MSYS_NO_PATHCONV=1` first so `/storage/...` is not rewritten.

## Where things live

| Location | Holds |
| --- | --- |
| `.../files/games/<name>/`, `.../files/imported/<name>/` | Game folders (read-only to the game) |
| App internal storage `files/profiles/<name>/` | Saves, `user.cfg` / `cam.cfg` changes, `SS2.log` |
| `files/touch-settings.cfg` | Touch layout and options |

Updating the app in place keeps all of it. **Uninstalling the app deletes all of it**, including saves; copy them
first if you care.

## Saves

Saves from a PC installation can be imported with it. Do not load vanilla saves into an SCP installation or the reverse.
