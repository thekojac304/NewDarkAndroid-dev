/* touch_ss2_test.c: host tests of the System Shock 2 touch bridge (touch_ss2.c) over the imported Shockolate core.
 * Build and run: port/backends/android/touch/tests/run.sh (any C11 compiler; no SDL, no Android).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_ss2.h"
#include <math.h>
#include <stdio.h>
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

int main(void) {
    test_tap_min_hold(); test_move_look_fire(); test_ownership_and_hysteresis(); test_modifier_spans_frames();
    test_cursor_mapping_and_click(); test_focus_cleanup(); test_mode_follow_and_toggle(); test_weapon_wheel_and_actions();
    test_physical_aggregation();
    printf("touch_ss2_test: %d of %d checks failed\n", fails, checks);
    return fails != 0;
}
