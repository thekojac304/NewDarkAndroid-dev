/* touch_ss2_test.c: host tests of the System Shock 2 touch bridge (touch_ss2.c) over the imported Shockolate core.
 * Build and run: port/backends/android/touch/tests/run.sh (any C11 compiler; no SDL, no Android).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_ss2.h"
#include "touch_binds.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __func__, __LINE__, #x); } } while (0)

/* the S26 Ultra in landscape: 2340x1080, an 800x600 game letterboxed to 1440x1080 at x = 450 */
#define W 2340
#define H 1080
static uint64_t now;
static PlatEvent out[512]; static int nout;
static void frame(Ss2Touch *s, uint64_t recentred) {
    ss2t_frame(s, now, W, H, true, 450, 0, 1440, 1080, 800, 600, recentred);
    nout = ss2t_flush(s, out, 512);
}
static void advance(Ss2Touch *s, uint64_t ms, uint64_t recentred) { now += ms * 1000000ull; frame(s, recentred); }
static void finger(Ss2Touch *s, TouchPhase p, int id, float x, float y) { ss2t_finger(s, p, 7, id, x, y, (uint32_t)(now / 1000000u)); }
static int find(int type, int key_or_button, int down) {
    for (int i = 0; i < nout; i++) {
        PlatEvent *e = &out[i]; if ((int)e->type != type) continue;
        if (type == PLAT_EV_KEY && e->key == key_or_button && e->down == down) return i;
        if (type == PLAT_EV_MOUSE_BUTTON && e->button == key_or_button && e->down == down) return i;
    }
    return -1;
}
/* a game in shoot mode recentres its cursor every frame */
static uint64_t shooting(void) { return now; }
static void enter_gameplay(Ss2Touch *s) { for (int i = 0; i < 4; i++) advance(s, 10, shooting()); CHECK(s->t.mode == TOUCH_GAMEPLAY); }
static TouchCircle ctl(Ss2Touch *s, int id) { TouchRect r = touch_control_bounds(&s->t, id); return (TouchCircle){ r.x + r.w / 2, r.y + r.h / 2, r.w / 2 }; }

static void test_tap_min_hold(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    TouchCircle f = ctl(&s, TC_FIRE);
    finger(&s, TOUCH_DOWN, 1, f.x, f.y); now += 5000000ull; finger(&s, TOUCH_UP, 1, f.x, f.y);   /* a 5 ms tap */
    frame(&s, shooting());
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 1) >= 0);          /* the press goes out */
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) < 0);           /* the release waits for the minimum hold */
    advance(&s, 30, shooting()); CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) < 0);
    advance(&s, 40, shooting()); CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0);
}

static void test_move_look_fire(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    TouchCircle f = ctl(&s, TC_FIRE);
    float r = s.t.layout.move_radius;
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400, 900 - r);   /* stick fully forward */
    finger(&s, TOUCH_DOWN, 2, 1300, 400);                                              /* look finger */
    finger(&s, TOUCH_DOWN, 3, f.x, f.y);                                               /* fire held */
    advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 1) >= 0);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 1) >= 0);
    finger(&s, TOUCH_MOTION, 2, 1300 + 144, 400);          /* 144 surface px = 80 game px */
    advance(&s, 10, shooting());
    int dx = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE && !out[i].abs) dx += out[i].dx;
    CHECK(dx >= 79 && dx <= 80);                           /* relative look in game pixels, at any screen size */
    finger(&s, TOUCH_UP, 2, 1444, 400); advance(&s, 100, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 0) < 0);            /* lifting the look finger releases neither stick nor fire */
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) < 0);
    CHECK(s.stick_key[0] && s.touch_btn[0] == 1);
    finger(&s, TOUCH_UP, 3, f.x, f.y); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0);
    finger(&s, TOUCH_UP, 1, 400, 900 - r); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 0) >= 0);
}

static void test_ownership_and_hysteresis(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    float r = s.t.layout.move_radius;
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400 + .25f * r, 900);   /* below the press threshold */
    advance(&s, 10, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_D, 1) < 0);
    finger(&s, TOUCH_MOTION, 1, 400 + .35f * r, 900); advance(&s, 10, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_D, 1) >= 0);
    finger(&s, TOUCH_MOTION, 1, 400 + .25f * r, 900); advance(&s, 10, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_D, 0) < 0);   /* hysteresis */
    finger(&s, TOUCH_DOWN, 2, 300, 950); finger(&s, TOUCH_MOTION, 2, 300, 950 + r);   /* a second finger in the stick area */
    advance(&s, 10, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_S, 1) < 0);           /* does not take over the stick */
    finger(&s, TOUCH_MOTION, 1, 400 + .1f * r, 900); advance(&s, 70, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_D, 0) >= 0);
    finger(&s, TOUCH_UP, 1, 400, 900); finger(&s, TOUCH_UP, 2, 300, 950 + r); advance(&s, 10, shooting());
    CHECK(!s.stick_key[0] && !s.stick_key[1] && !s.stick_key[2] && !s.stick_key[3]);
}

static void test_modifier_spans_frames(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    TouchCircle c = ctl(&s, TC_CROUCH), j = ctl(&s, TC_JUMP);
    finger(&s, TOUCH_DOWN, 1, c.x, c.y); advance(&s, 10, shooting());        /* Shift held by CROUCH */
    CHECK(find(PLAT_EV_KEY, SS2_DIK_LSHIFT, 1) >= 0);
    finger(&s, TOUCH_DOWN, 2, j.x, j.y); advance(&s, 10, shooting());        /* Space pressed while Shift is held */
    CHECK(find(PLAT_EV_KEY, SS2_DIK_SPACE, 1) >= 0);
    PlatEvent up; memset(&up, 0, sizeof up); up.type = PLAT_EV_KEY; up.key = SS2_DIK_LSHIFT; up.down = 1;
    CHECK(!ss2t_physical(&s, &up));                        /* a physical Shift press while touch holds it: no second edge */
    up.down = 0; CHECK(!ss2t_physical(&s, &up));           /* and its release does not release the touch Shift */
    finger(&s, TOUCH_UP, 2, j.x, j.y); advance(&s, 100, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_SPACE, 0) >= 0 && s.touch_key[SS2_DIK_LSHIFT] == 1);   /* Shift spans every frame */
    finger(&s, TOUCH_UP, 1, c.x, c.y); advance(&s, 100, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_LSHIFT, 0) >= 0);
}

static void test_cursor_mapping_and_click(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; frame(&s, 0);     /* no recentring: menu / interface: cursor mode */
    CHECK(s.t.mode == TOUCH_CURSOR);
    static const float pts[5][4] = { { 450, 0, 0, 0 }, { 1889, 0, 799, 0 }, { 450, 1079, 0, 599 }, { 1889, 1079, 799, 599 }, { 1170, 540, 400, 300 } };
    for (int i = 0; i < 5; i++) {
        finger(&s, TOUCH_DOWN, 1, pts[i][0], pts[i][1]); now += 50000000ull; finger(&s, TOUCH_UP, 1, pts[i][0], pts[i][1]);
        frame(&s, 0);
        int m = -1; for (int k = 0; k < nout; k++) if (out[k].type == PLAT_EV_MOUSE_MOVE && out[k].abs) m = k;
        int d = find(PLAT_EV_MOUSE_BUTTON, 0, 1);
        CHECK(m >= 0 && d > m);                            /* the cursor is placed before the click */
        if (m >= 0) { CHECK(fabsf(out[m].x - pts[i][2]) <= 1 && fabsf(out[m].y - pts[i][3]) <= 1); }
        advance(&s, 100, 0); CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0);
    }
    finger(&s, TOUCH_DOWN, 1, 100, 500); now += 50000000ull; finger(&s, TOUCH_UP, 1, 100, 500); frame(&s, 0);   /* in the bar: clamped */
    int m = -1; for (int k = 0; k < nout; k++) if (out[k].type == PLAT_EV_MOUSE_MOVE && out[k].abs) m = k;
    CHECK(m >= 0 && out[m].x == 0);
    advance(&s, 100, 0);
    /* a new tap within the previous click's minimum hold waits for its release: the cursor never moves under a held button */
    finger(&s, TOUCH_DOWN, 1, 800, 400); now += 20000000ull; finger(&s, TOUCH_UP, 1, 800, 400); frame(&s, 0);
    finger(&s, TOUCH_DOWN, 1, 1000, 400); now += 20000000ull; finger(&s, TOUCH_UP, 1, 1000, 400); frame(&s, 0);
    int moves = 0; for (int k = 0; k < nout; k++) moves += out[k].type == PLAT_EV_MOUSE_MOVE && out[k].abs;
    CHECK(moves == 0 && s.touch_btn[0] == 1);
    advance(&s, 100, 0); advance(&s, 100, 0); CHECK(s.touch_btn[0] == 0 && s.qn == 0);
    /* a drag: press at the start point, and the cursor moves on only a frame later (the game picks up what was under it) */
    finger(&s, TOUCH_DOWN, 1, 800, 400); advance(&s, 10, 0);
    finger(&s, TOUCH_MOTION, 1, 900, 500); advance(&s, 10, 0);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 1) >= 0);
    { int at = -1, moved = 0; for (int k = 0; k < nout; k++) { if (out[k].type == PLAT_EV_MOUSE_MOVE && out[k].abs) { if (at < 0) at = out[k].x; else moved = 1; } }
      CHECK(at == 194 && !moved); }                          /* only the start point (800 surface px = game x 194) so far */
    advance(&s, 70, 0);
    { int last = -1; for (int k = 0; k < nout; k++) if (out[k].type == PLAT_EV_MOUSE_MOVE && out[k].abs) last = out[k].x; CHECK(last == 250); }
    finger(&s, TOUCH_UP, 1, 1000, 600); advance(&s, 100, 0);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0);
    CHECK(s.touch_btn[0] == 0);
}

/* a 1280x720 game (1920x1080 at x = 210): the engine fits its 640x480 menus into 160,0 960x720 and scales the whole
   window onto them, so a touch on the menu is placed through that rectangle (ss2t_ui_rect) */
static int place_at(Ss2Touch *s, float x, float y, int *gx, int *gy) {
    finger(s, TOUCH_DOWN, 1, x, y); now += 50000000ull; finger(s, TOUCH_UP, 1, x, y);
    ss2t_frame(s, now, W, H, true, 210, 0, 1920, 1080, 1280, 720, 0); nout = ss2t_flush(s, out, 512);
    int m = -1; for (int k = 0; k < nout; k++) if (out[k].type == PLAT_EV_MOUSE_MOVE && out[k].abs) m = k;
    for (int i = 0; i < 3; i++) { now += 100000000ull; ss2t_frame(s, now, W, H, true, 210, 0, 1920, 1080, 1280, 720, 0); ss2t_flush(s, out + 256, 256); }
    if (m < 0) return 0;
    *gx = out[m].x; *gy = out[m].y; return 1;
}
static void test_ui_rect_mapping(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull;
    ss2t_frame(&s, now, W, H, true, 210, 0, 1920, 1080, 1280, 720, 0); ss2t_flush(&s, out, 512);
    int x, y;
    ss2t_ui_rect(&s, 160, 0, 960, 720);                     /* a menu */
    static const float pts[6][4] = { { 450, 0, 0, 0 }, { 1889, 0, 1279, 0 }, { 450, 1079, 0, 719 }, { 1889, 1079, 1279, 719 },
                                     { 1170, 540, 640, 360 }, { 1742, 434, 1148, 289 } };   /* the last: Load on the load screen */
    for (int i = 0; i < 6; i++) { CHECK(place_at(&s, pts[i][0], pts[i][1], &x, &y) && abs(x - (int)pts[i][2]) <= 1 && abs(y - (int)pts[i][3]) <= 1); }
    CHECK(place_at(&s, 300, 500, &x, &y) && x == 0);        /* beside the menu: its edge */
    CHECK(place_at(&s, 2000, 500, &x, &y) && x == 1279);
    ss2t_ui_rect(&s, 0, 0, 1280, 720);                      /* in game (the scaled HUD covers the picture): unchanged */
    CHECK(s.ui_w == 0);
    CHECK(place_at(&s, 450, 0, &x, &y) && x == 160 && y == 0);
    CHECK(place_at(&s, 2129, 1079, &x, &y) && x == 1279 && y == 719);
}

static void test_focus_cleanup(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    TouchCircle f = ctl(&s, TC_FIRE), j = ctl(&s, TC_JUMP); float r = s.t.layout.move_radius;
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400, 900 - r);
    finger(&s, TOUCH_DOWN, 2, f.x, f.y); finger(&s, TOUCH_DOWN, 3, j.x, j.y);
    advance(&s, 10, shooting());
    CHECK(s.touch_key[SS2_DIK_W] && s.touch_btn[0] && s.touch_key[SS2_DIK_SPACE]);
    ss2t_cancel(&s); nout = ss2t_flush(&s, out, 512);     /* focus lost / Home: everything up at once */
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 0) >= 0 && find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0 && find(PLAT_EV_KEY, SS2_DIK_SPACE, 0) >= 0);
    CHECK(!s.touch_key[SS2_DIK_W] && !s.touch_btn[0] && !s.touch_key[SS2_DIK_SPACE] && s.qn == 0);
    finger(&s, TOUCH_UP, 2, f.x, f.y); advance(&s, 100, shooting());   /* the stale finger's release changes nothing */
    CHECK(nout == 0);
}

