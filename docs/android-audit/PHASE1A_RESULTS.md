# Phase 1A results: Android ARM64 runtime bring-up

2026-10-17. Scope: back up the Shockolate touch work, fix the arm64 `setjmp` blocker (B1), and run the game-free
conformance suite for Android arm64. The test phone is a **Samsung Galaxy S26 Ultra**: the owner switched to it from the
Pixel 10 named in the plan. No game code was built, no APK was made, and no touch, NVScript or SCP work was
started.

## Summary

| Item | Result |
| --- | --- |
| Shockolate backup | **Done, verified.** Bundle plus byte-exact copies of all uncommitted work. The checkout was not changed. |
| B1 (`__builtin_setjmp` on arm64) | **Fixed** in `runtime/rt.h`. The NDK build of the conformance host now has no errors and no warnings. |
| Other platforms | **Unchanged.** `rt.h` preprocesses to byte-identical code for every existing target. Conformance passes on Windows x64 and Linux x86-64 (GCC). |
| Conformance on arm64 (QEMU user-mode emulation) | **0 of 511 failed**, both default and `--cache-regs --abi-check`. |
| Conformance on the device (Galaxy S26 Ultra) | **0 of 511 failed**, default and `--cache-regs --abi-check`, 5 runs each. The setjmp test passed 5 runs at `-O0` and `-O2`. |
| New regression coverage | `port/tests/setjmp` (host primitive) and 2 new guest checks in the conformance suite. Mutation-tested. |

QEMU (user-mode emulation of the same arm64 code and bionic) ran first, while no phone was connected. **The device
runs (§6) are the acceptance evidence**, and they agree with QEMU. Phase 1A's acceptance criterion is **met** on the
S26 Ultra.

## 1. Shockolate baseline backup

| | |
| --- | --- |
| Checkout | `<Shockolate Android checkout>` (main worktree). `<local path>` is a second worktree of the same repository, detached at `main` `0d71a89`. |
| Branch / HEAD | `touch-controls` @ `750237049b2e9906f5e3345ff05522558b9a9f47`, "DECKLIGHT-CLOSE2: document live QA and final deck status"; 38 commits ahead of `origin/touch-controls` |
| Uncommitted work | 32 modified tracked files, 47 untracked (non-ignored) files, no stashes |
| Backup location | `<local path>` (outside both repositories; see `README.txt` there) |

**Contents**

| File | What it holds |
| --- | --- |
| `touch-controls.bundle` | Refs `touch-controls` and `main` |
| `uncommitted-tracked.patch` | `git diff --binary HEAD` |
| `modified-files-worktree.tar` / `untracked-files.tar` | Byte-exact copies of all 79 files |
| `worktree-sha256.txt` | Checksum manifest of the 79 files |
| `BACKUP-SHA256.txt` | Checksums of the backup artefacts |

**Verification**

1. `git bundle verify`: "The bundle records a complete history."
2. A clone from the bundle has HEAD `7502370…` and 38 commits over `main`.
3. The patch applies cleanly (`git apply --check`) to that clone.
4. After extracting the tars, `sha256sum -c` matches all 79 files.
5. Patch-only restore: 71 files match byte for byte. The other 8 differ only in line endings, because the
   user-global `core.autocrlf=true` makes git normalise the patch. The tars are the byte-exact copy.
6. After the backup, the original worktree still matches the manifest, HEAD is unchanged, and `git status` still
   lists the same 79 entries.

Nothing was pushed, reset, cleaned, stashed or checked out in the Shockolate repository.

## 2. Prerequisites found

| Item | State |
| --- | --- |
| Android SDK | `%ANDROID_HOME%` (`ANDROID_HOME` set). Platforms android-34 and android-37.0. |
| NDK | `29.0.14206865` (r29), `Android (13989888, based on r563880c) clang version 21.0.0`. Only NDK installed; `ANDROID_NDK_ROOT` is not set. |
| CMake / Ninja | SDK `cmake 3.22.1` and `ninja 1.10.2` (neither is on `PATH`; the runner finds them in the SDK) |
| adb | `1.0.41` (platform-tools 37.0.1). At first `adb devices` was **empty** (checked 5 times; `adb mdns services` and Windows PnP showed nothing either). Later the owner connected the Galaxy S26 Ultra, serial `<serial>` (§6). |
| Python | 3.14.5 (global) with `capstone` 5.0.9 and `pytest`; **no** `pefile` or `ziglang` |
| Installed | A venv, `<repo>\build\venv`, with `--system-site-packages` (reuses the global capstone) plus `pefile` 2024.8.26 and `ziglang` 0.16.0. These are the two packages the suite needs (`run_conformance.py:9`). The global Python is unchanged, and `build/` is git-ignored. |
| Docker | 29.7.2 (WSL2 kernel 6.18). Existing local images were used: `gcc:13` (GCC 13.5.0, amd64), with Docker Desktop's QEMU `binfmt` handler for arm64. **Nothing was pulled.** |
| NewDark installs | Only `<LaunchBox install>\SS2.exe` (ProductVersion **2.47**, 5,116,416 bytes). It was read only, by its version resource. **No 2.48 install was found.** Not needed in this phase. |

