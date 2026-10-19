/* touch_ss2_android.c: the Android glue of the touch controls: raw touches, lifecycle, settings, haptics, drawing.
 *
 * Raw touchscreen events come from local.newdarkandroid.ss2.TouchSurface (touch_surface.c pushes them into SDL's
 * queue), never from SDL's finger or touch-to-mouse events, which are switched off. plat_sdl2.c calls the
 * android_touch_* functions below (weak references there, so builds without this directory are unaffected), all on the
 * game thread, which also owns the GL context on Android.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_ss2.h"
#include "touch_binds.h"
#include "touch_surface.h"
#include "ss2game.h"
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t user32_cursor_recentred_ns(void) __attribute__((weak));
void user32_cursor_pos(int *x, int *y) __attribute__((weak));
int d3d9_ui_rect(int r[4], int *bb_w, int *bb_h) __attribute__((weak));
char *vfs_map(const char *win, char *out, size_t n) __attribute__((weak));
int touch_draw_gles(const TouchControls *t, const Ss2Overlay *o, int w, int h, void *(*get_proc)(const char *));
static void tlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void tlog(const char *fmt, ...) {
    char b[300]; int n = snprintf(b, sizeof b, "[touch] "); va_list ap; va_start(ap, fmt); vsnprintf(b + n, sizeof b - (size_t)n, fmt, ap); va_end(ap);
    plat_log_write(PLAT_LOG_INFO, b);
}

static Ss2Touch touch;
static int initialized, background, unfocused, debug;
static int surf_w, surf_h;                  /* the touch surface (= drawable) size */
static char settings_path[1024]; static int future_settings;
int touch_keyboard_shown;                   /* touch_render_gles.c draws KBD pressed while it is up */
static int keyboard_changed;

static void save_settings(Ss2Touch *s) {
    s->t.save_failed = future_settings || !settings_path[0] || !touch_settings_save(&s->t.settings, settings_path);
    tlog("settings saved to %s: %s; cursor %s, speed %.2f", settings_path, s->t.save_failed ? "FAILED" : "ok",
         s->t.settings.cursor_relative ? "relative" : "direct", s->t.settings.cursor_speed);
}
static void cursor_pos(Ss2Touch *s, int *x, int *y) { (void)s; user32_cursor_pos(x, y); }   /* the relative cursor's start */
static void haptic(Ss2Touch *s, int level) {   /* NewDarkActivity.touchHaptic, as ShockolateActivity's */
    if (background || unfocused) return;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv(); jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = (*env)->GetObjectClass(env, activity);
    if (cls) {
        jmethodID m = (*env)->GetMethodID(env, cls, "touchHaptic", "(II)V");
        if (m) (*env)->CallVoidMethod(env, activity, m, (jint)s->t.settings.haptic_strength, (jint)level);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
    }
    (*env)->DeleteLocalRef(env, activity);
}
/* KBD: SDL's Android text input. Its keyboard sends each typed character as a key press and its text (SDL generates the
   scan codes), so the game sees what a physical keyboard sends. SDL keeps text events on from the start without a
   keyboard, so whether it is up is tracked here. Android's Back closes it: SDL then stops text input (keyboard_poll). */
static void text_events_on(void) {          /* as SDL starts: text from a physical keyboard, no on-screen keyboard */
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE); SDL_EventState(SDL_TEXTEDITING, SDL_ENABLE);
}
static void keyboard(Ss2Touch *s) {
    if (touch_keyboard_shown) { SDL_StopTextInput(); text_events_on(); touch_keyboard_shown = 0; }
    else { SDL_Rect r = { 0, 0, 1, 1 }; SDL_SetTextInputRect(&r); SDL_StartTextInput(); touch_keyboard_shown = 1; }
    s->typing = touch_keyboard_shown != 0;
    keyboard_changed = 1;                   /* called from inside the touch core: release held controls in the next poll */
    tlog("keyboard %s", touch_keyboard_shown ? "shown" : "hidden");
}
static void keyboard_poll(void) {
    if (touch_keyboard_shown && !SDL_IsTextInputActive()) {    /* Back (or the keyboard's hide key) closed it */
        text_events_on(); touch_keyboard_shown = 0; touch.typing = false; keyboard_changed = 1;
        tlog("keyboard hidden by Android");
    }
    if (keyboard_changed) { keyboard_changed = 0; ss2t_cancel(&touch); }   /* nothing touch holds (a walk, a turn) stays held */
}

