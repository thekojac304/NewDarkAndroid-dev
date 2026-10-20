/* touch_ss2.c: System Shock 2 bridge for the Shockolate touch controls (touch_ss2.h).
 *
 * The imported core (touch_controls.c) owns fingers, roles, the floating stick, look accumulation, fire/use, the
 * cursor finger and the MODE toggle, and reports ordered edges through its TouchEmit callback. This file turns those
 * edges into the portable host's PlatEvents, the same stream a keyboard and mouse produce:
 *   - stick       -> W A S D held (with hysteresis), the game's +walkfast/+backfast/+move*fast bindings;
 *   - look finger -> relative mouse motion (mouse look; the game's DirectInput mouse);
 *   - FIRE / USE  -> left button held / right click (+fire_weapon, frob_object);
 *   - actions     -> Space (jump), the crouch key (Shift by default, else the installation's own, ss2t_set_crouch_key), wheel up / down (next / previous weapon), R (reload), B (ammo), Esc;
 *     these are the bindings of the game's user.bnd (which replaces SHOCK.BND; Y is not bound there);
 *   - cursor mode -> the game's own cursor placed at the touched point (PlatEvent.abs), then left clicks or drags;
 *     or, with the relative cursor style (settings.cursor_relative), moved like a trackpad from where it is: a moving
 *     finger moves it, a tap clicks where it is, a finger held still presses for a drag.
 * Every press is queued with a minimum hold, so a tap shorter than a game frame still reaches the game, and keys a
 * physical keyboard holds are never released by touch (or the other way round).
 *
 * Radial menus (Phase 3F, the Shockolate wheel, touch_radial.c): in look mode, holding < or > (weapons), AMMO (the gun's
 * ammunition and settings, or the psi amp's powers), ITEM (hypos and patches) or II (quicksave, logs) for 400 ms opens a
 * wheel of what the game says is there (game_wheel, core/ss2game.c); releasing on an entry selects it (game_select runs
 * the game's own command), in the centre or outside cancels, and a short tap keeps the button's plain action. Quickload
 * asks first (settings.quickload_confirm). Phase 3G: the wheel opens around the held button, so the same finger slides
 * straight into an entry; at a screen edge its entries fan over the part of the circle that is on the screen
 * (touch_radial_open_at).
 *
 * Gameplay or cursor: the game tells which mode it is in by recentring its cursor every frame in shoot (mouse look)
 * mode and never in interface mode or menus (user32_cursor_recentred_ns). The MODE button sends Tab (toggle_mouse) so
 * the game switches, and the touch mode then follows the game, also when the game switches by itself (a keypad, the
 * Esc menu, the main menu).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_ss2.h"
#include <limits.h>
#include <stdio.h>
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
    s->rel_x = (float)e.x; s->rel_y = (float)e.y; s->rel_placed_x = e.x; s->rel_placed_y = e.y;   /* a later relative gesture goes on from here */
}

/* ---------------------------------------------------------------- relative (trackpad) cursor
 * The cursor finger (cursor mode, touch_controls.c) reports a position on touch (POINTER_MOVE), then either a tap
 * (POINTER_TAP: short, within the slop) or, past the slop, a drag (POINTER_DOWN, POINTER_MOVEs, POINTER_UP). With the
 * relative style the touch moves nothing, a drag moves the cursor by the finger's motion with no button, a tap clicks
 * where the cursor is, and a finger held still for SS2T_HOLD_MS presses the left button there; the drag that follows
 * then carries it (inventory items) until the finger lifts. The press reaches the game a frame before the cursor moves
 * on, as for a direct drag. */
