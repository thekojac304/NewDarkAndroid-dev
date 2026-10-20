/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <math.h>
#include <string.h>

static void emit(TouchControls *t, TouchOutput output, float x, float y) {
    if (t->emit) t->emit(t->user, output, x, y);
}

static TouchMode effective(const TouchControls *t) {
    return t->panel==TOUCH_UI_CLOSED && t->gameplay_available && !t->controller ? t->mode : TOUCH_CURSOR;
}
static void haptic(TouchControls *t, int level) {
    if (t->settings.haptics_enabled && t->enabled && t->gameplay_available &&
        !t->controller && t->panel==TOUCH_UI_CLOSED)
        emit(t,TOUCH_HAPTIC,(float)level,0);
}
static int radial_category(TouchAction action) {
    if (action==TOUCH_WEAPON_NEXT) return TOUCH_RADIAL_WEAPON;
    if (action==TOUCH_CONSUMABLE) return TOUCH_RADIAL_CONSUMABLE;
    if (action==TOUCH_EXPLOSIVE) return TOUCH_RADIAL_EXPLOSIVE;
    if (action==TOUCH_AMMO) return TOUCH_RADIAL_AMMO;
    return -1;
}
static bool radial_enabled(const TouchControls *t, int category) {
    /* Ammo is available immediately; the three existing optional wheel
       preferences and their stored values retain their original meaning. */
    return category==TOUCH_RADIAL_AMMO ||
           (category>=0 && category<3 && t->settings.wheel_enabled[category]);
}
/* NewDarkAndroid: the wheel an action opens when held (-1: none), from the host's mapping when it gives one */
static int action_radial(const TouchControls *t, TouchAction action) {
    if (t->radial_for_action) {
        int category=t->radial_for_action(t->user,action);
        return category>=0 && category<TOUCH_RADIAL_CATEGORY_COUNT ? category : -1;
    }
    int category=radial_category(action);
    return radial_enabled(t,category) ? category : -1;
}

void touch_init(TouchControls *t, TouchEmit callback, void *user) {
    memset(t, 0, sizeof(*t));
    t->mode = TOUCH_GAMEPLAY;
    t->effective_mode = TOUCH_CURSOR;
    t->move_owner = t->look_owner = t->cursor_owner = -1;
    t->compare_pressed=t->compare_notice=-1;
    touch_radial_close(&t->radial);
    t->emit = callback;
    t->user = user;
    touch_settings_defaults(&t->settings);
    t->saved_settings=t->settings;
}

void touch_cancel(TouchControls *t) {
    if (t->panel!=TOUCH_UI_CLOSED) {
        t->settings=t->saved_settings;
        t->panel=TOUCH_UI_CLOSED;
        touch_layout_refresh(t);
    }
    t->ui_owned=t->ui_drag=false;
    t->effective_mode=effective(t);
    memset(t->fingers, 0, sizeof(t->fingers));
    t->move_owner = t->look_owner = t->cursor_owner = -1;
    touch_radial_close(&t->radial);
    t->movement_x = t->movement_y = t->look_x = t->look_y = 0;
    t->fire_count = t->use_count = 0;
    memset(t->action_count, 0, sizeof(t->action_count));
    t->sprint_latched = false;
    t->compare_pressed=-1;
    emit(t, TOUCH_CANCEL, 0, 0);
}

void touch_configure(TouchControls *t, float w, float h, bool enabled,
                     bool gameplay_available, bool controller) {
    enabled = enabled && w > 0 && h > 0;
    if (w == t->layout.width && h == t->layout.height && enabled == t->enabled &&
        gameplay_available == t->gameplay_available && controller == t->controller) return;
    touch_cancel(t);
    t->enabled = enabled;
    t->gameplay_available = gameplay_available;
    t->pause_available = gameplay_available;
    t->controller = controller;
    t->effective_mode = effective(t);
    t->layout.width=w; t->layout.height=h;
    touch_layout_refresh(t);
    t->cursor_slop = h * (8.0f / 768.0f);
    t->look_scale = 1.0f;
}

void touch_set_mode(TouchControls *t, TouchMode mode) {
    if (t->mode == mode) return;
    touch_cancel(t);
    t->mode = mode;
    t->effective_mode = effective(t);
    emit(t, TOUCH_MODE_CHANGED, 0, 0);
}

