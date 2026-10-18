/* touch_ss2.c: System Shock 2 bridge for the Shockolate touch controls (touch_ss2.h).
 *
 * The imported core (touch_controls.c) owns fingers, roles, the floating stick, look accumulation, fire/use, the
 * cursor finger and the MODE toggle, and reports ordered edges through its TouchEmit callback. This file turns those
 * edges into the portable host's PlatEvents, the same stream a keyboard and mouse produce:
 *   - stick       -> W A S D held (with hysteresis), the game's +walkfast/+backfast/+move*fast bindings;
 *   - look finger -> relative mouse motion (mouse look; the game's DirectInput mouse);
 *   - FIRE / USE  -> left button held / right click (+fire_weapon, frob_object);
 *   - actions     -> Space (jump), Shift (crouch), wheel up / down (next / previous weapon), R (reload), B (ammo), Esc;
 *     these are the bindings of the game's user.bnd (which replaces SHOCK.BND; Y is not bound there);
 *   - cursor mode -> the game's own cursor placed at the touched point (PlatEvent.abs), then left clicks or drags.
 * Every press is queued with a minimum hold, so a tap shorter than a game frame still reaches the game, and keys a
 * physical keyboard holds are never released by touch (or the other way round).
 *
 * Gameplay or cursor: the game tells which mode it is in by recentring its cursor every frame in shoot (mouse look)
 * mode and never in interface mode or menus (user32_cursor_recentred_ns). The MODE button sends Tab (toggle_mouse) so
 * the game switches, and the touch mode then follows the game, also when the game switches by itself (a keypad, the
 * Esc menu, the main menu).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_ss2.h"
#include <string.h>

/* ---------------------------------------------------------------- the queue */
static void push(Ss2Touch *s, uint64_t due, PlatEvent ev) {
    if (s->qn == SS2T_QUEUE) return;       /* 256 pending edges: cannot happen with fingers; refuse new presses */
    s->q[s->qn].due = due; s->q[s->qn].ev = ev; s->qn++;
}
static void key(Ss2Touch *s, int dik, int down, uint64_t due) {
    PlatEvent e; memset(&e, 0, sizeof e); e.type = PLAT_EV_KEY; e.key = dik; e.down = down; push(s, due, e);
}
static void button(Ss2Touch *s, int b, int down, uint64_t due) {
    PlatEvent e; memset(&e, 0, sizeof e); e.type = PLAT_EV_MOUSE_BUTTON; e.button = b; e.down = down; push(s, due, e);
}
static void tap_key(Ss2Touch *s, int dik) { key(s, dik, 1, s->now); key(s, dik, 0, s->now); }
static void wheel(Ss2Touch *s, int notches) {
    PlatEvent e; memset(&e, 0, sizeof e); e.type = PLAT_EV_MOUSE_WHEEL; e.dy = notches * 120; push(s, s->now, e);
}
static void place(Ss2Touch *s, float x, float y) {          /* the game's cursor at the touched point */
    float gx, gy;
    TouchRect pic = { s->pic_x, s->pic_y, s->pic_w, s->pic_h };
    if (s->pic_w <= 0 || s->pic_h <= 0 || s->game_w <= 0 || s->game_h <= 0) return;
    touch_map_cursor(x, y, pic, s->pic_w / (float)s->game_w, s->pic_h / (float)s->game_h, s->game_w, s->game_h, &gx, &gy);
    if (s->ui_w > 0 && s->ui_h > 0) {      /* the engine scales the whole window onto its 2D picture (ss2t_ui_rect) */
        gx = (gx - (float)s->ui_x) * (float)s->game_w / (float)s->ui_w; gy = (gy - (float)s->ui_y) * (float)s->game_h / (float)s->ui_h;
        if (gx < 0) gx = 0; if (gx > (float)(s->game_w - 1)) gx = (float)(s->game_w - 1);
        if (gy < 0) gy = 0; if (gy > (float)(s->game_h - 1)) gy = (float)(s->game_h - 1);
    }
    PlatEvent e; memset(&e, 0, sizeof e); e.type = PLAT_EV_MOUSE_MOVE; e.abs = 1; e.x = (int)gx; e.y = (int)gy;
    push(s, s->now > s->move_hold_until ? s->now : s->move_hold_until, e);
}

/* everything touch holds goes up now; pending presses are dropped */
static void release_touch(Ss2Touch *s) {
    s->qn = 0;
    for (int k = 0; k < 256; k++) if (s->touch_key[k]) { s->touch_key[k] = 1; key(s, k, 0, 0); }
    for (int b = 0; b < 5; b++) if (s->touch_btn[b]) { s->touch_btn[b] = 1; button(s, b, 0, 0); }
    memset(s->stick_key, 0, sizeof s->stick_key);
}