static void test_mode_follow_and_toggle(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; frame(&s, 0); CHECK(s.t.mode == TOUCH_CURSOR);
    uint64_t once = now; advance(&s, 10, once); advance(&s, 10, once);   /* one recentring (a menu change): stays cursor */
    CHECK(s.t.mode == TOUCH_CURSOR);
    enter_gameplay(&s);                                      /* every frame: shoot mode */
    TouchRect m = touch_control_bounds(&s.t, TC_MODE);
    finger(&s, TOUCH_DOWN, 1, m.x + m.w / 2, m.y + m.h / 2); finger(&s, TOUCH_UP, 1, m.x + m.w / 2, m.y + m.h / 2);
    advance(&s, 10, shooting());
    CHECK(s.t.mode == TOUCH_CURSOR && find(PLAT_EV_KEY, SS2_DIK_TAB, 1) >= 0);   /* MODE: Tab to the game, cursor at once */
    advance(&s, 100, shooting()); CHECK(s.t.mode == TOUCH_CURSOR);                /* the game is still switching: no revert */
    uint64_t last = now;                                     /* the game is now in interface mode: no more recentring */
    for (int i = 0; i < 70; i++) advance(&s, 10, last);
    CHECK(s.t.mode == TOUCH_CURSOR);
    finger(&s, TOUCH_DOWN, 1, m.x + m.w / 2, m.y + m.h / 2); finger(&s, TOUCH_UP, 1, m.x + m.w / 2, m.y + m.h / 2);
    advance(&s, 10, last); CHECK(s.t.mode == TOUCH_GAMEPLAY);
    for (int i = 0; i < 70; i++) advance(&s, 10, last);      /* Tab had no effect (say, a menu): follow the game back */
    CHECK(s.t.mode == TOUCH_CURSOR);
}

static void test_weapon_wheel_and_actions(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    TouchCircle n = ctl(&s, TC_NEXT), p = ctl(&s, TC_PREV), rl = ctl(&s, TC_CONSUMABLE);
    finger(&s, TOUCH_DOWN, 1, n.x, n.y); finger(&s, TOUCH_UP, 1, n.x, n.y); advance(&s, 10, shooting());
    int w = -1; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_WHEEL) w = out[i].dy;
    CHECK(w == 120);
    finger(&s, TOUCH_DOWN, 1, p.x, p.y); finger(&s, TOUCH_UP, 1, p.x, p.y); advance(&s, 10, shooting());
    w = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_WHEEL) w = out[i].dy;
    CHECK(w == -120);
    finger(&s, TOUCH_DOWN, 1, rl.x, rl.y); advance(&s, 10, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_R, 1) >= 0);
    finger(&s, TOUCH_UP, 1, rl.x, rl.y); advance(&s, 100, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_R, 0) >= 0 || !s.touch_key[SS2_DIK_R]);
    CHECK(touch_control_hit(&s.t, TC_SPRINT, ctl(&s, TC_SPRINT).x, ctl(&s, TC_SPRINT).y) == false);   /* hidden controls */
}

static void test_physical_aggregation(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    PlatEvent k; memset(&k, 0, sizeof k); k.type = PLAT_EV_KEY; k.key = SS2_DIK_W; k.down = 1;
    CHECK(ss2t_physical(&s, &k));                          /* keyboard W reaches the game */
    float r = s.t.layout.move_radius;
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400, 900 - r); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 1) < 0);            /* already down: no second press */
    finger(&s, TOUCH_UP, 1, 400, 900 - r); advance(&s, 100, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 0) < 0);            /* touch lets go, the keyboard still holds W */
    k.down = 0; CHECK(ss2t_physical(&s, &k));              /* the keyboard's release does reach the game */
}

/* Phase 2B-2: KBD shows the Android keyboard in look and cursor mode; while it is up, look mode takes no walk, turn or fire */
static int kb_calls;
static void kb_toggle(Ss2Touch *s) { kb_calls++; s->typing = !s->typing; }
static void test_keyboard_button(void) {
    Ss2Touch s; ss2t_init(&s); s.keyboard = kb_toggle; kb_calls = 0; now = 1000000000ull; enter_gameplay(&s);
    TouchCircle k = ctl(&s, TC_TARGET), f = ctl(&s, TC_FIRE);
    CHECK(k.radius > 0);
    finger(&s, TOUCH_DOWN, 1, k.x, k.y); finger(&s, TOUCH_UP, 1, k.x, k.y); advance(&s, 10, shooting());
    CHECK(kb_calls == 1 && s.typing);
    CHECK(nout == 0);                                      /* KBD itself sends nothing to the game */
    float r = s.t.layout.move_radius;
    finger(&s, TOUCH_DOWN, 2, 400, 900); finger(&s, TOUCH_MOTION, 2, 400, 900 - r);     /* stick: ignored */
    finger(&s, TOUCH_DOWN, 3, 1300, 400); finger(&s, TOUCH_MOTION, 3, 1500, 400);        /* look: ignored */
    finger(&s, TOUCH_DOWN, 4, f.x, f.y);                                                  /* fire: ignored */
    advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 1) < 0 && find(PLAT_EV_MOUSE_BUTTON, 0, 1) < 0);
    int moved = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE) moved = 1;
    CHECK(!moved);
    finger(&s, TOUCH_UP, 2, 400, 900 - r); finger(&s, TOUCH_UP, 3, 1500, 400); finger(&s, TOUCH_UP, 4, f.x, f.y);
    TouchCircle p = ctl(&s, TC_PAUSE);                     /* II (Esc) still works while typing */
    finger(&s, TOUCH_DOWN, 5, p.x, p.y); finger(&s, TOUCH_UP, 5, p.x, p.y); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_ESC, 1) >= 0);
    advance(&s, 100, shooting());
    finger(&s, TOUCH_DOWN, 6, k.x, k.y); finger(&s, TOUCH_UP, 6, k.x, k.y); advance(&s, 10, shooting());
    CHECK(kb_calls == 2 && !s.typing);                     /* KBD again: hidden, the stick works again */
    finger(&s, TOUCH_DOWN, 7, 400, 900); finger(&s, TOUCH_MOTION, 7, 400, 900 - r); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, SS2_DIK_W, 1) >= 0);
    finger(&s, TOUCH_UP, 7, 400, 900 - r); advance(&s, 100, shooting());
    /* cursor mode (a menu: no recentring): KBD works, and taps still reach the game while typing */
    for (int i = 0; i < 70; i++) advance(&s, 10, 0);
    CHECK(s.t.mode == TOUCH_CURSOR);
    finger(&s, TOUCH_DOWN, 8, k.x, k.y); finger(&s, TOUCH_UP, 8, k.x, k.y); advance(&s, 10, 0);
    CHECK(kb_calls == 3 && s.typing);
    finger(&s, TOUCH_DOWN, 9, 1170, 540); finger(&s, TOUCH_UP, 9, 1170, 540); advance(&s, 10, 0);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 1) >= 0);          /* a tap on the save screen still clicks */
}

/* the defaults keep SET, AMMO/USE and KBD in the letterbox bars and MODE right of EQUIP, at 16:9 (1920x1080 at x = 210) */
static void test_default_layout(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull;
    ss2t_frame(&s, now, W, H, true, 210, 0, 1920, 1080, 1280, 720, 0);
    for (int id = 0; id < TC_COUNT; id++) {
        if (id != TC_SETTINGS && id != TC_AMMO && id != TC_TARGET && id != TC_PAUSE) continue;
        TouchRect b = touch_control_bounds(&s.t, id);
        CHECK(b.x >= 0 && (b.x + b.w <= 210 || b.x >= 2130) && b.x + b.w <= W);
    }
    TouchRect m = touch_control_bounds(&s.t, TC_MODE);
    CHECK(m.x >= 1900 && m.x + m.w <= W);                  /* EQUIP's right edge is at about x = 1884 with the largest interface */
    for (int a = 0; a < TC_COUNT; a++) for (int b = a + 1; b < TC_COUNT; b++) {   /* no two bar buttons overlap */
        if (touch_defaults[a].radius <= 0 || touch_defaults[b].radius <= 0 || a == TC_STICK || b == TC_STICK || a == TC_MODE || b == TC_MODE) continue;
        TouchRect ra = touch_control_bounds(&s.t, a), rb = touch_control_bounds(&s.t, b);
        CHECK(ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x || ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y);
    }
}

/* a stored layout the player never moved gets the new positions; one with any control moved is kept as it is */
static void test_layout_upgrade(void) {
    static const float old[TC_COUNT][2] = { {.86f,.73f}, {.69f,.85f}, {.95f,.52f}, {.79f,.54f}, {.55f,.78f}, {.55f,.90f}, {.52f,.10f},
        {.63f,.10f}, {.22f,.54f}, {.73f,.10f}, {.05f,.06f}, {.88f,.0825f}, {.16f,.06f}, {.20f,.77f}, {.09f,.30f} };
    TouchSettings t; touch_settings_defaults(&t);
    for (int i = 0; i < TC_COUNT; i++) { t.control[i].x = old[i][0]; t.control[i].y = old[i][1]; }
    t.control[TC_STICK].scale = 1.3f; t.sensitivity_x = 1.4f;
    TouchSettings moved = t;
    CHECK(touch_settings_upgrade_layout(&t));
    CHECK(t.control[TC_SETTINGS].x == touch_defaults[TC_SETTINGS].x && t.control[TC_MODE].x == touch_defaults[TC_MODE].x);
    CHECK(t.control[TC_STICK].scale == 1.3f && t.sensitivity_x == 1.4f);   /* stick size and look speed kept */
    moved.control[TC_JUMP].x = .91f;                       /* the player moved one button */
    TouchSettings before = moved;
    CHECK(!touch_settings_upgrade_layout(&moved));
    CHECK(!memcmp(&before, &moved, sizeof moved));
    TouchSettings resized = t; resized.control[TC_FIRE].scale = 1.2f;   /* already current: nothing to do */
    CHECK(!touch_settings_upgrade_layout(&resized));
    /* the same through the file: a Phase 2A file round-trips unchanged when loaded, and only memory is upgraded */
    const char *path = "touch_layout_upgrade_test.cfg";
    TouchSettings f; touch_settings_defaults(&f);
    for (int i = 0; i < TC_COUNT; i++) { f.control[i].x = old[i][0]; f.control[i].y = old[i][1]; }
    CHECK(touch_settings_save(&f, path));
    TouchSettings l; CHECK(touch_settings_load(&l, path) == TOUCH_LOAD_OK);
    CHECK(l.control[TC_SETTINGS].x == old[TC_SETTINGS][0]);
    CHECK(touch_settings_upgrade_layout(&l) && l.control[TC_SETTINGS].x == touch_defaults[TC_SETTINGS].x);
    remove(path);
}

/* ---------------------------------------------------------------- Phase 3D: the relative (trackpad) cursor style */
static void rel_on(Ss2Touch *s, float speed) { s->t.settings.cursor_relative = true; s->t.settings.cursor_speed = speed; }
static void menu_rel(Ss2Touch *s, float speed) { ss2t_init(s); now = 1000000000ull; frame(s, 0); rel_on(s, speed); }   /* a menu: cursor mode */
static int abs_moves(int *x, int *y) {     /* cursor placements in this frame's output; the last one's position */
    int n = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE && out[i].abs) { n++; *x = out[i].x; *y = out[i].y; }
    return n;
}
static int last_abs_index(void) { int m = -1; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE && out[i].abs) m = i; return m; }
static int held0, downs0, ups0, bad0;      /* left-button edges counted over several frames: every down has its up, in order */
static void tally_reset(void) { held0 = downs0 = ups0 = bad0 = 0; }
static void tally(void) {
    for (int i = 0; i < nout; i++) {
        if (out[i].type != PLAT_EV_MOUSE_BUTTON || out[i].button != 0) continue;
        if (out[i].down) { if (held0) bad0++; held0 = 1; downs0++; } else { if (!held0) bad0++; held0 = 0; ups0++; }
    }
}
static void advance_tally(Ss2Touch *s, int frames, uint64_t ms) { for (int i = 0; i < frames; i++) { advance(s, ms, 0); tally(); } }
static int fake_x, fake_y;                 /* the game's cursor as user32_cursor_pos would report it */
static void fake_cursor(Ss2Touch *s, int *x, int *y) { (void)s; *x = fake_x; *y = fake_y; }
static int haptics_seen;
static void count_haptic(Ss2Touch *s, int level) { (void)s; (void)level; haptics_seen++; }

static void test_rel_no_jump_and_motion(void) {
    Ss2Touch s; menu_rel(&s, 1); CHECK(s.t.mode == TOUCH_CURSOR);
    int x = -1, y = -1;
    finger(&s, TOUCH_DOWN, 1, 1500, 700); advance(&s, 10, 0);
    CHECK(nout == 0);                                        /* the touch moves nothing: no jump to the finger */
    finger(&s, TOUCH_MOTION, 1, 1505, 700); advance(&s, 10, 0);
    CHECK(nout == 0);                                        /* within the slop: still nothing */
    finger(&s, TOUCH_MOTION, 1, 1644, 700); advance(&s, 10, 0);    /* 144 surface px = 80 game px, from the centre (400,300) */
    CHECK(abs_moves(&x, &y) == 1 && x == 480 && y == 300);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 1) < 0);             /* moving presses no button */
    finger(&s, TOUCH_MOTION, 1, 1644, 808); advance(&s, 10, 0);    /* 108 px down = 60 game px */
    CHECK(abs_moves(&x, &y) == 1 && x == 480 && y == 360);
    finger(&s, TOUCH_UP, 1, 1644, 808); advance(&s, 100, 0);
    CHECK(nout == 0 && s.rel_state == SS2T_REL_IDLE);        /* lifting: no click, the cursor stays */
    /* the next gesture goes on from the cursor, wherever the finger lands; fractions of a pixel add up */
    finger(&s, TOUCH_DOWN, 2, 600, 200); advance(&s, 10, 0); CHECK(nout == 0);
    finger(&s, TOUCH_MOTION, 2, 618, 200); advance(&s, 10, 0);     /* past the slop: 18 px = 10 game px */
    CHECK(abs_moves(&x, &y) == 1 && x == 490 && y == 360);
    for (int i = 1; i <= 9; i++) { finger(&s, TOUCH_MOTION, 2, 618.0f + i, 200); advance(&s, 10, 0); }   /* 9 x 1 px = 5 game px */
    CHECK(s.rel_placed_x == 495);
    finger(&s, TOUCH_UP, 2, 627, 200); advance(&s, 100, 0);
    CHECK(s.touch_btn[0] == 0 && s.qn == 0);
}

