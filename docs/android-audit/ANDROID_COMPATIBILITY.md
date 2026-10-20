# Android compatibility of the NewDarkRecompiled portable host

Phase 0 audit, 2026-10-17. Source examined: `https://github.com/Owloeb/NewDarkRecompiled`, branch `main` at
`02f77da02d0743869defbf09171790e4ab2b6059` (2026-10-15), checked out in `<repo>` on the local branch
`android-port-audit`. Nothing was built or run on a device. The only compilation was a five-line probe of the NDK
compiler (see B1).

Labels used in the matrix:

| Label | Meaning |
| --- | --- |
| **Verified** | Already implemented, and verified on Android by this audit. |
| **Untested** | Implemented and portable in source, but never run on Android. |
| **Minor** | Minor integration work: a few lines to a small new file, with a known approach. |
| **Significant** | Significant implementation: a new subsystem or a change with design work. |
| **Blocker** | Confirmed blocker: shown to fail on the Android toolchain or platform. |
| **Unknown** | Can only be answered by running on hardware. |

"Verified" is used only once in this report (the setjmp probe below), because nothing else has been run on Android.

## 1. Summary

The portable host was designed for exactly this kind of port. Everything above `port/include/plat.h` contains no
operating-system calls (`port/README.md:171`). The core uses no inline assembly, no x86 intrinsics and no
architecture `#if` outside the backends (a source grep found only `__linux__`/`__APPLE__`/`_WIN64` conditionals in
`backends/posix`, `core/host.c:96` and `runtime/rt.h:93`). There is already one ARM port: the 32-bit PS Vita
(`port/backends/vita/`). It runs the shared OpenGL ES 2.0 renderer (`backends/sdl2/gl_render.c`) and plays the game,
including saves and level changes (`port/backends/vita/README.md`).

What is missing for Android is the outer layer, plus one confirmed toolchain blocker:

1. **Confirmed blocker:** the generated code's `setjmp` relies on `__builtin_setjmp`, which Android NDK clang does
   not support on arm64 (B1). The fix is small.
2. There is no Android build: no shared-library target, no NDK toolchain path in `build.py`, and no APK/Gradle project.
3. The SDL2 backend assumes a desktop. It has its own `main()`, requests desktop GL 2.1 by default, opens the soft
   keyboard at window creation, logs to stdout/stderr, and has no Android lifecycle handling.
4. Game data must be reachable through POSIX paths (`plat_fs_*` uses `open`/`opendir`), which rules out direct use of
   Storage Access Framework URIs.

## 2. Confirmed blocker

### B1. `__builtin_setjmp` is not available for aarch64 in NDK clang

- `runtime/rt.h:93-105`: on every host except `_WIN64`, `RT_HOST_SETJMP` is `__builtin_setjmp` and `RT_HOST_LONGJMP` is
  `__builtin_longjmp`. The lifter emits `RT_SETJMP(c)` for every guest `setjmp`/`_setjmp3` call (`lift.py:916`, `:922`,
  `:1156`). The Squirrel compiler uses setjmp/longjmp for syntax errors (`README.md`, Known Issues).
- **Verified here:** NDK `29.0.14206865` (clang 21.0.0) rejects both builtins for `--target=aarch64-linux-android26`
  with `error: __builtin_setjmp is not supported for the current target`. The same file compiles for
  `armv7a-linux-androideabi26`, which explains why the 32-bit Vita build (GCC) never hit this.
- **Fix:** follow the existing `_WIN64` branch. Under `__aarch64__` (or `__clang__ && !hasSjLjLowering`), use bionic's
  `_setjmp`/`_longjmp` from `<setjmp.h>` with `RT_HJ_WORDS = sizeof(jmp_buf)/sizeof(void*) + 1`. This is a change of
  about 6 lines to a shared runtime header. It should go upstream, because the same blocker affects arm64 Linux and
  macOS builds with clang. In `--cache-regs` mode, the lifter already spills and reloads registers around `RT_SETJMP`
  (`lift.py:348`), so `returns_twice` semantics are handled the same way as with the builtin. The conformance suite
  must be re-run on arm64 to confirm it.

No other construct was found that clang/aarch64 would reject. The generated code otherwise uses only
`__builtin_expect`, `__builtin_parity` and `__builtin_bswap32`. It already builds with clang (Zig) for the Windows
x64 target (`port/README.md:129`). This is a static finding: the generated C was not produced in this audit, because
lifting needs the user's `SS2.exe`.

## 3. Compatibility matrix

### 3.1 Compilation and runtime

