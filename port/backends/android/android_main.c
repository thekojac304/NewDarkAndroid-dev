/* android_main.c: the Android entry of the portable host (README.md in this directory).
 *
 * The app (android/) starts NewDarkActivity, an SDLActivity, in its own process. SDL's Java side loads libSDL2.so and
 * this library and calls SDL_main on its "SDLThread". SDL_main here
 *   - opens <run dir>/run.log: every plat_log_write line goes there and to logcat (tag "ss2port"), stray stdout/stderr
 *     to the file;
 *   - records how the process ends in <run dir>/status ("start <pid>", then "exit <code>" or "signal <n> ..."), which
 *     the launcher reads back from its own process;
 *   - checks, inside the app sandbox, what the host relies on: the process identity, the 4 GB guest reservation, a deep
 *     stack, threads and locks;
 *   - runs port_main on a thread with a large stack, because recompiled code makes a host C call for every guest call.
 * Arguments from the app: argv[1] is the run directory (it becomes the working directory), argv[2..] go to port_main,
 * except --android-crash-test, which makes the game thread crash instead (to test how a native crash is reported).
 *
 * The library is linked with -Wl,--wrap=exit,--wrap=plat_log_write (port/CMakeLists.txt). port_exit ends the process
 * with exit(); __wrap_exit records the status and calls _exit instead, so that no atexit handler or library destructor
 * runs while ART and the guest's threads are still running. The host sets its state up once per process (the guest
 * address space, thunks, the guest lock), so each run is a new process. */
#define _GNU_SOURCE
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/uio.h>
#include <unistd.h>
#include "plat.h"

#define TAG "ss2port"
#define GAME_STACK (64u << 20)          /* the game thread's stack; SDLThread has the Java default of about 1 MB */
#define PROBE_USE (48u << 20)           /* how much of a GAME_STACK-sized stack the preflight check uses */

int port_main(int argc, char **argv);
#define EXPORT __attribute__((visibility("default")))

static int log_fd = -1, status_fd = -1;
static uint64_t t_start;
static pthread_mutex_t log_m = PTHREAD_MUTEX_INITIALIZER;

/* ---------------------------------------------------------------- log and status */
void __wrap_plat_log_write(PlatLogLevel level, const char *line) {
    static const int prio[] = { ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR };
    __android_log_write(prio[(unsigned)level & 3], TAG, line);
    if (log_fd < 0) return;
    struct iovec v[2] = { { (void *)line, strlen(line) }, { (void *)"\n", 1 } };
    pthread_mutex_lock(&log_m); (void)writev(log_fd, v, 2); pthread_mutex_unlock(&log_m);
}
static void alog(PlatLogLevel level, const char *fmt, ...) {
    char b[1024]; int n = snprintf(b, sizeof b, "[android] ");
    va_list ap; va_start(ap, fmt); vsnprintf(b + n, sizeof b - (size_t)n, fmt, ap); va_end(ap);
    __wrap_plat_log_write(level, b);
}
static void status_line(const char *s) { if (status_fd >= 0) { (void)write(status_fd, s, strlen(s)); fsync(status_fd); } }
static double seconds(void) { return (double)(plat_time_ns() - t_start) / 1e9; }

__attribute__((noreturn)) void __wrap_exit(int code) {
    alog(PLAT_LOG_INFO, "process exit(%d) after %.3f s", code, seconds());
    char b[32]; snprintf(b, sizeof b, "exit %d\n", code); status_line(b);
    fflush(NULL); if (log_fd >= 0) fsync(log_fd);
    _exit(code);
}

/* A native crash: note it in the status and the log (async-signal-safe: write only), then hand the signal to the handler
 * that was there before (debuggerd: tombstone and backtrace in logcat). A fault repeats when the handler returns; a
 * signal sent with kill/raise/abort is raised again. */