static void test_rel_speed(void) {
    Ss2Touch s; menu_rel(&s, 2); int x = -1, y = -1;
    finger(&s, TOUCH_DOWN, 1, 1000, 500); finger(&s, TOUCH_MOTION, 1, 1072, 500); advance(&s, 10, 0);   /* 72 px = 40 game px, x2 */
    CHECK(abs_moves(&x, &y) == 1 && x == 480 && y == 300);
    finger(&s, TOUCH_UP, 1, 1072, 500); advance(&s, 100, 0);
    s.t.settings.cursor_speed = .5f;
    finger(&s, TOUCH_DOWN, 2, 1000, 500); finger(&s, TOUCH_MOTION, 2, 1144, 500); advance(&s, 10, 0);   /* 144 px = 80, x0.5 */
    CHECK(abs_moves(&x, &y) == 1 && x == 520);
    finger(&s, TOUCH_UP, 2, 1144, 500); advance(&s, 100, 0);
    /* the cursor speed is its own: mouse look in shoot mode still turns 80 game px for 144 surface px */
    enter_gameplay(&s);
    finger(&s, TOUCH_DOWN, 3, 1300, 400); advance(&s, 10, shooting());
    finger(&s, TOUCH_MOTION, 3, 1444, 400); advance(&s, 10, shooting());
    int dx = 0, placed = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE) { if (out[i].abs) placed = 1; else dx += out[i].dx; }
    CHECK(dx >= 79 && dx <= 80 && !placed);
    finger(&s, TOUCH_UP, 3, 1444, 400); advance(&s, 10, shooting());
}

static void test_rel_tap_and_double_tap(void) {
    Ss2Touch s; menu_rel(&s, 1); int x = -1, y = -1;
    finger(&s, TOUCH_DOWN, 1, 1500, 700); finger(&s, TOUCH_MOTION, 1, 1644, 700); finger(&s, TOUCH_UP, 1, 1644, 700);
    advance(&s, 100, 0); advance(&s, 100, 0);                /* the cursor is at 480,300 */
    finger(&s, TOUCH_DOWN, 1, 700, 900); now += 80000000ull; finger(&s, TOUCH_MOTION, 1, 704, 903); finger(&s, TOUCH_UP, 1, 704, 903);
    frame(&s, 0);                                            /* a short tap far from the cursor, with a little jitter */
    int d = find(PLAT_EV_MOUSE_BUTTON, 0, 1), m = last_abs_index();
    CHECK(abs_moves(&x, &y) == 1 && x == 480 && y == 300);   /* the click is where the cursor is, not where the finger was */
    CHECK(m >= 0 && d > m);                                  /* placed before the press */
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) < 0);             /* released after the minimum hold */
    advance(&s, 70, 0); CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0 && s.touch_btn[0] == 0);
    /* a double tap: two ordinary clicks in order, nothing left held */
    tally_reset();
    finger(&s, TOUCH_DOWN, 2, 700, 900); now += 40000000ull; finger(&s, TOUCH_UP, 2, 700, 900); frame(&s, 0); tally();
    now += 60000000ull; finger(&s, TOUCH_DOWN, 3, 702, 901); now += 40000000ull; finger(&s, TOUCH_UP, 3, 702, 901); frame(&s, 0); tally();
    advance_tally(&s, 10, 30);
    CHECK(downs0 == 2 && ups0 == 2 && !bad0 && !held0 && s.touch_btn[0] == 0 && s.qn == 0);
    /* rapid taps: five clicks, none lost, none stuck */
    tally_reset();
    for (int i = 0; i < 5; i++) { finger(&s, TOUCH_DOWN, 10 + i, 800, 800); now += 20000000ull; finger(&s, TOUCH_UP, 10 + i, 800, 800); advance(&s, 10, 0); tally(); }
    advance_tally(&s, 20, 30);
    CHECK(downs0 == 5 && ups0 == 5 && !bad0 && !held0 && s.touch_btn[0] == 0 && s.qn == 0);
    CHECK(s.rel_placed_x == 480 && s.rel_placed_y == 300);   /* taps never moved the cursor */
    /* a slow tap (longer than a tap, shorter than a hold) does nothing and leaves nothing behind */
    finger(&s, TOUCH_DOWN, 20, 800, 800); advance(&s, 200, 0); advance(&s, 200, 0); finger(&s, TOUCH_UP, 20, 800, 800); advance(&s, 100, 0);
    CHECK(nout == 0 && s.rel_state == SS2T_REL_IDLE && s.touch_btn[0] == 0);
}

static void test_rel_hold_drag(void) {
    Ss2Touch s; menu_rel(&s, 1); int x = -1, y = -1;
    s.haptic = count_haptic; s.t.settings.haptics_enabled = true; haptics_seen = 0;
    finger(&s, TOUCH_DOWN, 1, 1000, 500);
    advance(&s, 200, 0); advance(&s, 200, 0); CHECK(nout == 0);   /* 400 ms: not yet a hold */
    advance(&s, 110, 0);                                     /* held still for 510 ms: the left button goes down at the cursor */
    int d = find(PLAT_EV_MOUSE_BUTTON, 0, 1), m = last_abs_index();
    CHECK(abs_moves(&x, &y) == 1 && x == 400 && y == 300 && m >= 0 && d > m);
    CHECK(s.rel_state == SS2T_REL_HELD && s.touch_btn[0] == 1 && haptics_seen == 1);
    finger(&s, TOUCH_MOTION, 1, 1144, 500); advance(&s, 10, 0);   /* the drag: the press reaches the game before the cursor moves */
    CHECK(abs_moves(&x, &y) == 0);
    advance(&s, 60, 0); CHECK(abs_moves(&x, &y) == 1 && x == 480 && y == 300 && s.touch_btn[0] == 1);
    finger(&s, TOUCH_UP, 1, 1144, 500); advance(&s, 10, 0);  /* lifting releases it where the cursor is; the cursor stays */
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0 && abs_moves(&x, &y) == 0 && s.touch_btn[0] == 0 && s.rel_state == SS2T_REL_IDLE);
    /* a hold lifted without moving: a press and a release at the cursor (a click), nothing held */
    tally_reset();
    finger(&s, TOUCH_DOWN, 2, 900, 600); advance_tally(&s, 6, 100);
    CHECK(downs0 == 1 && held0);
    finger(&s, TOUCH_UP, 2, 900, 600); advance_tally(&s, 3, 30);
    CHECK(downs0 == 1 && ups0 == 1 && !bad0 && !held0 && s.rel_placed_x == 480);
    /* no haptics when they are off */
    s.t.settings.haptics_enabled = false; haptics_seen = 0;
    finger(&s, TOUCH_DOWN, 3, 900, 600); advance_tally(&s, 6, 100); finger(&s, TOUCH_UP, 3, 900, 600); advance_tally(&s, 3, 30);
    CHECK(haptics_seen == 0 && !held0);
    /* moving first is a move, never a hold, however long the finger stays down afterwards */
    tally_reset();
    finger(&s, TOUCH_DOWN, 4, 900, 600); finger(&s, TOUCH_MOTION, 4, 950, 600); advance_tally(&s, 10, 100);
    finger(&s, TOUCH_UP, 4, 950, 600); advance_tally(&s, 2, 30);
    CHECK(downs0 == 0 && ups0 == 0);
}

static void test_rel_cancel(void) {
    Ss2Touch s; menu_rel(&s, 1);
    /* focus lost during a held drag: the button is released at once, and the stale finger does nothing afterwards */
    finger(&s, TOUCH_DOWN, 1, 1000, 500); advance(&s, 300, 0); advance(&s, 300, 0);
    finger(&s, TOUCH_MOTION, 1, 1100, 500); advance(&s, 100, 0);
    CHECK(s.touch_btn[0] == 1 && s.rel_state == SS2T_REL_HELD);
    ss2t_cancel(&s); nout = ss2t_flush(&s, out, 512);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0 && s.touch_btn[0] == 0 && s.rel_state == SS2T_REL_IDLE && s.qn == 0);
    finger(&s, TOUCH_MOTION, 1, 1300, 500); finger(&s, TOUCH_UP, 1, 1300, 500); advance(&s, 100, 0);
    CHECK(nout == 0);
    /* focus lost while moving (no button): nothing to release, and the next touch still does not jump */
    finger(&s, TOUCH_DOWN, 2, 1000, 500); finger(&s, TOUCH_MOTION, 2, 1100, 500); advance(&s, 10, 0);
    int placed = s.rel_placed_x;
    ss2t_cancel(&s); nout = ss2t_flush(&s, out, 512);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) < 0 && s.rel_state == SS2T_REL_IDLE);
    finger(&s, TOUCH_DOWN, 3, 400, 900); advance(&s, 10, 0);
    CHECK(nout == 0 && s.rel_placed_x == placed);
    finger(&s, TOUCH_UP, 3, 400, 900); advance(&s, 10, 0);
    /* the picture moved (rotation, resize): the same release */
    finger(&s, TOUCH_DOWN, 4, 1000, 500); advance(&s, 300, 0); advance(&s, 300, 0); CHECK(s.touch_btn[0] == 1);
    ss2t_frame(&s, now, W, H, true, 451, 0, 1440, 1080, 800, 600, 0); nout = ss2t_flush(&s, out, 512);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 0) >= 0 && s.touch_btn[0] == 0 && s.rel_state == SS2T_REL_IDLE);
}

static void test_rel_fingers_and_use(void) {
    Ss2Touch s; menu_rel(&s, 1); int x = -1, y = -1;
    finger(&s, TOUCH_DOWN, 1, 1000, 500); finger(&s, TOUCH_MOTION, 1, 1072, 500); advance(&s, 10, 0);   /* 40 game px */
    CHECK(abs_moves(&x, &y) == 1 && x == 440);
    finger(&s, TOUCH_DOWN, 2, 1400, 300); finger(&s, TOUCH_MOTION, 2, 1700, 300); advance(&s, 10, 0);   /* a second finger: not the cursor's */
    CHECK(nout == 0);
    TouchCircle u = ctl(&s, TC_AMMO);                        /* USE (right click) with a third finger, the cursor finger still down */
    finger(&s, TOUCH_DOWN, 3, u.x, u.y); finger(&s, TOUCH_UP, 3, u.x, u.y); advance(&s, 10, 0);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 1, 1) >= 0 && abs_moves(&x, &y) == 0);   /* where the cursor is: no placement */
    advance(&s, 70, 0); CHECK(s.touch_btn[1] == 0);
    finger(&s, TOUCH_MOTION, 1, 1144, 500); advance(&s, 10, 0);   /* the cursor finger goes on */
    CHECK(abs_moves(&x, &y) == 1 && x == 480);
    finger(&s, TOUCH_UP, 2, 1700, 300); finger(&s, TOUCH_UP, 1, 1144, 500); advance(&s, 100, 0);
    CHECK(nout == 0 && s.touch_btn[0] == 0 && s.rel_state == SS2T_REL_IDLE);
    /* lifted far outside where it started (in the letterbox bar): the cursor stops at the picture's edge, nothing is held */
    finger(&s, TOUCH_DOWN, 4, 1800, 500); finger(&s, TOUCH_MOTION, 4, 100, 500); finger(&s, TOUCH_UP, 4, 100, 500); advance(&s, 100, 0);
    CHECK(abs_moves(&x, &y) == 1 && x == 0 && s.touch_btn[0] == 0 && s.qn == 0);
}

static void test_rel_modes(void) {
    Ss2Touch s; menu_rel(&s, 1); int x = -1, y = -1;
    finger(&s, TOUCH_DOWN, 1, 1000, 500); advance(&s, 300, 0); advance(&s, 300, 0); CHECK(s.touch_btn[0] == 1);   /* a held drag */
    enter_gameplay(&s);                                      /* the game switches to shoot mode: everything is released */
    CHECK(s.touch_btn[0] == 0 && s.rel_state == SS2T_REL_IDLE && s.t.mode == TOUCH_GAMEPLAY);
    finger(&s, TOUCH_UP, 1, 1000, 500); advance(&s, 10, shooting());
    TouchRect m = touch_control_bounds(&s.t, TC_MODE);       /* MODE to the cursor, then the relative cursor at once */
    finger(&s, TOUCH_DOWN, 2, m.x + m.w / 2, m.y + m.h / 2); finger(&s, TOUCH_UP, 2, m.x + m.w / 2, m.y + m.h / 2);
    uint64_t last = now; advance(&s, 10, last);
    CHECK(s.t.mode == TOUCH_CURSOR);
    for (int i = 0; i < 70; i++) advance(&s, 10, last);      /* the game is in interface mode now */
    finger(&s, TOUCH_DOWN, 3, 1500, 300); advance(&s, 10, last); CHECK(nout == 0);
    finger(&s, TOUCH_MOTION, 3, 1572, 300); advance(&s, 10, last);
    CHECK(abs_moves(&x, &y) == 1 && x == 440 && y == 300);   /* from where it was (no jump), 40 game px */
    finger(&s, TOUCH_UP, 3, 1572, 300); advance(&s, 100, last);
    CHECK(s.touch_btn[0] == 0);
}