static void rel_scale(const Ss2Touch *s, float *kx, float *ky) {
    /* game (window) pixels per surface pixel: at speed 1 the cursor moves as far on screen as the finger. In a 4:3 menu
       fitted into a wider picture the engine scales the whole window onto the menu (ss2t_ui_rect): more window pixels */
    float speed = s->t.settings.cursor_speed;
    *kx = speed * (float)s->game_w / s->pic_w; *ky = speed * (float)s->game_h / s->pic_h;
    if (s->ui_w > 0 && s->ui_h > 0) { *kx *= (float)s->game_w / (float)s->ui_w; *ky *= (float)s->game_h / (float)s->ui_h; }
}
static void rel_clamp(Ss2Touch *s) {
    if (s->rel_x > (float)(s->game_w - 1)) s->rel_x = (float)(s->game_w - 1);
    if (s->rel_x < 0) s->rel_x = 0;
    if (s->rel_y > (float)(s->game_h - 1)) s->rel_y = (float)(s->game_h - 1);
    if (s->rel_y < 0) s->rel_y = 0;
}
static int rel_px(float v) { return (int)(v + .5f); }    /* the nearest whole pixel (v >= 0 after rel_clamp) */
static void rel_place(Ss2Touch *s) {
    PlatEvent e; memset(&e, 0, sizeof e); e.type = PLAT_EV_MOUSE_MOVE; e.abs = 1; e.x = rel_px(s->rel_x); e.y = rel_px(s->rel_y);
    s->rel_placed_x = e.x; s->rel_placed_y = e.y;
    push(s, s->now > s->move_hold_until ? s->now : s->move_hold_until, e);
}
static void rel_begin(Ss2Touch *s, float x, float y) {      /* the cursor finger touched: the cursor stays where it is */
    int cx, cy;
    if (s->cursor_pos) {                    /* the game's cursor: moved by the game itself or by a physical mouse since? */
        s->cursor_pos(s, &cx, &cy);
        if (cx != s->rel_placed_x || cy != s->rel_placed_y) { s->rel_x = (float)cx; s->rel_y = (float)cy; }
    } else if (s->rel_x < 0) { s->rel_x = (float)(s->game_w / 2); s->rel_y = (float)(s->game_h / 2); }
    rel_clamp(s);
    s->rel_last_x = x; s->rel_last_y = y; s->rel_state = SS2T_REL_DOWN;
}
static void rel_move(Ss2Touch *s, float x, float y) {
    float kx, ky; rel_scale(s, &kx, &ky);
    s->rel_x += (x - s->rel_last_x) * kx; s->rel_y += (y - s->rel_last_y) * ky; s->rel_last_x = x; s->rel_last_y = y;
    rel_clamp(s);
    if (rel_px(s->rel_x) != s->rel_placed_x || rel_px(s->rel_y) != s->rel_placed_y) rel_place(s);   /* whole pixels reach the game */
}
static void rel_click(Ss2Touch *s) {
    s->move_hold_until = 0; rel_place(s); button(s, 0, 1, s->now); button(s, 0, 0, s->now);
}
static void rel_press(Ss2Touch *s) {        /* held still: the left button goes down where the cursor is */
    s->rel_state = SS2T_REL_HELD;
    s->move_hold_until = 0; rel_place(s); button(s, 0, 1, s->now); s->move_hold_until = s->now + SS2T_MIN_HOLD_NS;
    if (s->haptic && s->t.settings.haptics_enabled) s->haptic(s, 2);
}
static void rel_end(Ss2Touch *s) {          /* the cursor finger lifted: the cursor stays; a held press is released */
    if (s->rel_state == SS2T_REL_HELD) button(s, 0, 0, s->now);
    s->rel_state = SS2T_REL_IDLE;
}
static void rel_tick(Ss2Touch *s, uint32_t now_ms) {
    if (s->rel_state != SS2T_REL_DOWN || s->t.cursor_owner < 0) return;
    const TouchFinger *f = &s->t.fingers[s->t.cursor_owner];
    if (f->active && !f->dragging && (uint32_t)(now_ms - f->down_ms) >= SS2T_HOLD_MS) rel_press(s);
}