| Area | Status | Evidence / required work |
| --- | --- | --- |
| Android NDK + clang | **Minor** (after B1) | `port/CMakeLists.txt:24` accepts GNU or Clang. Generated code is compiled with `-w -fwrapv -fno-strict-aliasing -ffp-contract=off` (`:47`). `-ffp-contract=off` is essential on arm64, which has FMA; it is already set. |
| ARM64 ABI | **Untested** | Host is little-endian as required (`rt.h:1-3`). Guest memory access is via `memcpy` (`rt.h:57-64`), so unaligned access is safe. x87 is modelled with `double` (`rt.h:142-218`), with no `long double`. |
| Generated-C build target | **Minor** | `CMakeLists.txt:92` builds `add_executable(ss2port ...)`. Android/SDL needs a shared library (`libmain.so`) that exports `SDL_main`. Add `if(ANDROID) add_library(main SHARED ...)`. Recommend `-fvisibility=hidden`, so the hot global `M` (`host.c:21`) is not reached through the GOT in PIC code. |
| Build driver | **Minor** | `port/build.py:51` accepts only `--target linux|windows`. Lifting is plain Python (`pefile`, `capstone`) and should run on Windows. Add an `android` target that passes `-DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=...`, or build the lifted output from Gradle's `externalNativeBuild`. |
| Guest address-space reservation | **Untested** (likely fine) | On 64-bit hosts `vm_init` reserves 4 GB (`host.c:167-189`) through `mmap(PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE)` (`plat_posix.c:34-39`). An arm64 Android process has 39-bit or larger VA, so 4 GB of reserve is small. Under heuristic overcommit, a 4 GB `MAP_NORESERVE` mapping on a 12 GB phone should succeed. Only touched pages cost RAM: Vita measurements show about 90-116 MB touched (`vita/README.md`, Memory). Fallback: `--guest-space/--guest-backed` (`host.c:385-386`). |
| Memory protection / discard | **Untested** | `plat_mem_discard` uses `madvise(MADV_DONTNEED)` under `__linux__`, which bionic defines (`plat_posix.c:42`). Regions are 64 KB-granular (`host.c:93`), a multiple of both 4 KB and 16 KB pages. No `PROT_EXEC` is needed (no JIT), so there is no W^X issue. |
| 16 KB page size | **Untested** | NDK r28+ links with 16 KB max-page-size by default. The VM granularity is 64 KB. No assumption of 4 KB pages was found in `port/`. |
| Threads and synchronisation | **Untested** | POSIX threads and condition variables with `CLOCK_MONOTONIC` (`plat_posix.c:59-101`) work on bionic. The guest lock is a FIFO ticket lock (`core/threads.c:1-40`). `pthread_setname_np` is gated on `__linux__`, which bionic also defines. |
| **Main-thread stack size** | **Minor (required)** | Every guest call is a host C call, so the host stack must be deep. The Vita backend runs `port_main` on its own 8 MB pthread for exactly this reason (`plat_vita.c:356-358`). SDL2's Android `SDLThread` is a Java `Thread` with the default (~1 MB) stack. The Android entry must start `port_main` on a large pthread, or give `SDLThread` a large stack size. Otherwise deep recursion (Squirrel compiler, engine) will overflow. |
| Platform system calls | **Untested** | Only `mmap/mprotect/madvise`, `clock_gettime`, `nanosleep`, `pthread_*`, `open/read/write/lseek/ftruncate/stat/mkdir/rmdir/unlink/rename/opendir`. All are in bionic. |
| Start-up / argv | **Minor** | `port_main(argc, argv)` (`host.c:373`) takes options and the path of `SS2.exe`. The game folder is derived from that path (`:397-398`). The Android entry must build argv (the Vita does this from `ss2port.txt`, `plat_vita.c:339-352`). Configuration is read with `getenv` throughout `plat_sdl2.c`/`gl_render.c`, so the Android entry must `setenv` before `port_main`, or read an options file. |
| Executable identity at runtime | **Untested** | `host.c:400-403` refuses to run unless the `SS2.exe` in the game folder has exactly the size and CRC that the build was lifted from. The copied game folder must therefore contain the same `SS2.exe` that was used to build the APK. |
| Shutdown | **Minor** | `port_exit` calls `exit()` (`host.c:114`). On Android this ends the whole process, which is acceptable for a game. The exact interaction with `SDLActivity` (returning from `SDL_main` versus `exit()`) still needs to be checked on hardware. |
| Signals / crash diagnostics | **Minor** | Only SIGINT/SIGTERM handlers exist (`host.c:374`). On Android, guest faults are reported through `port_die`, which logs the guest state, the last host calls and the ebp chain (`host.c:79-88`, `115-119`). Native crashes (SIGSEGV) produce an Android tombstone, but nothing would be in a log the user can find (see Logging). |
| Logging | **Minor (required)** | `plat_log_write` writes to stdout/stderr (`plat_posix.c:22-27`). On Android those go nowhere. An Android `plat_log_write` must send to `__android_log_write` and to a log file in app storage (the Vita writes `ss2port.log`, `plat_vita.c:330-332`). |
| Preemption | **Untested** | `RT_PREEMPT` uses a relaxed atomic load at every loop head (`rt.h:132-136`), which is portable. |