static bool in_circle(float x, float y, TouchCircle c) {
    /* NewDarkAndroid: a zero-radius control is hidden (System Shock 2 leaves some System Shock actions unmapped). */
    if (c.radius <= 0) return false;
    x -= c.x; y -= c.y;
    return x*x + y*y <= c.radius*c.radius;
}

static bool in_rect(float x, float y, TouchRect r) {
    return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
}

TouchRect touch_compare_bounds(const TouchControls *t, int button) {
    float w=t->layout.width,h=t->layout.height;
    if (button<0 || button>1 || w<=0 || h<=0) return (TouchRect){0};
    return (TouchRect){w*.018f,h*(button ? .365f : .265f),w*.125f,h*.08f};
}

int touch_compare_hit(const TouchControls *t, float x, float y) {
    if (!t->enabled || !t->compare_enabled || !t->gameplay_available ||
        t->controller || t->panel!=TOUCH_UI_CLOSED) return -1;
    for (int button=0;button<2;++button)
        if (in_rect(x,y,touch_compare_bounds(t,button))) return button;
    return -1;
}

static void move_finger(TouchControls *t, TouchFinger *f, float x, float y) {
    if (f->owns_look) {
        t->look_x += (x - f->x) * t->look_scale * t->settings.sensitivity_x;
        t->look_y += (y - f->y) * t->look_scale * t->settings.sensitivity_y *
                     (t->settings.invert_y ? -1.0f : 1.0f);
    }
    f->x = x; f->y = y;
    if (f->role == TOUCH_MOVE) {
        float dx = (x - t->anchor_x) / t->layout.move_radius;
        float dy = (y - t->anchor_y) / t->layout.move_radius;
        float magnitude = sqrtf(dx*dx + dy*dy);
        if (magnitude > 1) { dx /= magnitude; dy /= magnitude; }
        t->movement_x = dx; t->movement_y = dy;
    }
}

void touch_tick(TouchControls *t, uint32_t now_ms) {
    if (!t->enabled || !t->gameplay_available || t->controller ||
        t->panel!=TOUCH_UI_CLOSED || t->radial.open) return;
    for (int i=0;i<TOUCH_MAX_FINGERS;++i) {
        TouchFinger *f=&t->fingers[i];
        if (!f->active || !f->radial_pending || f->radial_cancelled ||
            (uint32_t)(now_ms-f->down_ms)<(t->radial_hold_ms ? t->radial_hold_ms : TOUCH_RADIAL_HOLD_MS)) continue;
        int category=t->radial_for_action ? action_radial(t,f->action) : radial_category(f->action);
        if (category<0) { f->radial_pending=false; f->radial_cancelled=true; continue; }
        if (t->effective_mode!=TOUCH_GAMEPLAY && category!=TOUCH_RADIAL_AMMO) continue;
        /* Refuse a wheel during a held attack/use; require a fresh gesture. */
        f->radial_pending=false;
        if (t->fire_count || t->use_count || t->cursor_owner>=0) {
            f->radial_cancelled=true; continue;
        }
        TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]={{0}};
        int count=t->radial_populate ? t->radial_populate(t->user,(TouchRadialCategory)category,entries) : 0;
        if (count<0 || count>TOUCH_RADIAL_MAX_ENTRIES ||
            ((category==TOUCH_RADIAL_AMMO || t->radial_for_action) && !count) ||
            !(t->radial_anchored                 /* NewDarkAndroid Phase 3G: around the held button */
              ? touch_radial_open_at(&t->radial,i,(TouchRadialCategory)category,t->layout.width,t->layout.height,
                                     t->layout.action[f->action].x,t->layout.action[f->action].y,f->x,f->y,entries,count)
              : touch_radial_open(&t->radial,i,(TouchRadialCategory)category,
                                  t->layout.width,t->layout.height,entries,count))) {
            f->radial_cancelled=true;
            continue;
        }
        f->radial_opened=true;
        /* Existing look ownership ends at open. No look accumulation or
           resumed look is allowed until that finger is lifted and pressed anew. */
        if (t->look_owner>=0) {
            t->fingers[t->look_owner].owns_look=false;
            t->fingers[t->look_owner].role=TOUCH_NONE;
            t->look_owner=-1; t->look_x=t->look_y=0;
        }
        for (int j=0;j<TOUCH_MAX_FINGERS;++j)
            if (j!=i && t->fingers[j].radial_pending) {
                t->fingers[j].radial_pending=false;
                t->fingers[j].radial_cancelled=true;
            }
        touch_radial_highlight(&t->radial,i,f->x,f->y);
        haptic(t,2);
        break;
    }
}