static void test_rel_follows_game_cursor(void) {
    Ss2Touch s; menu_rel(&s, 1); s.cursor_pos = fake_cursor; int x = -1, y = -1;
    fake_x = 100; fake_y = 100;                              /* the game's cursor (a physical mouse put it there) */
    finger(&s, TOUCH_DOWN, 1, 1000, 500); finger(&s, TOUCH_MOTION, 1, 1144, 500); advance(&s, 10, 0);
    CHECK(abs_moves(&x, &y) == 1 && x == 180 && y == 100);
    finger(&s, TOUCH_UP, 1, 1144, 500); advance(&s, 100, 0);
    fake_x = 180;                                            /* the game reports where touch put it: go on from there */
    finger(&s, TOUCH_DOWN, 2, 1000, 500); finger(&s, TOUCH_MOTION, 2, 1036, 500); advance(&s, 10, 0);
    CHECK(abs_moves(&x, &y) == 1 && x == 200);
    finger(&s, TOUCH_UP, 2, 1036, 500); advance(&s, 100, 0);
    fake_x = 600; fake_y = 500;                              /* the mouse moved it again */
    finger(&s, TOUCH_DOWN, 3, 1000, 500); finger(&s, TOUCH_MOTION, 3, 1036, 500); advance(&s, 10, 0);
    CHECK(abs_moves(&x, &y) == 1 && x == 620 && y == 500);
    finger(&s, TOUCH_UP, 3, 1036, 500); advance(&s, 100, 0);
    PlatEvent b; memset(&b, 0, sizeof b); b.type = PLAT_EV_MOUSE_BUTTON; b.button = 0; b.down = 1;   /* a physical click still works */
    CHECK(ss2t_physical(&s, &b)); b.down = 0; CHECK(ss2t_physical(&s, &b));
}

/* a 1280x720 game (1920x1080 at x = 210) on a 640x480 menu fitted into 160,0 960x720: the cursor still moves as far on screen
   as the finger, continuously, and stops at the edges without a dead zone when it comes back */
static void frame720(Ss2Touch *s, uint64_t ms) { now += ms * 1000000ull; ss2t_frame(s, now, W, H, true, 210, 0, 1920, 1080, 1280, 720, 0); nout = ss2t_flush(s, out, 512); }
static void test_rel_menu_mapping(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; frame720(&s, 0); rel_on(&s, 1);
    ss2t_ui_rect(&s, 160, 0, 960, 720); int x = -1, y = -1;
    finger(&s, TOUCH_DOWN, 1, 1000, 500); finger(&s, TOUCH_MOTION, 1, 1090, 500); frame720(&s, 10);
    CHECK(abs_moves(&x, &y) == 1 && x == 720 && y == 360);   /* 90 surface px = 80 window px = 60 menu px = 90 screen px */
    finger(&s, TOUCH_MOTION, 1, 1090, 590); frame720(&s, 10);
    CHECK(abs_moves(&x, &y) == 1 && y == 420);                   /* the menu fills the height: 90 px = 60 window px = 90 screen px */
    for (int i = 1; i <= 20; i++) { finger(&s, TOUCH_MOTION, 1, 1090.0f - 100 * i, 590); frame720(&s, 10); }   /* far past the left edge */
    CHECK(s.rel_placed_x == 0);
    finger(&s, TOUCH_MOTION, 1, -900, 590); frame720(&s, 10); /* back right by 10 px: moves at once */
    CHECK(abs_moves(&x, &y) == 1 && x == 9);
    finger(&s, TOUCH_UP, 1, -900, 590); frame720(&s, 100);
    ss2t_ui_rect(&s, 0, 0, 1280, 720);                       /* in game (the HUD covers the picture): 90 px = 60 game px */
    finger(&s, TOUCH_DOWN, 2, 1000, 500); finger(&s, TOUCH_MOTION, 2, 1090, 500); frame720(&s, 10);
    CHECK(abs_moves(&x, &y) == 1 && x == 69);
    finger(&s, TOUCH_UP, 2, 1090, 500); frame720(&s, 100);
}

/* settings: older files load as Direct at speed 1; the new keys round-trip; the panel switches style and speed; Reset is Direct */
static const char *panel_path = "touch_cursor_panel_test.cfg";
static void save_to_file(Ss2Touch *s) { s->t.save_failed = !touch_settings_save(&s->t.settings, panel_path); }
static void ui_tap(Ss2Touch *s, TouchUiAction a) {
    TouchRect r = touch_ui_bounds(&s->t, a); CHECK(r.w > 0);
    finger(s, TOUCH_DOWN, 90, r.x + r.w / 2, r.y + r.h / 2); finger(s, TOUCH_UP, 90, r.x + r.w / 2, r.y + r.h / 2);
}
static void write_file(const char *path, const char *text) { FILE *f = fopen(path, "wb"); if (f) { fputs(text, f); fclose(f); } }
static void test_cursor_settings(void) {
    TouchSettings d; touch_settings_defaults(&d); CHECK(!d.cursor_relative && d.cursor_speed == 1 && d.version == 3);
    const char *path = "touch_cursor_settings_test.cfg";
    write_file(path, "version=3\nopacity=0.7\nfire.x=0.8\nsensitivity_x=1.5\nstick_size=1.3\n");   /* a file from before this setting */
    TouchSettings l; CHECK(touch_settings_load(&l, path) == TOUCH_LOAD_OK);
    CHECK(!l.cursor_relative && l.cursor_speed == 1);
    CHECK(fabsf(l.opacity - .7f) < 1e-6f && fabsf(l.control[TC_FIRE].x - .8f) < 1e-6f && fabsf(l.sensitivity_x - 1.5f) < 1e-6f &&
          fabsf(l.control[TC_STICK].scale - 1.3f) < 1e-6f);  /* everything else kept */
    l.cursor_relative = true; l.cursor_speed = 2.5f; CHECK(touch_settings_save(&l, path));
    TouchSettings r; CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_OK);
    CHECK(r.cursor_relative && r.cursor_speed == 2.5f && fabsf(r.opacity - .7f) < 1e-6f && fabsf(r.sensitivity_x - 1.5f) < 1e-6f);
    write_file(path, "version=3\ncursor_relative=1\ncursor_speed=99\n");
    CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_OK && r.cursor_relative && r.cursor_speed == TOUCH_CURSOR_SPEED_MAX);
    write_file(path, "version=3\ncursor_relative=7\ncursor_speed=nan\n");
    CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_OK && !r.cursor_relative && r.cursor_speed == 1);
    write_file(path, "version=2\nopacity=0.5\n");
    CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_MIGRATED && !r.cursor_relative && r.cursor_speed == 1);
    remove(path);

    Ss2Touch s; ss2t_init(&s); s.save_settings = save_to_file; now = 1000000000ull; frame(&s, 0);
    TouchSettings before = s.t.settings;
    touch_ui_open(&s.t);
    CHECK(touch_ui_bounds(&s.t, TU_CURSOR_STYLE).w == 0);    /* on the second page, not the first */
    ui_tap(&s, TU_PAGE); CHECK(s.t.settings_page == 1);
    static const TouchUiAction page2[] = { TU_PAGE, TU_CURSOR_STYLE, TU_CURSOR_SPEED_DOWN, TU_CURSOR_SPEED_UP, TU_HAPTICS, TU_HAPTIC_STRENGTH,
                                           TU_WHEEL_WEAPON, TU_WHEEL_CONSUMABLE, TU_WHEEL_EXPLOSIVE, TU_RESET, TU_CANCEL, TU_SAVE };
    int n2 = (int)(sizeof page2 / sizeof page2[0]);
    for (int a = 0; a < n2; a++) { TouchRect ra = touch_ui_bounds(&s.t, page2[a]); CHECK(ra.w > 0 && ra.y >= 0 && ra.y + ra.h <= H);
        for (int b = a + 1; b < n2; b++) { TouchRect rb = touch_ui_bounds(&s.t, page2[b]);
            CHECK(ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x || ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y); } }
    ui_tap(&s, TU_CURSOR_STYLE); CHECK(s.t.settings.cursor_relative);
    for (int i = 0; i < 20; i++) touch_ui_action(&s.t, TU_CURSOR_SPEED_UP);
    CHECK(s.t.settings.cursor_speed == TOUCH_CURSOR_SPEED_MAX);
    for (int i = 0; i < 20; i++) touch_ui_action(&s.t, TU_CURSOR_SPEED_DOWN);
    CHECK(s.t.settings.cursor_speed == TOUCH_CURSOR_SPEED_MIN);
    ui_tap(&s, TU_CURSOR_SPEED_UP); ui_tap(&s, TU_CURSOR_SPEED_UP); ui_tap(&s, TU_CURSOR_SPEED_UP);
    CHECK(s.t.settings.cursor_speed == 1);
    ui_tap(&s, TU_CURSOR_SPEED_UP); CHECK(s.t.settings.cursor_speed == 1.25f);
    CHECK(s.t.settings.opacity == before.opacity && s.t.settings.sensitivity_x == before.sensitivity_x &&
          !memcmp(s.t.settings.control, before.control, sizeof before.control));   /* layout, opacity and look untouched */
    ui_tap(&s, TU_SAVE); CHECK(s.t.panel == TOUCH_UI_CLOSED && !s.t.save_failed);
    TouchSettings f; CHECK(touch_settings_load(&f, panel_path) == TOUCH_LOAD_OK && f.cursor_relative && f.cursor_speed == 1.25f);
    /* the style applies to the next touch, without a restart */
    finger(&s, TOUCH_DOWN, 1, 1500, 700); advance(&s, 10, 0); CHECK(nout == 0);
    finger(&s, TOUCH_UP, 1, 1500, 700); advance(&s, 100, 0);
    /* a restart: the stored style comes back */
    Ss2Touch s2; ss2t_init(&s2); CHECK(touch_settings_load(&s2.t.settings, panel_path) == TOUCH_LOAD_OK); now += 1000000000ull; frame(&s2, 0);
    finger(&s2, TOUCH_DOWN, 1, 1500, 700); advance(&s2, 10, 0); CHECK(nout == 0 && s2.rel_state == SS2T_REL_DOWN);
    finger(&s2, TOUCH_UP, 1, 1500, 700); advance(&s2, 100, 0);
    /* Reset goes back to Direct at speed 1; Cancel keeps what was saved */
    touch_ui_open(&s.t); touch_ui_action(&s.t, TU_PAGE); touch_ui_action(&s.t, TU_RESET);
    CHECK(!s.t.settings.cursor_relative && s.t.settings.cursor_speed == 1);
    touch_ui_action(&s.t, TU_CANCEL); CHECK(s.t.settings.cursor_relative && s.t.settings.cursor_speed == 1.25f);
    /* back to Direct through the panel: a tap places the cursor at the finger again */
    touch_ui_open(&s.t); touch_ui_action(&s.t, TU_PAGE); touch_ui_action(&s.t, TU_CURSOR_STYLE); touch_ui_action(&s.t, TU_SAVE);
    CHECK(!s.t.settings.cursor_relative);
    finger(&s, TOUCH_DOWN, 2, 1170, 540); now += 50000000ull; finger(&s, TOUCH_UP, 2, 1170, 540); frame(&s, 0);
    int x = -1, y = -1; CHECK(abs_moves(&x, &y) >= 1 && x == 400 && y == 300 && find(PLAT_EV_MOUSE_BUTTON, 0, 1) > last_abs_index());
    advance(&s, 100, 0);
    remove(panel_path);
}

/* ---------------------------------------------------------------- the crouch key from the installation's user.bnd */
static const char *const CROUCH_CMDS[] = { "crouch", "+crouchhold" };
static TouchBind bind_of(const char *text) { TouchBind b; touch_bind_find(text, strlen(text), CROUCH_CMDS, 2, SS2_DIK_LSHIFT, &b); return b; }