### 3.2 Graphics

| Area | Status | Evidence / required work |
| --- | --- | --- |
| SDL2 on Android | **Minor** | `plat_sdl2.c` defines `main()` with `SDL_MAIN_HANDLED` (`:11`, `:180-189`). Android needs `SDL_main` (or `main` without `SDL_MAIN_HANDLED`, so `SDL_main.h` renames it). Add a small `plat_android.c` or `#ifdef __ANDROID__` blocks for the entry point, logging, and options. |
| OpenGL ES context | **Minor** | By default the backend asks for desktop GL 2.1. ES 2.0 is requested only when `SS2PORT_GLES=1` (`plat_sdl2.c:44-46`). On Android, GLES must be the default. |
| GLES renderer | **Untested** (exercised on Vita via vitaGL, and on Mesa GLES per `port/README.md:207`) | `gl_render.c` is written for the GL 2.1 ∩ ES 2.0 subset, and loads every entry point through `SDL_GL_GetProcAddress` (`:428-433`). On GLES it disables border clamp and `GL_TEXTURE_MAX_LEVEL` (`:438`) and collapses incomplete mip chains (`:164`). |
| Shader generation and compilation | **Untested**; **Unknown** precision | The fixed-function state becomes generated GLSL ES 1.00, cached per state, with up to 256 programs (`gl_render.c:241-273`). Game pixel shaders (ps_2_0) are translated to GLSL (`dx/d3d9_shader.c`). The fragment shader default precision is `mediump` (`:204`). On mobile GPUs mediump is often FP16, below ps_2_0's FP24 minimum. Bloom/colour-correction banding and tiled-UV precision loss are possible. Test early, and consider `highp` when `GL_FRAGMENT_PRECISION_HIGH` is available. Shaders compile lazily at first use, which may cause first-draw hitches. |
| Texture formats | **Untested** | DXT is passed to the GPU only if `GL_EXT_texture_compression_s3tc` is advertised (`gl_render.c:436`). Otherwise `d3d9_format.c` decodes DXT to RGBA8 on the CPU (`d3d9.c:117`, `d3d9_format.c:40-61`). Most Android GLES drivers do not expose S3TC. Expect the CPU-decode path, which costs more upload time and 4-8x the texture memory of the compressed data (fine on a 12 GB phone; matters for high-resolution texture mods). |
| NPOT | **Minor** | `caps.npot` on GLES is true only with `GL_OES_texture_npot` (`gl_render.c:447`). An ES 3.x context supports full NPOT without listing that extension. Querying `GL_VERSION`/ES3 would avoid needless power-of-two padding. This is an optimisation, not a correctness issue. |
| Render targets / depth | **Untested** | Uses FBOs. Depth is `GL_DEPTH24_STENCIL8` when `GL_OES_packed_depth_stencil` is present, else `GL_DEPTH_COMPONENT16` (`gl_render.c:131`, `:438`). A 16-bit depth fallback would cause z-fighting; ES 3 devices have packed depth. |
| Framebuffer scaling and aspect | **Untested** | The game renders into an offscreen back buffer at the resolution it chose. `glr_present` letterboxes it into the window (`gl_render.c:404-414`, `plat_sdl2.c:21-25`). Display modes are reported from `plat_video_display_size` (`d3d9.c:981`, `:1014`), so NewDark can pick the phone's native 20:9 mode or a lower one through `cam.cfg` (`game_screen_size`). Rendering at native resolution (about 2400×1080) is a GPU question for hardware testing. |
| Landscape / portrait | **Minor** | Set `screenOrientation="sensorLandscape"` in the manifest. The renderer letterboxes, so no engine change is needed. |
| Surface lifecycle | **Significant (testing)**; **Unknown** behaviour | SDL2 on Android destroys and recreates the EGL surface on pause/resume, and normally keeps the context. The backend handles no `SDL_APP_*` events (`plat_sdl2.c:131-161`). With `SS2PORT_RENDER_THREAD=1`, the GL context lives on the render thread (`plat_sdl2.c:53-58`). SDL's resume path makes the context current on the event-pumping thread, which would conflict. Start with the render thread off on Android. Lifecycle handling (Phase 5) must be designed and tested on hardware. |
| Gamma | **Untested** | Done in a shader on present (`gl_render.c:404-424`). Portable. |