static struct sigaction old_act[65];
static char *put_u(char *p, uint64_t v, int base) {
    char t[24]; int n = 0; do { t[n++] = "0123456789abcdef"[v % (unsigned)base]; v /= (unsigned)base; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static void on_fatal(int sig, siginfo_t *si, void *uc) {
    (void)uc; char b[96], *p = b;
    memcpy(p, "signal ", 7); p = put_u(p + 7, (uint64_t)sig, 10);
    memcpy(p, " code ", 6); p += 6; if (si->si_code < 0) { *p++ = '-'; p = put_u(p, (uint64_t)-si->si_code, 10); } else p = put_u(p, (uint64_t)si->si_code, 10);
    memcpy(p, " addr 0x", 8); p = put_u(p + 8, (uint64_t)(uintptr_t)si->si_addr, 16); *p++ = '\n';
    if (status_fd >= 0) (void)write(status_fd, b, (size_t)(p - b));
    if (log_fd >= 0) { (void)write(log_fd, "[android] fatal ", 16); (void)write(log_fd, b, (size_t)(p - b)); }
    sigaction(sig, &old_act[sig], NULL);
    if (si->si_code <= 0) raise(sig);
}
static void catch_fatal_signals(void) {
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS };
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_fatal; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, &old_act[sigs[i]]);
}

/* ---------------------------------------------------------------- preflight checks */
static int n_checks, n_failed;
static void check(int ok, const char *what, const char *fmt, ...) {
    char d[512]; va_list ap; va_start(ap, fmt); vsnprintf(d, sizeof d, fmt, ap); va_end(ap);
    n_checks++; if (!ok) n_failed++;
    alog(ok ? PLAT_LOG_INFO : PLAT_LOG_ERROR, "check %s: %s (%s)", what, ok ? "OK" : "FAIL", d);
}
static void read_line(const char *path, char *out, size_t n) {
    out[0] = 0; int fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) { snprintf(out, n, "?"); return; }
    ssize_t r = read(fd, out, n - 1); close(fd); out[r > 0 ? r : 0] = 0;
    for (char *p = out; *p; p++) if (*p == '\n') { *p = 0; break; }
}

static void check_process(void) {
    char ctx[128], name[128], model[PROP_VALUE_MAX], rel[PROP_VALUE_MAX], sdk[PROP_VALUE_MAX], soc[PROP_VALUE_MAX];
    read_line("/proc/self/attr/current", ctx, sizeof ctx); read_line("/proc/self/cmdline", name, sizeof name);
    __system_property_get("ro.product.model", model); __system_property_get("ro.build.version.release", rel);
    __system_property_get("ro.build.version.sdk", sdk); __system_property_get("ro.soc.model", soc);
    Dl_info di; const char *lib = dladdr((void *)check_process, &di) && di.dli_fname ? di.dli_fname : "?";
    alog(PLAT_LOG_INFO, "device %s (%s), Android %s (API %s), page size %ld, %ld CPUs", model, soc, rel, sdk, sysconf(_SC_PAGESIZE), sysconf(_SC_NPROCESSORS_ONLN));
    alog(PLAT_LOG_INFO, "library %s", lib);
    check(getuid() >= 10000 && strstr(ctx, "untrusted_app") != NULL, "app process", "pid %d, process %s, uid %d, SELinux %s", getpid(), name, getuid(), ctx);
}

static void check_reservation(void) {          /* the same call vm_init makes for the guest, released again */
    const uint64_t size = 4ull << 30; uint64_t saved = g_plat_backed; g_plat_backed = 0;
    uint64_t t = plat_time_ns(); uint8_t *p = plat_mem_reserve(size); int err = errno; g_plat_backed = saved;
    if (!p) { check(0, "4 GB guest reservation", "mmap failed: %s", strerror(err)); return; }
    static const uint64_t at[] = { 0, 0x00400000, 0x7FFFFFFF, 0x80000000, 0xFFFF0000, 0xFFFFFFFF };
    int ok = 1;
    for (size_t i = 0; i < sizeof at / sizeof *at; i++) { volatile uint8_t *q = p + at[i]; if (*q != 0) ok = 0; *q = (uint8_t)(i + 1); if (*q != (uint8_t)(i + 1)) ok = 0; }
    plat_mem_discard(p + 0x00400000, 0x10000); if (p[0x00400000] != 0) ok = 0;
    plat_mem_release(p, size);
    check(ok, "4 GB guest reservation", "at %p, zero-filled, written and read back at 6 places from 0 to 4 GB-1, discard reads back zero, released; %.1f ms",
          (void *)p, (double)(plat_time_ns() - t) / 1e6);
}