static void test_bind_parse(void) {
    /* the stock 2.48 user.bnd (SCP Beta 8 installation): Shift is kept, the build's tested default */
    TouchBind b = bind_of("; This file was generated by System Shock (tm) 2.\r\n\r\nbind keypad_center crouch\r\nbind shift crouch\r\n"
                          "bind space +jump\r\ncrouch_unmount 1\r\n");
    CHECK(b.dik == SS2_DIK_LSHIFT && !strcmp(b.command, "crouch"));
    /* the user's SCP Beta 4 user.bnd: Shift creeps, C crouches (toggle), Ctrl and keypad 5 crouch while held */
    b = bind_of("bind c crouch\nbind ctrl +crouchhold\nbind e+shift \"+fly -1\"\nbind keypad_center +crouchhold\n"
                "bind keypad_end crouch\nbind shift +creepon\nbind space +jump\n");
    CHECK(b.dik == 0x2E && !strcmp(b.key, "c") && !strcmp(b.command, "crouch"));          /* C, the toggle, not keypad 1 */
    /* only the held form bound: Ctrl (a modifier) before the keypad */
    b = bind_of("bind keypad_center +crouchhold\nbind ctrl +crouchhold\nbind shift +creepon\n");
    CHECK(b.dik == 0x1D && !strcmp(b.command, "+crouchhold"));
    /* the last binding of a key counts; unbind removes; chords and mouse names are never used; quotes and case */
    CHECK(bind_of("bind shift crouch\nbind shift +creepon\nbind x crouch\n").dik == 0x2D);
    CHECK(bind_of("bind c crouch\nunbind c\n").dik == 0);
    CHECK(bind_of("bind c+shift crouch\nbind mouse3 crouch\n").dik == 0);
    CHECK(bind_of("BIND V \"Crouch\"\n").dik == 0x2F);
    CHECK(bind_of("bind c crouch_unmount\nbind z \"crouch; jump\"\n").dik == 0);              /* not exactly the command */
    CHECK(bind_of("").dik == 0);
    /* key names */
    CHECK(touch_bind_key_dik("shift") == SS2_DIK_LSHIFT && touch_bind_key_dik("space") == SS2_DIK_SPACE && touch_bind_key_dik("TAB") == SS2_DIK_TAB);
    CHECK(touch_bind_key_dik("f1") == 0x3B && touch_bind_key_dik("F10") == 0x44 && touch_bind_key_dik("f12") == 0x58 && touch_bind_key_dik("f13") == 0);
    CHECK(touch_bind_key_dik("keypad_center") == 0x4C && touch_bind_key_dik("keypad_slash") == 0xB5 && touch_bind_key_dik("del") == 0xD3);
    CHECK(touch_bind_key_dik("0") == 0x0B && touch_bind_key_dik("1") == 0x02 && touch_bind_key_dik("`") == 0x29 && touch_bind_key_dik("m") == 0x32);
    CHECK(touch_bind_key_dik("mouse1") == 0 && touch_bind_key_dik("joy_axisx") == 0 && touch_bind_key_dik("c+ctrl") == 0);
}

static void test_crouch_key(void) {
    Ss2Touch s; ss2t_init(&s); now = 1000000000ull; enter_gameplay(&s);
    TouchCircle c = ctl(&s, TC_CROUCH);
    CHECK(s.crouch_dik == SS2_DIK_LSHIFT);                 /* default: the stock binding */
    ss2t_set_crouch_key(&s, 0x2E);                         /* the legacy installation: C */
    finger(&s, TOUCH_DOWN, 1, c.x, c.y); now += 20000000ull; finger(&s, TOUCH_UP, 1, c.x, c.y);   /* a 20 ms tap */
    frame(&s, shooting());
    CHECK(find(PLAT_EV_KEY, 0x2E, 1) >= 0 && find(PLAT_EV_KEY, SS2_DIK_LSHIFT, 1) < 0);   /* C, never Shift (creep) */
    CHECK(find(PLAT_EV_KEY, 0x2E, 0) < 0);                 /* held at least the minimum, so the game sees the toggle */
    advance(&s, 70, shooting()); CHECK(find(PLAT_EV_KEY, 0x2E, 0) >= 0);
    /* moving and aiming while crouching: stick, look and CROUCH together */
    float r = s.t.layout.move_radius;
    finger(&s, TOUCH_DOWN, 2, 400, 900); finger(&s, TOUCH_MOTION, 2, 400, 900 - r);
    finger(&s, TOUCH_DOWN, 3, 1300, 400); advance(&s, 10, shooting());
    finger(&s, TOUCH_DOWN, 1, c.x, c.y); finger(&s, TOUCH_MOTION, 3, 1300 + 144, 400); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_KEY, 0x2E, 1) >= 0 && s.stick_key[0]);
    int dx = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE && !out[i].abs) dx += out[i].dx;
    CHECK(dx >= 79 && dx <= 80);
    /* a held-only binding (+crouchhold on Ctrl): held exactly as long as the finger */
    finger(&s, TOUCH_UP, 1, c.x, c.y); advance(&s, 100, shooting());
    ss2t_set_crouch_key(&s, 0x1D);
    finger(&s, TOUCH_DOWN, 1, c.x, c.y); advance(&s, 10, shooting()); CHECK(find(PLAT_EV_KEY, 0x1D, 1) >= 0);
    for (int k = 0; k < 10; k++) { advance(&s, 50, shooting()); CHECK(find(PLAT_EV_KEY, 0x1D, 0) < 0); }   /* half a second, frames as a game's */
    CHECK(s.touch_key[0x1D] == 1);
    /* the binding changes while CROUCH is down: the old key is released, nothing else stays held */
    ss2t_set_crouch_key(&s, 0x2E); frame(&s, shooting());
    CHECK(find(PLAT_EV_KEY, 0x1D, 0) >= 0 && s.touch_key[0x1D] == 0 && s.crouch_dik == 0x2E);
    ss2t_set_crouch_key(&s, 0); CHECK(s.crouch_dik == SS2_DIK_LSHIFT);                       /* 0: back to Shift */
    finger(&s, TOUCH_UP, 1, c.x, c.y); finger(&s, TOUCH_UP, 2, 400, 900 - r); finger(&s, TOUCH_UP, 3, 1444, 400); advance(&s, 100, shooting());
}

/* ---------------------------------------------------------------- Phase 3F: radial menus over a fake game (core/ss2game.c's role) */
static int fg_refreshes, fg_selects, fg_cat, fg_id, fg_haptic1, fg_haptic2; static bool fg_refuse;
static TouchRadialEntry fg_e[TOUCH_RADIAL_CATEGORY_COUNT][TOUCH_RADIAL_MAX_ENTRIES]; static int fg_n[TOUCH_RADIAL_CATEGORY_COUNT];
static char fg_why[64], fg_msg[64];
static void fg_refresh(Ss2Touch *s) { (void)s; fg_refreshes++; }
static int fg_wheel(Ss2Touch *s, int cat, TouchRadialEntry *e, int max, char *title, size_t tn, char *why, size_t wn) {
    (void)s; int n = fg_n[cat] < max ? fg_n[cat] : max;
    memcpy(e, fg_e[cat], (size_t)n * sizeof *e); snprintf(title, tn, "WHEEL %d", cat); snprintf(why, wn, "%s", n ? "" : fg_why);
    return n;
}
static bool fg_select(Ss2Touch *s, int cat, int id) { (void)s; fg_selects++; fg_cat = cat; fg_id = id; return !fg_refuse; }
static bool fg_message(Ss2Touch *s, char *out, size_t n) { (void)s; if (!fg_msg[0]) return false; snprintf(out, n, "%s", fg_msg); fg_msg[0] = 0; return true; }
static void fg_haptic(Ss2Touch *s, int level) { (void)s; if (level == 1) fg_haptic1++; else fg_haptic2++; }
static void fg_entries(int cat, int n, int first_id) {
    fg_n[cat] = n;
    for (int i = 0; i < n; i++) { TouchRadialEntry *e = &fg_e[cat][i]; memset(e, 0, sizeof *e);
        e->id = first_id + i; snprintf(e->label, sizeof e->label, "E%d", i); snprintf(e->name, sizeof e->name, "ENTRY %d", i); e->available = true; e->count = 1; }
}
/* a game in shoot mode with the radial services, SS2's wheel preferences on, a 2340x1080 surface (800x600 game at x = 450) */
static void game_init(Ss2Touch *s) {
    ss2t_init(s); s->game_on = true; s->game_refresh = fg_refresh; s->game_wheel = fg_wheel; s->game_select = fg_select; s->game_message = fg_message;
    s->haptic = fg_haptic;
    s->t.settings.wheel_enabled[0] = s->t.settings.wheel_enabled[1] = s->t.settings.wheel_enabled[2] = true;
    fg_refreshes = fg_selects = fg_haptic1 = fg_haptic2 = 0; fg_cat = fg_id = -1; fg_refuse = false; fg_why[0] = fg_msg[0] = 0;
    fg_entries(TOUCH_RADIAL_WEAPON, 4, 100); fg_entries(TOUCH_RADIAL_AMMO, 3, 200); fg_entries(TOUCH_RADIAL_CONSUMABLE, 5, 300);
    fg_entries(TOUCH_RADIAL_SYSTEM, 4, 1);
    now = 1000000000ull; enter_gameplay(s);
}
static void segment(const Ss2Touch *s, int i, float *x, float *y) {       /* the middle of segment i of the open wheel (or fan) */
    const TouchRadialSelector *r = &s->t.radial;
    touch_radial_segment_point(r, i, (r->inner_radius + r->outer_radius) / 2, x, y);
}
static int game_input;                       /* keys, buttons and wheel notches sent to the game, over several frames */
static void count_input(void) { for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_KEY || out[i].type == PLAT_EV_MOUSE_BUTTON || out[i].type == PLAT_EV_MOUSE_WHEEL) game_input++; }
static void run_ms(Ss2Touch *s, int ms) { for (int t = 0; t < ms; t += 10) { advance(s, 10, shooting()); count_input(); } }
static void hold(Ss2Touch *s, int id, TouchCircle c, int ms) { finger(s, TOUCH_DOWN, id, c.x, c.y); count_input(); run_ms(s, ms); }

static void test_radial_weapon_hold_select(void) {
    Ss2Touch s; game_init(&s); game_input = 0;
    TouchCircle n = ctl(&s, TC_NEXT), p = ctl(&s, TC_PREV);
    finger(&s, TOUCH_DOWN, 1, n.x, n.y);
    CHECK(fg_refreshes == 1);                              /* the game is read as the finger lands, long before the wheel opens */
    run_ms(&s, 380); CHECK(!s.t.radial.open);              /* not at 380 ms (Shockolate's 320 is too short here) */
    run_ms(&s, 30); CHECK(s.t.radial.open && s.t.radial.category == TOUCH_RADIAL_WEAPON && s.t.radial.count == 4);
    Ss2Overlay o; ss2t_overlay(&s, &o); CHECK(!strcmp(o.wheel_title, "WHEEL 0"));
    float x, y; segment(&s, 2, &x, &y);
    finger(&s, TOUCH_MOTION, 1, x, y); run_ms(&s, 20); CHECK(s.t.radial.highlighted == 2);
    finger(&s, TOUCH_UP, 1, x, y); run_ms(&s, 100);
    CHECK(!s.t.radial.open && fg_selects == 1 && fg_cat == TOUCH_RADIAL_WEAPON && fg_id == 102);
    CHECK(game_input == 0);                                /* no wheel notch, no key, no button: the selection is the game's command */
    /* < opens the same wheel */
    hold(&s, 2, p, 450); CHECK(s.t.radial.open && s.t.radial.category == TOUCH_RADIAL_WEAPON);
    segment(&s, 0, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); finger(&s, TOUCH_UP, 2, x, y); run_ms(&s, 100);
    CHECK(fg_selects == 2 && fg_id == 100 && game_input == 0);
}

static void tap_frames(Ss2Touch *s, TouchCircle c, int ms) {   /* a tap lasting ms, the game running its frames meanwhile */
    finger(s, TOUCH_DOWN, 1, c.x, c.y); for (int t = 0; t < ms; t += 10) advance(s, 10, shooting());
    finger(s, TOUCH_UP, 1, c.x, c.y); frame(s, shooting());
}
static int notch_sum(void) { int w = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_WHEEL) w += out[i].dy; return w; }
static void test_radial_short_tap_unchanged(void) {
    Ss2Touch s; game_init(&s);
    TouchCircle n = ctl(&s, TC_NEXT), p = ctl(&s, TC_PREV), a = ctl(&s, TC_AMMO), ii = ctl(&s, TC_PAUSE);
    tap_frames(&s, n, 120); CHECK(notch_sum() == 120);    /* > : one notch up, on release */
    advance(&s, 10, shooting()); CHECK(notch_sum() == 0); /* exactly once */
    tap_frames(&s, p, 120); CHECK(notch_sum() == -120);   /* < : one notch down */
    advance(&s, 100, shooting());
    tap_frames(&s, a, 120); CHECK(find(PLAT_EV_KEY, SS2_DIK_B, 1) >= 0);   /* AMMO: B (cycle_ammo) */
    advance(&s, 100, shooting()); CHECK(!s.touch_key[SS2_DIK_B]);
    tap_frames(&s, ii, 120); CHECK(find(PLAT_EV_KEY, SS2_DIK_ESC, 1) >= 0); /* II: Esc */
    advance(&s, 100, shooting());
    tap_frames(&s, n, 390); CHECK(notch_sum() == 120 && !s.t.radial.open);  /* just under the hold time: still a tap */
    advance(&s, 100, shooting());
    CHECK(fg_selects == 0 && !s.t.radial.open);
    /* without the game's state (another executable, no game yet) a hold is a plain press, as before Phase 3F */
    s.game_on = false;
    finger(&s, TOUCH_DOWN, 1, n.x, n.y); frame(&s, shooting());
    CHECK(notch_sum() == 120);                             /* on the press, as Phase 2A */
    run_ms(&s, 600); CHECK(!s.t.radial.open); finger(&s, TOUCH_UP, 1, n.x, n.y); advance(&s, 100, shooting());
}

