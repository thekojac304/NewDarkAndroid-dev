/* plat_sdl2.c: the reference desktop backend (window, input, audio) on SDL2. Rendering is in gl_render.c.
 *
 * The game draws into an offscreen picture of exactly the size it asked for; every presented frame is scaled into the
 * window with black bars, so the window can have any size and the game's resolution stays what it chose. Mouse
 * positions are reported in that picture's pixels. Environment: SS2PORT_FULLSCREEN=0/1 overrides the game's choice,
 * SS2PORT_GLES=1 asks for OpenGL ES 2.0 instead of desktop OpenGL 2.1, SS2PORT_RENDER_THREAD=1 runs the renderer on a thread of
 * its own (gl_thread.c; the GL context then belongs to that thread). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <unistd.h>
#endif
#define SDL_MAIN_HANDLED          /* our own main(): no SDL_main redirection on Windows */
#include <SDL.h>
#include "plat.h"
#include "gl_render.h"
#ifdef __ANDROID__   /* the touch controls (backends/android/touch/); weak: absent from builds without them */
void android_touch_init(void) __attribute__((weak));
int  android_touch_event(const SDL_Event *e) __attribute__((weak));
int  android_touch_filter(const PlatEvent *p) __attribute__((weak));
void android_touch_poll(void (*sink)(const PlatEvent *, void *), void *user, int dw, int dh, int bx, int by, int bw, int bh,
                        int game_w, int game_h, int focused) __attribute__((weak));
void android_touch_draw(int w, int h) __attribute__((weak));
#define FORWARD(p) (!android_touch_filter || android_touch_filter(p))   /* a physical key/button touch does not already hold */
#else
#define FORWARD(p) 1
#endif

static SDL_Window *win; static SDL_GLContext ctx;
static void port_log_plat_early(const char *m) { char b[200]; snprintf(b, sizeof b, "[sdl] %s", m); plat_log_write(PLAT_LOG_INFO, b); }
static int surf_w = 640, surf_h = 480, want_fullscreen, rel_mode;

/* ---------------------------------------------------------------- window */
static void letterbox(int *x, int *y, int *w, int *h) {
    int ww, wh; SDL_GL_GetDrawableSize(win, &ww, &wh);
    double s = (double)ww / surf_w < (double)wh / surf_h ? (double)ww / surf_w : (double)wh / surf_h;
    *w = (int)(surf_w * s + 0.5); *h = (int)(surf_h * s + 0.5); *x = (ww - *w) / 2; *y = (wh - *h) / 2;
}
void gl_present_rect(int *x, int *y, int *w, int *h, int *ww, int *wh) { letterbox(x, y, w, h); SDL_GL_GetDrawableSize(win, ww, wh); }
/* SS2PORT_PROFILE=1: every 5 s, one line with the frame rate, frame times (mean, median, 95th and 99th percentile, worst),
   frames over 50 and 100 ms (stalls), the share of time spent in SwapWindow, the resident memory and the audio callbacks
   (count, longest gap, peak level, silent buffers: does the mixer keep up and produce sound). A frame time is from
   one swap to the next, so it includes everything the game did in between. */