static uintptr_t probe_low;
__attribute__((noinline)) static unsigned probe(unsigned depth) {
    volatile char buf[32768];
    for (size_t i = 0; i < sizeof buf; i += 4096) buf[i] = (char)depth;
    if ((uintptr_t)buf < probe_low) probe_low = (uintptr_t)buf;
    unsigned r = depth ? probe(depth - 1) : 0;
    return r + (unsigned char)buf[0];
}
typedef struct { size_t size, guard; uintptr_t lo, hi, used; unsigned sum; } StackInfo;
static void stack_info(StackInfo *s) {
    pthread_attr_t a; void *lo = NULL; size_t sz = 0, g = 0;
    if (!pthread_getattr_np(pthread_self(), &a)) { pthread_attr_getstack(&a, &lo, &sz); pthread_attr_getguardsize(&a, &g); pthread_attr_destroy(&a); }
    s->size = sz; s->guard = g; s->lo = (uintptr_t)lo; s->hi = (uintptr_t)lo + sz;
}
static void *probe_thread(void *arg) {
    StackInfo *s = arg; stack_info(s); pthread_setname_np(pthread_self(), "ss2-stackprobe");
    uintptr_t top = (uintptr_t)__builtin_frame_address(0); probe_low = top;
    s->sum = probe(PROBE_USE / 32768); s->used = top - probe_low;
    return NULL;
}
static pthread_attr_t game_attr(void) {
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, GAME_STACK); return a;
}
static void check_stack(void) {
    StackInfo s; memset(&s, 0, sizeof s); pthread_attr_t a = game_attr(); pthread_t t;
    int e = pthread_create(&t, &a, probe_thread, &s); pthread_attr_destroy(&a);
    if (e) { check(0, "deep stack", "pthread_create with a %u MB stack: %s", GAME_STACK >> 20, strerror(e)); return; }
    pthread_join(t, NULL);
    check(s.size >= GAME_STACK && s.used >= PROBE_USE, "deep stack", "thread with the game thread's attributes: stack %zu KB + guard %zu KB; recursion used %zu KB of it",
          s.size >> 10, s.guard >> 10, (size_t)(s.used >> 10));
}

/* the plat.h threads, locks and condition variables the core is built on (the conformance suite checks the guest side) */
typedef struct { PlatMutex *m; PlatCond *c; int count, done; } Sync;
static void sync_worker(void *arg) {
    Sync *s = arg;
    for (int i = 0; i < 100000; i++) { plat_mutex_lock(s->m); s->count++; plat_mutex_unlock(s->m); }
    plat_mutex_lock(s->m); s->done++; plat_cond_broadcast(s->c); plat_mutex_unlock(s->m);
}
static void check_sync(void) {
    Sync s = { plat_mutex_new(), plat_cond_new(), 0, 0 }; enum { N = 4 };
    int started = 0; for (int i = 0; i < N; i++) started += plat_thread_start(sync_worker, &s, "ss2-sync") == 0;
    uint64_t t = plat_time_ns(); int timed_out = 0;
    plat_mutex_lock(s.m);
    while (s.done < started && !timed_out) timed_out = plat_cond_wait_ns(s.c, s.m, 10000000000ull) && s.done < started;
    uint64_t t1 = plat_time_ns(), waited; int to;         /* nobody signals now: the wait must time out (a spurious wakeup waits again) */
    do { to = plat_cond_wait_ns(s.c, s.m, 20000000ull); waited = plat_time_ns() - t1; } while (!to && waited < 20000000ull);
    int count = s.count; plat_mutex_unlock(s.m);
    check(started == N && !timed_out && count == N * 100000 && to == 1 && waited >= 19000000ull, "threads and locks",
          "%d threads x 100000 locked increments = %d in %.1f ms; a 20 ms timed wait timed out after %.1f ms", started, count, (double)(t1 - t) / 1e6, (double)waited / 1e6);
    if (!timed_out) { plat_cond_free(s.c); plat_mutex_free(s.m); }       /* a stuck worker may still use them */
}

