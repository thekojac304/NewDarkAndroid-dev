# Known limitations (v0.1.0-beta.1)

**Distribution.** Source only. You must build your own APK from your own game. No prebuilt APK is offered.

**Hardware.** ARM64 only; Android 8.0+; OpenGL ES 2.0. Tested on **one device**: Samsung Galaxy S26 Ultra
(Android 17). Other devices, tablets and low-end GPUs have not been tested. No gamepad support.

**Game versions.** Only the retail NewDark 2.48 `SS2.exe` (5,121,024 bytes). NewDark 2.47 and earlier, and the 25th
Anniversary Remaster, are unsupported.

**Play coverage.** Testing was scripted and short sessions plus the owner's hands-on play: boot, menus, new game, several
decks, save/load, audio, SCP Beta 4 and Beta 8 start-up and early game. A full campaign playthrough, long soak sessions
and every psi power, replicator and deck change have **not** been verified. Expect rough edges.

**Mods.** Native `.osm` mods must be recompiled into your build; only two NVScript versions are known
([MODS.md](MODS.md)). `fmsel.dll`, exe patches, D3D wrappers and non-Indeo cutscene packs are unsupported.

**Controls.** CROUCH follows a single-key binding; chords fall back to Shift. Layout is tuned for phones.

**Other.** The Windows host builds one NVScript and the PS Vita build accepts one. `targetSdk 34` (not Play Store
ready). Debug-signed APKs only. Uninstalling the app deletes its saves. Builds contain generated game code and must
not be shared.