static void port_log_plat(const char *fmt, ...);
static int prof_on = -1;
static volatile uint32_t au_calls, au_silent, au_peak, au_gap_us;
static void prof_frame(uint64_t t_before, uint64_t t_after) {
    enum { N = 4096 }; static uint32_t ft[N]; static unsigned n; static uint64_t t0, last, swap_ns;
    if (prof_on < 0) prof_on = getenv("SS2PORT_PROFILE") && atoi(getenv("SS2PORT_PROFILE"));
    if (!prof_on) return;
    swap_ns += t_after - t_before;
    if (!t0) { t0 = last = t_after; return; }
    if (n < N) ft[n++] = (uint32_t)((t_after - last) / 1000);           /* microseconds */
    last = t_after;
    if (t_after - t0 < 5000000000ull) return;
    double dt = (double)(t_after - t0) / 1e9, sum = 0; unsigned over50 = 0, over100 = 0;
    for (unsigned i = 1; i < n; i++) { uint32_t v = ft[i]; unsigned j = i; while (j && ft[j - 1] > v) { ft[j] = ft[j - 1]; j--; } ft[j] = v; }   /* a few hundred values */
    for (unsigned i = 0; i < n; i++) { sum += ft[i]; over50 += ft[i] > 50000; over100 += ft[i] > 100000; }
    long rss_kb = -1;
#ifdef __linux__
    { FILE *f = fopen("/proc/self/statm", "r"); long pages, res; if (f) { if (fscanf(f, "%ld %ld", &pages, &res) == 2) rss_kb = res * (sysconf(_SC_PAGESIZE) / 1024); fclose(f); } }
#endif
    port_log_plat("profile: %.1f fps | frame ms mean %.1f p50 %.1f p95 %.1f p99 %.1f max %.1f | >50ms %u >100ms %u | swap %.0f%% | rss %ld MB"
                  " | audio %u callbacks, longest gap %.1f ms, peak %u, %u silent",
                  n / dt, n ? sum / n / 1000 : 0, n ? ft[n / 2] / 1000.0 : 0, n ? ft[n * 95 / 100] / 1000.0 : 0, n ? ft[n * 99 / 100] / 1000.0 : 0,
                  n ? ft[n - 1] / 1000.0 : 0, over50, over100, 100.0 * (double)swap_ns / (dt * 1e9), rss_kb / 1024,
                  au_calls, au_gap_us / 1000.0, au_peak, au_silent);
    t0 = t_after; n = 0; swap_ns = 0; au_calls = au_silent = au_peak = au_gap_us = 0;
}
/* SS2PORT_SHOT_DIR=<folder>: frame capture for tests and bug reports, of the picture as shown (bars and touch controls
   included). Whenever <folder>/shot.req exists (looked for every 15 frames), the frame is written to
   <folder>/shot_NNN.bmp at half size and shot.req is removed. It reads the GL back buffer, so it works whatever covers
   the window (another app's overlay, a locked desktop). */
static void shot_maybe(void) {
    static const char *dir; static int init, n; static unsigned frame;
    if (!init) { init = 1; dir = getenv("SS2PORT_SHOT_DIR"); }
    if (!dir || ++frame % 15) return;
    char path[1200]; snprintf(path, sizeof path, "%s/shot.req", dir);
    FILE *f = fopen(path, "rb"); if (!f) return; fclose(f); remove(path);
    typedef void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
    ReadPixels rp = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
    int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
    uint8_t *px = rp && w > 1 && h > 1 ? malloc((size_t)w * h * 4) : NULL; if (!px) return;
    rp(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px);
    int ow = w / 2, oh = h / 2, stride = (ow * 3 + 3) & ~3; uint32_t size = 54u + (uint32_t)stride * oh;
    uint8_t hd[54] = { 'B', 'M' }; memcpy(hd + 2, &size, 4); hd[10] = 54; hd[14] = 40; memcpy(hd + 18, &ow, 4); memcpy(hd + 22, &oh, 4);
    hd[26] = 1; hd[28] = 24;
    snprintf(path, sizeof path, "%s/shot_%03d.bmp", dir, n++);
    if ((f = fopen(path, "wb"))) {             /* GL rows start at the bottom, as BMP rows do */
        uint8_t *row = calloc(1, (size_t)stride); fwrite(hd, 1, 54, f);
        for (int y = 0; y < oh; y++) { const uint8_t *s = px + (size_t)(2 * y) * w * 4; for (int x = 0; x < ow; x++) { row[3 * x] = s[8 * x + 2]; row[3 * x + 1] = s[8 * x + 1]; row[3 * x + 2] = s[8 * x]; } fwrite(row, 1, (size_t)stride, f); }
        free(row); fclose(f); port_log_plat("frame capture: %s (%dx%d)", path, ow, oh);
    }
    free(px);
}
void gl_swap(void) {
#ifdef __ANDROID__
    if (android_touch_draw) { int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh); android_touch_draw(dw, dh); }   /* over the picture and bars */
