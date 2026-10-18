# Test tools (`android/tools/`)

## `uhid_input.c`: a USB keyboard and mouse made from adb

A test-only tool. It runs on the device as the `shell` user, which may open `/dev/uhid`. It creates one HID device with
a keyboard and a mouse. Android treats it like any external keyboard and mouse:

- it reports `KEYBOARD | ALPHAKEY | EXTERNAL` and `CURSOR | EXTERNAL` in `dumpsys input`;
- keys can be held for as long as needed;
- SDL's relative mouse mode gets real pointer capture with raw motion, as with a Bluetooth or USB mouse.

So it exercises the same path in the app as a physical keyboard and mouse. The app does not know about it, and it is
not a touch-control system. Nothing in the APK depends on it.

**Build and start**

```sh
NDK=$ANDROID_HOME/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin    # linux-x86_64 on Linux
$NDK/clang --target=aarch64-linux-android26 -O2 -Wall -Wextra -o build/android/tools/uhid_input android/tools/uhid_input.c
adb push build/android/tools/uhid_input /data/local/tmp/ && adb shell chmod 755 /data/local/tmp/uhid_input
adb shell 'nohup /data/local/tmp/uhid_input /data/local/tmp/uhid.cmd > /data/local/tmp/uhid.log 2>&1 &'
```

Git Bash on Windows rewrites `/data/...` arguments. Set `MSYS_NO_PATHCONV=1`, or run these from PowerShell.

**Send commands** by appending lines to the command file. SELinux does not let the shell user create FIFOs in
`/data/local/tmp`, so the tool follows a plain file, like `tail -f`.

```sh
adb shell "echo 'tap esc' >> /data/local/tmp/uhid.cmd"
adb shell "printf '%s\n' 'down w' 'sleep 2000' 'up w' 'move 400 0 20 400' 'click 2' >> /data/local/tmp/uhid.cmd"
adb shell "echo quit >> /data/local/tmp/uhid.cmd"        # removes the device again
```

| Command | Effect |
| --- | --- |
| `down KEY`, `up KEY`, `tap KEY [ms]` | Hold, release, or press a key for `ms` (default 80). Keys: `a`-`z`, `0`-`9`, `esc`, `enter`, `space`, `tab`, `backspace`, `minus`, `equal`, `comma`, `period`, `slash`, `semicolon`, `apostrophe`, `grave`, brackets, `backslash`, `f1`-`f12`, arrows, `home`, `end`, `pageup`, `pagedown`, `insert`, `delete`, `lshift`, `rshift`, `lctrl`, `rctrl`, `lalt`, `ralt`, `kp0`-`kp9`, `kpenter` |
| `move DX DY [STEPS] [MS]` | Relative motion, split into `STEPS` reports over `MS` milliseconds |
| `btn N down`, `btn N up`, `click [N] [ms]` | Mouse button `N` (1 left, 2 right, 3 middle) |
| `wheel N`, `sleep MS`, `release`, `quit` | `release` lets go of every key and button |

**Touchscreen mode (`-t`).** `uhid_input -t FILE` creates a 5-contact HID multitouch touchscreen instead. Android
classifies it as `TOUCH | TOUCH_MT | EXTERNAL`, source `TOUCHSCREEN`, and delivers its contacts through the normal input
stack as several fingers at once, which `adb shell input` cannot do. It answers the kernel's contact-maximum feature
request. Android maps an external touchscreen in the display's natural (portrait) orientation, so send `rotate 90` first
to give coordinates in landscape screen pixels.

| Command (`-t`) | Effect |
| --- | --- |
| `touch ID down X Y`, `touch ID move X Y`, `touch ID up` | Contact ID (0-4) at screen pixel X, Y |
| `swipe ID X0 Y0 X1 Y1 [STEPS] [MS]` | Move a contact that is down; the other contacts stay put |
| `screen W H`, `rotate 90` / `rotate 0` | Coordinate space (default 2340×1080); landscape mapping |

```sh
adb shell 'nohup /data/local/tmp/uhid_input -t /data/local/tmp/touch.cmd > /data/local/tmp/touch.log 2>&1 &'
adb shell "printf '%s\n' 'rotate 90' 'touch 0 down 468 900' 'swipe 0 468 900 468 760 6 120' 'touch 1 down 1400 400' \
  'swipe 1 1400 400 1600 400 10 400' 'touch 1 up' 'touch 0 up' >> /data/local/tmp/touch.cmd"
```

**Pointing at a menu item.** While the game holds the mouse in relative mode (it does in its menus too), the game moves its
own cursor by the raw motion. `move -3000 -3000` therefore puts the cursor at the top left of the game picture, and a
second `move X Y` puts it at game pixel (X, Y) of the 800×600 picture.

**Notes**

- Samsung One UI shows a one-time "Use Esc as Back?" prompt the first time a new keyboard presses Esc. Dismiss it on the
  device.
- While the device exists, Android hides the on-screen keyboard (a hardware keyboard is attached). End it with `quit`.
