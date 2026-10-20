# NewDarkAndroid v0.1.0-beta.1 (source release)

First public beta of an **unofficial, community-made Android port of System Shock 2 (NewDark 2.48)**. It is not
affiliated with or endorsed by Nightdive Studios, Looking Glass Studios or any other rights holder.

**This is a source release.** There is **no APK and no game content** in it. System Shock 2's code and data are
copyrighted and cannot be redistributed; this project turns *your own* legitimate copy into an Android app on *your own*
PC. See [docs/android/BUILDING.md](../android/BUILDING.md).

## What you get

- The Android app (launcher, game-folder importer, interface-size setting, controls guide).
- Full touchscreen controls: floating stick, swipe look, FIRE/USE/JUMP/CROUCH/RELOAD, LOOK/CURSOR mode, direct or
  relative (trackpad) cursor, on-screen keyboard, layout editor.
- Radial menus that open around the button you hold: weapons, ammo and firing modes, psi powers, hypos/patches, system.
- Mod support: data mods as on PC; native modules when recompiled in; **SCP Beta 4 (NVScript 1.2.4) and SCP Beta 8**
  side by side, picked by file identity.
- The portable host layer (Win32/DirectX/DirectSound shims, SDL 2 + OpenGL ES) and the recompiler tools it builds on
  (Owloeb/NewDarkRecompiled).
- Docs: building, game files, controls, mods, troubleshooting, known limitations, licensing.

## Requirements

ARM64 Android 8.0+, OpenGL ES 2.0, a legitimate System Shock 2 with the **NewDark 2.48** `SS2.exe`
(5,121,024 bytes), and a PC with Python 3, JDK 17, Android SDK 34, NDK 29.0.14206865, CMake 3.22.1.

## Known limitations

Tested on a single device (Galaxy S26 Ultra, Android 17). No full-campaign playthrough has been verified. Native mods
must be recompiled into your build; only two NVScript versions are supported. No gamepad support. Debug-signed builds
only. See [KNOWN_LIMITATIONS](../android/KNOWN_LIMITATIONS.md).

## Licensing

MIT (tools), GPL-3.0-or-later (touch controls, derived from Shockolate), LGPL-2.1 (FFmpeg Indeo 5 decoder), zlib (SDL,
fetched at build time). The game and everything generated from it are not covered and are not distributed. See
[LICENSING.md](../../LICENSING.md) and [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). **Do not share APKs you
build.**

## Credits

[Owloeb/NewDarkRecompiled](https://github.com/Owloeb/NewDarkRecompiled), [Shockolate](https://github.com/Interrupt/systemshock)
and its Halo-derived touch design, SDL, FFmpeg. System Shock 2 © Looking Glass Studios / Nightdive Studios.

## Verify your download

`SHA256SUMS.txt` lists the source archives' checksums.
