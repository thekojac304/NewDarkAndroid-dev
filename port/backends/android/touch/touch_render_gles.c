/* touch_render_gles.c: the Shockolate touch overlay (touch_render.c at 7502370) drawn with OpenGL ES 2.
 *
 * Shockolate draws with the SDL_Renderer 2D API, which the portable host does not have. The drawing code below is that
 * file's: the same shapes, colours, layout and 3x5 bitmap font. Only its primitives changed: rectangles, lines and
 * circles become triangles in one vertex batch, drawn after the game's picture and before the buffer swap
 * (plat_sdl2.c gl_swap), in surface pixels including the letterbox bars. The GL state it changes is put back.
 * Visual-compare and QA panels are left out (System Shock QA tools).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int touch_keyboard_shown;         /* touch_ss2_android.c: the Android keyboard is up (KBD is drawn pressed) */

/* ---------------------------------------------------------------- the GLES 2 primitive layer */
typedef struct { float x, y; uint8_t r, g, b, a; } V;
static V *vb; static int vn, vcap;
static uint8_t cr, cg, cb, ca;
static void put(float x, float y) {
    if (vn == vcap) { int n = vcap ? vcap * 2 : 8192; V *p = realloc(vb, (size_t)n * sizeof *p); if (!p) return; vb = p; vcap = n; }
    vb[vn++] = (V){ x, y, cr, cg, cb, ca };
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2) { put(x0, y0); put(x1, y1); put(x2, y2); }
static void quad4(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
    tri(x0, y0, x1, y1, x2, y2); tri(x0, y0, x2, y2, x3, y3);
}
static void fill_rect(float x, float y, float w, float h) { quad4(x, y, x + w, y, x + w, y + h, x, y + h); }
static void disc(float x, float y, float r) {
    int seg = r > 60 ? 64 : 40;
    for (int i = 0; i < seg; i++) {
        float a = i * 6.2831853f / seg, b = (i + 1) * 6.2831853f / seg;
        tri(x, y, x + cosf(a) * r, y + sinf(a) * r, x + cosf(b) * r, y + sinf(b) * r);
    }
}
static void ring(float x, float y, float r, float width) {
    for (int i = 0; i < 64; i++) {
        float a = i * 6.2831853f / 64, b = (i + 1) * 6.2831853f / 64, ro = r, ri = fmaxf(0, r - width);
        quad4(x + cosf(a) * ri, y + sinf(a) * ri, x + cosf(a) * ro, y + sinf(a) * ro, x + cosf(b) * ro, y + sinf(b) * ro, x + cosf(b) * ri, y + sinf(b) * ri);
    }
}