/* ---------------------------------------------------------------- radial menus (Phase 3F) */
static void toast(Ss2Touch *s, const char *text) {
    snprintf(s->toast, sizeof s->toast, "%s", text); s->toast_until = s->now + SS2T_TOAST_NS;
}
/* which wheel a held button opens (core: radial_for_action); -1: the button's plain action at once */
static int radial_for_action(void *user, TouchAction a) {
    Ss2Touch *s = user;
    if (!s->game_on || !s->game_wheel || s->t.effective_mode != TOUCH_GAMEPLAY) return -1;
    switch (a) {
    case TOUCH_WEAPON_PREV: case TOUCH_WEAPON_NEXT: return s->t.settings.wheel_enabled[0] ? TOUCH_RADIAL_WEAPON : -1;
    case TOUCH_AMMO: return TOUCH_RADIAL_AMMO;
    case TOUCH_EXPLOSIVE: return s->t.settings.wheel_enabled[1] ? TOUCH_RADIAL_CONSUMABLE : -1;    /* ITEM */
    case TOUCH_PAUSE: return s->t.settings.wheel_enabled[2] ? TOUCH_RADIAL_SYSTEM : -1;           /* II held */
    default: return -1;
    }
}
static int populate(void *user, TouchRadialCategory category, TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]) {
    Ss2Touch *s = user; char why[64] = "";
    int n = s->game_wheel ? s->game_wheel(s, (int)category, entries, TOUCH_RADIAL_MAX_ENTRIES, s->wheel_title, sizeof s->wheel_title, why, sizeof why) : 0;
    if (n <= 0) { toast(s, why[0] ? why : "NOT AVAILABLE NOW"); return 0; }     /* the wheel stays shut; nothing happens */
    s->last_highlight = -2; s->next_wheel_refresh = s->now + SS2T_WHEEL_REFRESH_NS;
    return n > TOUCH_RADIAL_MAX_ENTRIES ? TOUCH_RADIAL_MAX_ENTRIES : n;
}
static void radial_select(Ss2Touch *s, int category, int id) {
    if (category == TOUCH_RADIAL_SYSTEM && id == SS2T_SYS_QUICKLOAD && s->t.settings.quickload_confirm) {
        s->confirm_until = s->now + SS2T_CONFIRM_NS; return;                        /* ask first */
    }
    if (!s->game_select || !s->game_select(s, category, id)) toast(s, "NOT AVAILABLE NOW");
}
static void radial_frame(Ss2Touch *s) {
    TouchRadialSelector *r = &s->t.radial;
    if (r->open) {
        if (r->highlighted != s->last_highlight) {                                   /* a light tick per segment */
            if (r->highlighted >= 0 && s->last_highlight != -2 && s->haptic && s->t.settings.haptics_enabled) s->haptic(s, 1);
            s->last_highlight = r->highlighted;
        }
        if (s->now >= s->next_wheel_refresh && s->game_wheel) {                     /* availability while it is open */
            s->next_wheel_refresh = s->now + SS2T_WHEEL_REFRESH_NS;
            if (s->game_refresh) s->game_refresh(s);
            TouchRadialEntry fresh[TOUCH_RADIAL_MAX_ENTRIES]; char title[64], why[64];
            memset(fresh, 0, sizeof fresh);
            int n = s->game_wheel(s, (int)r->category, fresh, TOUCH_RADIAL_MAX_ENTRIES, title, sizeof title, why, sizeof why);
            touch_radial_refresh(r, fresh, n > 0 ? n : 0);
        }
    }
    char m[64];
    if (s->game_message && s->game_message(s, m, sizeof m) && m[0]) toast(s, m);
    if (s->toast_until && s->now >= s->toast_until) { s->toast_until = 0; s->toast[0] = 0; }
    if (s->confirm_until && s->now >= s->confirm_until) s->confirm_until = 0;
    /* ITEM is shown only when it has something to do */
    if (!s->game_on || !s->game_wheel || !s->t.settings.wheel_enabled[1]) s->t.layout.action[TOUCH_EXPLOSIVE].radius = 0;
    else if (s->t.layout.action[TOUCH_EXPLOSIVE].radius <= 0) touch_layout_refresh(&s->t);
}
TouchRect ss2t_confirm_rect(const TouchControls *t, int which) {
    float w = t->layout.width, h = t->layout.height;
    if (which == 0) return (TouchRect){ w * .30f, h * .30f, w * .40f, h * .40f };
    return (TouchRect){ which == 1 ? w * .33f : w * .52f, h * .55f, w * .15f, h * .11f };
}
static bool in_rect(TouchRect r, float x, float y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }
void ss2t_overlay(const Ss2Touch *s, Ss2Overlay *o) {
    o->wheel_title = s->t.radial.open ? s->wheel_title : "";
    o->toast = s->toast_until ? s->toast : "";
    o->confirm = s->confirm_until != 0;
}