/* Phase 3F: the radial menus read the game through core/ss2game.c (its wheels, by the core's categories) */
static int wheel_of(int category) {
    switch (category) {
    case TOUCH_RADIAL_WEAPON: return SS2G_WEAPONS;
    case TOUCH_RADIAL_AMMO: return SS2G_AMMO;
    case TOUCH_RADIAL_CONSUMABLE: return SS2G_ITEMS;
    case TOUCH_RADIAL_SYSTEM: return SS2G_SYSTEM;
    default: return -1;
    }
}
static void game_refresh(Ss2Touch *s) { (void)s; ss2g_refresh(); }
static int game_wheel(Ss2Touch *s, int category, TouchRadialEntry *e, int max, char *title, size_t tn, char *why, size_t wn) {
    (void)s; int w = wheel_of(category); if (w < 0) return 0;
    Ss2gEntry g[SS2G_MAX]; if (max > SS2G_MAX) max = SS2G_MAX;
    int n = ss2g_wheel((Ss2gWheel)w, g, max, title, tn, why, wn);
    for (int i = 0; i < n; i++) {
        memset(&e[i], 0, sizeof e[i]);
        e[i].id = g[i].id; snprintf(e[i].label, sizeof e[i].label, "%s", g[i].label); snprintf(e[i].name, sizeof e[i].name, "%s", g[i].name);
        e[i].available = g[i].available != 0; e[i].current = g[i].current != 0;
        e[i].count = g[i].count > 0 ? (unsigned)g[i].count : 0; e[i].no_count = g[i].count < 0;
    }
    if (debug) tlog("wheel %d: %d entries '%s'%s%s", category, n, title ? title : "", why && why[0] ? ": " : "", why ? why : "");
    return n;
}
static bool game_select(Ss2Touch *s, int category, int id) {
    (void)s; int w = wheel_of(category); bool r = w >= 0 && ss2g_select((Ss2gWheel)w, id);
    tlog("wheel %d: selected %d%s", category, id, r ? "" : " (refused)");
    return r;
}
static bool game_message(Ss2Touch *s, char *out, size_t n) { (void)s; return ss2g_message(out, n) != 0; }
_Static_assert(SS2T_SYS_QUICKLOAD == SS2G_SYS_QUICKLOAD, "the bridge's quickload id");
/* System Shock 2's touch settings on top of Shockolate's: its three wheels on (< > held, ITEM, II held). A file saved
   before Phase 3F keeps these (its wheel keys did nothing in System Shock 2) and puts ITEM, never shown, at its default. */
static void ss2_settings_defaults(TouchSettings *s) { s->wheel_enabled[0] = s->wheel_enabled[1] = s->wheel_enabled[2] = true; }
static const TouchSettingsGame ss2_settings = { ss2_settings_defaults, TC_EXPLOSIVE };

/* Before SDL_Init: touches must never become SDL mouse events (they would enter the relative-motion accumulator) */
void android_touch_init(void) {
    const char *on = getenv("SS2PORT_TOUCH");      /* the launcher sets 0 for runs without touch controls (--ez touch false) */
    if (on && *on == '0') { tlog("touch controls off"); return; }
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS, "0", SDL_HINT_OVERRIDE);
    touch_settings_game = &ss2_settings;            /* before ss2t_init: its defaults include the game's */
    ss2t_init(&touch);
    touch.save_settings = save_settings; touch.haptic = haptic; touch.keyboard = keyboard;
    touch.game_refresh = game_refresh; touch.game_wheel = game_wheel; touch.game_select = game_select; touch.game_message = game_message;
    if (user32_cursor_pos) touch.cursor_pos = cursor_pos;
    debug = getenv("SS2PORT_TOUCH_DEBUG") != NULL;
    initialized = 1;
}