### 3.3 Audio

| Area | Status | Evidence / required work |
| --- | --- | --- |
| SDL audio | **Untested** | `plat_audio_open` uses `SDL_OpenAudioDevice` with S16 stereo and 1024-sample buffers (`plat_sdl2.c:166-173`). SDL2 on Android uses AAudio or OpenSL ES. |
| Software mixer | **Untested** | DirectSound is emulated entirely in software. Each buffer is resampled to the output rate (`dx/dsound.c:4`, `:79`, `:106`). No device-specific code. |
| Music | **Untested** | SS2 music is ordinary sound buffers through the same mixer. |
| Latency / underruns | **Unknown** | 1024 frames at 44.1/48 kHz is about 21-23 ms per callback, reasonable on Android. There is no underrun counter. Add one for Phase 5 measurement. |
| Pause on focus loss | **Minor** | Core code never calls `plat_audio_pause` (grep). SDL2 for Android pauses audio devices itself on `onPause`. Confirm on hardware. |
| Cutscene audio | **Untested** | The PCM track of the AVI is decoded by `video/lavshim.c` and played through the same DirectSound path. |

### 3.4 Filesystem

| Area | Status | Evidence / required work |
| --- | --- | --- |
| Case-insensitive Windows paths | **Untested** (portable) | `core/vfs.c` normalises `\`, `.`, `..` and trailing dots. It resolves each component case-insensitively against cached directory listings (`vfs.c:50-83`). Any drive letter maps to the game folder (`vfs.c:3`, `:52`). |
| Absolute paths in config | **Minor risk** | Because every drive maps to the game folder, `<local path>` becomes `<game>/Games/SS2/DMM/x`. Mod paths that a Windows mod manager writes as *absolute* paths would not resolve. Relative paths (`.\DMM\...`) work. Inspect each copied install's `cam_mod.ini`. A fix (strip a known Windows install prefix) would live in `vfs.c`. |
| Game directory selection | **Minor** | Set from the `SS2.exe` path argument (`host.c:396-398`). |
| Android scoped storage | **Significant** | `plat_fs_*` uses POSIX `open`/`opendir` (`plat_posix.c:110-146`), so it cannot read SAF `content://` URIs. Options are compared in IMPLEMENTATION_PLAN.md §2. |
| Read-only game files + writable saves/config/logs | **Untested** (implemented) | `SS2PORT_WRITE_DIR` overlays a write folder. Reads look there first. Writes always go there, and a file opened for update is copied there first (`vfs.c:84-106`, `:145-161`, `plat_posix.c:147`). This matches the "leave the supplied game folder unchanged" requirement. |
| Overlay listing gap | **Minor risk** | `FindFirstFileA` enumerates **one** directory: the write-dir copy if that directory exists there, otherwise the game folder's (`kernel32_file.c:146-158`, `vfs.c:89-96`). If the game ever creates a write-dir directory with the same name as a game-data directory, listings would hide the game copy. Saves (`save_N` folders) are unaffected. Watch for it in testing; a merged listing is a contained fix. |
| Mod search paths | See MOD_COMPATIBILITY.md | Handled by the recompiled engine itself, through the VFS. |
| File access performance | **Unknown** | Directory listings are cached (`vfs.c:15-45`). Guest `FILE` writes are buffered at 64 KB (`crt/stdio.c`; `vita/PERF.md`, Results log 3). App-private storage on flash will be far faster than the Vita card (~10-12 MB/s). Storage reached through FUSE (`/storage/emulated/0`) is slower per call. |

### 3.5 Other