/* ---------------------------------------------------------------- core edges -> game input */
static void action(Ss2Touch *s, TouchAction a, bool down) {
    switch (a) {
    case TOUCH_JUMP: key(s, SS2_DIK_SPACE, down, s->now); break;          /* +jump: held */
    case TOUCH_CROUCH: key(s, SS2_DIK_LSHIFT, down, s->now); break;       /* crouch */
    case TOUCH_WEAPON_NEXT: if (down) wheel(s, 1); break;                 /* mouse_wheel_up: cycle_weapon 1 */
    case TOUCH_WEAPON_PREV: if (down) wheel(s, -1); break;                /* mouse_wheel_down: cycle_weapon -1 */
    case TOUCH_CONSUMABLE: if (down) tap_key(s, SS2_DIK_R); break;        /* RELOAD: reload_gun */
    case TOUCH_PAUSE: if (down) tap_key(s, SS2_DIK_ESC); break;           /* sim_menu, skips cutscenes too */
    case TOUCH_AMMO:                                                      /* cycle_ammo; in cursor mode a right click (use an item) */
        if (!down) break;
        if (s->t.effective_mode == TOUCH_CURSOR) { button(s, 1, 1, s->now); button(s, 1, 0, s->now); }
        else tap_key(s, SS2_DIK_B);
        break;
    default: break;                                                       /* hidden in this layout */
    }
}
static void output(void *user, TouchOutput o, float x, float y) {
    Ss2Touch *s = user;
    switch (o) {
    case TOUCH_FIRE_DOWN: button(s, 0, 1, s->now); break;
    case TOUCH_FIRE_UP: button(s, 0, 0, s->now); break;
    case TOUCH_USE_PRESS: button(s, 1, 1, s->now); button(s, 1, 0, s->now); break;
    case TOUCH_ACTION_DOWN: case TOUCH_ACTION_UP: action(s, (TouchAction)(int)x, o == TOUCH_ACTION_DOWN); break;
    case TOUCH_POINTER_MOVE: place(s, x, y); break;
    case TOUCH_POINTER_DOWN:               /* a drag: press at the start point; the game picks up what is there before the cursor moves */
        s->move_hold_until = 0; place(s, x, y); button(s, 0, 1, s->now); s->move_hold_until = s->now + SS2T_MIN_HOLD_NS; break;
    case TOUCH_POINTER_UP: place(s, x, y); button(s, 0, 0, s->now); break;
    case TOUCH_POINTER_TAP: s->move_hold_until = 0; place(s, x, y); button(s, 0, 1, s->now); button(s, 0, 0, s->now); break;
    case TOUCH_MODE_CHANGED:
        if (!s->syncing) { s->user_toggle_at = s->now; tap_key(s, SS2_DIK_TAB); }   /* toggle_mouse: shoot <-> interface */
        break;
    case TOUCH_CANCEL: release_touch(s); break;
    case TOUCH_SETTINGS_SAVE: if (s->save_settings) s->save_settings(s); else s->t.save_failed = true; break;
    case TOUCH_HAPTIC: if (s->haptic) s->haptic(s, (int)x); break;
    default: break;                        /* radials are off; no UI-change or QA handling needed */
    }
    (void)y;
}

/* ---------------------------------------------------------------- public */
void ss2t_init(Ss2Touch *s) {
    memset(s, 0, sizeof *s);
    touch_init(&s->t, output, s);
    s->t.radial_populate = NULL;           /* no item wheels yet: hold-to-wheel cancels, a tap still acts */
}

void ss2t_frame(Ss2Touch *s, uint64_t now, float w, float h, bool enabled, float px, float py, float pw, float ph,
                int game_w, int game_h, uint64_t recentred) {
    s->now = now; s->recentred = recentred;
    if (px != s->pic_x || py != s->pic_y || pw != s->pic_w || ph != s->pic_h || game_w != s->game_w || game_h != s->game_h) {
        touch_cancel(&s->t);               /* the picture moved: cursor mapping and held controls are stale */
        s->pic_x = px; s->pic_y = py; s->pic_w = pw; s->pic_h = ph; s->game_w = game_w; s->game_h = game_h;
    }
    enabled = enabled && pw > 0 && ph > 0 && game_w > 0 && game_h > 0;
    touch_configure(&s->t, w, h, enabled, true, false);
    s->t.look_scale = pw > 0 ? (float)game_w / pw : 1.0f;   /* a swipe moves the view by game pixels, at any resolution */
    if (s->t.stick_reference_height != ph) { s->t.stick_reference_height = ph; touch_layout_refresh(&s->t); }
    if (recentred && recentred != s->recent[0]) { s->recent[2] = s->recent[1]; s->recent[1] = s->recent[0]; s->recent[0] = recentred; }
    s->game_shoot = s->recent[2] && now >= s->recent[2] && now - s->recent[2] < SS2T_SHOOT_NS;
    TouchMode want = s->game_shoot ? TOUCH_GAMEPLAY : TOUCH_CURSOR;
    if (s->t.enabled && s->t.mode != want && s->t.panel == TOUCH_UI_CLOSED &&
        (!s->user_toggle_at || now - s->user_toggle_at >= SS2T_FOLLOW_NS)) {
        s->syncing = true; touch_set_mode(&s->t, want); s->syncing = false;
    }
    touch_tick(&s->t, (uint32_t)(now / 1000000u));
}