static void late_init(void) {                /* once SDL runs: the event type and the settings file */
    static int done; if (done) return; done = 1;
    touch_surface_init();
    const char *dir = SDL_AndroidGetInternalStoragePath();
    if (dir && snprintf(settings_path, sizeof settings_path, "%s/touch-settings.cfg", dir) < (int)sizeof settings_path) {
        TouchLoadResult r = touch_settings_load(&touch.t.settings, settings_path);
        future_settings = r == TOUCH_LOAD_FUTURE;
        int upgraded = (r == TOUCH_LOAD_OK || r == TOUCH_LOAD_MIGRATED) && touch_settings_upgrade_layout(&touch.t.settings);
        touch.t.saved_settings = touch.t.settings; touch_layout_refresh(&touch.t);
        tlog("touch controls on; settings %s (%s)%s; cursor %s, speed %.2f", settings_path, r == TOUCH_LOAD_DEFAULTS ? "defaults" : r == TOUCH_LOAD_OK ? "loaded" :
             r == TOUCH_LOAD_MIGRATED ? "migrated" : r == TOUCH_LOAD_FUTURE ? "newer version: defaults, not saved" : "invalid: defaults",
             upgraded ? "; layout never moved: current default positions (file unchanged)" : "",
             touch.t.settings.cursor_relative ? "relative" : "direct", touch.t.settings.cursor_speed);
        tlog("radial menus: weapon wheel %s, ITEM %s, system wheel %s, quickload confirm %s%s", touch.t.settings.wheel_enabled[0] ? "on" : "off",
             touch.t.settings.wheel_enabled[1] ? "on" : "off", touch.t.settings.wheel_enabled[2] ? "on" : "off", touch.t.settings.quickload_confirm ? "on" : "off",
             touch.t.settings.game_radials ? "" : " (defaults: the file predates them)");
    } else settings_path[0] = 0;
}

/* CROUCH presses the key the installation's own bind file gives crouch. The game reads user.bnd through the folder overlay
   (the profile's copy, which the game itself writes, before the game folder's), and so does this. Prefer the toggle
   (crouch), else the held form (+crouchhold); Shift, the stock binding, when it is one of them. Read again each time
   touch enters LOOK mode, so a binding changed in the game's Options applies once the menu closes. */
static void resolve_crouch(void) {
    static char last[160];
    static const char *const cmds[] = { "crouch", "+crouchhold" };
    static char text[65536];
    char path[1024], msg[160]; TouchBind b; int dik = SS2_DIK_LSHIFT;
    if (!vfs_map || !vfs_map("user.bnd", path, sizeof path)) return;
    FILE *f = fopen(path, "rb");
    if (!f) snprintf(msg, sizeof msg, "no user.bnd: Shift (the stock binding)");
    else {
        size_t n = fread(text, 1, sizeof text, f); fclose(f);
        if (touch_bind_find(text, n, cmds, 2, SS2_DIK_LSHIFT, &b)) {
            dik = b.dik;
            snprintf(msg, sizeof msg, "'%s' (%s, %s), DIK 0x%02x", b.key, b.command, b.command[0] == '+' ? "held" : "toggle", b.dik);
        } else snprintf(msg, sizeof msg, "nothing bound to crouch: Shift (the stock binding)");
    }
    ss2t_set_crouch_key(&touch, dik);
    if (strcmp(msg, last)) { snprintf(last, sizeof last, "%s", msg); tlog("CROUCH key from %s: %s", path, msg); }
}

static uint64_t now_ns(void) { return plat_time_ns(); }

/* Every SDL event: returns 1 when it was a touch event (consumed). Lifecycle and window events cancel held controls. */
int android_touch_event(const SDL_Event *e) {
    if (!initialized) return 0;
    late_init();
    TouchSurfaceEvent s;
    if (touch_surface_failed()) { ss2t_cancel(&touch); if (touch_surface_read(e, &s)) return 1; }   /* a lost release: drop everything */
    if (touch_surface_read(e, &s)) {
        if (s.phase == TOUCH_ABORT || s.width <= 0 || s.height <= 0) { ss2t_cancel(&touch); return 1; }
        float sw = surf_w ? (float)surf_w : (float)s.width, sh = surf_h ? (float)surf_h : (float)s.height;
        float x = s.x * sw / (float)s.width, y = s.y * sh / (float)s.height;                          /* surface view -> drawable */
        if (debug) tlog("finger %lld/%lld %s at %.0f,%.0f", (long long)s.device, (long long)s.finger,
                        s.phase == TOUCH_DOWN ? "down" : s.phase == TOUCH_UP ? "up" : "move", x, y);
        touch.now = now_ns();
        ss2t_finger(&touch, s.phase, s.device, s.finger, x, y, (uint32_t)(touch.now / 1000000u));
        return 1;
    }
    switch (e->type) {
    case SDL_APP_WILLENTERBACKGROUND: case SDL_APP_DIDENTERBACKGROUND: case SDL_APP_TERMINATING:
        background = 1; ss2t_cancel(&touch); break;
    case SDL_APP_WILLENTERFOREGROUND: case SDL_APP_DIDENTERFOREGROUND:
        background = e->type != SDL_APP_DIDENTERFOREGROUND; ss2t_cancel(&touch); break;
    case SDL_DISPLAYEVENT: if (e->display.event == SDL_DISPLAYEVENT_ORIENTATION) ss2t_cancel(&touch); break;
    case SDL_WINDOWEVENT:
        switch (e->window.event) {
        case SDL_WINDOWEVENT_FOCUS_LOST: unfocused = 1; ss2t_cancel(&touch); break;
        case SDL_WINDOWEVENT_FOCUS_GAINED: unfocused = 0; ss2t_cancel(&touch); break;
        case SDL_WINDOWEVENT_SIZE_CHANGED: case SDL_WINDOWEVENT_RESIZED: case SDL_WINDOWEVENT_DISPLAY_CHANGED:
        case SDL_WINDOWEVENT_MINIMIZED: case SDL_WINDOWEVENT_HIDDEN: ss2t_cancel(&touch); break;
        }
        break;
    case SDL_FINGERDOWN: case SDL_FINGERUP: case SDL_FINGERMOTION: return 1;   /* never used: their coordinates are SDL's, not ours */
    }
    return 0;
}

