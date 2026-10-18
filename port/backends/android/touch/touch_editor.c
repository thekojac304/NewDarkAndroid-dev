/* Shared layout, hit testing and touch-only settings UI. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <math.h>

static float clamp(float x, float low, float high) { return fmaxf(low,fminf(high,x)); }
static bool inside(TouchRect r, float x, float y) {
    return r.w>0 && r.h>0 && x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h;
}
static TouchCircle control_circle(const TouchControls *t, int id) {
    if (id==TC_FIRE) return t->layout.fire;
    if (id==TC_USE) return t->layout.use;
    if (id==TC_SETTINGS) return t->layout.settings;
    if (id==TC_STICK) return t->layout.stick;
    if (id==TC_AMMO) return t->layout.action[TOUCH_AMMO];
    if (id>=TC_JUMP && id<=TC_PAUSE) return t->layout.action[id-TC_JUMP];
    return (TouchCircle){0};
}
TouchRect touch_control_bounds(const TouchControls *t, int id) {
    if (id==TC_MODE) return t->layout.toggle;
    TouchCircle c=control_circle(t,id);
    return (TouchRect){c.x-c.radius,c.y-c.radius,2*c.radius,2*c.radius};
}
bool touch_control_hit(const TouchControls *t, int id, float x, float y) {
    if (id==TC_MODE) return inside(t->layout.toggle,x,y);
    TouchCircle c=control_circle(t,id);
    float dx=x-c.x, dy=y-c.y;
    return c.radius>0 && dx*dx+dy*dy<=c.radius*c.radius;
}
void touch_layout_refresh(TouchControls *t) {
    float w=t->layout.width, h=t->layout.height;
    if (w<=0 || h<=0) return;
    touch_settings_validate(&t->settings);
    for (int id=0; id<TC_COUNT; ++id) {
        const TouchPlacement *p=&t->settings.control[id];
        float halfw, halfh;
        if (id==TC_MODE) {
            halfw=fminf(w*.09f*p->scale,w/2);
            halfh=fminf(h*.0375f*p->scale,h/2);
        } else {
            float reference=id==TC_STICK && t->stick_reference_height>0 ? t->stick_reference_height : h;
            halfw=halfh=fminf(reference*touch_defaults[id].radius*p->scale,fminf(w,h)*.49f);
        }
        float x=clamp(p->x*w,halfw,w-halfw), y=clamp(p->y*h,halfh,h-halfh);
        TouchCircle c={x,y,halfw};
        if (id==TC_MODE) t->layout.toggle=(TouchRect){x-halfw,y-halfh,2*halfw,2*halfh};
        else if (id==TC_FIRE) t->layout.fire=c;
        else if (id==TC_USE) t->layout.use=c;
        else if (id==TC_SETTINGS) t->layout.settings=c;
        else if (id==TC_STICK) { t->layout.stick=c; t->layout.move_radius=c.radius; }
        else if (id==TC_AMMO) t->layout.action[TOUCH_AMMO]=c;
        else t->layout.action[id-TC_JUMP]=c;
    }
    t->layout.move_split=w*.45f;
}
void touch_editor_position(TouchControls *t, int id, float x, float y) {
    if (id<0 || id>=TC_COUNT || !isfinite(x) || !isfinite(y)) return;
    TouchRect r=touch_control_bounds(t,id);
    float w=t->layout.width, h=t->layout.height;
    if (w<=0 || h<=0) return;
    t->settings.control[id].x=clamp(x,r.w/2,w-r.w/2)/w;
    t->settings.control[id].y=clamp(y,r.h/2,h-r.h/2)/h;
    touch_layout_refresh(t);
}
static void ui_emit(TouchControls *t, TouchOutput e) {
    if (t->emit) t->emit(t->user,e,0,0);
}
void touch_ui_open(TouchControls *t) {
    if (!t->enabled || t->controller || (!t->gameplay_available && !t->pause_available)) return;
    touch_cancel(t);
    t->saved_settings=t->settings;
    t->panel=TOUCH_UI_SETTINGS;
    t->effective_mode=TOUCH_CURSOR;
    t->selected=TC_FIRE;
    t->settings_page=0;
    t->save_failed=false;
    ui_emit(t,TOUCH_UI_CHANGED);
}
void touch_ui_action(TouchControls *t, TouchUiAction action) {
    if (t->panel==TOUCH_UI_CLOSED) return;
    if (action>=TU_QA) {
        if (!t->qa_enabled || !t->gameplay_available) return;
        if (action==TU_QA && t->panel==TOUCH_UI_SETTINGS) {
            t->panel=TOUCH_UI_QA;
            t->ui_owned=false;
            t->qa_status="SELECT A TOOL OR DESTINATION";
            ui_emit(t,TOUCH_UI_CHANGED);
        } else if (t->panel==TOUCH_UI_QA) {
            if (action==TU_QA_CLOSE) {
                touch_cancel(t);
                ui_emit(t,TOUCH_UI_CHANGED);
            } else if (action==TU_QA_GOD || action==TU_QA_HEAL || action==TU_QA_LOADOUT ||
                       (action>=TU_QA_DECK_FIRST && action<=TU_QA_DECK_LAST)) {
                if (t->emit) t->emit(t->user,TOUCH_QA_COMMAND,(float)action,0);
            }
        }
        return;
    }
    t->save_failed=false;
    switch (action) {
    case TU_EDIT: t->panel=TOUCH_UI_EDITOR; break;
    case TU_BACK: t->panel=TOUCH_UI_SETTINGS; break;
    case TU_OPACITY_DOWN: t->settings.opacity-=.05f; break;
    case TU_OPACITY_UP: t->settings.opacity+=.05f; break;
    case TU_X_DOWN: t->settings.sensitivity_x-=.1f; break;
    case TU_X_UP: t->settings.sensitivity_x+=.1f; break;
    case TU_Y_DOWN: t->settings.sensitivity_y-=.1f; break;
    case TU_Y_UP: t->settings.sensitivity_y+=.1f; break;
    case TU_INVERT: t->settings.invert_y=!t->settings.invert_y; break;
    case TU_STICK_DOWN: t->settings.control[TC_STICK].scale-=.1f; break;
    case TU_STICK_UP: t->settings.control[TC_STICK].scale+=.1f; break;
    case TU_PREV: t->selected=(t->selected+TC_COUNT-1)%TC_COUNT; break;
    case TU_NEXT: t->selected=(t->selected+1)%TC_COUNT; break;
    case TU_SCALE_DOWN: t->settings.control[t->selected].scale-=.1f; break;
    case TU_SCALE_UP: t->settings.control[t->selected].scale+=.1f; break;
    case TU_DOCK: t->toolbar_top=!t->toolbar_top; break;
    case TU_PAGE: t->settings_page=1-t->settings_page; break;
    case TU_HAPTICS: t->settings.haptics_enabled=!t->settings.haptics_enabled; break;
    case TU_HAPTIC_STRENGTH: t->settings.haptic_strength=t->settings.haptic_strength%3+1; break;
    case TU_WHEEL_WEAPON: t->settings.wheel_enabled[0]=!t->settings.wheel_enabled[0]; break;
    case TU_WHEEL_CONSUMABLE: t->settings.wheel_enabled[1]=!t->settings.wheel_enabled[1]; break;
    case TU_WHEEL_EXPLOSIVE: t->settings.wheel_enabled[2]=!t->settings.wheel_enabled[2]; break;
    case TU_RESET: touch_settings_defaults(&t->settings); break;
    case TU_SAVE:
        touch_settings_validate(&t->settings);
        ui_emit(t,TOUCH_SETTINGS_SAVE);
        if (t->save_failed) break;
        t->saved_settings=t->settings;
        /* fall through */
    case TU_CANCEL:
        touch_cancel(t);
        ui_emit(t,TOUCH_UI_CHANGED);
        break;
    default: break;
    }
    touch_settings_validate(&t->settings);
    touch_layout_refresh(t);
}
TouchRect touch_ui_bounds(const TouchControls *t, TouchUiAction action) {
    float w=t->layout.width,h=t->layout.height;
    TouchRect r={0};
    if (t->panel==TOUCH_UI_SETTINGS) {
        float y=0;
        if (t->settings_page==1) {
            switch (action) {
            case TU_PAGE: r=(TouchRect){.67f,.055f,.12f,.065f}; break;
            case TU_HAPTICS: r=(TouchRect){.22f,.18f,.56f,.085f}; break;
            case TU_HAPTIC_STRENGTH: r=(TouchRect){.22f,.285f,.56f,.085f}; break;
            case TU_WHEEL_WEAPON: r=(TouchRect){.22f,.40f,.56f,.085f}; break;
            case TU_WHEEL_CONSUMABLE: r=(TouchRect){.22f,.51f,.56f,.085f}; break;
            case TU_WHEEL_EXPLOSIVE: r=(TouchRect){.22f,.62f,.56f,.085f}; break;
            case TU_RESET: r=(TouchRect){.22f,.77f,.56f,.065f}; break;
            case TU_CANCEL: r=(TouchRect){.22f,.87f,.26f,.075f}; break;
            case TU_SAVE: r=(TouchRect){.52f,.87f,.26f,.075f}; break;
            default: break;
            }
        } else switch (action) {
        case TU_EDIT: r=(TouchRect){.22f,.16f,t->qa_enabled && t->gameplay_available ? .34f : .56f,.075f}; break;
        case TU_QA:
            if (t->qa_enabled && t->gameplay_available) r=(TouchRect){.58f,.16f,.20f,.075f};
            break;
        case TU_PAGE: r=(TouchRect){.67f,.055f,.12f,.065f}; break;
        case TU_OPACITY_DOWN: case TU_OPACITY_UP: y=.27f; break;
        case TU_X_DOWN: case TU_X_UP: y=.37f; break;
        case TU_Y_DOWN: case TU_Y_UP: y=.47f; break;
        case TU_INVERT: r=(TouchRect){.22f,.57f,.56f,.075f}; break;
        case TU_STICK_DOWN: case TU_STICK_UP: y=.67f; break;
        case TU_RESET: r=(TouchRect){.22f,.77f,.56f,.065f}; break;
        case TU_CANCEL: r=(TouchRect){.22f,.87f,.26f,.075f}; break;
        case TU_SAVE: r=(TouchRect){.52f,.87f,.26f,.075f}; break;
        default: break;
        }
        if (y) {
            bool plus=action==TU_OPACITY_UP || action==TU_X_UP || action==TU_Y_UP || action==TU_STICK_UP;
            r=(TouchRect){plus ? .68f : .22f,y,.10f,.075f};
        }
    } else if (t->panel==TOUCH_UI_QA && t->qa_enabled) {
        if (action==TU_QA_GOD) r=(TouchRect){.12f,.16f,.24f,.09f};
        else if (action==TU_QA_HEAL) r=(TouchRect){.38f,.16f,.24f,.09f};
        else if (action==TU_QA_LOADOUT) r=(TouchRect){.64f,.16f,.24f,.09f};
        else if (action==TU_QA_CLOSE) r=(TouchRect){.32f,.87f,.36f,.09f};
        else if (action>=TU_QA_DECK_FIRST && action<=TU_QA_DECK_LAST) {
            int i=action-TU_QA_DECK_FIRST;
            r=(TouchRect){.12f+(i%3)*.26f,.30f+(i/3)*.105f,.24f,.09f};
        }
    } else if (t->panel==TOUCH_UI_EDITOR) {
        /* The default center strip leaves all Alpha 2.2 controls visible.
           Moving it to the top lets controls be placed beneath that strip. */
        float y=t->toolbar_top ? .012f : .292f;
        switch (action) {
        case TU_PREV: r=(TouchRect){.01f,y,.06f,.065f}; break;
        case TU_NEXT: r=(TouchRect){.28f,y,.06f,.065f}; break;
        case TU_SCALE_DOWN: r=(TouchRect){.35f,y,.08f,.065f}; break;
        case TU_SCALE_UP: r=(TouchRect){.44f,y,.08f,.065f}; break;
        case TU_OPACITY_DOWN: r=(TouchRect){.53f,y,.08f,.065f}; break;
        case TU_OPACITY_UP: r=(TouchRect){.62f,y,.08f,.065f}; break;
        case TU_DOCK: r=(TouchRect){.72f,y,.26f,.065f}; break;
        case TU_BACK: r=(TouchRect){.01f,y+.085f,.22f,.065f}; break;
        case TU_RESET: r=(TouchRect){.25f,y+.085f,.22f,.065f}; break;
        case TU_CANCEL: r=(TouchRect){.49f,y+.085f,.22f,.065f}; break;
        case TU_SAVE: r=(TouchRect){.73f,y+.085f,.25f,.065f}; break;
        default: break;
        }
    }
    r.x*=w; r.w*=w; r.y*=h; r.h*=h;
    return r;
}
void touch_ui_finger(TouchControls *t, TouchPhase phase, int64_t device, int64_t finger,
                     float x, float y) {
    if (phase==TOUCH_DOWN) {
        if (t->ui_owned) return;
        t->ui_owned=true; t->ui_device=device; t->ui_finger=finger;
        t->ui_start_x=x; t->ui_start_y=y; t->ui_action=-1; t->ui_drag=false;
        for (int a=0; a<TU_COUNT; ++a) if (inside(touch_ui_bounds(t,(TouchUiAction)a),x,y)) {
            t->ui_action=a; return;
        }
        if (t->panel==TOUCH_UI_EDITOR) {
            float closest=1e30f;
            for (int id=0; id<TC_COUNT; ++id) if (touch_control_hit(t,id,x,y)) {
                TouchRect r=touch_control_bounds(t,id);
                float dx=x-r.x-r.w/2,dy=y-r.y-r.h/2,dist=dx*dx+dy*dy;
                if (dist<closest) {
                    closest=dist; t->selected=id; t->ui_drag=true;
                    t->ui_offset_x=dx; t->ui_offset_y=dy;
                }
            }
        }
        return;
    }
    if (!t->ui_owned || device!=t->ui_device || finger!=t->ui_finger) return;
    if (t->ui_drag) touch_editor_position(t,t->selected,x-t->ui_offset_x,y-t->ui_offset_y);
    float dx=x-t->ui_start_x,dy=y-t->ui_start_y;
    if (dx*dx+dy*dy>t->cursor_slop*t->cursor_slop) t->ui_action=-1;
    if (phase!=TOUCH_UP) return;
    int action=t->ui_action;
    t->ui_owned=t->ui_drag=false;
    if (action>=0 && inside(touch_ui_bounds(t,(TouchUiAction)action),x,y))
        touch_ui_action(t,(TouchUiAction)action);
}