#endif
    shot_maybe();
    uint64_t t = plat_time_ns(); SDL_GL_SwapWindow(win); prof_frame(t, plat_time_ns());
}
int gl_thread_spawn(void (*fn)(void *), void *arg) { return plat_thread_start(fn, arg, "render"); }
/* GL set-up, run on the GL thread (the render thread, or this one) */
typedef struct { int gles, ret, w, h; } GlSetup;
static void gl_setup(void *p) {
    GlSetup *g = p; if (glt_on) SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(getenv("SS2PORT_NOVSYNC") ? 0 : 1);
    g->ret = gl_init(SDL_GL_GetProcAddress, g->gles);
    if (!g->ret) { PlatVideoCaps c; gl_caps(&c); glt_set_caps(&c); }
}
static void gl_resize(void *p) { GlSetup *g = p; gl_resize_backbuffer(g->w, g->h); }
static void gl_release(void *p) { (void)p; gl_shutdown(); if (glt_on) SDL_GL_MakeCurrent(win, NULL); }
int plat_video_open(int w, int h, int fullscreen, const char *title) {
    const char *fs = getenv("SS2PORT_FULLSCREEN"); if (fs) fullscreen = atoi(fs);
    surf_w = w > 0 ? w : 640; surf_h = h > 0 ? h : 480; want_fullscreen = fullscreen;
    if (!win) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL video: %s\n", SDL_GetError()); return -1; }
        int gles = getenv("SS2PORT_GLES") && atoi(getenv("SS2PORT_GLES"));
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gles ? SDL_GL_CONTEXT_PROFILE_ES : 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gles ? 0 : 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        win = SDL_CreateWindow(title ? title : "System Shock 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, surf_w, surf_h,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!win) { fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return -1; }
        ctx = SDL_GL_CreateContext(win);
        if (!ctx) { fprintf(stderr, "OpenGL context: %s\n", SDL_GetError()); return -1; }
        const char *rt = getenv("SS2PORT_RENDER_THREAD");
        if (rt && atoi(rt)) {
            SDL_GL_MakeCurrent(win, NULL);                                   /* the context moves to the render thread */
            if (glt_start()) { fprintf(stderr, "render thread: could not start; rendering on this thread\n"); SDL_GL_MakeCurrent(win, ctx); }
            else port_log_plat_early("renderer on its own thread");
        }
        GlSetup g = { gles, 0, 0, 0 }; glt_call(gl_setup, &g);
        if (g.ret) return -1;
#ifndef __ANDROID__                 /* on Android it would open the on-screen keyboard */
        SDL_StartTextInput();
#endif
    } else {
        SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!fullscreen) { int cw, ch; SDL_GetWindowSize(win, &cw, &ch); if (cw < surf_w || ch < surf_h) SDL_SetWindowSize(win, surf_w, surf_h); }
        if (title) SDL_SetWindowTitle(win, title);
    }
    { GlSetup g = { 0, 0, surf_w, surf_h }; glt_call(gl_resize, &g); }
    return 0;
}
void plat_video_close(void) { if (ctx) { glt_call(gl_release, NULL); SDL_GL_DeleteContext(ctx); ctx = NULL; } if (win) { SDL_DestroyWindow(win); win = NULL; } }
void plat_video_caps(PlatVideoCaps *c) { if (!glt_get_caps(c)) gl_caps(c); }
void plat_video_window_size(int *w, int *h) { *w = surf_w; *h = surf_h; }
void plat_video_display_size(int *w, int *h) {
    SDL_DisplayMode m; if (SDL_WasInit(SDL_INIT_VIDEO) || !SDL_InitSubSystem(SDL_INIT_VIDEO)) { if (!SDL_GetDesktopDisplayMode(0, &m)) { *w = m.w; *h = m.h; return; } }
    *w = 1920; *h = 1080;
}
void plat_video_set_title(const char *t) { if (win) SDL_SetWindowTitle(win, t); }
#include <stdarg.h>
static void port_log_plat(const char *fmt, ...) { char b[300]; int n = snprintf(b, sizeof b, "[sdl] "); va_list ap; va_start(ap, fmt); vsnprintf(b + n, sizeof b - (size_t)n, fmt, ap); va_end(ap); plat_log_write(PLAT_LOG_INFO, b); }
static int warp_pending, warp_x, warp_y, skip_first_rel;   /* entering relative mode can report one huge jump (seen under WSLg: -2477,-638) */      /* the motion event our own warp produces is not player movement */
static float mouse_scale = 1.0f, frac_x, frac_y;
#ifdef __ANDROID__
/* Android: SDL asks for pointer capture straight from this thread, and the view ignores a request while it still believes it
   has capture. A release followed at once by a new request (the game frees and recaptures the mouse when it opens its
   menu) therefore leaves the pointer uncaptured while SDL is in relative mode: hover positions then arrive as huge relative
   motion. So shortly after every switch to relative mode, ask again through SDL's own reclaimRelativeMouseModeIfNeeded. */
