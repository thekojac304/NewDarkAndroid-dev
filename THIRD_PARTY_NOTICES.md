# Third-party notices

NewDarkAndroid is an unofficial fan project. It uses the following work. See [LICENSING.md](LICENSING.md) for how each
part may be redistributed.

## NewDarkRecompiled (MIT)

Static recompiler, Windows host, portable host and DirectX/Win32/CRT shims.
Copyright (c) 2026 Owen (Owloeb) and contributors. <https://github.com/Owloeb/NewDarkRecompiled>. The MIT license text
is in `LICENSE`. That license covers the tools only, **not** System Shock 2, the Dark Engine, NewDark, or anything
generated from them.

## Shockolate (GPL-3.0-or-later)

The touch-control core in `port/backends/android/touch/` (floating stick, look, FIRE/USE, radial selector, settings
and layout editor, renderer design) and `TouchSurface.java` are derived from the Android touch controls of a Shockolate
fork (`touch-controls` branch, commit `750237049b2e9906f5e3345ff05522558b9a9f47`), which in turn derives from
**Shockolate**, the System Shock source port, <https://github.com/Interrupt/systemshock> (baseline commit
`4cc3d07dfff2d11b6d3a0a9960a51cf4ca253690`), under GPL-3.0-or-later; the touch-control design descends from a Halo-based
mobile control scheme. License text: `LICENSES/GPL-3.0.txt`. Our changes are listed in
`port/backends/android/touch/README.md`. No System Shock (1) engine code is used (the Shockolate bridge was replaced by
`touch_ss2.c`).

## SDL 2 (zlib)

Simple DirectMedia Layer 2.32.10, <https://github.com/libsdl-org/SDL>, Copyright (C) 1997-2025 Sam Lantinga and
contributors, zlib license. **Not in this repository**: fetched at build time into `build/deps/SDL2`. Its Java sources
and `libSDL2.so` are packaged in the APK you build.

## FFmpeg Indeo 5 decoder (LGPL-2.1-or-later)

`video/ffmpeg/libavcodec/{indeo5.c,indeo5data.h,ivi.c,ivi.h,ivi_dsp.c,ivi_dsp.h,vlc.c,vlc.h,get_bits.h,mathops.h}` are
unmodified FFmpeg sources (see `video/ffmpeg/FFMPEG_COMMIT`), Copyright (c) the FFmpeg developers, licensed under the
GNU LGPL 2.1 or later (`video/ffmpeg/COPYING.LGPLv2.1`). They are compiled into the game library. `video/lavshim.c` and
`video/ffmpeg/compat/` are our glue. Anyone distributing a binary containing them must meet the LGPL's relinking and
notice terms; this project distributes source only.

## Android SDK / NDK / Gradle (build tools)

Used to build; none of it is distributed here except the Gradle wrapper (`android/gradle/wrapper/gradle-wrapper.jar`,
Apache-2.0).

## Games, engines, mods (not included)

System Shock 2 © Looking Glass Studios / Nightdive Studios; NewDark engine updates by Tom N Harris and contributors;
the System Shock 2 Community Patch and NVScript by their authors. None of these are distributed or modified by this
project, and none of their authors or owners endorse it.