## 3. The setjmp fix

### Problem (reproduced before the change)

```
nd_f000.c:51255:33: error: __builtin_setjmp is not supported for the current target
port/core/host.c:303:29: error: __builtin_longjmp is not supported for the current target
```

These were the only two errors in a full NDK build of the conformance host. The build was run with
`ninja -k 0`, so every file was compiled.

### Change (`runtime/rt.h`, +10 lines)

A new branch between the existing `_WIN64` branch and the builtin default:

```c
#elif defined(__aarch64__) && defined(__clang__)
#include <setjmp.h>
#define RT_HJ_WORDS (sizeof(jmp_buf) / sizeof(void *) + 1)
#define RT_HOST_SETJMP(b) _setjmp(*(jmp_buf *)(void *)(b))
#define RT_HOST_LONGJMP(b) _longjmp(*(jmp_buf *)(void *)(b), 1)
```

No generated code, lifter, `host.c` or backend changed.

### Why this is correct, not just compilable

**Function choice**

- `_setjmp` and `_longjmp` are used rather than `setjmp` and `longjmp`.
- Bionic's `setjmp` saves the signal mask with a `sigprocmask` system call on every guest setjmp. The builtin never
  touched the mask. `_setjmp` keeps those semantics and that cost.

**Calling convention**

- Bionic declares `int _setjmp(jmp_buf) __returns_twice` and `__noreturn void _longjmp(jmp_buf, int)`. Clang also
  knows `_setjmp` as a returns-twice library builtin. The compiler therefore keeps the frame and does not tail-call
  across it.
- The arm64 `jmp_buf` is `long[32]` (256 bytes). It saves x19–x30, sp and d8–d15: every AAPCS64 callee-saved
  register. `RT_HJ_WORDS` is 33.
- In the disassembly, `SjRec.buf` sits at offset `0x108` (264 bytes = 33 words). `SjRec.hj` stays first and
  `_Alignas(16)`.

**Frame lifetime**

- `RT_SETJMP` is a macro, so `_setjmp` is called directly in the recompiled caller's own C frame.
- That frame is live whenever `rt_longjmp` jumps back, because the guest longjmp comes from a callee. This is the
  same rule the builtin required.

**What survives the second return**

- The C standard guarantees only locals that are not modified between `setjmp` and `longjmp`.
- With the builtin, LLVM forced live values to the stack. With `_setjmp`, they come back in callee-saved registers
  that `_longjmp` restores.
- What generated code relies on after the second return:
  - **Default lift:** guest state lives in `c->…` memory. The disassembly of the generated setjmp site
    (`nd_00403eb0`) uses only `x19` (`CPU *c`), `w20` (guest jump-buffer address) and `x21` (`&M`) after `_setjmp`
    returns. All three are callee-saved, set before the call and never modified.
  - **Flag temporaries:** the per-function `CF`/`ZF`/`SF`/`OF`/`PF` locals are rewritten by the next guest instruction
    before they are read. No x86 code reads flags across a `call`.
  - **`--cache-regs`:** the lifter wraps the site as `{ SPILL_; RT_SETJMP(c); RELOAD_; }` (`lift.py:348`). The
    generated code reloads all eight `r_*` registers from `c->` right after the jump. So `r_*` locals modified after
    the setjmp are overwritten before use. `lift_x87.py:47` already excludes functions containing `RT_SETJMP` from
    x87 caching.

**Stack alignment**

- `_longjmp` restores the exact sp that `_setjmp` saw, and AAPCS64 keeps that 16-byte aligned.
- The new test checks this directly: frame address and stack position are identical after every jump, 16-byte
  aligned, with no drift over 200,000 jumps.