void ss2t_ui_rect(Ss2Touch *s, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || (x == 0 && y == 0 && w == s->game_w && h == s->game_h)) x = y = w = h = 0;
    s->ui_x = x; s->ui_y = y; s->ui_w = w; s->ui_h = h;
}

void ss2t_finger(Ss2Touch *s, TouchPhase phase, int64_t device, int64_t finger, float x, float y, uint32_t ms) {
    touch_finger(&s->t, phase, device, finger, x, y, ms);
}

void ss2t_cancel(Ss2Touch *s) {
    touch_cancel(&s->t);
    memset(s->phys_key, 0, sizeof s->phys_key); memset(s->phys_btn, 0, sizeof s->phys_btn);   /* the host forgets them too */
}

bool ss2t_physical(Ss2Touch *s, const PlatEvent *e) {
    if (e->type == PLAT_EV_KEY) {
        int k = e->key & 0xFF;
        if (e->repeat) return !s->touch_key[k];
        if ((s->phys_key[k] != 0) == (e->down != 0)) return false;
        s->phys_key[k] = (uint8_t)(e->down != 0);
        return !s->touch_key[k];
    }
    if (e->type == PLAT_EV_MOUSE_BUTTON && e->button >= 0 && e->button < 5) {
        if ((s->phys_btn[e->button] != 0) == (e->down != 0)) return false;
        s->phys_btn[e->button] = (uint8_t)(e->down != 0);
        return !s->touch_btn[e->button];
    }
    return true;
}

/* stick deflection -> movement keys, with hysteresis per key */
static void stick(Ss2Touch *s) {
    bool active = s->t.enabled && s->t.effective_mode == TOUCH_GAMEPLAY && s->t.move_owner >= 0;
    float v[4] = { -s->t.movement_y, -s->t.movement_x, s->t.movement_y, s->t.movement_x };   /* W A S D */
    static const int dik[4] = { SS2_DIK_W, SS2_DIK_A, SS2_DIK_S, SS2_DIK_D };
    for (int i = 0; i < 4; i++) {
        bool on = active && (s->stick_key[i] ? v[i] > SS2T_STICK_OFF : v[i] > SS2T_STICK_ON);
        if (on != s->stick_key[i]) { s->stick_key[i] = on; key(s, dik[i], on, s->now); }
    }
}

int ss2t_flush(Ss2Touch *s, PlatEvent *out, int max) {
    int n = 0;
    stick(s);
    uint8_t blocked_key[256]; bool blocked_mouse = false; memset(blocked_key, 0, sizeof blocked_key);
    int keep = 0;
    for (int i = 0; i < s->qn; i++) {
        Ss2Pending p = s->q[i];
        PlatEvent *e = &p.ev;
        bool mouse = e->type != PLAT_EV_KEY;  /* cursor placement and buttons stay in order with each other */
        int k = e->key & 0xFF;
        bool blocked = mouse ? blocked_mouse : blocked_key[k];
        uint64_t due = p.due;
        if (!blocked && p.due && !e->down && e->type == PLAT_EV_KEY && s->touch_key[k] == 1 && due < s->pressed_at_key[k] + SS2T_MIN_HOLD_NS)
            due = s->pressed_at_key[k] + SS2T_MIN_HOLD_NS;
        if (!blocked && p.due && !e->down && e->type == PLAT_EV_MOUSE_BUTTON && s->touch_btn[e->button] == 1 &&
            due < s->pressed_at_btn[e->button] + SS2T_MIN_HOLD_NS)
            due = s->pressed_at_btn[e->button] + SS2T_MIN_HOLD_NS;
        if (blocked || s->now < due || n == max) {
            if (mouse) blocked_mouse = true; else blocked_key[k] = 1;
            s->q[keep++] = p; continue;
        }
        if (e->type == PLAT_EV_KEY) {          /* aggregate: only the first touch press and the last release are edges */
            if (e->down) { if (s->touch_key[k]++ == 0) { s->pressed_at_key[k] = s->now; if (!s->phys_key[k]) out[n++] = *e; } }
            else if (s->touch_key[k] && --s->touch_key[k] == 0 && !s->phys_key[k]) out[n++] = *e;
        } else if (e->type == PLAT_EV_MOUSE_BUTTON) {
            int b = e->button;
            if (e->down) { if (s->touch_btn[b]++ == 0) { s->pressed_at_btn[b] = s->now; if (!s->phys_btn[b]) out[n++] = *e; } }
            else if (s->touch_btn[b] && --s->touch_btn[b] == 0 && !s->phys_btn[b]) out[n++] = *e;
        } else out[n++] = *e;                  /* cursor placement */
    }
    s->qn = keep;
    if (n < max) {                             /* look: relative mouse motion, only in gameplay */
        int dx, dy; touch_consume_look(&s->t, &dx, &dy);
        if ((dx || dy) && s->t.effective_mode == TOUCH_GAMEPLAY && s->t.enabled) {
            PlatEvent e; memset(&e, 0, sizeof e); e.type = PLAT_EV_MOUSE_MOVE; e.dx = dx; e.dy = dy; out[n++] = e;
        }
    }
    return n;
}
