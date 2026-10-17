/* port/tests/setjmp/test.c: host test of RT_HOST_SETJMP / RT_HOST_LONGJMP (runtime/rt.h), the jump under every guest
 * setjmp/longjmp, used the way core/host.c uses it. No game files and no lifter.   sh port/tests/setjmp/run.sh
 * Checks: the second return comes after a longjmp from deep below, through frames of varying size that overwrite the
 * callee-saved registers; integer and floating-point locals live across the setjmp (more than the caller-saved
 * registers hold) come back unchanged; the frame and stack pointer come back exactly, 16-byte aligned, with no drift
 * over many jumps; RT_SETJMP restores the guest registers and returns the longjmp value; threads jump independently.
 * Exit status 0 when every check passes. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#define NOINLINE __attribute__((noinline))
static int fails; static long checks;
#define CHECK(x) do { checks++; if (!(x)) { if (fails++ < 20) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct { _Alignas(16) void *hj[RT_HJ_WORDS]; uint32_t buf; int used; } Rec;   /* as core/host.c's SjRec */

static volatile uint64_t vin = 0x9E3779B97F4A7C15ull; static volatile double fin = 1.0 / 3.0; static volatile uintptr_t sink;
NOINLINE static uintptr_t sp_here(void) { _Alignas(16) volatile char probe[16]; probe[0] = 0; volatile uintptr_t p = (uintptr_t)probe; return p; }

/* the longjmp side: recursion through frames of different sizes, each keeping values live across its call (so it
 * saves and reuses the callee-saved registers), then the jump from the bottom */
NOINLINE static void dive(Rec *r, int depth, unsigned salt) {
    volatile char pad[1 + salt % 61]; pad[0] = (char)depth;
    uint64_t a = salt * 3u + (unsigned)depth, b = a ^ vin, c = a * 7, d = b + c, e = d ^ a, f = e * 13, g = f + b, h = g ^ c;
    double x = depth * 1.25 + fin, y = x * 3.5, z = y - x, w = z * z;
    if (depth == 0) RT_HOST_LONGJMP(r->hj);
    dive(r, depth - 1, salt * 1103515245u + 12345u);
    sink = (uintptr_t)(a + b + c + d + e + f + g + h + (uint64_t)pad[0]) + (uintptr_t)(x + y + z + w);
}

/* the setjmp side: values live across the setjmp, compared with copies kept in memory */
NOINLINE static int jump_once(Rec *r, int depth, uint64_t seed) {
    volatile uint64_t ei[10]; volatile double ef[8];
    uint64_t v0 = seed ^ vin, v1 = v0 * 3, v2 = v0 ^ 0xA5A5, v3 = v0 + 77, v4 = v0 * v0, v5 = ~v0, v6 = v0 << 7, v7 = v0 >> 3, v8 = v0 * 11 + 1, v9 = v0 ^ v0 >> 9;
    double f0 = fin * (double)(seed & 1023), f1 = f0 * 2.5, f2 = f0 - 1.0 / 7, f3 = f0 * f0, f4 = -f0, f5 = f0 + 1e-9, f6 = f0 * 0.75, f7 = f0 / 7;
    ei[0] = v0; ei[1] = v1; ei[2] = v2; ei[3] = v3; ei[4] = v4; ei[5] = v5; ei[6] = v6; ei[7] = v7; ei[8] = v8; ei[9] = v9;
    ef[0] = f0; ef[1] = f1; ef[2] = f2; ef[3] = f3; ef[4] = f4; ef[5] = f5; ef[6] = f6; ef[7] = f7;
    void *fa = __builtin_frame_address(0); uintptr_t sp0 = sp_here();
    volatile int stage = 0; r->buf = 0xC0FFEE00u ^ (uint32_t)seed; r->used = depth;      /* RT_HJ_WORDS too small would overwrite these */
    if (RT_HOST_SETJMP(r->hj) == 0) { stage = 1; dive(r, depth, (unsigned)seed); stage = 99; }
    int ok = stage == 1 && __builtin_frame_address(0) == fa && sp_here() == sp0 && sp0 % 16 == 0 && (uintptr_t)fa % 16 == 0;
    ok &= r->buf == (0xC0FFEE00u ^ (uint32_t)seed) && r->used == depth;
    ok &= v0 == ei[0] && v1 == ei[1] && v2 == ei[2] && v3 == ei[3] && v4 == ei[4] && v5 == ei[5] && v6 == ei[6] && v7 == ei[7] && v8 == ei[8] && v9 == ei[9];
    ok &= f0 == ef[0] && f1 == ef[1] && f2 == ef[2] && f3 == ef[3] && f4 == ef[4] && f5 == ef[5] && f6 == ef[6] && f7 == ef[7];
    return ok;
}

static int jump_loop(Rec *r, int n, uint64_t seed) {
    int ok = 1; uintptr_t sp0 = sp_here();
    for (int i = 0; i < n; i++) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; ok &= jump_once(r, (int)(seed >> 58), seed); }
    return ok && sp_here() == sp0;
}