/* ---------------------------------------------------------------- drawing (touch_render.c, unchanged logic) */
static void text(const char *s, float cx, float cy, float size, float max_width) {
    static const char letters[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ<>0123456789.-+%/";
    static const unsigned char glyph[][5]={
        {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},
        {7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},
        {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
        {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
        {2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
        {5,5,2,2,2},{7,1,2,4,7},{1,2,4,2,1},{4,2,1,2,4},
        {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},
        {5,5,7,1,1},{7,4,6,1,6},{3,4,7,5,7},{7,1,2,2,2},
        {7,5,7,5,7},{7,5,7,1,6},{0,0,0,0,2},{0,0,7,0,0},
        {0,2,7,2,0},{5,1,2,4,5},{1,1,2,4,4}
    };
    size=fminf(size,max_width/fmaxf(1,(float)strlen(s)*4));
    float x=cx-(float)strlen(s)*4*size/2;
    for (; *s; ++s,x+=4*size) {
        const char *p=strchr(letters,*s);
        if (!p || !*s) continue;
        for (int y=0;y<5;++y) for (int col=0;col<3;++col)
            if (glyph[p-letters][y] & (1u<<(2-col))) fill_rect(x+col*size,cy-2.5f*size+y*size,size,size);
    }
}
static void color(int red, int green, int blue, int alpha, float opacity) {
    cr=(uint8_t)red; cg=(uint8_t)green; cb=(uint8_t)blue; ca=(uint8_t)lroundf(alpha*opacity);
}
static void rect(TouchRect b, bool fill, float stroke) {
    if (fill) fill_rect(b.x,b.y,b.w,b.h);
    else { fill_rect(b.x,b.y,b.w,stroke); fill_rect(b.x,b.y+b.h-stroke,b.w,stroke); fill_rect(b.x,b.y,stroke,b.h); fill_rect(b.x+b.w-stroke,b.y,stroke,b.h); }
}
static void circle(TouchCircle c, bool held, bool selected, float opacity, float stroke) {
    color(selected ? 86 : held ? 35 : 12, selected ? 62 : held ? 127 : 39, selected ? 27 : held ? 143 : 53,
          selected ? 205 : held ? 230 : 145,opacity);
    disc(c.x,c.y,c.radius);
    color(selected ? 255 : held ? 235 : 173, selected ? 211 : held ? 250 : 226, selected ? 99 : held ? 252 : 232,
          selected ? 255 : 235,opacity);
    ring(c.x,c.y,c.radius,stroke);
}
static bool held(const TouchControls *t,int id) {
    if (id==TC_FIRE) return t->fire_count>0;
    if (id==TC_USE) return t->use_count>0;
    if (id==TC_STICK) return t->move_owner>=0;
    if (id==TC_AMMO) {
        for (int i=0;i<TOUCH_MAX_FINGERS;++i)
            if (t->fingers[i].active && t->fingers[i].action==TOUCH_AMMO &&
                (t->fingers[i].radial_pending || t->fingers[i].radial_opened)) return true;
        return false;
    }
    if (id==TC_SPRINT) return t->sprint_latched;
    return id>=TC_JUMP && id<=TC_PAUSE && t->action_count[id-TC_JUMP]>0;
}
static void controls(const TouchControls *t) {
    bool preview=t->panel!=TOUCH_UI_CLOSED;
    float h=t->layout.height,stroke=fmaxf(1,roundf(h/384));
    for (int id=0;id<TC_COUNT;++id) {
        if (id!=TC_MODE && touch_defaults[id].radius<=0) continue;       /* NewDarkAndroid: hidden in this layout */
        if (!preview) {
            if (id==TC_PAUSE && !t->pause_available) continue;
            if (id!=TC_PAUSE && id!=TC_SETTINGS && !t->gameplay_available) continue;
            if (id!=TC_PAUSE && id!=TC_SETTINGS && id!=TC_MODE && id!=TC_AMMO && id!=TC_TARGET &&
                t->effective_mode!=TOUCH_GAMEPLAY) continue;
        }
        TouchRect b=touch_control_bounds(t,id);
        bool selected=t->panel==TOUCH_UI_EDITOR && t->selected==id;
        float opacity=selected ? fmaxf(.85f,t->settings.opacity) : t->settings.opacity;
        bool down=!preview && (held(t,id) || (id==TC_TARGET && touch_keyboard_shown));   /* lit while the keyboard is up */
        const char *name=touch_defaults[id].label;
        if (id==TC_MODE) {
            color(t->mode==TOUCH_GAMEPLAY ? 15 : 59, t->mode==TOUCH_GAMEPLAY ? 58 : 59,
                  t->mode==TOUCH_GAMEPLAY ? 70 : 82,190,opacity); rect(b,true,stroke);
            bool cursor=t->mode==TOUCH_CURSOR;
            color(selected || cursor ? 255 : 173,selected || cursor ? 211 : 226, selected || cursor ? 99 : 232,240,opacity);
            rect(b,false,stroke);
            name=t->mode==TOUCH_GAMEPLAY ? "LOOK" : "CURSOR";
        } else {
            TouchCircle c={b.x+b.w/2,b.y+b.h/2,b.w/2};
            if (id==TC_STICK && !preview && t->move_owner>=0) { c.x=t->anchor_x; c.y=t->anchor_y; }
            circle(c,down,selected,opacity,stroke);
            if (id==TC_STICK) {
                TouchCircle nub={c.x+t->movement_x*c.radius,c.y+t->movement_y*c.radius,c.radius*.37f};
                circle(nub,down,false,opacity,stroke);
                if (!preview) continue;
            }
            if (id==TC_AMMO && t->effective_mode==TOUCH_CURSOR && !preview) name="USE";   /* right click in cursor mode */
        }
        color(selected ? 255 : 226,selected ? 224 : 245,selected ? 135 : 245,245, fmaxf(.6f,opacity));
        text(name,b.x+b.w/2,b.y+b.h/2,h/240*t->settings.control[id].scale,b.w*.86f);
    }
}
static const char *ui_label(const TouchControls *t,int action) {
    static const char *names[TU_COUNT]={"EDIT LAYOUT","-","+","-","+","-","+",
        "INVERT Y OFF","-","+","RESET TOUCH SETTINGS","CANCEL","SAVE / DONE",
        "<",">","SIZE -","SIZE +","SETTINGS","MOVE TOOLBAR","MORE >",
        "HAPTICS OFF","HAPTIC STRENGTH 1","WEAPON WHEEL OFF",
        "CONSUMABLE WHEEL OFF","EXPLOSIVE WHEEL OFF","CURSOR DIRECT","-","+"};
    if (action>=TU_QA) return "";
    if (action==TU_INVERT && t->settings.invert_y) return "INVERT Y ON";
    if (action==TU_PAGE) return t->settings_page ? "< BACK" : "MORE >";
    if (action==TU_HAPTICS) return t->settings.haptics_enabled ? "HAPTICS ON" : "HAPTICS OFF";
    if (action==TU_HAPTIC_STRENGTH) {
        static const char *levels[]={"HAPTIC STRENGTH 1","HAPTIC STRENGTH 2","HAPTIC STRENGTH 3"};
        return levels[t->settings.haptic_strength-1];
    }
    if (action==TU_WHEEL_WEAPON) return t->settings.wheel_enabled[0] ? "WEAPON WHEEL ON" : "WEAPON WHEEL OFF";
    if (action==TU_WHEEL_CONSUMABLE) return t->settings.wheel_enabled[1] ? "CONSUMABLE WHEEL ON" : "CONSUMABLE WHEEL OFF";
    if (action==TU_CURSOR_STYLE) return t->settings.cursor_relative ? "CURSOR RELATIVE - TRACKPAD" : "CURSOR DIRECT";
    if (action==TU_WHEEL_EXPLOSIVE) return t->settings.wheel_enabled[2] ? "EXPLOSIVE WHEEL ON" : "EXPLOSIVE WHEEL OFF";
    if (t->panel==TOUCH_UI_EDITOR) {
        if (action==TU_OPACITY_DOWN) return "OPACITY -";
        if (action==TU_OPACITY_UP) return "OPACITY +";
        if (action==TU_RESET) return "RESET DEFAULTS";
    }
    return names[action];
}
static void panel(const TouchControls *t) {
    float w=t->layout.width,h=t->layout.height,stroke=fmaxf(1,roundf(h/384));
    char value[100];
    if (t->panel==TOUCH_UI_SETTINGS) {
        color(4,10,17,160,1); rect((TouchRect){0,0,w,h},true,stroke);
        color(15,28,40,250,1); rect((TouchRect){w*.18f,h*.035f,w*.64f,h*.94f},true,stroke);
        color(208,241,244,255,1);
        text("TOUCH SETTINGS",w*.5f,h*.09f,h/185,w*.5f);
        const float values[4]={t->settings.opacity*100,t->settings.sensitivity_x,
                              t->settings.sensitivity_y,t->settings.control[TC_STICK].scale};
        const char *names[4]={"OPACITY","LOOK X","LOOK Y","STICK SIZE"};
        const float rows[4]={.3075f,.4075f,.5075f,.7075f};
        for (int i=0;i<4 && !t->settings_page;++i) {
            if (i==0) snprintf(value,sizeof(value),"%s %.0f%%",names[i],values[i]);
            else snprintf(value,sizeof(value),"%s %.2f",names[i],values[i]);
            text(value,w*.5f,h*rows[i],h/255,w*.33f);
        }
        if (t->settings_page) {             /* NewDarkAndroid: the relative cursor's speed, between its - and + */
            snprintf(value,sizeof(value),"CURSOR SPEED %.2f",t->settings.cursor_speed);
            text(value,w*.5f,h*.2775f,h/255,w*.33f);
        }
    } else if (t->panel==TOUCH_UI_EDITOR) {
        if (t->ui_drag) return; /* captured drag can cross either toolbar edge */
        float y=t->toolbar_top ? 0 : h*.28f;
        color(11,23,34,248,1); rect((TouchRect){0,y,w,h*.17f},true,stroke);
        color(240,231,190,255,1);
        snprintf(value,sizeof(value),"%s %.1f",touch_defaults[t->selected].key,t->settings.control[t->selected].scale);
        for (char *p=value;*p;++p) if (*p>='a' && *p<='z') *p=(char)(*p-'a'+'A');
        text(value,w*.175f,y+h*.0445f,h/290,w*.19f);
        color(213,239,241,220,1);
        snprintf(value,sizeof(value),"DRAG A CONTROL   OPACITY %.0f%%",t->settings.opacity*100);
        text(value,w*.5f,t->toolbar_top ? h*.20f : h*.25f,h/330,w*.75f);
    } else return;
    for (int a=0;a<TU_COUNT;++a) {
        TouchRect b=touch_ui_bounds(t,(TouchUiAction)a);
        if (b.w<=0) continue;
        bool down=t->ui_owned && t->ui_action==a;
        color(down ? 43 : 26,down ? 119 : 51,down ? 129 : 65,255,1); rect(b,true,stroke);
        color(a==TU_SAVE ? 105 : 115,a==TU_SAVE ? 222 : 161,190,255,1); rect(b,false,stroke);
        color(227,244,245,255,1);
        text(ui_label(t,a),b.x+b.w/2,b.y+b.h/2,h/270,b.w*.88f);
    }
    if (t->save_failed) {
        color(255,153,132,255,1);
        text("SAVE FAILED - RETRY OR CANCEL",w*.5f,h*.125f,h/310,w*.60f);
    }
}

/* ---------------------------------------------------------------- GL submission */
#define GLFN(ret, name, args) static ret (GL_APIENTRY *p_##name) args;
#define GLFNS \
    GLFN(GLuint, glCreateShader, (GLenum)) GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    GLFN(void, glCompileShader, (GLuint)) GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint *)) GLFN(GLuint, glCreateProgram, (void)) \
    GLFN(void, glAttachShader, (GLuint, GLuint)) GLFN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    GLFN(void, glLinkProgram, (GLuint)) GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint *)) GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
    GLFN(void, glUseProgram, (GLuint)) GLFN(void, glUniform2f, (GLint, GLfloat, GLfloat)) GLFN(void, glGenBuffers, (GLsizei, GLuint *)) \
    GLFN(void, glBindBuffer, (GLenum, GLuint)) GLFN(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) GLFN(void, glEnableVertexAttribArray, (GLuint)) \
    GLFN(void, glDisableVertexAttribArray, (GLuint)) GLFN(void, glDrawArrays, (GLenum, GLint, GLsizei)) GLFN(void, glGetIntegerv, (GLenum, GLint *)) \
    GLFN(GLboolean, glIsEnabled, (GLenum)) GLFN(void, glEnable, (GLenum)) GLFN(void, glDisable, (GLenum)) GLFN(void, glBlendFunc, (GLenum, GLenum)) \
    GLFN(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) GLFN(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    GLFN(void, glBindFramebuffer, (GLenum, GLuint))
GLFNS
#undef GLFN
static GLuint prog, vbo; static GLint u_size; static int gl_state;   /* 0 not tried, 1 ready, -1 failed */

static int gl_setup(void *(*get)(const char *)) {
#define GLFN(ret, name, args) if (!(p_##name = (ret (GL_APIENTRY *) args)get(#name))) return -1;
    GLFNS
#undef GLFN
    static const char *vs = "attribute vec2 a_pos; attribute vec4 a_col; uniform vec2 u_size; varying vec4 v_col;\n"
        "void main() { v_col = a_col; gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0); }\n";
    static const char *fs = "precision mediump float; varying vec4 v_col; void main() { gl_FragColor = v_col; }\n";
    GLuint s[2]; const char *src[2] = { vs, fs }; GLint ok = 0;
    for (int i = 0; i < 2; i++) {
        s[i] = p_glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER); p_glShaderSource(s[i], 1, &src[i], NULL); p_glCompileShader(s[i]);
        p_glGetShaderiv(s[i], GL_COMPILE_STATUS, &ok); if (!ok) return -1;
    }
    prog = p_glCreateProgram(); p_glAttachShader(prog, s[0]); p_glAttachShader(prog, s[1]);
    p_glBindAttribLocation(prog, 0, "a_pos"); p_glBindAttribLocation(prog, 1, "a_col"); p_glLinkProgram(prog);
    p_glGetProgramiv(prog, GL_LINK_STATUS, &ok); if (!ok) return -1;
    u_size = p_glGetUniformLocation(prog, "u_size"); p_glGenBuffers(1, &vbo);
    return 1;
}