**Selection condition: `__aarch64__ && __clang__`**

- The failure belongs to clang's arm64 backend, not to Android.
- GCC on arm64 supports the builtin, so it keeps it.
- Clang on arm64 with glibc, musl and macOS also compiles with the new branch (checked by compiling with Zig). Those
  hosts were not run.

### Behaviour on other platforms

`rt.h` before and after was preprocessed through a translation unit that uses `RT_SETJMP`, `RT_HOST_LONGJMP` and
`RT_HJ_WORDS`:

| Target (Zig 0.16 clang) | Result |
| --- | --- |
| x86_64-linux-gnu | Identical (1088 lines) |
| x86_64-windows-gnu (`_WIN64` branch) | Identical (976 lines) |
| x86-linux-gnu | Identical (1097 lines) |
| arm-linux-gnueabihf (32-bit ARM; the Vita's architecture) | Identical (1077 lines) |
| aarch64 with `__clang__` undefined (the GCC path) | Still `__builtin_setjmp` |
| aarch64-linux-gnu clang | Changed: the new branch. The old `rt.h` fails here with the same two errors. |

The Vita (arm-vita-eabi GCC, `__arm__`, not `__aarch64__`) therefore takes the unchanged builtin path. The Vita SDK is
not installed, so no Vita build was run.

## 4. Regression coverage added

### `port/tests/setjmp/` (new): `test.c` and `run.sh [android [SERIAL]]`

- Built at `-O0` and `-O2`.
- It uses `RT_HOST_SETJMP`/`RT_HOST_LONGJMP` the way `core/host.c` does, with the jump buffer first in a 16-aligned
  record and guard fields after it.
- **6075 checks:**
  - longjmp from 0–63 recursive frames of varying size, each saving and reusing callee-saved registers;
  - 10 integer and 8 double locals live across the setjmp, compared with copies in memory;
  - frame address and stack position identical and 16-byte aligned after each jump, and constant across 200,000
    jumps;
  - guard fields after the buffer intact (catches an undersized `RT_HJ_WORDS`);
  - the real `RT_SETJMP` macro with a guest side modelled on `host.c`: 1000 round trips, guest
    EBX/ESI/EDI/EBP/ESP restored, value 0 → 1;
  - 4 threads × 20,000 jumps each.

**Mutation check** (scratch builds, not committed; run under QEMU arm64):

| Mutant | `-O0` | `-O2` |
| --- | --- | --- |
| Jump that restores only x29/x30/sp | Passes: at `-O0` every local is on the stack | Crashes (SIGSEGV), non-zero exit |
| `RT_HJ_WORDS 5` with the real `_setjmp` | Crashes, non-zero exit | Crashes, non-zero exit |

The `-O2` build is the one that matters. `run.sh` always builds both.

While writing the test, its first version raced between threads in a stack-probe helper, which shared a global. That
was a bug in the test, not in the runtime. It was caught at `-O0` under QEMU and fixed. Afterwards there were 5/5
clean runs at each level.

### `port/tests/guest/conformance.c` (+2 checks; 509 → 511 on `null`)

- `sj_deep` calls `_setjmp3` with three locals live across it. The guest compiler (Zig, `-O1`, x86) puts them in
  EBX/EDI/ESI, as the lifted listing confirms.
- `sj_down` recurses 0–20 levels, overwriting EBX/ESI/EDI, then calls `longjmp`. This runs 420 times.
- The checks: every result is correct, and the guest stack position is the same afterwards.
- This exercises the real `rt_sj_begin`/`rt_sj_resume`/`rt_longjmp` in `core/host.c`, through lifted code, in both
  lift modes.
- `port/README.md` now says 511 (`null`) and 538 (`sdl2`). The +2 checks are in the `crt` section, which runs on every
  backend. **The `sdl2` figure is arithmetic: `sdl2` was not run.**

### `port/tests/run_conformance.py --target android`

- New options: `--ndk`, `--android-api` (default 26), `--serial`, `--device-dir`
  (default `/data/local/tmp/ss2conformance`).
- What it does:
  - finds the NDK (`--ndk`, `ANDROID_NDK_ROOT`/`_HOME`, else the newest under `ANDROID_HOME/ndk`);
  - takes CMake, Ninja and adb from `PATH` or the SDK;
  - configures with the NDK toolchain file for `arm64-v8a`;
  - pushes `ss2port` and `conformance.exe` to a fresh directory on the device and runs them with `adb shell`, which
    returns the exit status;
  - with `--abi-check`, reads the device's `*.log` files.
- Only `--backend null` is allowed.
- The native and `--target windows` paths are unchanged apart from the summary label.

## 5. Test commands and results

### Commands

The working directory is `<repo>` (Git Bash). `PY=build/venv/Scripts/python.exe`.

```bash
# Android arm64 on the device (see §6 for the exact runs)
$PY port/tests/run_conformance.py --target android --serial <serial> --build build/conf-android [--no-rebuild-guest]
$PY port/tests/run_conformance.py --target android --serial <serial> --cache-regs --abi-check --build build/conf-android-cr [--no-rebuild-guest]
ADB=$ANDROID_HOME/platform-tools/adb sh port/tests/setjmp/run.sh android <serial>

# Windows x64, natively. See the note below the results table.
$PY port/tests/run_conformance.py --target windows [--cache-regs --abi-check] --build build/conf-win[-cr]   # guest + lift
cmake -S port -B build/conf-win/host-null-win -G Ninja -DPORT_GENERATED=… -DPORT_BACKEND=null -DCMAKE_TOOLCHAIN_FILE=<scratch>/windows-zig-native.cmake [-DPORT_ABI_CHECK=ON]
CC="$PY -m ziglang cc -target x86_64-windows-gnu" LIBS= sh port/tests/setjmp/run.sh

# arm64 under QEMU: the same NDK build with -DCMAKE_EXE_LINKER_FLAGS=-static, run in the local gcc:13 container
docker run --rm -v <repo>/build:/b gcc:13 sh -c 'cd /b/conf-android/qemu-run && ./ss2port conformance.exe'

# Linux x86-64 GCC 13.5 in the local gcc:13 container: a scratch script that compiles the port/CMakeLists.txt
# source set with its flags (RelWithDebInfo, null backend; the image has no CMake), then runs it
CC=gcc sh port/tests/setjmp/run.sh
```

**Native Windows build:** upstream's `windows-zig.cmake` wraps Zig in `#!/bin/sh` scripts, which Windows cannot run
(`CreateProcess: %1 is not a valid Win32 application`). The workaround is a scratch toolchain file that uses `.cmd`
wrappers. This is pre-existing and was not changed. Upstream documents the Windows target as cross-built from Linux or
WSL.

### Results

| Platform | Mode | Result |
| --- | --- | --- |
| Android arm64, NDK r29 (dynamic PIE, `ss2port` 11.6 MB) | Build, default and `--cache-regs --abi-check` | Builds; 0 errors, 0 warnings |
| arm64 under QEMU (static bionic, same code) | Default | **0 of 511 checks failed**, exit 0, `ExitProcess(0)` |
| arm64 under QEMU | `--cache-regs --abi-check` | **0 of 511 checks failed**, exit 0, 0 ABI violations |
| arm64 under QEMU | `setjmp` test `-O0` / `-O2` | 6075/6075 each, 5 runs each |
| Windows x64 (Zig clang, native) | Default | 0 of 511 failed, exit 0 |
| Windows x64 | `--cache-regs --abi-check` | 0 of 511 failed, 0 ABI violations |
| Windows x64 | `setjmp` test `-O0` / `-O2` | 6071/6071 each (`_WIN64` branch; the 4 `pthread_create` checks exist only on POSIX) |
| Linux x86-64 GCC 13.5 (Docker) | Default | 0 of 511 failed, exit 0 |
| Linux x86-64 GCC 13.5 | `--cache-regs --abi-check` | 0 of 511 failed, exit 0 |
| Linux x86-64 GCC 13.5 | `setjmp` test `-O0` / `-O2` | 6075/6075 each (builtin branch) |
| **Galaxy S26 Ultra** (Android 17, adb shell) | Default | **0 of 511 checks failed**, `ExitProcess(0)`, PASS; 5/5 runs |
| **Galaxy S26 Ultra** | `--cache-regs --abi-check` | **0 of 511 checks failed**, `ABI check: 0 call site(s)…`, PASS; 5/5 runs |
| **Galaxy S26 Ultra** | `setjmp` test `-O0` / `-O2` | **6075/6075 each**; 5/5 runs |
| **Galaxy S26 Ultra** | Broken jump mutants (`-O2`) | Both crash (SIGSEGV); `adb shell` returns status 139 |

**`--cache-regs`:** it passes on the device and in every other environment where it was run. No speed claim is made:
nothing was timed. A conformance run takes about 1.5 s, which says nothing about game performance.

### Validation areas (on the S26 Ultra, both lift modes; also under QEMU)

| Area | Evidence |
| --- | --- |
| Guest memory mapping | `vm_init` (`host.c:167-189`) reserves 4 GB for the guest at start-up; on the device: `guest address space: 4096 MB at 0x7458c00000 (4096 MB backed)`. Every check runs inside it. No `--guest-space` was needed. |
| Function dispatch | The host bound 147 imports, all implemented. The C runtime and Win32 sections cover indirect calls and re-entrant callbacks (`qsort` comparators that sort). `--cache-regs` adds the indirect-call cache and the ABI checks. |
| Threading | `threads` section: `_beginthreadex`/`CreateThread`, critical sections, interlocked operations, events, `PulseEvent`, WINMM timers. The device's `--verbose` log shows the guest threads starting and ending with the expected codes (11, 22, 7). Plus 4 host threads in the setjmp test. |
| Stack behaviour | Deep guest recursion. The new check that the stack position is unchanged after 420 nonlocal jumps. Exact stack and frame restore in the setjmp test. |
| Nonlocal jumps | 3 original and 2 new guest checks. The setjmp test. |
| Filesystem | `stdio` section: stdio, low-level and Win32 file APIs, case-insensitive paths, mmio RIFF from a file. It runs in the test's working directory. |
| Runtime initialisation | "running the recompiled entry point 004010a0 (147 imports, 0 without an implementation)" |
| Process exit | `ExitProcess(0)`, host exit status 0. On the device, `adb shell` returns the status: 0 for passing runs, 139 for the crashing mutants. So a failure cannot be reported as a pass. |

## 6. Device evidence: Samsung Galaxy S26 Ultra

The owner chose the **Galaxy S26 Ultra** as the target phone in place of the Pixel 10. The device was connected over
USB with debugging authorised, and all runs were done through `adb shell` as the `shell` user.

| Property | Value |
| --- | --- |
| Model | `SM-S948U1` (`ro.product.device` `m3q`), serial `<serial>` |
| SoC | Qualcomm `SM8850` (`ro.soc.manufacturer` QTI), 8 CPUs, `arm64-v8a` |
| Android | 17, API 37; build `samsung/m3quew/m3q:17/CP2A.260605.016/S948U1UES4BZIG_OYM4BZIG:user/release-keys` |
| Kernel | `6.12.69-android16-6-pb4d3caf-abogkiS948USQS4BZIG-4k`, aarch64 |
| Page size | 4096 (a 4 KB kernel; 16 KB pages remain untested) |
| RAM | MemTotal 11,350,976 kB |
| Shell | `uid=2000(shell)`, SELinux **Enforcing**, context `u:r:shell:s0`, `ulimit -s` 8192 KB |

**Results** (binaries from the commits in §8: NDK r29, API 26, dynamic PIE; installed only under `/data/local/tmp`)

| Run | Result |
| --- | --- |
| `sh port/tests/setjmp/run.sh android <serial>` ×5 | `-O0` and `-O2`: `setjmp (C library _setjmp / _longjmp, RT_HJ_WORDS=33): 6075 checks, all passed`, every run |
| `run_conformance.py --target android --serial <serial> --build build/conf-android --no-rebuild-guest` ×5 | `conformance: 0 of 511 checks failed`, `[port] ExitProcess(0)`, `conformance (null, android): PASS`, every run |
| `run_conformance.py … --cache-regs --abi-check --build build/conf-android-cr --no-rebuild-guest` ×5 | `0 of 511 checks failed`, `ABI check: 0 call site(s) broke the calling-convention contract`, PASS, every run |
| `./ss2port --verbose conformance.exe` (default build) | `guest address space: 4096 MB at 0x7458c00000 (4096 MB backed)`; guest thread stacks mapped; threads 1008/1012/1016 ended with 11/22/7 |
| Scratch mutants `mutant-noregs`, `mutant-smallbuf` (`-O2`, §4) | `Segmentation fault`, `adb shell` exit status **139**. The real test returns 0. |

The first default run's full log is `build/conf-android/device-run.txt`. The `[port] not found` lines in it are the
expected misses that the `stdio` section probes for.

**Two problems in `port/tests/setjmp/run.sh android`, fixed in `92a701a`** (test script only):

1. Git Bash rewrote `/data/local/tmp/...` into a Windows path. The fix sets `MSYS_NO_PATHCONV=1`.
2. Files pushed from Windows had no execute bit, so the shell reported "Permission denied". The fix runs `chmod 755`.

The push failure also no longer slips past `set -e`. `run_conformance.py` was not affected: it passes argument lists
from native Python and already ran `chmod`.

To clean up: `adb shell rm -rf /data/local/tmp/ss2conformance /data/local/tmp/ss2setjmp`.

## 7. Regressions

None found:

- Windows x64, Linux x86-64 and the Galaxy S26 Ultra pass all 511 checks in both modes.
- Preprocessing proves the existing configurations compile identical code.

One issue appeared during the work: the test-only thread race (§4). It was fixed before the commit.

## 8. Commits and branch

| | |
| --- | --- |
| Repository | `<repo>` (upstream `Owloeb/NewDarkRecompiled`; nothing pushed) |
| Branch | `android-port`, from `android-port-audit` = `main` @ `02f77da` |
| `5a26315` | docs: Phase 0 Android port audit (the six existing reports, committed unchanged) |
| `8962e14` | runtime: arm64 clang host setjmp via `_setjmp`/`_longjmp`; Android conformance target |
| `5488c34` | docs: this report (first version, before the device runs) |
| `92a701a` | tests/setjmp: `run.sh android` works from Git Bash on Windows (§6) |
| (next) | docs: this report, with the device results |

**Files in `8962e14`**

| File | Change |
| --- | --- |
| `runtime/rt.h` | +10 lines |
| `port/tests/run_conformance.py` | +41/−7 |
| `port/tests/guest/conformance.c` | +18 |
| `port/README.md` | +4/−1 |
| `port/tests/setjmp/test.c`, `run.sh` | New |

**Not committed**

- `build/` (venv, build trees; git-ignored).
- The scratch Windows toolchain wrappers, mutants and Linux build script (session scratch directory).

## 9. Remaining blockers

No blocker remains for Phase 1A. Open points:

1. **Untested on the device:**
   - **16 KB pages.** This S26 Ultra kernel uses 4 KB pages. The host's 64 KB VM granularity should cover 16 KB, but
     it is unverified.
   - **MTE in `_longjmp`.** MTE is not enabled for shell processes.
   - **The app context.** A future APK runs as an untrusted_app with its own SELinux domain, memory limits and
     overcommit. The 4 GB reservation succeeded from `adb shell`; this has to be checked again from the APK in
     Phase 1B/1C.
2. **NewDark 2.48 install.** It is still absent and is needed before any game lift (Phase 1B onward).
3. **Native Windows builds with the upstream Zig toolchain** do not work: shell-script wrappers. This does not block
   Android, which uses NDK CMake. It only affects running `--target windows` without WSL.

One open question from the audit is now settled: running `lift.py` and `gen_hostdata.py` on native Windows Python
3.14 works. It was verified on the conformance program, not yet on `SS2.exe`.

## 10. Recommended Phase 1B scope

**Start condition:** met (§6). The target device is now the Galaxy S26 Ultra. The Phase 0 documents still name the
Pixel 10; read that as the S26 Ultra.

**In scope**

1. **Android build target:**
   - `port/CMakeLists.txt` `if(ANDROID)`: shared `libmain.so` with `-fvisibility=hidden`, keeping the executable
     target for `adb` tests;
   - `port/build.py --target android`, or a lift-only switch used from Gradle.
2. **`port/backends/android/`, headless:**
   - `plat_log_write` to logcat and a log file;
   - an options file to argv;
   - `port_main` on an 8–64 MB pthread;
   - `SS2PORT_WRITE_DIR` in app storage.
3. **With a 2.48 install:**
   - lift `out/port` on the PC (native Windows Python now looks viable; time it);
   - build the `null`-backend game for arm64;
   - run it on the S26 Ultra with `adb shell … --frames 600`;
   - acceptance: `allobjs.osm`, `Squirrel.osm` and `lgvid.dll` load as recompiled modules, "first frame presented",
     clean exit, no guest fault.
   - Repeat with `--cache-regs --abi-check`.
4. **Optional, small:** a native-Windows fallback in `port/cmake` (`.cmd` wrappers), proposed upstream together with
   the `rt.h` change.

**Out of scope for 1B:** SDL2, GLES, audio, the APK and Gradle (Phase 1C/2), touch, NVScript and SCP.
