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

int main(void) {
    test_tap_min_hold(); test_move_look_fire(); test_ownership_and_hysteresis(); test_modifier_spans_frames();
    test_cursor_mapping_and_click(); test_focus_cleanup(); test_mode_follow_and_toggle(); test_weapon_wheel_and_actions();
    test_physical_aggregation(); test_ui_rect_mapping(); test_keyboard_button(); test_default_layout(); test_layout_upgrade();
    test_rel_no_jump_and_motion(); test_rel_speed(); test_rel_tap_and_double_tap(); test_rel_hold_drag(); test_rel_cancel();
    test_rel_fingers_and_use(); test_rel_modes(); test_rel_follows_game_cursor(); test_rel_menu_mapping(); test_cursor_settings();
    test_bind_parse(); test_crouch_key();
    printf("touch_ss2_test: %d of %d checks failed\n", fails, checks);
    return fails != 0;
}