/* everything touch holds goes up now; pending presses are dropped */
static void release_touch(Ss2Touch *s) {
    s->qn = 0; s->rel_state = SS2T_REL_IDLE; s->confirm_until = 0;
    for (int k = 0; k < 256; k++) if (s->touch_key[k]) { s->touch_key[k] = 1; key(s, k, 0, 0); }
    for (int b = 0; b < 5; b++) if (s->touch_btn[b]) { s->touch_btn[b] = 1; button(s, b, 0, 0); }
    memset(s->stick_key, 0, sizeof s->stick_key);
}

/* ---------------------------------------------------------------- core edges -> game input */
static void action(Ss2Touch *s, TouchAction a, bool down) {
    switch (a) {
    case TOUCH_JUMP: key(s, SS2_DIK_SPACE, down, s->now); break;          /* +jump: held */
    case TOUCH_CROUCH: key(s, s->crouch_dik, down, s->now); break;        /* crouch (a toggle) or +crouchhold (held) */
    case TOUCH_WEAPON_NEXT: if (down) wheel(s, 1); break;                 /* mouse_wheel_up: cycle_weapon 1 */
    case TOUCH_WEAPON_PREV: if (down) wheel(s, -1); break;                /* mouse_wheel_down: cycle_weapon -1 */
    case TOUCH_CONSUMABLE: if (down) tap_key(s, SS2_DIK_R); break;        /* RELOAD: reload_gun */
    case TOUCH_PAUSE: if (down) tap_key(s, SS2_DIK_ESC); break;           /* sim_menu, skips cutscenes too */
    case TOUCH_AMMO:                                                      /* cycle_ammo; in cursor mode a right click (use an item) */
        if (!down) break;
        if (s->t.effective_mode == TOUCH_CURSOR) { button(s, 1, 1, s->now); button(s, 1, 0, s->now); }
        else tap_key(s, SS2_DIK_B);
        break;
    case TOUCH_TARGET_ID: if (down && s->keyboard) s->keyboard(s); break;   /* KBD: the Android keyboard (text arrives as keys) */
    case TOUCH_EXPLOSIVE: if (down) toast(s, "HOLD ITEM FOR HYPOS AND PATCHES"); break;   /* ITEM: only the wheel */
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
    case TOUCH_POINTER_MOVE:
        if (s->rel_state == SS2T_REL_IDLE && s->t.settings.cursor_relative) rel_begin(s, x, y);   /* touched: no jump */
        else if (s->rel_state != SS2T_REL_IDLE) rel_move(s, x, y);
        else place(s, x, y);
        break;
    case TOUCH_POINTER_DOWN:               /* a drag: press at the start point; the game picks up what is there before the cursor moves */
        if (s->rel_state == SS2T_REL_DOWN) { s->rel_state = SS2T_REL_MOVING; break; }   /* relative: a move, no button */
        if (s->rel_state != SS2T_REL_IDLE) break;                                       /* relative: pressed by the hold */
        s->move_hold_until = 0; place(s, x, y); button(s, 0, 1, s->now); s->move_hold_until = s->now + SS2T_MIN_HOLD_NS; break;
    case TOUCH_POINTER_UP:
        if (s->rel_state != SS2T_REL_IDLE) { rel_end(s); break; }
        place(s, x, y); button(s, 0, 0, s->now); break;
    case TOUCH_POINTER_TAP:
        if (s->rel_state == SS2T_REL_HELD) { rel_end(s); break; }
        if (s->rel_state != SS2T_REL_IDLE) { s->rel_state = SS2T_REL_IDLE; rel_click(s); break; }   /* relative: where the cursor is */
        s->move_hold_until = 0; place(s, x, y); button(s, 0, 1, s->now); button(s, 0, 0, s->now); break;
    case TOUCH_MODE_CHANGED:
        if (!s->syncing) { s->user_toggle_at = s->now; tap_key(s, SS2_DIK_TAB); }   /* toggle_mouse: shoot <-> interface */
        break;
    case TOUCH_CANCEL: release_touch(s); break;
    case TOUCH_SETTINGS_SAVE: if (s->save_settings) s->save_settings(s); else s->t.save_failed = true; break;
    case TOUCH_HAPTIC: if (s->haptic) s->haptic(s, (int)x); break;
    case TOUCH_RADIAL_SELECT: radial_select(s, (int)x, (int)y); break;
    default: break;                        /* no UI-change or QA handling needed */
    }
}