void touch_finger(TouchControls *t, TouchPhase phase, int64_t device, int64_t id,
                  float x, float y, uint32_t timestamp_ms) {
    if (phase == TOUCH_ABORT) { touch_cancel(t); return; }
    /* Raw surface pixels, including bars. Ownership precedes any UI mapping. */
    if (!t->enabled || !isfinite(x) || !isfinite(y)) return;
    if (t->panel!=TOUCH_UI_CLOSED) {
        touch_ui_finger(t,phase,device,id,x,y);
        return;
    }
    if (phase==TOUCH_DOWN && !t->controller && (t->gameplay_available || t->pause_available) &&
        touch_control_hit(t,TC_SETTINGS,x,y)) {
        touch_ui_open(t);
        return;
    }
    int index = -1, free_slot = -1;
    for (int i = 0; i < TOUCH_MAX_FINGERS; ++i) {
        TouchFinger *f = &t->fingers[i];
        if (f->active && f->touch == device && f->finger == id) index = i;
        if (!f->active && free_slot < 0) free_slot = i;
    }
    if (phase == TOUCH_DOWN) {
        if (index >= 0 || free_slot < 0) return;
        index = free_slot;
        TouchRole role = TOUCH_NONE;
        int compare=touch_compare_hit(t,x,y);
        if (compare>=0) {
            role=compare ? TOUCH_COMPARE_POST : TOUCH_COMPARE_MODE;
            t->compare_pressed=compare;
        } else if (!t->controller && t->pause_available &&
            in_circle(x, y, t->layout.action[TOUCH_PAUSE])) {
            role = TOUCH_ACTION;
        } else if (!t->controller && t->gameplay_available && in_rect(x, y, t->layout.toggle)) {
            haptic(t,2);
            touch_set_mode(t, t->mode == TOUCH_GAMEPLAY ? TOUCH_CURSOR : TOUCH_GAMEPLAY);
            role = TOUCH_MODE_TOGGLE;
        } else if (!t->controller && t->gameplay_available &&
                   (in_circle(x,y,t->layout.action[TOUCH_AMMO]) ||
                    in_circle(x,y,t->layout.action[TOUCH_TARGET_ID]))) {   /* NewDarkAndroid: KBD in both modes */
            role = TOUCH_ACTION;
        } else if (t->effective_mode == TOUCH_CURSOR) {
            if (!t->radial.open && t->cursor_owner < 0) role = TOUCH_CURSOR_OWNER;
        } else {
            for (int action = 0; action < TOUCH_PAUSE; ++action)
                if (in_circle(x, y, t->layout.action[action])) {
                    role = TOUCH_ACTION; break;
                }
        }
        if (role == TOUCH_NONE && !t->radial.open && t->effective_mode == TOUCH_GAMEPLAY &&
            in_circle(x, y, t->layout.fire)) role = TOUCH_FIRE;
        else if (role == TOUCH_NONE && !t->radial.open && t->effective_mode == TOUCH_GAMEPLAY &&
                 in_circle(x, y, t->layout.use)) role = TOUCH_USE;
        else if (role == TOUCH_NONE && t->effective_mode == TOUCH_GAMEPLAY &&
                 x < t->layout.move_split && t->move_owner < 0) role = TOUCH_MOVE;
        else if (role == TOUCH_NONE && !t->radial.open && t->effective_mode == TOUCH_GAMEPLAY &&
                 x >= t->layout.move_split && t->look_owner < 0) role = TOUCH_LOOK;

        TouchFinger *f = &t->fingers[index];
        *f = (TouchFinger){.touch=device, .finger=id, .active=true, .role=role,
            .x=x, .y=y, .start_x=x, .start_y=y, .down_ms=timestamp_ms};
        if (role == TOUCH_ACTION) {
            for (int action = 0; action < TOUCH_ACTION_COUNT; ++action)
                if (in_circle(x, y, t->layout.action[action])) {
                    f->action = (TouchAction)action; break;
                }
            if (action_radial(t,f->action)>=0) {
                /* A pending wheel owns this tap until release or hold. */
                if (t->radial.open) f->radial_cancelled=true;
                else f->radial_pending=true;
            } else {
                if (t->action_count[f->action]++ == 0 && f->action != TOUCH_SPRINT) {
                    emit(t, TOUCH_ACTION_DOWN, (float)f->action, 0);
                    haptic(t,1);
                }
            }
        }
        if (role == TOUCH_MOVE) {
            t->move_owner = index; t->anchor_x = x; t->anchor_y = y;
        }
        if ((role == TOUCH_LOOK || role == TOUCH_FIRE) && t->look_owner < 0) {
            t->look_owner = index; f->owns_look = true;
        }
        if (role == TOUCH_FIRE && t->fire_count++ == 0) { emit(t, TOUCH_FIRE_DOWN, x, y); haptic(t,1); }
        if (role == TOUCH_USE && t->use_count++ == 0) { emit(t, TOUCH_USE_PRESS, x, y); haptic(t,1); }
        if (role == TOUCH_CURSOR_OWNER) {
            t->cursor_owner = index;
            emit(t, TOUCH_POINTER_MOVE, x, y); /* position, never press yet */
        }
        return;
    }
    /* Unknown motion/up, including cancelled fingers, can never claim a role. */
    if (index < 0) return;
    TouchFinger *f = &t->fingers[index];
    move_finger(t, f, x, y);
    if (f->radial_pending) {
        float dx=x-f->start_x,dy=y-f->start_y;
        float slop=fmaxf(t->cursor_slop*2,t->layout.action[f->action].radius*.45f);
        if (dx*dx+dy*dy>slop*slop) {
            f->radial_pending=false;
            f->radial_cancelled=true;
        }
    }
    touch_tick(t,timestamp_ms);
    if (f->radial_opened && t->radial.open)
        touch_radial_highlight(&t->radial,index,x,y);
    if (f->role == TOUCH_CURSOR_OWNER) {
        float dx = x - f->start_x, dy = y - f->start_y;
        if (!f->dragging && dx*dx + dy*dy > t->cursor_slop*t->cursor_slop) {
            f->dragging = true;
            /* Start the original UI drag at the touched object, not at the
               first point outside slop (which could already be another object). */
            emit(t, TOUCH_POINTER_DOWN, f->start_x, f->start_y);
        }
        if (f->dragging) {
            emit(t, TOUCH_POINTER_MOVE, x, y);
            if (phase == TOUCH_UP) emit(t, TOUCH_POINTER_UP, x, y);
        } else if (phase == TOUCH_UP && timestamp_ms - f->down_ms <=
                   (t->gameplay_available ? TOUCH_TAP_MS : TOUCH_MENU_TAP_MS)) {
            /* Anchor taps to their initial position so small jitter cannot
               move a click onto the neighbouring small original UI target. */
            emit(t, TOUCH_POINTER_TAP, f->start_x, f->start_y);
        }
    }
    if (phase != TOUCH_UP) return;
    if (f->role==TOUCH_COMPARE_MODE || f->role==TOUCH_COMPARE_POST) {
        int button=f->role==TOUCH_COMPARE_POST;
        t->compare_pressed=-1;
        float dx=x-f->start_x,dy=y-f->start_y;
        if (timestamp_ms-f->down_ms<=TOUCH_TAP_MS &&
            dx*dx+dy*dy<=t->cursor_slop*t->cursor_slop*4 &&
            touch_compare_hit(t,x,y)==button)
            emit(t,TOUCH_VISUAL_COMPARE,(float)button,0);
        f->active=false;
        return;
    }
    if (f->role == TOUCH_MOVE) {
        t->move_owner = -1; t->movement_x = t->movement_y = 0;
    }
    if (f->owns_look) t->look_owner = -1;
    if (f->role == TOUCH_FIRE && --t->fire_count == 0) emit(t, TOUCH_FIRE_UP, x, y);
    if (f->role == TOUCH_USE) --t->use_count;
    if (f->radial_opened) {
        int category=t->radial.category;
        int selected=touch_radial_release(&t->radial,index,x,y);
        if (selected>=0) {
            emit(t,TOUCH_RADIAL_SELECT,(float)category,(float)selected);
            haptic(t,2);
        }
    } else if (f->radial_pending && !f->radial_cancelled) {
        /* Only the enabled host action is deferred. Disabled hosts still
           dispatch on down, as they did before Alpha 4. */
        emit(t,TOUCH_ACTION_DOWN,(float)f->action,0);
        emit(t,TOUCH_ACTION_UP,(float)f->action,0);
        haptic(t,1);
    } else if (f->role == TOUCH_ACTION && !f->radial_cancelled &&
               --t->action_count[f->action] == 0) {
        if (f->action == TOUCH_SPRINT) {
            float dx = x - f->start_x, dy = y - f->start_y;
            if (timestamp_ms - f->down_ms <= TOUCH_TAP_MS &&
                dx*dx + dy*dy <= t->cursor_slop*t->cursor_slop &&
                in_circle(x, y, t->layout.action[TOUCH_SPRINT])) {
                t->sprint_latched = !t->sprint_latched;
                emit(t, t->sprint_latched ? TOUCH_ACTION_DOWN : TOUCH_ACTION_UP,
                     (float)TOUCH_SPRINT, 0);
                haptic(t,2);
            }
        } else emit(t, TOUCH_ACTION_UP, (float)f->action, 0);
    }
    if (f->role == TOUCH_CURSOR_OWNER) t->cursor_owner = -1;
    f->active = false;
}