| Area | Status | Evidence / required work |
| --- | --- | --- |
| Cutscenes | **Untested** | `lgvid.dll` is recompiled. Its `ffmpeg.dll` calls go to the built-in AVI + Indeo 5 + PCM decoder (`video/lavshim.c`, `port/dx/video.c`), which is plain C (`ffcompat.h:14` sets `ARCH_AARCH64 0`, the generic C path). Only the formats the game ships are supported (`README.md`, Known Issues). |
| Input: keyboard/mouse events | **Untested** | Backends deliver DIK scan codes and mouse events through `plat_video_poll` (`plat.h:94-132`). `win32/user32.c:193-231` turns them into window messages and DirectInput state. TOUCH_CONTROLS.md covers the bridge. |
| Touch default behaviour | **Untested** | SDL2 synthesises mouse events from touches by default, so an unmodified SDL backend would already get a basic pointer. |
| Soft keyboard | **Minor (required)** | `SDL_StartTextInput()` is called at window creation (`plat_sdl2.c:61`). On Android this opens the on-screen keyboard immediately. Call it only when text entry is wanted, as the Shockolate port does (see TOUCH_CONTROLS.md). |
| Physical controllers | **Significant** (new) | The SDL2 backend has no game-controller support. The Vita mapping (`plat_vita.c:15-22`, `:177-235`) is a ready template for a `SDL_GameController` mapping. |
| Pause/resume, backgrounding | **Significant (testing)** | See Surface lifecycle. While SDL blocks in `SDL_PollEvent` during pause (default `SDL_HINT_ANDROID_BLOCK_ON_PAUSE`), the game thread holds the guest lock (shims run under it, `core.h:90-102`), so all guest threads stop. That is the desired freeze. `GetTickCount` jumps on resume (`host.c:428`), and the engine's reaction to a large frame delta is **Unknown**. |
| Crash logging | **Minor** | See Logging. Also keep `--trace` reachable from an options file (`host.c:380`). |
| Modern Android devices | **Untested** | No JIT, no executable memory, no `dlopen` of user content, and no direct syscalls, so nothing should conflict with SELinux/W^X policies. targetSdk 34-35 changes affect only storage (above). |

## 4. Required backend changes (summary)

All of these sit below `plat.h`, apart from item 1, which is a shared runtime header and should go upstream.

1. `runtime/rt.h`: aarch64 setjmp fallback (B1).
2. `port/CMakeLists.txt`: `if(ANDROID)` builds a shared `libmain.so`, links SDL2 from the Android project, `log`,
   `GLESv2` (optional, since functions are loaded dynamically) and `android`.
3. Android entry (new `port/backends/android/` or `#ifdef __ANDROID__` in `backends/sdl2`):
   - `SDL_main`, which builds argv (game path, options from a file or an intent);
   - runs `port_main` on a pthread with a large stack (8-64 MB);
   - `setenv("SS2PORT_GLES","1")` and `SS2PORT_WRITE_DIR=<app storage>`;
   - `plat_log_write` to logcat plus a log file;
   - no `SDL_StartTextInput` at start-up;
   - handle `SDL_APP_WILLENTERBACKGROUND`/`DIDENTERFOREGROUND` (pause audio, release keys, keep GL on one thread).
4. Input: the touch-control layer and the SS2 bridge produce `PlatEvent`s (TOUCH_CONTROLS.md), plus game-controller
   mapping.
5. `port/backends/posix/plat_posix.c` can be reused unchanged with `PORT_SYSTEM=posix`.

`gl_render.c`, `gl_thread.c`, `core/`, `win32/`, `crt/`, `dx/` and `video/` need no Android-specific change to start.
The GLES tweaks (precision, ES3 caps) are optional quality and performance work after first light.

## 5. Distribution constraint (non-technical, but affects the architecture)

The APK necessarily contains machine code generated from the user's `SS2.exe`, `allobjs.osm`, `Squirrel.osm` and
`lgvid.dll`. Upstream is explicit that generated code and binaries built from it "are not distributed"
(`README.md`, Legal and Ground Rules), and the Vita README says to never share the VPK. So, unlike the Shockolate
port (open engine source, distributable APK), this APK is a **personal build**. Each user lifts and builds from their
own copy. The build pipeline must stay reproducible on the user's Windows PC, and no release artefact may contain
game-derived code.

## 6. Open questions (need hardware)

- Does a 4 GB `MAP_NORESERVE` reserve succeed on the Pixel 10 under its overcommit policy? (Fallback: `--guest-space`.)
- GPU driver (Pixel 10 / Tensor G5): S3TC availability, mediump precision artefacts, shader compile time, FBO depth
  formats.
- Does the GL context survive background/foreground with SDL2's default handling?
- In "interface mode" (inventory/PDA cursor), does SS2 keep DirectInput acquired so the host stays in captured mode?
  This determines whether absolute touch positioning works unchanged (`user32.c:205-213`, `:333-353`; see
  TOUCH_CONTROLS.md).
- Frame rate and thermals: no Android measurements exist (see IMPLEMENTATION_PLAN.md §5).
