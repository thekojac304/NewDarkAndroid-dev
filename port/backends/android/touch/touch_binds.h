/* touch_binds.h: which key the player's own bind file gives a game command (System Shock 2's user.bnd).
 * Touch buttons press keys, so they only work when the key matches the installation's bindings. The stock 2.48
 * user.bnd binds Shift to crouch; a player's own user.bnd may not (the user's SCP Beta 4 installation: Shift creeps, C
 * crouches, Ctrl crouches while held). No SDL, no host: tested on the PC (tests/).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_BINDS_H
#define TOUCH_BINDS_H
#include <stddef.h>

/* The DirectInput scan code of a bind-file key name ("c", "shift", "keypad_center", "f1"), 0 when it is not a single
   keyboard key (a mouse or joystick name, a chord such as "c+shift", or unknown). */
int touch_bind_key_dik(const char *name);

typedef struct {
    int dik;                 /* the key to press, 0: none found */
    char key[24];            /* its name in the bind file */
    char command[24];        /* the command it is bound to (one of those asked for) */
} TouchBind;

/* Reads a bind file's text (n bytes, need not be NUL-terminated): the last "bind KEY COMMAND" of each key counts, as
   when the game reads it. Among the keys now bound to commands[0], then commands[1], ... (exactly that command, with or
   without quotes), returns the best: the key `prefer` (the build's default, so an installation with the default
   bindings keeps the tested behaviour), then a plain key, then Shift/Ctrl/Alt, then a keypad key; chords are never used.
   Returns 1 when a key was found. */
int touch_bind_find(const char *text, size_t n, const char *const *commands, int ncommands, int prefer, TouchBind *out);
#endif