#include <jni.h>
static uint64_t reclaim_at;
static void android_reclaim_pointer(void) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv(); if (!env) return;
    jobject activity = (jobject)SDL_AndroidGetActivity();          /* FindClass on this thread would not see the app's classes */
    jclass act = activity ? (*env)->GetObjectClass(env, activity) : NULL;
    jmethodID get = act ? (*env)->GetStaticMethodID(env, act, "getMotionListener", "()Lorg/libsdl/app/SDLGenericMotionListener_API12;") : NULL;
    jobject ml = get ? (*env)->CallStaticObjectMethod(env, act, get) : NULL;
    jclass mc = ml ? (*env)->GetObjectClass(env, ml) : NULL;
    jmethodID re = mc ? (*env)->GetMethodID(env, mc, "reclaimRelativeMouseModeIfNeeded", "()V") : NULL;
    if (re) (*env)->CallVoidMethod(env, ml, re);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); port_log_plat("mouse: could not reclaim pointer capture"); }
    if (mc) (*env)->DeleteLocalRef(env, mc);
    if (ml) (*env)->DeleteLocalRef(env, ml);
    if (act) (*env)->DeleteLocalRef(env, act);
    if (activity) (*env)->DeleteLocalRef(env, activity);
}
#endif
void plat_video_mouse_mode(int relative, int visible) {
    static int last = -1, rel_was; rel_mode = relative;
    if (!win) return;
    int r = SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE); if (relative && !rel_was) skip_first_rel = 1; SDL_ShowCursor(visible && !relative ? SDL_ENABLE : SDL_DISABLE);
#ifdef __ANDROID__
    if (relative && !rel_was) reclaim_at = plat_time_ns() + 300000000ull;     /* once the release has reached the view */
#endif
    rel_was = relative;
    if (relative != last) {        /* quiet unless capture failed or SS2PORT_MOUSE_DEBUG is set */
        last = relative;
        if (r || getenv("SS2PORT_MOUSE_DEBUG")) port_log_plat("mouse: %s%s%s", relative ? "captured (relative motion)" : "free", r ? " - SDL refused: " : "", r ? SDL_GetError() : "");
    }
}
void plat_video_warp_mouse(int x, int y) {
    if (!win || rel_mode) return; int bx, by, bw, bh; letterbox(&bx, &by, &bw, &bh);
    int ww, wh, dw, dh; SDL_GetWindowSize(win, &ww, &wh); SDL_GL_GetDrawableSize(win, &dw, &dh);
    warp_x = (int)((bx + (double)x * bw / surf_w) * ww / dw); warp_y = (int)((by + (double)y * bh / surf_h) * wh / dh); warp_pending = 1;
    SDL_WarpMouseInWindow(win, warp_x, warp_y);
}