static void test_radial_cancel(void) {
    Ss2Touch s; game_init(&s); game_input = 0;
    TouchCircle n = ctl(&s, TC_NEXT);
    hold(&s, 1, n, 450); CHECK(s.t.radial.open);
    float cx = s.t.radial.cx, cy = s.t.radial.cy;
    finger(&s, TOUCH_MOTION, 1, cx, cy); finger(&s, TOUCH_UP, 1, cx, cy); run_ms(&s, 100);   /* released in the centre */
    CHECK(!s.t.radial.open && fg_selects == 0);
    hold(&s, 2, n, 450); CHECK(s.t.radial.open);
    float far = s.t.radial.outer_radius * 1.4f;
    finger(&s, TOUCH_MOTION, 2, cx, cy - far); finger(&s, TOUCH_UP, 2, cx, cy - far); run_ms(&s, 100);   /* outside the wheel */
    CHECK(!s.t.radial.open && fg_selects == 0);
    /* a finger that slides off the button before the wheel opens: neither a wheel nor the tap action */
    finger(&s, TOUCH_DOWN, 3, n.x, n.y); run_ms(&s, 100);
    finger(&s, TOUCH_MOTION, 3, n.x + n.radius * 3, n.y); run_ms(&s, 500);
    CHECK(!s.t.radial.open);
    finger(&s, TOUCH_UP, 3, n.x + n.radius * 3, n.y); run_ms(&s, 100);
    CHECK(fg_selects == 0 && game_input == 0);
}

static void test_radial_multitouch(void) {
    Ss2Touch s; game_init(&s);
    float r = s.t.layout.move_radius; TouchCircle a = ctl(&s, TC_AMMO), f = ctl(&s, TC_FIRE);
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400, 900 - r); advance(&s, 10, shooting());
    CHECK(s.stick_key[0] && find(PLAT_EV_KEY, SS2_DIK_W, 1) >= 0);
    finger(&s, TOUCH_DOWN, 2, 1300, 400);                  /* a look finger */
    finger(&s, TOUCH_DOWN, 3, a.x, a.y); for (int i = 0; i < 45; i++) advance(&s, 10, shooting());
    CHECK(s.t.radial.open && s.t.radial.category == TOUCH_RADIAL_AMMO && s.t.radial.owner >= 0);
    CHECK(s.stick_key[0] && s.t.move_owner >= 0);          /* still walking */
    CHECK(s.t.look_owner < 0);                             /* look ownership ends when the wheel opens */
    float x, y; segment(&s, 1, &x, &y);
    finger(&s, TOUCH_DOWN, 4, x, y); finger(&s, TOUCH_UP, 4, x, y); advance(&s, 10, shooting());   /* another finger on a segment */
    CHECK(s.t.radial.open && fg_selects == 0 && s.t.radial.highlighted != 1);
    finger(&s, TOUCH_DOWN, 5, f.x, f.y); advance(&s, 10, shooting());
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 0, 1) < 0);           /* FIRE does not fire while a wheel is open */
    finger(&s, TOUCH_MOTION, 2, 1500, 400); advance(&s, 10, shooting());
    int moved = 0; for (int i = 0; i < nout; i++) if (out[i].type == PLAT_EV_MOUSE_MOVE) moved = 1;
    CHECK(!moved);                                         /* the old look finger does not turn the view */
    finger(&s, TOUCH_MOTION, 3, x, y); finger(&s, TOUCH_UP, 3, x, y); advance(&s, 10, shooting());
    CHECK(fg_selects == 1 && fg_cat == TOUCH_RADIAL_AMMO && fg_id == 201 && s.stick_key[0]);
    finger(&s, TOUCH_UP, 5, f.x, f.y); finger(&s, TOUCH_UP, 2, 1500, 400); finger(&s, TOUCH_UP, 1, 400, 900 - r);
    advance(&s, 100, shooting()); CHECK(!s.stick_key[0] && !s.touch_key[SS2_DIK_W] && !s.touch_btn[0]);
}

static void test_radial_cancel_by_lifecycle(void) {
    Ss2Touch s; game_init(&s); game_input = 0;
    TouchCircle n = ctl(&s, TC_NEXT); float r = s.t.layout.move_radius, x, y;
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400, 900 - r);
    hold(&s, 2, n, 450); CHECK(s.t.radial.open && s.touch_key[SS2_DIK_W]);
    ss2t_cancel(&s); nout = ss2t_flush(&s, out, 512);     /* focus lost, Home */
    CHECK(!s.t.radial.open && find(PLAT_EV_KEY, SS2_DIK_W, 0) >= 0 && !s.touch_key[SS2_DIK_W]);
    segment(&s, 1, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); finger(&s, TOUCH_UP, 2, x, y); finger(&s, TOUCH_UP, 1, 400, 900);
    advance(&s, 100, shooting()); CHECK(fg_selects == 0 && nout == 0);   /* the stale fingers do nothing */
    /* the game leaves shoot mode (a menu, the Tab key): the wheel closes */
    hold(&s, 3, n, 450); CHECK(s.t.radial.open);
    uint64_t last = now; for (int i = 0; i < 40; i++) advance(&s, 10, last);
    CHECK(s.t.mode == TOUCH_CURSOR && !s.t.radial.open);
    finger(&s, TOUCH_UP, 3, x, y); advance(&s, 100, last); CHECK(fg_selects == 0);
    /* MODE pressed by another finger while a wheel is open */
    enter_gameplay(&s); hold(&s, 4, n, 450); CHECK(s.t.radial.open);
    TouchRect m = touch_control_bounds(&s.t, TC_MODE);
    finger(&s, TOUCH_DOWN, 5, m.x + m.w / 2, m.y + m.h / 2); finger(&s, TOUCH_UP, 5, m.x + m.w / 2, m.y + m.h / 2);
    CHECK(!s.t.radial.open); finger(&s, TOUCH_UP, 4, x, y); advance(&s, 100, shooting()); CHECK(fg_selects == 0);
}

static void test_radial_cursor_mode_ammo(void) {
    Ss2Touch s; game_init(&s);
    uint64_t last = now; for (int i = 0; i < 40; i++) advance(&s, 10, last);   /* interface mode */
    CHECK(s.t.mode == TOUCH_CURSOR);
    TouchCircle a = ctl(&s, TC_AMMO);
    finger(&s, TOUCH_DOWN, 1, a.x, a.y); frame(&s, last);
    CHECK(find(PLAT_EV_MOUSE_BUTTON, 1, 1) >= 0);          /* AMMO is USE (right click) at once in cursor mode, as before */
    for (int i = 0; i < 60; i++) advance(&s, 10, last);
    CHECK(!s.t.radial.open); finger(&s, TOUCH_UP, 1, a.x, a.y); advance(&s, 100, last); CHECK(fg_selects == 0);
}

static void test_radial_empty_and_unavailable(void) {
    Ss2Touch s; game_init(&s); game_input = 0;
    TouchCircle n = ctl(&s, TC_NEXT); float x, y; Ss2Overlay o;
    fg_n[TOUCH_RADIAL_WEAPON] = 0; snprintf(fg_why, sizeof fg_why, "NO WEAPONS CARRIED");
    hold(&s, 1, n, 450); CHECK(!s.t.radial.open);          /* nothing to offer: no wheel ... */
    ss2t_overlay(&s, &o); CHECK(!strcmp(o.toast, "NO WEAPONS CARRIED"));   /* ... but the reason */
    finger(&s, TOUCH_UP, 1, n.x, n.y); run_ms(&s, 100); CHECK(fg_selects == 0 && game_input == 0);   /* and no notch either */
    run_ms(&s, 2000); ss2t_overlay(&s, &o); CHECK(!o.toast[0]);                                       /* the notice goes */
    fg_entries(TOUCH_RADIAL_WEAPON, 4, 100); fg_e[TOUCH_RADIAL_WEAPON][1].available = false;
    hold(&s, 2, n, 450); CHECK(s.t.radial.open);
    segment(&s, 1, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); run_ms(&s, 20); CHECK(s.t.radial.highlighted == -1);
    finger(&s, TOUCH_UP, 2, x, y); run_ms(&s, 100); CHECK(fg_selects == 0);            /* an unavailable entry is never chosen */
    /* the game changes while the wheel is open: the entry is refreshed and becomes unavailable */
    hold(&s, 3, n, 450); CHECK(s.t.radial.open); int before = fg_refreshes;
    fg_e[TOUCH_RADIAL_WEAPON][2].available = false;
    run_ms(&s, 300); CHECK(fg_refreshes > before && !s.t.radial.entries[2].available);
    segment(&s, 2, &x, &y); finger(&s, TOUCH_MOTION, 3, x, y); finger(&s, TOUCH_UP, 3, x, y); run_ms(&s, 100);
    CHECK(fg_selects == 0);
    /* a refused selection and the game's result both show as a notice */
    fg_refuse = true; hold(&s, 4, n, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 4, x, y); finger(&s, TOUCH_UP, 4, x, y); run_ms(&s, 20);
    ss2t_overlay(&s, &o); CHECK(fg_selects == 1 && !strcmp(o.toast, "NOT AVAILABLE NOW"));
    snprintf(fg_msg, sizeof fg_msg, "PISTOL"); run_ms(&s, 20); ss2t_overlay(&s, &o); CHECK(!strcmp(o.toast, "PISTOL"));
}

static void test_radial_haptics(void) {
    Ss2Touch s; game_init(&s); s.t.settings.haptics_enabled = true;
    TouchCircle n = ctl(&s, TC_NEXT); float x, y;
    hold(&s, 1, n, 450); CHECK(s.t.radial.open && fg_haptic2 == 1);   /* the wheel opens: one strong tick */
    for (int i = 0; i < 4; i++) { segment(&s, i, &x, &y); finger(&s, TOUCH_MOTION, 1, x, y); run_ms(&s, 20); }
    CHECK(fg_haptic1 == 4);                                /* one light tick per segment entered */
    finger(&s, TOUCH_MOTION, 1, x, y); run_ms(&s, 20); CHECK(fg_haptic1 == 4);   /* staying on one: no more */
    finger(&s, TOUCH_UP, 1, x, y); run_ms(&s, 100); CHECK(fg_selects == 1);
    s.t.settings.haptics_enabled = false; fg_haptic1 = fg_haptic2 = 0;
    hold(&s, 2, n, 450); segment(&s, 1, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); run_ms(&s, 20);
    CHECK(fg_haptic1 == 0 && fg_haptic2 == 0);             /* haptics off: none */
    finger(&s, TOUCH_UP, 2, x, y); run_ms(&s, 100);
}

static void test_system_wheel_quickload(void) {
    Ss2Touch s; game_init(&s); game_input = 0; Ss2Overlay o;
    TouchCircle ii = ctl(&s, TC_PAUSE); float x, y;
    hold(&s, 1, ii, 450); CHECK(s.t.radial.open && s.t.radial.category == TOUCH_RADIAL_SYSTEM);
    segment(&s, 0, &x, &y); finger(&s, TOUCH_MOTION, 1, x, y); finger(&s, TOUCH_UP, 1, x, y); run_ms(&s, 100);
    CHECK(fg_selects == 1 && fg_cat == TOUCH_RADIAL_SYSTEM && fg_id == 1 && game_input == 0);   /* no Esc */
    /* quickload (id 4, segment 3): a question first */
    hold(&s, 2, ii, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); finger(&s, TOUCH_UP, 2, x, y); run_ms(&s, 50);
    ss2t_overlay(&s, &o); CHECK(fg_selects == 1 && o.confirm);
    TouchRect no = ss2t_confirm_rect(&s.t, 2), yes = ss2t_confirm_rect(&s.t, 1);
    finger(&s, TOUCH_DOWN, 3, no.x + no.w / 2, no.y + no.h / 2); finger(&s, TOUCH_UP, 3, no.x + no.w / 2, no.y + no.h / 2); run_ms(&s, 50);
    ss2t_overlay(&s, &o); CHECK(!o.confirm && fg_selects == 1 && !strcmp(o.toast, "QUICKLOAD CANCELLED") && game_input == 0);
    hold(&s, 4, ii, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 4, x, y); finger(&s, TOUCH_UP, 4, x, y); run_ms(&s, 50);
    finger(&s, TOUCH_DOWN, 5, 100, 100); finger(&s, TOUCH_UP, 5, 100, 100); run_ms(&s, 50);   /* a touch anywhere else: no */
    ss2t_overlay(&s, &o); CHECK(!o.confirm && fg_selects == 1);
    hold(&s, 6, ii, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 6, x, y); finger(&s, TOUCH_UP, 6, x, y); run_ms(&s, 50);
    finger(&s, TOUCH_DOWN, 7, yes.x + yes.w / 2, yes.y + yes.h / 2); finger(&s, TOUCH_UP, 7, yes.x + yes.w / 2, yes.y + yes.h / 2); run_ms(&s, 50);
    CHECK(fg_selects == 2 && fg_id == SS2T_SYS_QUICKLOAD && game_input == 0);   /* LOAD: the quickload, once */
    /* the question goes by itself after 6 s, and with focus loss; neither loads */
    hold(&s, 8, ii, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 8, x, y); finger(&s, TOUCH_UP, 8, x, y); run_ms(&s, 6100);
    ss2t_overlay(&s, &o); CHECK(!o.confirm && fg_selects == 2);
    hold(&s, 9, ii, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 9, x, y); finger(&s, TOUCH_UP, 9, x, y); run_ms(&s, 50);
    ss2t_cancel(&s); ss2t_overlay(&s, &o); CHECK(!o.confirm);
    finger(&s, TOUCH_DOWN, 10, yes.x + yes.w / 2, yes.y + yes.h / 2); finger(&s, TOUCH_UP, 10, yes.x + yes.w / 2, yes.y + yes.h / 2); run_ms(&s, 50);
    CHECK(fg_selects == 2);
    /* QUICKLOAD CONFIRM off: at once */
    s.t.settings.quickload_confirm = false;
    hold(&s, 11, ii, 450); segment(&s, 3, &x, &y); finger(&s, TOUCH_MOTION, 11, x, y); finger(&s, TOUCH_UP, 11, x, y); run_ms(&s, 50);
    CHECK(fg_selects == 3 && fg_id == SS2T_SYS_QUICKLOAD);
    /* the system wheel switched off: II is Esc on the press again */
    s.t.settings.wheel_enabled[2] = false;
    finger(&s, TOUCH_DOWN, 12, ii.x, ii.y); frame(&s, shooting()); CHECK(find(PLAT_EV_KEY, SS2_DIK_ESC, 1) >= 0);
    finger(&s, TOUCH_UP, 12, ii.x, ii.y); advance(&s, 100, shooting());
}