/* RT_SETJMP as generated code uses it, with a guest-register side that follows core/host.c */
uint8_t *M; static uint8_t guest_mem[1 << 16];
enum { BUF = 0x8000, STACK_TOP = 0xF000 };
static Rec grec; static uint32_t g_esp, g_ebp, g_ebx, g_esi, g_edi, g_val; static int g_begins, g_resumes;
void **rt_sj_begin(CPU *c, uint32_t buf) { g_begins++; grec.buf = buf; g_esp = c->esp; g_ebp = c->ebp; g_ebx = c->ebx; g_esi = c->esi; g_edi = c->edi; return grec.hj; }
void rt_sj_resume(CPU *c, uint32_t buf) { g_resumes++; CHECK(buf == grec.buf); c->esp = g_esp; c->ebp = g_ebp; c->ebx = g_ebx; c->esi = g_esi; c->edi = g_edi; c->eax = g_val; }
void rt_longjmp(CPU *c) { uint32_t buf = RD32(c->esp + 4), val = RD32(c->esp + 8); CHECK(buf == grec.buf); g_val = val ? val : 1; RT_HOST_LONGJMP(grec.hj); }

NOINLINE static void guest_callee(CPU *c, int depth, uint32_t val) {   /* a recompiled function nested below: longjmp(BUF, val) */
    c->ebx ^= 0xDEAD0000u + (uint32_t)depth; c->esi += 4; c->edi = ~c->edi; c->ebp = c->esp;
    PUSH32(c, (uint32_t)depth);
    if (depth) { CALLPUSH(c, 0x402000u); guest_callee(c, depth - 1, val); return; }
    PUSH32(c, val); PUSH32(c, BUF); CALLPUSH(c, 0x403000u); rt_longjmp(c);
}
NOINLINE static void guest_caller(CPU *c, int depth, uint32_t val, uint32_t *ret) {   /* if (setjmp(BUF) == 0) callee(); */
    c->ebx = 0x11111111u; c->esi = 0x22222222u; c->edi = 0x33333333u; c->ebp = 0x44444444u;
    uint32_t esp0 = c->esp;
    PUSH32(c, BUF); CALLPUSH(c, 0x401000u); RT_SETJMP(c);
    c->esp += 4;                                                /* the caller pops the argument (cdecl) */
    if (c->eax == 0) { c->ebx = c->esi = c->edi = c->ebp = 0; CALLPUSH(c, 0x401100u); guest_callee(c, depth, val); CHECK(!"longjmp returned"); return; }
    CHECK(c->esp == esp0); CHECK(c->ebx == 0x11111111u && c->esi == 0x22222222u && c->edi == 0x33333333u && c->ebp == 0x44444444u);
    *ret = c->eax;
}
static void test_rt_setjmp(void) {
    M = guest_mem; CPU cpu; memset(&cpu, 0, sizeof cpu); cpu.esp = STACK_TOP;
    for (int i = 0; i < 1000; i++) {
        uint32_t val = (uint32_t)i % 5, ret = 0xFFFFFFFFu;     /* longjmp(buf, 0) makes setjmp return 1 */
        guest_caller(&cpu, i % 40, val, &ret);
        CHECK(ret == (val ? val : 1u)); CHECK(cpu.esp == STACK_TOP);
    }
    CHECK(g_begins == 1000 && g_resumes == 1000);
}

/* threads: each jumps on its own stack with its own buffer */
#define NTHREADS 4
static int thread_ok[NTHREADS];
static void thread_body(int i) { static Rec rec[NTHREADS]; thread_ok[i] = jump_loop(&rec[i], 20000, 1000u + (uint64_t)i); }
#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID p) { thread_body((int)(intptr_t)p); return 0; }
static void test_threads(void) {
    HANDLE h[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) h[i] = CreateThread(NULL, 1 << 20, thread_main, (LPVOID)(intptr_t)i, 0, NULL);
    WaitForMultipleObjects(NTHREADS, h, TRUE, INFINITE);
    for (int i = 0; i < NTHREADS; i++) { CloseHandle(h[i]); CHECK(thread_ok[i]); }
}
#else
static void *thread_main(void *p) { thread_body((int)(intptr_t)p); return NULL; }
static void test_threads(void) {
    pthread_t t[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) CHECK(pthread_create(&t[i], NULL, thread_main, (void *)(intptr_t)i) == 0);
    for (int i = 0; i < NTHREADS; i++) { pthread_join(t[i], NULL); CHECK(thread_ok[i]); }
}
#endif

int main(void) {
#if defined(_WIN64)
    const char *how = "C runtime _setjmp (NULL frame) / longjmp";
#elif defined(__aarch64__) && defined(__clang__)
    const char *how = "C library _setjmp / _longjmp";
#else
    const char *how = "__builtin_setjmp / __builtin_longjmp";
#endif
    static Rec rec;
    CHECK(sizeof rec.hj >= 5 * sizeof(void *) && (uintptr_t)rec.hj % 16 == 0);
    for (int d = 0; d < 64; d++) CHECK(jump_once(&rec, d, 0x1234u + (uint64_t)d));   /* each depth once */
    CHECK(jump_loop(&rec, 200000, 42));                                               /* many jumps, no drift */
    test_rt_setjmp();
    test_threads();
    printf("setjmp (%s, RT_HJ_WORDS=%d): %ld checks, %s\n", how, (int)RT_HJ_WORDS, checks, fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