/* ---------------------------------------------------------------- input: SDL scan codes (USB HID) to DirectInput scan codes */
static int dik_of(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) { static const uint8_t l[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C }; return l[s - SDL_SCANCODE_A]; }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 0x3B + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_RETURN: return 0x1C; case SDL_SCANCODE_ESCAPE: return 0x01; case SDL_SCANCODE_BACKSPACE: return 0x0E; case SDL_SCANCODE_TAB: return 0x0F; case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_MINUS: return 0x0C; case SDL_SCANCODE_EQUALS: return 0x0D; case SDL_SCANCODE_LEFTBRACKET: return 0x1A; case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_SEMICOLON: return 0x27; case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29; case SDL_SCANCODE_COMMA: return 0x33; case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35; case SDL_SCANCODE_CAPSLOCK: return 0x3A; case SDL_SCANCODE_F11: return 0x57; case SDL_SCANCODE_F12: return 0x58; case SDL_SCANCODE_PRINTSCREEN: return 0xB7;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46; case SDL_SCANCODE_PAUSE: return 0xC5; case SDL_SCANCODE_INSERT: return 0xD2; case SDL_SCANCODE_HOME: return 0xC7; case SDL_SCANCODE_PAGEUP: return 0xC9;
    case SDL_SCANCODE_DELETE: return 0xD3; case SDL_SCANCODE_END: return 0xCF; case SDL_SCANCODE_PAGEDOWN: return 0xD1; case SDL_SCANCODE_RIGHT: return 0xCD; case SDL_SCANCODE_LEFT: return 0xCB;
    case SDL_SCANCODE_DOWN: return 0xD0; case SDL_SCANCODE_UP: return 0xC8; case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45; case SDL_SCANCODE_KP_DIVIDE: return 0xB5; case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_KP_MINUS: return 0x4A; case SDL_SCANCODE_KP_PLUS: return 0x4E; case SDL_SCANCODE_KP_ENTER: return 0x9C; case SDL_SCANCODE_KP_1: return 0x4F; case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51; case SDL_SCANCODE_KP_4: return 0x4B; case SDL_SCANCODE_KP_5: return 0x4C; case SDL_SCANCODE_KP_6: return 0x4D; case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48; case SDL_SCANCODE_KP_9: return 0x49; case SDL_SCANCODE_KP_0: return 0x52; case SDL_SCANCODE_KP_PERIOD: return 0x53; case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
    case SDL_SCANCODE_APPLICATION: return 0xDD; case SDL_SCANCODE_LCTRL: return 0x1D; case SDL_SCANCODE_LSHIFT: return 0x2A; case SDL_SCANCODE_LALT: return 0x38; case SDL_SCANCODE_LGUI: return 0xDB;
    case SDL_SCANCODE_RCTRL: return 0x9D; case SDL_SCANCODE_RSHIFT: return 0x36; case SDL_SCANCODE_RALT: return 0xB8; case SDL_SCANCODE_RGUI: return 0xDC;
    default: return 0;
    }
}
static void to_surface(int wx, int wy, int *x, int *y) {
    int bx, by, bw, bh; letterbox(&bx, &by, &bw, &bh);
    int ww, wh, dw, dh; SDL_GetWindowSize(win, &ww, &wh); SDL_GL_GetDrawableSize(win, &dw, &dh);
    double px = (double)wx * dw / (ww ? ww : 1), py = (double)wy * dh / (wh ? wh : 1);
    *x = bw ? (int)((px - bx) * surf_w / bw) : 0; *y = bh ? (int)((py - by) * surf_h / bh) : 0;
    if (*x < 0) *x = 0; if (*y < 0) *y = 0; if (*x >= surf_w) *x = surf_w - 1; if (*y >= surf_h) *y = surf_h - 1;
}
static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p; uint32_t c = *s++; int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (n) c &= 0x3F >> n; for (int i = 0; i < n && (*s & 0xC0) == 0x80; i++) c = (c << 6) | (*s++ & 0x3F);
    *p = (const char *)s; return c;
}
/* SS2PORT_INPUT_DEBUG=1: log every input, window and app event except mouse motion (SS2PORT_MOUSE_DEBUG logs that), up to 5000 lines */
static void input_debug(const SDL_Event *e) {
    static int dbg = -1; static unsigned n; if (dbg < 0) dbg = getenv("SS2PORT_INPUT_DEBUG") != NULL;
    if (!dbg || n >= 5000) return;
    switch (e->type) {
    case SDL_KEYDOWN: case SDL_KEYUP: n++; port_log_plat("input: key %s scancode %d (%s) dik 0x%02x repeat %d", e->type == SDL_KEYDOWN ? "down" : "up", e->key.keysym.scancode, SDL_GetScancodeName(e->key.keysym.scancode), dik_of(e->key.keysym.scancode), e->key.repeat); break;
    case SDL_TEXTINPUT: n++; port_log_plat("input: text \"%s\"", e->text.text); break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: n++; port_log_plat("input: mouse button %d %s at %d,%d%s", e->button.button, e->type == SDL_MOUSEBUTTONDOWN ? "down" : "up", e->button.x, e->button.y, e->button.which == SDL_TOUCH_MOUSEID ? " (from touch)" : ""); break;
    case SDL_MOUSEWHEEL: n++; port_log_plat("input: wheel %d,%d", e->wheel.x, e->wheel.y); break;
    case SDL_FINGERDOWN: case SDL_FINGERUP: n++; port_log_plat("input: finger %s at %.3f,%.3f", e->type == SDL_FINGERDOWN ? "down" : "up", e->tfinger.x, e->tfinger.y); break;
    case SDL_WINDOWEVENT: n++; port_log_plat("input: window event %d (%d,%d)", e->window.event, e->window.data1, e->window.data2); break;
    case SDL_APP_TERMINATING: case SDL_APP_WILLENTERBACKGROUND: case SDL_APP_DIDENTERBACKGROUND: case SDL_APP_WILLENTERFOREGROUND: case SDL_APP_DIDENTERFOREGROUND: n++; port_log_plat("input: app event 0x%x", e->type); break;
    default: break;
    }
}
void plat_video_poll(void (*sink)(const PlatEvent *, void *), void *user) {
    if (!win) return;
    SDL_Event e; PlatEvent p;
#ifdef __ANDROID__
    if (reclaim_at && plat_time_ns() >= reclaim_at) { reclaim_at = 0; if (rel_mode) android_reclaim_pointer(); }
#endif
    while (SDL_PollEvent(&e)) {
        memset(&p, 0, sizeof p); input_debug(&e);
#ifdef __ANDROID__
        if (android_touch_event && android_touch_event(&e)) continue;      /* a touch: the touch controls' (touch_ss2.c) */
#endif
        switch (e.type) {
        case SDL_QUIT: p.type = PLAT_EV_QUIT; sink(&p, user); break;
#ifdef __ANDROID__
        /* the activity is being destroyed (finished, or cleared from its task): there is no window to come back to, and
           the game would keep running without one, so end the process (port_exit flushes buffered file writes first) */
        case SDL_APP_TERMINATING: { void port_exit(int code); port_log_plat("the Android activity is being destroyed: ending the game"); port_exit(0); break; }
#endif
        case SDL_KEYDOWN: case SDL_KEYUP:
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) { want_fullscreen = !want_fullscreen; SDL_SetWindowFullscreen(win, want_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); break; }
            p.type = PLAT_EV_KEY; p.down = e.type == SDL_KEYDOWN; p.repeat = e.key.repeat; p.key = dik_of(e.key.keysym.scancode); if (p.key && FORWARD(&p)) sink(&p, user); break;
        case SDL_TEXTINPUT: { const char *t = e.text.text; while (*t) { p.type = PLAT_EV_TEXT; p.text = utf8_next(&t); sink(&p, user); } break; }
        case SDL_MOUSEMOTION: {
            p.type = PLAT_EV_MOUSE_MOVE; to_surface(e.motion.x, e.motion.y, &p.x, &p.y);
            if (warp_pending && e.motion.x == warp_x && e.motion.y == warp_y) { warp_pending = 0; p.dx = p.dy = 0; sink(&p, user); break; }
            { static int dbg = -1; if (dbg < 0) dbg = getenv("SS2PORT_MOUSE_DEBUG") != NULL; static unsigned n;
              if (dbg && n++ < 3000) port_log_plat("motion x=%d y=%d xrel=%d yrel=%d %s", e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel, rel_mode ? "captured" : "free"); }
            if (rel_mode && skip_first_rel) { skip_first_rel = 0; p.dx = p.dy = 0; sink(&p, user); break; }
            float fx = e.motion.xrel * mouse_scale + frac_x, fy = e.motion.yrel * mouse_scale + frac_y;
            p.dx = (int)fx; p.dy = (int)fy; frac_x = fx - (float)p.dx; frac_y = fy - (float)p.dy;
            sink(&p, user); break; }
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
            static const int map[6] = { -1, 0, 2, 1, 3, 4 }; int b = e.button.button < 6 ? map[e.button.button] : -1; if (b < 0) break;
            p.type = PLAT_EV_MOUSE_BUTTON; p.down = e.type == SDL_MOUSEBUTTONDOWN; p.button = b; if (FORWARD(&p)) sink(&p, user); break; }
        case SDL_MOUSEWHEEL: p.type = PLAT_EV_MOUSE_WHEEL; p.dy = (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y) * 120; if (p.dy) sink(&p, user); break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { p.type = PLAT_EV_FOCUS; p.down = e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED; sink(&p, user); if (p.down && rel_mode) SDL_SetRelativeMouseMode(SDL_TRUE); }
            else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { p.type = PLAT_EV_RESIZE; p.x = e.window.data1; p.y = e.window.data2; sink(&p, user); }
            break;
        }
    }
