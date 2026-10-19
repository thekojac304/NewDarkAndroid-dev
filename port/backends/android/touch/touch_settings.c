/* SPDX-License-Identifier: GPL-3.0-or-later */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#endif
#include "touch_settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NewDarkAndroid: System Shock 2 labels; the keys (the settings file format) are kept.
   CONS is RELOAD, AMMO cycles ammunition (or right-clicks in cursor mode), "target" is KBD (the Android keyboard, for
   save names and the console), and the actions System Shock 2 has no equivalent for (explosive, sprint) have radius 0,
   which hides them. Phase 2B-2: SET, AMMO and KBD sit in the letterbox bars and MODE right of the game's EQUIP panel,
   off the 640x480 interface at 16:9 (Phase 2B-1 found SET over the PDA, AMMO/USE beside its arrow, MODE over EQUIP).
   Phase 3F: "explosive" becomes ITEM (hold: the hypo/patch wheel), in the left bar below AMMO; sprint stays hidden. */
#ifdef TOUCH_SHOCKOLATE_LAYOUT
/* Shockolate's own table (7502370), only for building its accepted suites against this core
   (docs/android-audit/phase3f/run_shockolate_suites.sh): they test that layout, SPRINT and all. */
const TouchDefault touch_defaults[TC_COUNT] = {
    {"fire", "FIRE", .86f,.73f,.095f}, {"use", "USE", .69f,.85f,.065f},
    {"jump", "JUMP", .95f,.52f,.05f}, {"crouch", "CROUCH", .79f,.54f,.05f},
    {"previous", "<", .55f,.78f,.045f}, {"next", ">", .55f,.90f,.045f},
    {"consumable", "CONS", .52f,.10f,.045f}, {"explosive", "BOOM", .63f,.10f,.045f},
    {"sprint", "RUN", .22f,.54f,.05f}, {"target", "ID", .73f,.10f,.045f},
    {"pause", "II", .05f,.06f,.045f}, {"mode", "MODE", .88f,.0825f,0},
    {"settings", "SET", .16f,.06f,.035f}, {"stick", "MOVE", .20f,.77f,.11f},
    {"ammo", "AMMO", .43f,.10f,.045f}
};
#else
const TouchDefault touch_defaults[TC_COUNT] = {
    {"fire", "FIRE", .86f,.73f,.095f}, {"use", "USE", .69f,.85f,.065f},
    {"jump", "JUMP", .95f,.52f,.05f}, {"crouch", "CROUCH", .79f,.54f,.05f},
    {"previous", "<", .55f,.78f,.045f}, {"next", ">", .55f,.90f,.045f},
    {"consumable", "RELOAD", .52f,.10f,.045f}, {"explosive", "ITEM", .045f,.47f,.045f},
    {"sprint", "", .22f,.54f,0}, {"target", "KBD", .955f,.25f,.04f},
    {"pause", "II", .05f,.06f,.045f}, {"mode", "MODE", .905f,.0825f,0},
    {"settings", "SET", .045f,.19f,.035f}, {"stick", "MOVE", .20f,.77f,.11f},
    {"ammo", "AMMO", .045f,.33f,.045f}
};
#endif
/* The Phase 2A/2B-1 default positions. A stored layout that still has every control exactly there was never moved. */
static const float previous_defaults[TC_COUNT][2] = {
    {.86f,.73f}, {.69f,.85f}, {.95f,.52f}, {.79f,.54f}, {.55f,.78f}, {.55f,.90f}, {.52f,.10f}, {.63f,.10f},
    {.22f,.54f}, {.73f,.10f}, {.05f,.06f}, {.88f,.0825f}, {.16f,.06f}, {.20f,.77f}, {.09f,.30f}
};
bool touch_settings_upgrade_layout(TouchSettings *s) {
    for (int i=0; i<TC_COUNT; ++i) {
        const TouchPlacement *p=&s->control[i];
        if (fabsf(p->x-previous_defaults[i][0])>1e-5f || fabsf(p->y-previous_defaults[i][1])>1e-5f ||
            (i!=TC_STICK && fabsf(p->scale-1)>1e-5f)) return false;    /* moved or resized: the player's layout */
    }
    for (int i=0; i<TC_COUNT; ++i) { s->control[i].x=touch_defaults[i].x; s->control[i].y=touch_defaults[i].y; }
    return true;
}
static float bounded(float v, float low, float high, float fallback) {
    return !isfinite(v) ? fallback : fmaxf(low, fminf(high, v));
}
const TouchSettingsGame *touch_settings_game;
void touch_settings_defaults(TouchSettings *s) {
    memset(s, 0, sizeof(*s));
    s->version=TOUCH_SETTINGS_VERSION;
    s->opacity=1; s->sensitivity_x=s->sensitivity_y=1;
    /* First introduction defaults off: no unexpected vibration or hold delay. */
    s->haptics_enabled=false; s->haptic_strength=1;
    s->cursor_relative=false; s->cursor_speed=1;
    s->quickload_confirm=true;
    for (int i=0; i<TC_COUNT; ++i)
        s->control[i]=(TouchPlacement){touch_defaults[i].x,touch_defaults[i].y,1};
    if (touch_settings_game && touch_settings_game->defaults) touch_settings_game->defaults(s);
}
void touch_settings_validate(TouchSettings *s) {
    s->version=TOUCH_SETTINGS_VERSION;
    s->opacity=bounded(s->opacity,TOUCH_OPACITY_MIN,1,1);
    s->sensitivity_x=bounded(s->sensitivity_x,TOUCH_SENS_MIN,TOUCH_SENS_MAX,1);
    s->sensitivity_y=bounded(s->sensitivity_y,TOUCH_SENS_MIN,TOUCH_SENS_MAX,1);
    if (s->haptic_strength<1 || s->haptic_strength>3) s->haptic_strength=1;
    s->cursor_speed=bounded(s->cursor_speed,TOUCH_CURSOR_SPEED_MIN,TOUCH_CURSOR_SPEED_MAX,1);
    for (int i=0; i<TC_COUNT; ++i) {
        TouchPlacement *p=&s->control[i];
        p->x=bounded(p->x,0,1,touch_defaults[i].x);
        p->y=bounded(p->y,0,1,touch_defaults[i].y);
        p->scale=bounded(p->scale,TOUCH_SCALE_MIN,TOUCH_SCALE_MAX,1);
    }
}
TouchLoadResult touch_settings_load(TouchSettings *s, const char *path) {
    touch_settings_defaults(s);
    FILE *f=path ? fopen(path,"rb") : NULL;
    if (!f) return TOUCH_LOAD_DEFAULTS;
    char line[256], key[80], extra;
    float value;
    unsigned version=0;
    size_t bytes=0;
    bool bad=false;
    bool wheel[3]={s->wheel_enabled[0],s->wheel_enabled[1],s->wheel_enabled[2]};   /* NewDarkAndroid: see game_radials */
    TouchPlacement item=touch_settings_game ? s->control[touch_settings_game->item_control] : (TouchPlacement){0};
    while (fgets(line,sizeof(line),f)) {
        bytes+=strlen(line);
        if (bytes>16384 || (!strchr(line,'\n') && !feof(f))) { bad=true; break; }
        if (sscanf(line,"%79[^=]=%f %c",key,&value,&extra)!=2) continue;
        if (!strcmp(key,"version")) {
            if (isfinite(value) && value>=1 && value<=1000 && value==floorf(value))
                version=(unsigned)value;
            else bad=true;
        } else if (!strcmp(key,"opacity")) s->opacity=value;
        else if (!strcmp(key,"sensitivity_x")) s->sensitivity_x=value;
        else if (!strcmp(key,"sensitivity_y")) s->sensitivity_y=value;
        else if (!strcmp(key,"invert_y")) s->invert_y=value==1;
        else if (!strcmp(key,"haptics_enabled")) s->haptics_enabled=value==1;
        else if (!strcmp(key,"haptic_strength"))
            s->haptic_strength=isfinite(value) && value>=1 && value<=3 && value==floorf(value) ? (unsigned)value : 1;
        else if (!strcmp(key,"weapon_wheel")) wheel[0]=value==1;
        else if (!strcmp(key,"consumable_wheel")) wheel[1]=value==1;
        else if (!strcmp(key,"explosive_wheel")) wheel[2]=value==1;
        else if (!strcmp(key,"quickload_confirm")) s->quickload_confirm=value==1;
        else if (!strcmp(key,"game_radials")) s->game_radials=value==1;
        else if (!strcmp(key,"stick_size")) s->control[TC_STICK].scale=value;
        else if (!strcmp(key,"cursor_relative")) s->cursor_relative=value==1;
        else if (!strcmp(key,"cursor_speed")) s->cursor_speed=value;
        else for (int i=0; i<TC_COUNT; ++i) {
            char field[80];
            snprintf(field,sizeof(field),"%s.x",touch_defaults[i].key);
            if (!strcmp(key,field)) s->control[i].x=value;
            snprintf(field,sizeof(field),"%s.y",touch_defaults[i].key);
            if (!strcmp(key,field)) s->control[i].y=value;
            snprintf(field,sizeof(field),"%s.scale",touch_defaults[i].key);
            if (!strcmp(key,field)) s->control[i].scale=value;
        }
    }
    bad=bad || ferror(f); fclose(f);
    if (!touch_settings_game || s->game_radials) memcpy(s->wheel_enabled,wheel,sizeof(wheel));
    else s->control[touch_settings_game->item_control]=item;  /* never shown before: the stored place is not the player's */
    if (bad || !version || version>TOUCH_SETTINGS_VERSION) {
        touch_settings_defaults(s);
        return version>TOUCH_SETTINGS_VERSION ? TOUCH_LOAD_FUTURE : TOUCH_LOAD_INVALID;
    }
    touch_settings_validate(s);
    return version<TOUCH_SETTINGS_VERSION ? TOUCH_LOAD_MIGRATED : TOUCH_LOAD_OK;
}
bool touch_settings_save(const TouchSettings *settings, const char *path) {
    if (!path || !*path) return false;
    char temp[1024];
    int n=snprintf(temp,sizeof(temp),"%s.tmp",path);
    if (n<0 || (size_t)n>=sizeof(temp)) return false;
    TouchSettings s=*settings;
    touch_settings_validate(&s);
    FILE *f=fopen(temp,"wb");
    if (!f) return false;
    fprintf(f,"version=%u\nopacity=%.9g\nsensitivity_x=%.9g\nsensitivity_y=%.9g\ninvert_y=%d\n",
            s.version,s.opacity,s.sensitivity_x,s.sensitivity_y,s.invert_y);
    fprintf(f,"haptics_enabled=%d\nhaptic_strength=%u\nweapon_wheel=%d\nconsumable_wheel=%d\nexplosive_wheel=%d\n",
            s.haptics_enabled,s.haptic_strength,s.wheel_enabled[0],s.wheel_enabled[1],s.wheel_enabled[2]);
    fprintf(f,"cursor_relative=%d\ncursor_speed=%.9g\n",s.cursor_relative,s.cursor_speed);
    if (touch_settings_game) fprintf(f,"quickload_confirm=%d\ngame_radials=1\n",s.quickload_confirm);
    for (int i=0; i<TC_COUNT; ++i) {
        const TouchPlacement *p=&s.control[i];
        fprintf(f,"%s.x=%.9g\n%s.y=%.9g\n%s.scale=%.9g\n",touch_defaults[i].key,p->x,
                touch_defaults[i].key,p->y,touch_defaults[i].key,p->scale);
    }
    bool ok=!ferror(f) && fflush(f)==0;
#ifdef _WIN32
    if (ok) ok=_commit(_fileno(f))==0;
#else
    if (ok) ok=fsync(fileno(f))==0;
#endif
    if (fclose(f)!=0) ok=false;
    if (ok) {
#ifdef _WIN32
        ok=MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        ok=rename(temp,path)==0;
#endif
    }
    if (!ok) remove(temp);
    return ok;
}