/* ---------------------------------------------------------------- the game thread */
/* The SDL2 backend's entry (backends/sdl2/plat_sdl2.c: SDL set-up, then port_main); absent from null-backend builds. */
extern int plat_sdl2_main(int argc, char **argv) __attribute__((weak));
typedef struct { int argc; char **argv; int ret, crash_test; } GameArgs;
static void *game_thread(void *arg) {
    GameArgs *g = arg; StackInfo s; stack_info(&s); pthread_setname_np(pthread_self(), "ss2-game");
    alog(PLAT_LOG_INFO, "game thread: stack %zu KB + guard %zu KB at %p-%p", s.size >> 10, s.guard >> 10, (void *)s.lo, (void *)s.hi);
    if (g->crash_test) { alog(PLAT_LOG_WARN, "crash test: writing to address 0x10"); *(volatile int *)(uintptr_t)0x10 = 1; }
    g->ret = plat_sdl2_main ? plat_sdl2_main(g->argc, g->argv) : port_main(g->argc, g->argv);
    return NULL;
}

EXPORT int SDL_main(int argc, char **argv) {
    t_start = plat_time_ns();
    if (argc < 3) { __android_log_print(ANDROID_LOG_ERROR, TAG, "SDL_main: expected <run dir> <port arguments...>, got %d arguments", argc - 1); exit(64); }
    const char *dir = argv[1];
    char path[1024];
    snprintf(path, sizeof path, "%s/run.log", dir); log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    snprintf(path, sizeof path, "%s/status", dir); status_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0 || status_fd < 0 || chdir(dir)) { __android_log_print(ANDROID_LOG_ERROR, TAG, "SDL_main: cannot use run directory %s: %s", dir, strerror(errno)); exit(65); }
    if (log_fd >= 0) { dup2(log_fd, 1); dup2(log_fd, 2); }
    catch_fatal_signals();
    char b[64]; snprintf(b, sizeof b, "start %d\n", getpid()); status_line(b);
    { char l[600]; int n = snprintf(l, sizeof l, "native entry SDL_main; run directory %s; port arguments:", dir);
      for (int i = 2; i < argc && n < (int)sizeof l - 2; i++) n += snprintf(l + n, sizeof l - (size_t)n, " %s", argv[i]);
      alog(PLAT_LOG_INFO, "%s", l); }

    check_process(); check_reservation(); check_stack(); check_sync();
    alog(n_failed ? PLAT_LOG_ERROR : PLAT_LOG_INFO, "preflight: %d of %d checks failed", n_failed, n_checks);

    static char *av[64]; int ac = 0, crash_test = 0; av[ac++] = "ss2port";
    for (int i = 2; i < argc && ac < 63; i++)
        if (!strcmp(argv[i], "--android-crash-test")) crash_test = 1;
        else if (!strcmp(argv[i], "--android-env") && i + 1 < argc) {       /* NAME=value, e.g. SS2PORT_WRITE_DIR=... */
            char *v = argv[++i], *eq = strchr(v, '=');
            if (eq) { *eq = 0; setenv(v, eq + 1, 1); alog(PLAT_LOG_INFO, "environment %s=%s", v, eq + 1); *eq = '='; }
        } else av[ac++] = argv[i];
    av[ac] = NULL;
    if (plat_sdl2_main) {               /* the game's window: OpenGL ES; the GL context stays on the game thread, which also pumps SDL's events */
        setenv("SS2PORT_GLES", "1", 0); unsetenv("SS2PORT_RENDER_THREAD");
        setenv("SS2PORT_FULLSCREEN", "1", 0); setenv("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight", 0);   /* SDL_HINT_ORIENTATIONS */
        setenv("SDL_ANDROID_TRAP_BACK_BUTTON", "1", 0);      /* Back (or a keyboard's Esc mapped to Back) must not finish the game's activity */
        alog(PLAT_LOG_INFO, "backend sdl2: OpenGL ES, fullscreen landscape, renderer on the game thread");
    }
    GameArgs g = { ac, av, 0, crash_test }; pthread_attr_t a = game_attr(); pthread_t t;
    alog(PLAT_LOG_INFO, "starting port_main after %.3f s", seconds());
    int e = pthread_create(&t, &a, game_thread, &g); pthread_attr_destroy(&a);
    if (e) { alog(PLAT_LOG_ERROR, "could not start the game thread: %s", strerror(e)); exit(66); }
    pthread_join(t, NULL);
    alog(PLAT_LOG_INFO, "port_main returned %d", g.ret);
    exit(g.ret);
}