#ifdef __ANDROID__
    if (android_touch_poll) {               /* the touch controls' events, after the platform's */
        int bx, by, bw, bh, dw, dh; letterbox(&bx, &by, &bw, &bh); SDL_GL_GetDrawableSize(win, &dw, &dh);
        android_touch_poll(sink, user, dw, dh, bx, by, bw, bh, surf_w, surf_h, (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0);
    }
#endif
}

/* ---------------------------------------------------------------- audio */
static SDL_AudioDeviceID adev; static void (*afill)(int16_t *, int, void *); static void *auser;
/* audio statistics for the SS2PORT_PROFILE line (written by SDL's audio thread, read and reset by the game thread) */
static uint64_t au_last;
static void audio_cb(void *u, Uint8 *stream, int len) {
    (void)u; afill((int16_t *)stream, len / 4, auser);
    if (prof_on <= 0) return;
    uint64_t t = plat_time_ns(); const int16_t *s = (const int16_t *)stream; int peak = 0;
    for (int i = 0; i < len / 2; i++) { int v = s[i] < 0 ? -s[i] : s[i]; if (v > peak) peak = v; }
    if (au_last && (t - au_last) / 1000 > au_gap_us) au_gap_us = (uint32_t)((t - au_last) / 1000);
    au_last = t; au_calls++; if (!peak) au_silent++; if ((uint32_t)peak > au_peak) au_peak = (uint32_t)peak;
}
int plat_audio_open(int rate, void (*fill)(int16_t *, int, void *), void *user) {
    if (getenv("SS2PORT_NOSOUND") || SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
    SDL_AudioSpec want = { 0 }, have; want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    afill = fill; auser = user;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!adev) { fprintf(stderr, "SDL audio: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(adev, 0); return have.freq;
}
void plat_audio_close(void) { if (adev) { SDL_CloseAudioDevice(adev); adev = 0; } }
void plat_audio_pause(int p) { if (adev) SDL_PauseAudioDevice(adev, p); }

void plat_alert(const char *title, const char *text) { if (!getenv("SS2PORT_NOALERT")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, win); }

int port_main(int argc, char **argv);
#ifdef __ANDROID__                  /* SDL calls SDL_main (backends/android/android_main.c), which runs this on its game thread */
int plat_sdl2_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
    /* SS2PORT_MOUSE_WARP=1: emulate captured mouse by re-centring the pointer (for systems without pointer capture,
       such as some remote desktops); SS2PORT_MOUSE_SCALE: multiply mouse motion (e.g. 0.5) */
    const char *mw = getenv("SS2PORT_MOUSE_WARP"); SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, mw && *mw == '1' ? "1" : "0");
    const char *ms = getenv("SS2PORT_MOUSE_SCALE"); if (ms && atof(ms) > 0) mouse_scale = (float)atof(ms);
    SDL_SetMainReady();
#ifdef __ANDROID__
    if (android_touch_init) android_touch_init();   /* before SDL_Init: SDL must not turn touches into mouse events */
#endif
    if (SDL_Init(0)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    int r = port_main(argc, argv);
    SDL_Quit(); return r;
}