static void test_item_button(void) {
    Ss2Touch s; game_init(&s); Ss2Overlay o;
    ss2t_frame(&s, now, W, H, true, 210, 0, 1920, 1080, 1280, 720, now);   /* 16:9: the bars are 210 px wide */
    TouchRect it = touch_control_bounds(&s.t, TC_EXPLOSIVE);
    CHECK(it.w > 0 && it.x >= 0 && it.x + it.w <= 210);   /* ITEM in the left bar, off the game's picture */
    for (int id = 0; id < TC_COUNT; id++) {
        if (id == TC_EXPLOSIVE || id == TC_STICK || touch_defaults[id].radius <= 0) continue;
        TouchRect b = touch_control_bounds(&s.t, id);
        CHECK(it.x + it.w <= b.x || b.x + b.w <= it.x || it.y + it.h <= b.y || b.y + b.h <= it.y);   /* overlaps nothing */
    }
    enter_gameplay(&s); game_input = 0;
    TouchCircle c = ctl(&s, TC_EXPLOSIVE);
    finger(&s, TOUCH_DOWN, 1, c.x, c.y); now += 100000000ull; finger(&s, TOUCH_UP, 1, c.x, c.y); run_ms(&s, 50);
    ss2t_overlay(&s, &o); CHECK(!strncmp(o.toast, "HOLD ITEM", 9) && game_input == 0);   /* a tap only explains */
    hold(&s, 2, c, 450); CHECK(s.t.radial.open && s.t.radial.category == TOUCH_RADIAL_CONSUMABLE && s.t.radial.count == 5);
    float x, y; segment(&s, 4, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); finger(&s, TOUCH_UP, 2, x, y); run_ms(&s, 50);
    CHECK(fg_selects == 1 && fg_id == 304 && game_input == 0);
    s.t.settings.wheel_enabled[1] = false; run_ms(&s, 20);   /* ITEM WHEEL off: the button goes */
    CHECK(touch_control_bounds(&s.t, TC_EXPLOSIVE).w == 0 && !touch_control_hit(&s.t, TC_EXPLOSIVE, c.x, c.y));
    s.t.settings.wheel_enabled[1] = true; run_ms(&s, 20); CHECK(touch_control_bounds(&s.t, TC_EXPLOSIVE).w > 0);
    s.game_on = false; run_ms(&s, 20); CHECK(touch_control_bounds(&s.t, TC_EXPLOSIVE).w == 0);   /* no game state: hidden too */
}

/* ---------------------------------------------------------------- Phase 3G: wheels anchored at the held button */
static void frame_at(Ss2Touch *s, int w, int h, int gw, int gh) {   /* a w x h screen, the game gw x gh fitted into it */
    float sc = fminf((float)w / (float)gw, (float)h / (float)gh); int vw = (int)((float)gw * sc), vh = (int)((float)gh * sc);
    ss2t_frame(s, now, w, h, true, (w - vw) / 2, (h - vh) / 2, vw, vh, gw, gh, now);
    nout = ss2t_flush(s, out, 512); count_input();
}
static void run_at(Ss2Touch *s, int ms, int w, int h, int gw, int gh) { for (int t = 0; t < ms; t += 10) { now += 10000000ull; frame_at(s, w, h, gw, gh); } }
static bool on_screen(float x, float y, int w, int h) { return x >= 0 && y >= 0 && x <= (float)w && y <= (float)h; }
/* what an anchored wheel promises: it opens around the held button b, the finger starts in its cancel circle with
   nothing highlighted, every segment (rim and label) is on the screen, and sweeping round from the first segment to the
   last meets each segment once, in order, with no gap (no ambiguity) */
static void check_anchored(Ss2Touch *s, TouchCircle b, int w, int h) {
    const TouchRadialSelector *r = &s->t.radial;
    CHECK(r->open && r->count > 0);
    if (!r->open || r->count <= 0) return;
    CHECK(r->cx == b.x && r->cy == b.y);                                                   /* around the button itself */
    CHECK(hypotf(b.x - r->cx, b.y - r->cy) + (float)h * .02f < r->inner_radius);          /* the finger in the cancel circle */
    CHECK(r->highlighted == -1);
    float half = (touch_radial_is_fan(r) ? r->span : 6.2831853f) / 2 / (float)r->count;
    for (int i = 0; i < r->count; i++) {
        float x, y; TouchRadialSelector c = *r;
        for (int e = -1; e <= 1; e++) {                     /* the segment's two edges and its middle, at the rim */
            float a = touch_radial_segment_angle(r, i) + (float)e * half;
            CHECK(on_screen(r->cx + cosf(a) * r->outer_radius, r->cy + sinf(a) * r->outer_radius, w, h));
        }
        touch_radial_segment_point(r, i, r->outer_radius * .68f, &x, &y); CHECK(on_screen(x, y, w, h));   /* its label */
        segment(s, i, &x, &y); CHECK(touch_radial_highlight(&c, c.owner, x, y) == i);
    }
    TouchRadialSelector c = *r; int last = -1, seen = 0;
    float a0 = touch_radial_segment_angle(r, 0), a1 = touch_radial_segment_angle(r, r->count - 1), d = (r->inner_radius + r->outer_radius) / 2;
    for (float a = a0; a <= a1 + 1e-4f; a += .01f) {
        int k = touch_radial_highlight(&c, c.owner, r->cx + cosf(a) * d, r->cy + sinf(a) * d);
        CHECK(k >= 0 && k >= last && k <= last + 1);
        if (k != last) seen++;
        last = k;
    }
    CHECK(seen == r->count && last == r->count - 1);
}
/* hold b until its wheel opens; returns whether it did */
static bool hold_at(Ss2Touch *s, int id, TouchCircle b, int w, int h, int gw, int gh) {
    finger(s, TOUCH_DOWN, id, b.x, b.y); run_at(s, 450, w, h, gw, gh); return s->t.radial.open;
}

static void test_anchored_default_layout(void) {
    /* the phone at the game resolutions in use (16:9 enlarged interface, 4:3 letterboxed), and other screen shapes */
    static const int sizes[][4] = { {2340,1080,1280,720}, {2340,1080,800,600}, {2340,1080,1024,768}, {1920,1080,1280,720},
                                    {1280,800,800,600}, {1024,768,1024,768}, {1080,1080,800,600} };
    static const int buttons[] = { TC_NEXT, TC_PREV, TC_AMMO, TC_EXPLOSIVE, TC_PAUSE };
    static const int wheels[] = { TOUCH_RADIAL_WEAPON, TOUCH_RADIAL_WEAPON, TOUCH_RADIAL_AMMO, TOUCH_RADIAL_CONSUMABLE, TOUCH_RADIAL_SYSTEM };
    for (unsigned z = 0; z < sizeof sizes / sizeof sizes[0]; z++) {
        int w = sizes[z][0], h = sizes[z][1], gw = sizes[z][2], gh = sizes[z][3];
        for (int many = 0; many < 2; many++) for (unsigned k = 0; k < sizeof buttons / sizeof buttons[0]; k++) {
            Ss2Touch s; game_init(&s); run_at(&s, 50, w, h, gw, gh); CHECK(s.t.mode == TOUCH_GAMEPLAY);
            if (many) { fg_entries(TOUCH_RADIAL_WEAPON, 8, 100); fg_entries(TOUCH_RADIAL_AMMO, 6, 200); fg_entries(TOUCH_RADIAL_CONSUMABLE, 16, 300); fg_entries(TOUCH_RADIAL_SYSTEM, 5, 1); }
            s.t.settings.quickload_confirm = false;            /* (QUICKLOAD, the system wheel's last entry, would ask first) */
            TouchCircle b = ctl(&s, buttons[k]); game_input = 0;
            CHECK(hold_at(&s, 1, b, w, h, gw, gh) && (int)s.t.radial.category == wheels[k]);
            if (!s.t.radial.open) { printf("  (screen %dx%d, button %d)\n", w, h, buttons[k]); continue; }
            check_anchored(&s, b, w, h);
            int n = s.t.radial.count, before = fg_selects;
            /* lifted where it was pressed (no slide): nothing, neither the wheel's entry nor the tap action */
            finger(&s, TOUCH_UP, 1, b.x, b.y); run_at(&s, 50, w, h, gw, gh);
            CHECK(!s.t.radial.open && fg_selects == before && game_input == 0);
            /* the same finger slides from the button straight into the last entry */
            hold_at(&s, 2, b, w, h, gw, gh); float x, y; segment(&s, n - 1, &x, &y);
            finger(&s, TOUCH_MOTION, 2, (b.x + x) / 2, (b.y + y) / 2); finger(&s, TOUCH_MOTION, 2, x, y); run_at(&s, 20, w, h, gw, gh);
            CHECK(s.t.radial.highlighted == n - 1);
            finger(&s, TOUCH_UP, 2, x, y); run_at(&s, 50, w, h, gw, gh);
            CHECK(fg_selects == before + 1 && fg_cat == wheels[k] && fg_id == fg_e[wheels[k]][n - 1].id && game_input == 0);
            if (fg_selects != before + 1) printf("  (screen %dx%d game %dx%d, button %d, %d entries: fan %d, highlighted %d, open %d)\n", w, h, gw, gh, buttons[k], n, touch_radial_is_fan(&s.t.radial), s.t.radial.highlighted, s.t.radial.open);
            /* out past the rim, then lifted: cancelled */
            hold_at(&s, 3, b, w, h, gw, gh); segment(&s, 0, &x, &y);
            float dx = x - s.t.radial.cx, dy = y - s.t.radial.cy, l = hypotf(dx, dy), far = s.t.radial.outer_radius * 1.3f;
            float ox = s.t.radial.cx + dx / l * far, oy = s.t.radial.cy + dy / l * far;
            finger(&s, TOUCH_MOTION, 3, x, y); finger(&s, TOUCH_MOTION, 3, ox, oy); finger(&s, TOUCH_UP, 3, ox, oy); run_at(&s, 50, w, h, gw, gh);
            CHECK(!s.t.radial.open && fg_selects == before + 1);
            /* onto an entry and back to the button: cancelled */
            hold_at(&s, 4, b, w, h, gw, gh); segment(&s, 0, &x, &y);
            finger(&s, TOUCH_MOTION, 4, x, y); run_at(&s, 20, w, h, gw, gh); CHECK(s.t.radial.highlighted == 0);
            finger(&s, TOUCH_MOTION, 4, b.x, b.y); run_at(&s, 20, w, h, gw, gh); CHECK(s.t.radial.highlighted == -1);
            finger(&s, TOUCH_UP, 4, b.x, b.y); run_at(&s, 50, w, h, gw, gh);
            CHECK(!s.t.radial.open && fg_selects == before + 1 && game_input == 0);
        }
    }
    /* the phone's own layout: < > fan upwards from the bottom edge, AMMO and ITEM rightwards from the left edge, II from
       the top-left corner down and to the right */
    Ss2Touch s; game_init(&s); TouchCircle b; float x, y;
    static const int fans[][3] = { {TC_NEXT, 0, -1}, {TC_PREV, 0, -1}, {TC_AMMO, 1, 0}, {TC_EXPLOSIVE, 1, 0}, {TC_PAUSE, 1, 1} };
    for (int k = 0; k < 5; k++) {
        b = ctl(&s, fans[k][0]); hold(&s, 1, b, 450); CHECK(s.t.radial.open && touch_radial_is_fan(&s.t.radial));
        float sx = 0, sy = 0; for (int i = 0; i < s.t.radial.count; i++) { segment(&s, i, &x, &y); sx += x - s.t.radial.cx; sy += y - s.t.radial.cy; }
        if (fans[k][1]) CHECK(sx > 0); else CHECK(fabsf(sx) < fabsf(sy));   /* towards the inside of the screen */
        if (fans[k][2] < 0) CHECK(sy < 0); else if (fans[k][2] > 0) CHECK(sy > 0);
        finger(&s, TOUCH_UP, 1, b.x, b.y); run_ms(&s, 50);
    }
    CHECK(fg_selects == 0);
}

