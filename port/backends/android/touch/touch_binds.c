/* touch_binds.c: the key a bind file gives a command (touch_binds.h).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_binds.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int ieq(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

int touch_bind_key_dik(const char *name) {
    static const struct { const char *n; int d; } t[] = {
        { "esc", 0x01 }, { "-", 0x0C }, { "=", 0x0D }, { "backspace", 0x0E }, { "tab", 0x0F }, { "[", 0x1A }, { "]", 0x1B },
        { "enter", 0x1C }, { "ctrl", 0x1D }, { ";", 0x27 }, { "'", 0x28 }, { "`", 0x29 }, { "shift", 0x2A }, { "\\", 0x2B },
        { ",", 0x33 }, { ".", 0x34 }, { "/", 0x35 }, { "keypad_star", 0x37 }, { "alt", 0x38 }, { "space", 0x39 },
        { "keypad_home", 0x47 }, { "keypad_up", 0x48 }, { "keypad_pgup", 0x49 }, { "keypad_minus", 0x4A }, { "keypad_left", 0x4B },
        { "keypad_center", 0x4C }, { "keypad_right", 0x4D }, { "keypad_plus", 0x4E }, { "keypad_end", 0x4F }, { "keypad_down", 0x50 },
        { "keypad_pgdn", 0x51 }, { "keypad_ins", 0x52 }, { "keypad_del", 0x53 }, { "f11", 0x57 }, { "f12", 0x58 },
        { "keypad_enter", 0x9C }, { "keypad_slash", 0xB5 }, { "home", 0xC7 }, { "up", 0xC8 }, { "pgup", 0xC9 }, { "left", 0xCB },
        { "right", 0xCD }, { "end", 0xCF }, { "down", 0xD0 }, { "pgdn", 0xD1 }, { "ins", 0xD2 }, { "del", 0xD3 } };
    static const char letters[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";   /* DIK 0x10 onwards */
    if (!name || !*name || strchr(name + 1, '+')) return 0;                       /* a chord ("c+shift"); "+" alone is no key name here */
    if (!name[1]) {
        char c = (char)tolower((unsigned char)name[0]);
        if (c >= '1' && c <= '9') return 0x02 + (c - '1');
        if (c == '0') return 0x0B;
        for (int i = 0; i < (int)sizeof letters - 1; i++) if (letters[i] && letters[i] == c) return 0x10 + i;
    }
    if (tolower((unsigned char)name[0]) == 'f' && isdigit((unsigned char)name[1]) && (!name[2] || (isdigit((unsigned char)name[2]) && !name[3]))) {
        int k = name[1] - '0'; if (name[2]) k = k * 10 + (name[2] - '0');
        if (k >= 1 && k <= 10) return 0x3A + k;
    }
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (ieq(name, t[i].n)) return t[i].d;
    return 0;
}

/* 0 the preferred key, 1 a plain key, 2 Shift/Ctrl/Alt (they also change other keys' bindings while held), 3 keypad */
static int rank(const char *key, int dik, int prefer) {
    if (dik == prefer) return 0;
    if (dik == 0x2A || dik == 0x1D || dik == 0x38) return 2;
    if (!strncmp(key, "keypad_", 7)) return 3;
    return 1;
}

typedef struct { char key[24], cmd[48]; } Entry;

int touch_bind_find(const char *text, size_t n, const char *const *commands, int ncommands, int prefer, TouchBind *out) {
    static Entry e[512]; int ne = 0;                   /* the file's bindings, the last of each key */
    memset(out, 0, sizeof *out);
    for (size_t i = 0; i < n;) {
        char line[256]; size_t l = 0;
        while (i < n && text[i] != '\n' && text[i] != '\r') { if (l < sizeof line - 1) line[l++] = text[i]; i++; }
        while (i < n && (text[i] == '\n' || text[i] == '\r')) i++;
        line[l] = 0;
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        char word[8]; size_t w = 0;                     /* "bind" or "unbind", in any letter case */
        while (p[w] && p[w] != ' ' && p[w] != '\t' && w < sizeof word - 1) { word[w] = (char)tolower((unsigned char)p[w]); w++; }
        word[w] = 0;
        int unbind = !strcmp(word, "unbind");
        if (!unbind && strcmp(word, "bind")) continue;
        p += w;
        while (*p == ' ' || *p == '\t') p++;
        char key[24]; size_t k = 0;
        while (*p && *p != ' ' && *p != '\t') { if (k < sizeof key - 1) key[k++] = (char)tolower((unsigned char)*p); p++; }
        key[k] = 0; if (!k) continue;
        while (*p == ' ' || *p == '\t') p++;
        char cmd[48]; size_t c = 0;
        if (*p == '"') { p++; while (*p && *p != '"') { if (c < sizeof cmd - 1) cmd[c++] = *p; p++; } }
        else while (*p) { if (c < sizeof cmd - 1) cmd[c++] = *p; p++; }
        while (c && (cmd[c - 1] == ' ' || cmd[c - 1] == '\t')) c--;
        cmd[c] = 0;
        if (unbind) cmd[0] = 0;
        int j = 0; while (j < ne && strcmp(e[j].key, key)) j++;
        if (j == ne) { if (ne == (int)(sizeof e / sizeof *e)) continue; ne++; }
        snprintf(e[j].key, sizeof e[j].key, "%s", key); snprintf(e[j].cmd, sizeof e[j].cmd, "%s", cmd);
    }
    for (int ci = 0; ci < ncommands; ci++) {
        int best = -1, best_rank = 99;
        for (int j = 0; j < ne; j++) {
            if (!ieq(e[j].cmd, commands[ci])) continue;
            int dik = touch_bind_key_dik(e[j].key); if (!dik) continue;
            int r = rank(e[j].key, dik, prefer);
            if (r < best_rank) { best = j; best_rank = r; }
        }
        if (best >= 0) {
            out->dik = touch_bind_key_dik(e[best].key);
            snprintf(out->key, sizeof out->key, "%s", e[best].key); snprintf(out->command, sizeof out->command, "%s", commands[ci]);
            return 1;
        }
    }
    return 0;
}