void touch_map_cursor(float x, float y, TouchRect viewport, float sx, float sy,
                      int width, int height, float *gx, float *gy) {
    *gx = fmaxf(0, fminf((x - viewport.x) / sx, (float)(width - 1)));
    *gy = fmaxf(0, fminf((y - viewport.y) / sy, (float)(height - 1)));
}

void touch_consume_look(TouchControls *t, int *dx, int *dy) {
    *dx = (int)t->look_x; *dy = (int)t->look_y;
    t->look_x -= *dx; t->look_y -= *dy;
}

void touch_merge_axes(const TouchControls *t, int walk_strength, int *x, int *y) {
    if (!t->enabled || t->effective_mode != TOUCH_GAMEPLAY) return;
    if (!*x) *x = (int)lroundf(t->movement_x * walk_strength);
    if (!*y) *y = (int)lroundf(-t->movement_y * walk_strength);
}

void touch_movie_tap_reset(TouchMovieTap *tap) {
    memset(tap, 0, sizeof(*tap));
}

bool touch_movie_tap_event(TouchMovieTap *tap, TouchPhase phase,
                           int64_t device, int64_t finger, float x, float y,
                           uint32_t timestamp_ms, float slop) {
    if (phase == TOUCH_ABORT) { touch_movie_tap_reset(tap); return false; }
    if (!isfinite(x) || !isfinite(y) || !isfinite(slop) || slop < 0) return false;
    int index = -1, free_slot = -1;
    for (int i = 0; i < TOUCH_MAX_FINGERS; ++i) {
        TouchFinger *f = &tap->fingers[i];
        if (f->active && f->touch == device && f->finger == finger) index = i;
        if (!f->active && free_slot < 0) free_slot = i;
    }
    if (phase == TOUCH_DOWN) {
        if (index >= 0) return false;
        if (free_slot < 0) { tap->blocked = tap->overflow = true; return false; }
        if (tap->active_count == 0) tap->blocked = tap->overflow;
        TouchFinger *f = &tap->fingers[free_slot];
        *f = (TouchFinger){.touch=device, .finger=finger, .active=true,
                           .start_x=x, .start_y=y, .down_ms=timestamp_ms};
        if (++tap->active_count > 1) tap->blocked = true;
        return false;
    }
    if (index < 0) return false;
    TouchFinger *f = &tap->fingers[index];
    float dx = x - f->start_x, dy = y - f->start_y;
    if (dx*dx + dy*dy > slop*slop) tap->blocked = true;
    if (phase != TOUCH_UP) return false;
    bool skip = tap->active_count == 1 && !tap->blocked &&
                timestamp_ms - f->down_ms <= TOUCH_TAP_MS;
    f->active = false;
    --tap->active_count;
    if (tap->active_count == 0) tap->blocked = tap->overflow;
    return skip;
}

uint8_t touch_mouse_buttons(const TouchMouseButtons *b) {
    return b->source[0] | b->source[1] | b->source[2];
}

bool touch_mouse_button(TouchMouseButtons *b, TouchMouseSource source, uint8_t bit, bool down) {
    uint8_t before = touch_mouse_buttons(b);
    if (down) b->source[source] |= bit;
    else b->source[source] &= (uint8_t)~bit;
    return ((before ^ touch_mouse_buttons(b)) & bit) != 0;
}

bool touch_key_held(const TouchKeySources *k) {
    return k->source[0] || k->source[1] || k->source[2];
}

bool touch_key_source(TouchKeySources *k, TouchKeySource source, bool down) {
    bool before = touch_key_held(k);
    k->source[source] = down;
    return before != touch_key_held(k);
}