static void test_anchored_custom_layout(void) {
    /* a layout the player moved: the weapon button in each corner, at each edge and in the middle */
    static const float spots[][2] = { {0,0}, {1,0}, {0,1}, {1,1}, {.5f,0}, {.5f,1}, {0,.5f}, {1,.5f}, {.5f,.5f}, {.3f,.85f}, {.97f,.12f} };
    for (unsigned p = 0; p < sizeof spots / sizeof spots[0]; p++) for (int n = 1; n <= 16; n += 5) {
        Ss2Touch s; game_init(&s); fg_entries(TOUCH_RADIAL_WEAPON, n, 100);
        for (int id = 0; id < TC_COUNT; id++)                   /* the other buttons out of the way */
            if (id != TC_NEXT && id != TC_STICK && id != TC_FIRE && id != TC_USE) touch_editor_position(&s.t, id, W * .62f, H * .4f);
        touch_editor_position(&s.t, TC_NEXT, spots[p][0] * W, spots[p][1] * H);
        TouchCircle b = ctl(&s, TC_NEXT); game_input = 0;
        hold(&s, 1, b, 450); CHECK(s.t.radial.open && s.t.radial.category == TOUCH_RADIAL_WEAPON && s.t.radial.count == n);
        if (!s.t.radial.open || s.t.radial.count != n) printf("  (spot %.2f %.2f, %d entries: open %d, count %d, button %.0f %.0f r %.0f)\n", spots[p][0], spots[p][1], n, s.t.radial.open, s.t.radial.count, b.x, b.y, b.radius);
        if (!s.t.radial.open) continue;
        check_anchored(&s, b, W, H);
        if (spots[p][0] == .5f && spots[p][1] == .5f) {      /* room all round: a whole wheel, entry 0 at the top as before */
            CHECK(!touch_radial_is_fan(&s.t.radial) && fabsf(touch_radial_segment_angle(&s.t.radial, 0) + 1.5707963f) < 1e-4f);
            CHECK(s.t.radial.cx == b.x && s.t.radial.cy == b.y);
        }
        float x, y; segment(&s, n / 2, &x, &y); finger(&s, TOUCH_MOTION, 1, x, y); finger(&s, TOUCH_UP, 1, x, y); run_ms(&s, 50);
        CHECK(fg_selects == 1 && fg_id == 100 + n / 2 && game_input == 0);
        if (n == 1 && touch_radial_is_fan(&s.t.radial)) CHECK(s.t.radial.outer_radius < H * .29f);   /* one entry: no larger wheel */
    }
}

static void test_anchored_while_moving(void) {
    Ss2Touch s; game_init(&s);
    float r = s.t.layout.move_radius; TouchCircle n = ctl(&s, TC_NEXT), a = ctl(&s, TC_AMMO); float x, y;
    /* walking, the stick finger moving all the time, while > opens its fan and a weapon is chosen from it */
    finger(&s, TOUCH_DOWN, 1, 400, 900); finger(&s, TOUCH_MOTION, 1, 400, 900 - r); advance(&s, 10, shooting());
    CHECK(s.stick_key[0]);
    finger(&s, TOUCH_DOWN, 2, n.x, n.y);
    for (int t = 0; t < 45; t++) { finger(&s, TOUCH_MOTION, 1, 400 + (t % 2 ? r * .2f : -r * .2f), 900 - r); advance(&s, 10, shooting()); }
    CHECK(s.t.radial.open && s.t.radial.owner >= 0 && s.stick_key[0] && s.t.move_owner >= 0);
    check_anchored(&s, n, W, H);
    segment(&s, 1, &x, &y); finger(&s, TOUCH_MOTION, 2, x, y); finger(&s, TOUCH_MOTION, 1, 400 - r * .2f, 900 - r);
    finger(&s, TOUCH_UP, 2, x, y); advance(&s, 10, shooting());
    CHECK(fg_selects == 1 && fg_id == 101 && s.stick_key[0]);
    /* AMMO's fan, with the stick still held; then Home with it open: everything released, nothing chosen */
    hold(&s, 3, a, 450); CHECK(s.t.radial.open && s.stick_key[0]); check_anchored(&s, a, W, H);
    segment(&s, 2, &x, &y); finger(&s, TOUCH_MOTION, 3, x, y); run_ms(&s, 20); CHECK(s.t.radial.highlighted == 2);
    ss2t_cancel(&s); nout = ss2t_flush(&s, out, 512);
    CHECK(!s.t.radial.open && !s.touch_key[SS2_DIK_W]);
    finger(&s, TOUCH_UP, 3, x, y); finger(&s, TOUCH_UP, 1, 400, 900 - r); advance(&s, 100, shooting());
    CHECK(fg_selects == 1 && !s.stick_key[0] && !s.touch_btn[0] && !s.touch_btn[1]);
}

static void test_anchored_off_is_centred(void) {
    /* without the SS2 hook (Shockolate's own behaviour) a wheel still opens at the screen centre, unchanged */
    Ss2Touch s; game_init(&s); s.t.radial_anchored = false;
    TouchCircle n = ctl(&s, TC_NEXT); hold(&s, 1, n, 450);
    CHECK(s.t.radial.open && !touch_radial_is_fan(&s.t.radial) && s.t.radial.cx == W * .5f && s.t.radial.cy == H * .51f);
    CHECK(fabsf(s.t.radial.outer_radius - H * .28f) < .01f && fabsf(s.t.radial.inner_radius - H * .28f * .28f) < .01f);
    float x, y; segment(&s, 0, &x, &y); CHECK(fabsf(x - W * .5f) < .5f && y < s.t.radial.cy);   /* entry 0 at the top */
    finger(&s, TOUCH_MOTION, 1, x, y); finger(&s, TOUCH_UP, 1, x, y); run_ms(&s, 50); CHECK(fg_selects == 1 && fg_id == 100);
    /* and a selector set up without the Phase 3G fields (span 0) behaves as a whole wheel */
    TouchRadialSelector r; memset(&r, 0, sizeof r); r.open = true; r.owner = 0; r.cx = 100; r.cy = 100; r.inner_radius = 10; r.outer_radius = 50; r.count = 4;
    for (int i = 0; i < 4; i++) r.entries[i].available = true;
    CHECK(touch_radial_highlight(&r, 0, 100, 70) == 0 && touch_radial_highlight(&r, 0, 130, 100) == 1 && touch_radial_highlight(&r, 0, 100, 130) == 2);
}

static void test_many_entries(void) {
    Ss2Touch s; game_init(&s);
    fg_entries(TOUCH_RADIAL_CONSUMABLE, 14, 300);
    TouchCircle c = ctl(&s, TC_EXPLOSIVE); hold(&s, 1, c, 450);
    CHECK(s.t.radial.open && s.t.radial.count == 14 && s.t.radial.outer_radius > H * .3f);   /* a larger wheel */
    check_anchored(&s, c, W, H);                           /* Phase 3G: at ITEM, a fan wholly on the screen */
    for (int i = 0; i < 14; i += 3) { float x, y; segment(&s, i, &x, &y); finger(&s, TOUCH_MOTION, 1, x, y); run_ms(&s, 10); CHECK(s.t.radial.highlighted == i); }
    float x, y; segment(&s, 13, &x, &y); finger(&s, TOUCH_MOTION, 1, x, y); finger(&s, TOUCH_UP, 1, x, y); run_ms(&s, 50);
    CHECK(fg_selects == 1 && fg_id == 313);
}

/* the SS2 settings: Shockolate's file and defaults unless touch_settings_game is set; then a file from before Phase 3F gets
   the wheels on (its wheel keys did nothing in System Shock 2) and ITEM at its default, and everything else stays */
static void ss2_defaults_for_test(TouchSettings *t) { t->wheel_enabled[0] = t->wheel_enabled[1] = t->wheel_enabled[2] = true; }
static const TouchSettingsGame ss2_game_for_test = { ss2_defaults_for_test, TC_EXPLOSIVE };
static void test_settings_3f(void) {
    const char *path = "touch_settings_3f_test.cfg"; TouchSettings l, r;
    /* Shockolate (no game hook): defaults off, no new keys written */
    touch_settings_game = NULL;
    touch_settings_defaults(&l); CHECK(!l.wheel_enabled[0] && !l.wheel_enabled[1] && !l.wheel_enabled[2] && l.quickload_confirm);
    CHECK(touch_settings_save(&l, path));
    { FILE *f = fopen(path, "rb"); char b[4096] = ""; size_t n = f ? fread(b, 1, sizeof b - 1, f) : 0; if (f) fclose(f); b[n] = 0;
      CHECK(!strstr(b, "game_radials") && !strstr(b, "quickload_confirm")); }
    /* System Shock 2: a Phase 3E file (the player's layout moved, relative cursor, wheels stored off, ITEM never shown) */
    touch_settings_game = &ss2_game_for_test;
    write_file(path, "version=3\nopacity=0.6\ncursor_relative=1\ncursor_speed=1.5\nweapon_wheel=0\nconsumable_wheel=0\nexplosive_wheel=0\n"
                     "fire.x=0.81\nfire.y=0.7\nexplosive.x=0.63\nexplosive.y=0.1\nstick_size=1.2\n");
    CHECK(touch_settings_load(&l, path) == TOUCH_LOAD_OK);
    CHECK(l.wheel_enabled[0] && l.wheel_enabled[1] && l.wheel_enabled[2] && l.quickload_confirm && !l.game_radials);
    CHECK(fabsf(l.control[TC_EXPLOSIVE].x - touch_defaults[TC_EXPLOSIVE].x) < 1e-6f && fabsf(l.control[TC_EXPLOSIVE].y - touch_defaults[TC_EXPLOSIVE].y) < 1e-6f);
    CHECK(fabsf(l.opacity - .6f) < 1e-6f && l.cursor_relative && l.cursor_speed == 1.5f && fabsf(l.control[TC_FIRE].x - .81f) < 1e-6f &&
          fabsf(l.control[TC_STICK].scale - 1.2f) < 1e-6f);                  /* the player's own settings, all kept */
    CHECK(!touch_settings_upgrade_layout(&l));            /* a moved layout is never replaced */
    /* saved by this build: from now on the wheel preferences are the player's */
    l.wheel_enabled[0] = false; l.quickload_confirm = false; l.control[TC_EXPLOSIVE].y = .6f; CHECK(touch_settings_save(&l, path));
    CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_OK && r.game_radials && !r.wheel_enabled[0] && r.wheel_enabled[1] && !r.quickload_confirm &&
          fabsf(r.control[TC_EXPLOSIVE].y - .6f) < 1e-6f && r.cursor_relative);
    /* an older build reads the new file: the new keys are ignored, everything else as before */
    touch_settings_game = NULL;
    CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_OK && r.cursor_relative && fabsf(r.opacity - .6f) < 1e-6f && !r.wheel_enabled[0]);
    touch_settings_game = &ss2_game_for_test;
    /* no file: SS2's defaults */
    remove(path); CHECK(touch_settings_load(&r, path) == TOUCH_LOAD_DEFAULTS && r.wheel_enabled[0] && r.wheel_enabled[2] && r.quickload_confirm);
    /* the panel: page 2 has QUICKLOAD CONFIRM, every box on screen, none overlapping; RESET gives SS2's defaults */
    Ss2Touch s; ss2t_init(&s); s.save_settings = save_to_file; now = 1000000000ull; frame(&s, 0);
    touch_ui_open(&s.t); ui_tap(&s, TU_PAGE);
    static const TouchUiAction page2[] = { TU_PAGE, TU_CURSOR_STYLE, TU_CURSOR_SPEED_DOWN, TU_CURSOR_SPEED_UP, TU_HAPTICS, TU_HAPTIC_STRENGTH,
                                           TU_WHEEL_WEAPON, TU_WHEEL_CONSUMABLE, TU_WHEEL_EXPLOSIVE, TU_QUICKLOAD_CONFIRM, TU_RESET, TU_CANCEL, TU_SAVE };
    int n2 = (int)(sizeof page2 / sizeof page2[0]);
    for (int a = 0; a < n2; a++) { TouchRect ra = touch_ui_bounds(&s.t, page2[a]); CHECK(ra.w > 0 && ra.y >= 0 && ra.y + ra.h <= H);
        for (int b = a + 1; b < n2; b++) { TouchRect rb = touch_ui_bounds(&s.t, page2[b]);
            CHECK(ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x || ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y); } }
    CHECK(s.t.settings.quickload_confirm); ui_tap(&s, TU_QUICKLOAD_CONFIRM); CHECK(!s.t.settings.quickload_confirm);
    ui_tap(&s, TU_WHEEL_WEAPON); CHECK(!s.t.settings.wheel_enabled[0]);
    touch_ui_action(&s.t, TU_RESET); CHECK(s.t.settings.wheel_enabled[0] && s.t.settings.quickload_confirm);
    touch_ui_action(&s.t, TU_CANCEL);
    remove(path); remove(panel_path);
    touch_settings_game = NULL;
}

int main(void) {
    test_tap_min_hold(); test_move_look_fire(); test_ownership_and_hysteresis(); test_modifier_spans_frames();
    test_cursor_mapping_and_click(); test_focus_cleanup(); test_mode_follow_and_toggle(); test_weapon_wheel_and_actions();
    test_physical_aggregation(); test_ui_rect_mapping(); test_keyboard_button(); test_default_layout(); test_layout_upgrade();
    test_rel_no_jump_and_motion(); test_rel_speed(); test_rel_tap_and_double_tap(); test_rel_hold_drag(); test_rel_cancel();
    test_rel_fingers_and_use(); test_rel_modes(); test_rel_follows_game_cursor(); test_rel_menu_mapping(); test_cursor_settings();
    test_bind_parse(); test_crouch_key();
    test_radial_weapon_hold_select(); test_radial_short_tap_unchanged(); test_radial_cancel(); test_radial_multitouch();
    test_radial_cancel_by_lifecycle(); test_radial_cursor_mode_ammo(); test_radial_empty_and_unavailable(); test_radial_haptics();
    test_system_wheel_quickload(); test_item_button(); test_many_entries(); test_settings_3f();
    test_anchored_default_layout(); test_anchored_custom_layout(); test_anchored_while_moving(); test_anchored_off_is_centred();
    printf("touch_ss2_test: %d of %d checks failed\n", fails, checks);
    return fails != 0;
}