/* ---------------------------------------------------------------- public */
void ss2t_init(Ss2Touch *s) {
    memset(s, 0, sizeof *s);
    touch_init(&s->t, output, s);
    s->rel_x = s->rel_y = -1; s->rel_placed_x = s->rel_placed_y = INT_MIN;     /* not known yet */
    s->t.radial_populate = populate;       /* the wheels come from the game (game_wheel); without it they never open */
    s->t.radial_for_action = radial_for_action; s->t.radial_hold_ms = SS2T_RADIAL_HOLD_MS;
    s->t.radial_anchored = true;           /* Phase 3G: each wheel opens around the button being held */
    s->crouch_dik = SS2_DIK_LSHIFT;        /* the stock user.bnd: bind shift crouch */
}

void ss2t_set_crouch_key(Ss2Touch *s, int dik) {
    if (dik <= 0 || dik > 255) dik = SS2_DIK_LSHIFT;
    if (dik == s->crouch_dik) return;
    if (s->touch_key[s->crouch_dik]) ss2t_cancel(s);     /* CROUCH is down: its key must not stay pressed */
    s->crouch_dik = dik;
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
    rel_tick(s, (uint32_t)(now / 1000000u));
    radial_frame(s);
}

void ss2t_ui_rect(Ss2Touch *s, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || (x == 0 && y == 0 && w == s->game_w && h == s->game_h)) x = y = w = h = 0;
    s->ui_x = x; s->ui_y = y; s->ui_w = w; s->ui_h = h;
}

void ss2t_finger(Ss2Touch *s, TouchPhase phase, int64_t device, int64_t finger, float x, float y, uint32_t ms) {
    /* While typing in look mode (the console), a new finger must not walk, turn, fire or use: only II, KBD, MODE and SET
       take it. Cursor mode (menus, the save screen) is unchanged, so the game's own buttons can still be tapped. */
    if (s->typing && phase == TOUCH_DOWN && s->t.panel == TOUCH_UI_CLOSED && s->t.effective_mode == TOUCH_GAMEPLAY &&
        !touch_control_hit(&s->t, TC_PAUSE, x, y) && !touch_control_hit(&s->t, TC_TARGET, x, y) &&
        !touch_control_hit(&s->t, TC_MODE, x, y) && !touch_control_hit(&s->t, TC_SETTINGS, x, y)) return;
    if (s->confirm_until && phase == TOUCH_DOWN) {      /* the quickload question takes the next touch, wherever it is */
        if (in_rect(ss2t_confirm_rect(&s->t, 1), x, y)) {
            s->confirm_until = 0;
            if (!s->game_select || !s->game_select(s, TOUCH_RADIAL_SYSTEM, SS2T_SYS_QUICKLOAD)) toast(s, "NOT AVAILABLE NOW");
        } else { s->confirm_until = 0; toast(s, "QUICKLOAD CANCELLED"); }
        return;                                         /* that finger is never the core's: its moves and lift are ignored */
    }
    if (phase == TOUCH_DOWN && s->game_on && s->game_refresh && s->t.panel == TOUCH_UI_CLOSED && s->t.effective_mode == TOUCH_GAMEPLAY &&
        (touch_control_hit(&s->t, TC_PREV, x, y) || touch_control_hit(&s->t, TC_NEXT, x, y) || touch_control_hit(&s->t, TC_AMMO, x, y) ||
         touch_control_hit(&s->t, TC_EXPLOSIVE, x, y) || touch_control_hit(&s->t, TC_PAUSE, x, y)))
        s->game_refresh(s);                             /* a wheel may open: read the game now, well before the hold ends */
    if (phase != TOUCH_DOWN) rel_tick(s, ms);           /* a hold that ends with this event still presses first */
    touch_finger(&s->t, phase, device, finger, x, y, ms);
    if (s->rel_state != SS2T_REL_IDLE && s->t.cursor_owner < 0) rel_end(s);   /* lifted with neither a tap nor a drag */
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