/* Called on the GL thread with the window's framebuffer bound, w x h = the drawable (= touch surface) size. */
int touch_draw_gles(const TouchControls *t, int w, int h, void *(*get_proc)(const char *)) {
    if (!t->enabled || (!t->gameplay_available && !t->pause_available) || t->controller || w <= 0 || h <= 0) return 0;
    if (!gl_state) gl_state = gl_setup(get_proc);
    if (gl_state < 0) return -1;
    vn = 0;
    controls(t);
    if (t->panel!=TOUCH_UI_CLOSED) panel(t);
    if (!vn) return 0;
    GLint old_prog = 0, old_buf = 0, old_vp[4]; GLboolean blend = p_glIsEnabled(GL_BLEND), scissor = p_glIsEnabled(GL_SCISSOR_TEST),
        depth = p_glIsEnabled(GL_DEPTH_TEST), cull = p_glIsEnabled(GL_CULL_FACE);
    p_glGetIntegerv(GL_CURRENT_PROGRAM, &old_prog); p_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buf); p_glGetIntegerv(GL_VIEWPORT, old_vp);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0); p_glViewport(0, 0, w, h);
    p_glDisable(GL_SCISSOR_TEST); p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_CULL_FACE); p_glColorMask(1, 1, 1, 1);
    p_glEnable(GL_BLEND); p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    p_glUseProgram(prog); p_glUniform2f(u_size, (float)w, (float)h);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo); p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vn * sizeof(V)), vb, GL_STREAM_DRAW);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void *)0);
    p_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), (void *)8);
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1);
    p_glDrawArrays(GL_TRIANGLES, 0, vn);
    p_glDisableVertexAttribArray(1);
    /* put back what the game's renderer may rely on between its own state settings */
    p_glUseProgram((GLuint)old_prog); p_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)old_buf); p_glViewport(old_vp[0], old_vp[1], old_vp[2], old_vp[3]);
    if (!blend) p_glDisable(GL_BLEND);
    if (scissor) p_glEnable(GL_SCISSOR_TEST);
    if (depth) p_glEnable(GL_DEPTH_TEST);
    if (cull) p_glEnable(GL_CULL_FACE);
    return vn;
}