/* A physical key or mouse button: 1 when it should reach the game (touch_ss2.c: per-source aggregation). */
int android_touch_filter(const PlatEvent *p) { return !initialized || ss2t_physical(&touch, p); }

/* Once per plat_video_poll, after SDL's events: frame state, then the due touch events into the game's input. */
void android_touch_poll(void (*sink)(const PlatEvent *, void *), void *user, int dw, int dh, int bx, int by, int bw, int bh,
                        int game_w, int game_h, int focused) {
    if (!initialized) return;
    late_init();
    surf_w = dw; surf_h = dh;
    keyboard_poll();
    touch.game_on = ss2g_available() != 0;
    TouchMode before = touch.t.mode;
    ss2t_frame(&touch, now_ns(), (float)dw, (float)dh, focused && !background && !unfocused, (float)bx, (float)by, (float)bw, (float)bh,
               game_w, game_h, user32_cursor_recentred_ns ? user32_cursor_recentred_ns() : 0);
    int r[4], bbw, bbh;
    if (d3d9_ui_rect && d3d9_ui_rect(r, &bbw, &bbh) && bbw > 0 && bbh > 0) {      /* back-buffer pixels -> game pixels */
        int ux = r[0] * game_w / bbw, uy = r[1] * game_h / bbh, uw = r[2] * game_w / bbw, uh = r[3] * game_h / bbh;
        int was[4] = { touch.ui_x, touch.ui_y, touch.ui_w, touch.ui_h };
        ss2t_ui_rect(&touch, ux, uy, uw, uh);
        if (debug && (was[0] != touch.ui_x || was[1] != touch.ui_y || was[2] != touch.ui_w || was[3] != touch.ui_h))
            tlog("game 2D picture %d,%d %dx%d of %dx%d%s", ux, uy, uw, uh, game_w, game_h, touch.ui_w ? "" : " (whole picture)");
    }
    static int was_gameplay;                /* also when the MODE button switched (in the event handler, before this poll) */
    if (touch.t.mode == TOUCH_GAMEPLAY && !was_gameplay) resolve_crouch();
    was_gameplay = touch.t.mode == TOUCH_GAMEPLAY;
    if (touch.t.mode != before) tlog("mode %s (game %s)", touch.t.mode == TOUCH_GAMEPLAY ? "LOOK" : "CURSOR", touch.game_shoot ? "shoot" : "interface/menu");
    PlatEvent out[64]; int n;
    do {
        n = ss2t_flush(&touch, out, 64);
        for (int i = 0; i < n; i++) {
            if (debug && out[i].type == PLAT_EV_KEY) tlog("key 0x%02x %s", out[i].key, out[i].down ? "down" : "up");
            if (debug && out[i].type == PLAT_EV_MOUSE_BUTTON) tlog("button %d %s", out[i].button, out[i].down ? "down" : "up");
            if (debug && out[i].type == PLAT_EV_MOUSE_MOVE && out[i].abs) tlog("cursor at %d,%d", out[i].x, out[i].y);
            sink(&out[i], user);
        }
    } while (n == 64);
}

/* GL thread, before the buffer swap: the overlay over the whole drawable (bars included). */
void android_touch_draw(int w, int h) {
    if (!initialized) return;
    static int failed;
    Ss2Overlay o; ss2t_overlay(&touch, &o);
    if (touch_draw_gles(&touch.t, &o, w, h, SDL_GL_GetProcAddress) < 0 && !failed) { failed = 1; tlog("overlay: GL setup failed; controls work, nothing drawn"); }
}
