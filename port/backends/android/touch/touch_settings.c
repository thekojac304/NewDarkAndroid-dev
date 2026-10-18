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

/* NewDarkAndroid: System Shock 2 labels; the keys (the settings file format) and the System Shock positions are kept.
   CONS is RELOAD, AMMO cycles ammunition (or right-clicks in cursor mode, so it moved into the left letterbox bar, off
   the game's interface), and the actions System Shock 2 has no equivalent for (explosive, sprint, target) have radius 0,
   which hides them. */
const TouchDefault touch_defaults[TC_COUNT] = {
    {"fire", "FIRE", .86f,.73f,.095f}, {"use", "USE", .69f,.85f,.065f},
    {"jump", "JUMP", .95f,.52f,.05f}, {"crouch", "CROUCH", .79f,.54f,.05f},
    {"previous", "<", .55f,.78f,.045f}, {"next", ">", .55f,.90f,.045f},
    {"consumable", "RELOAD", .52f,.10f,.045f}, {"explosive", "", .63f,.10f,0},
    {"sprint", "", .22f,.54f,0}, {"target", "", .73f,.10f,0},
    {"pause", "II", .05f,.06f,.045f}, {"mode", "MODE", .88f,.0825f,0},
    {"settings", "SET", .16f,.06f,.035f}, {"stick", "MOVE", .20f,.77f,.11f},
    {"ammo", "AMMO", .09f,.30f,.045f}
};
static float bounded(float v, float low, float high, float fallback) {
    return !isfinite(v) ? fallback : fmaxf(low, fminf(high, v));
}
void touch_settings_defaults(TouchSettings *s) {
    memset(s, 0, sizeof(*s));
    s->version=TOUCH_SETTINGS_VERSION;
    s->opacity=1; s->sensitivity_x=s->sensitivity_y=1;
    /* First introduction defaults off: no unexpected vibration or hold delay. */
    s->haptics_enabled=false; s->haptic_strength=1;
    for (int i=0; i<TC_COUNT; ++i)
        s->control[i]=(TouchPlacement){touch_defaults[i].x,touch_defaults[i].y,1};
}
void touch_settings_validate(TouchSettings *s) {
    s->version=TOUCH_SETTINGS_VERSION;
    s->opacity=bounded(s->opacity,TOUCH_OPACITY_MIN,1,1);
    s->sensitivity_x=bounded(s->sensitivity_x,TOUCH_SENS_MIN,TOUCH_SENS_MAX,1);
    s->sensitivity_y=bounded(s->sensitivity_y,TOUCH_SENS_MIN,TOUCH_SENS_MAX,1);
    if (s->haptic_strength<1 || s->haptic_strength>3) s->haptic_strength=1;
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
        else if (!strcmp(key,"weapon_wheel")) s->wheel_enabled[0]=value==1;
        else if (!strcmp(key,"consumable_wheel")) s->wheel_enabled[1]=value==1;
        else if (!strcmp(key,"explosive_wheel")) s->wheel_enabled[2]=value==1;
        else if (!strcmp(key,"stick_size")) s->control[TC_STICK].scale=value;
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
