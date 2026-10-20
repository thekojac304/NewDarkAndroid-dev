# Troubleshooting

| Symptom | Likely cause / fix |
| --- | --- |
| Launcher says **unsupported SS2.exe** | Your `SS2.exe` is not the NewDark 2.48 file the APK was built from (2.47 and the 25th Anniversary Remaster do not work). Update the game with the NewDark 2.48 patch, or rebuild against your file |
| **No SS2.exe** on the card | The folder was imported or pushed one level too deep or too shallow; `SS2.exe` must be directly in the game folder |
| `adb push` folders cannot be entered; import "cannot create ..." | Run `adb shell chmod -R a+rX,go-w <games dir>`; prefer the in-app importer |
| Black screen or immediate exit | Launcher, Advanced, last run's log; also `adb logcat -s ss2port NewDarkLauncher`. Check `SS2.log` in `files/profiles/<name>/` |
| No sound at all | An `sfx_device 4` (OpenAL) line from a PC install; the launcher changes it to 1 at each start. If you edit `cam.cfg` yourself, keep `sfx_device 1` |
| SCP "script modules failed" message on Earth | NVScript not recompiled into the APK, or not one of the two supported versions ([MODS.md](MODS.md)) |
| Touch buttons overlap the HUD | SET, then the layout editor |
| Home, then the icon: game gone | Android may kill the game process (low memory). Saves are as of your last save |
| `adb` paths mangled in Git Bash | `export MSYS_NO_PATHCONV=1` |
| Build: "SDL 2 source not found" or "native libraries not built" | Follow [BUILDING.md](BUILDING.md) steps 1 and 2 first |
| Bug report | Include launcher, Advanced: version, device, the log, and `SS2.log`. **Do not attach game files or your saves** |
