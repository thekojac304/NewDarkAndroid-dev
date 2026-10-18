/* harness/sse_test/driver.c: runs every stub natively and as lifted C from the same states and compares (run.py). 32-bit
 * x86 Windows only: the lifted code uses guest address == host address (RT_IDENTITY). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include "rt.h"
#include "forms.h"

uint8_t *M;
#define BASE 0x00400000u
#define STUB 32
guest_fn sx_lookup(uint32_t va);
extern char tst_base[];

typedef struct {                                 /* the native trampoline's layout: keep the offsets in step with it */
    uint32_t r[8];                               /*   0: eax ecx edx ebx esp ebp esi edi (esp, ebp not used) */
    uint32_t mxcsr, pad[3];                      /*  32 */
    XMM x[8];                                    /*  48 */
    uint32_t io[2];                              /* 176: flags in, flags out (EBP points here) */
} St;
void run_native(void *fn, St *s);
__asm__(
    ".intel_syntax noprefix\n"
    ".text\n.globl _run_native\n_run_native:\n"
    "  push ebp\n  push ebx\n  push esi\n  push edi\n"
    "  mov eax, [esp + 20]\n  mov ecx, [esp + 24]\n  mov [_g_fn], eax\n  mov [_g_st], ecx\n"
    "  ldmxcsr [ecx + 32]\n"
    "  movups xmm0, [ecx + 48]\n  movups xmm1, [ecx + 64]\n  movups xmm2, [ecx + 80]\n  movups xmm3, [ecx + 96]\n"
    "  movups xmm4, [ecx + 112]\n  movups xmm5, [ecx + 128]\n  movups xmm6, [ecx + 144]\n  movups xmm7, [ecx + 160]\n"
    "  mov eax, [ecx]\n  mov edx, [ecx + 8]\n  mov ebx, [ecx + 12]\n  mov esi, [ecx + 24]\n  mov edi, [ecx + 28]\n"
    "  lea ebp, [ecx + 176]\n  mov ecx, [ecx + 4]\n"
    "  call dword ptr [_g_fn]\n"
    "  push ecx\n  mov ecx, [_g_st]\n  pop dword ptr [ecx + 4]\n"
    "  mov [ecx], eax\n  mov [ecx + 8], edx\n  mov [ecx + 12], ebx\n  mov [ecx + 24], esi\n  mov [ecx + 28], edi\n"
    "  movups [ecx + 48], xmm0\n  movups [ecx + 64], xmm1\n  movups [ecx + 80], xmm2\n  movups [ecx + 96], xmm3\n"
    "  movups [ecx + 112], xmm4\n  movups [ecx + 128], xmm5\n  movups [ecx + 144], xmm6\n  movups [ecx + 160], xmm7\n"
    "  stmxcsr [ecx + 32]\n"
    "  mov dword ptr [_g_mx_cleared], 0x1F80\n  ldmxcsr [_g_mx_cleared]\n"
    "  pop edi\n  pop esi\n  pop ebx\n  pop ebp\n  ret\n"
    ".att_syntax prefix\n");
void *g_fn; St *g_st; uint32_t g_mx_cleared;

static jmp_buf jb; static int fault_kind;
void rt_call_import(CPU *c, int idx) { (void)c; (void)idx; fault_kind = 100; longjmp(jb, 1); }
void rt_call_external(CPU *c, uint32_t t) { (void)c; (void)t; fault_kind = 101; longjmp(jb, 1); }
void rt_fault(CPU *c, uint32_t a, int kind) { (void)c; (void)a; fault_kind = kind; longjmp(jb, 1); }
void rt_budget_exhausted(CPU *c) { (void)c; fault_kind = 102; longjmp(jb, 1); }
static void *sj_dummy[8];
void **rt_sj_begin(CPU *c, uint32_t buf) { (void)c; (void)buf; return sj_dummy; }
void rt_sj_resume(CPU *c, uint32_t buf) { (void)c; (void)buf; }
void rt_longjmp(CPU *c) { (void)c; fault_kind = 103; longjmp(jb, 1); }

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }
static uint32_t rf32(void) {                      /* a float lane: specials, rounding cases, ordinary values, raw bits */
    static const float sp[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 1.5f, 2.5f, -2.5f, 3.5f, -0.5f, 1e-40f, -1e-40f, 3.4e38f,
                                2147483648.0f, -2147483648.0f, 2147483520.0f, 1e10f, -1e10f, 65535.5f, 100.0f };
    uint32_t k = rnd() % 10; float f;
    if (k < 3) f = sp[rnd() % (sizeof sp / sizeof sp[0])];
    else if (k == 3) { uint32_t v = rnd() % 4; uint32_t b = v == 0 ? 0x7F800000u : v == 1 ? 0xFF800000u : v == 2 ? 0x7FC00000u : 0xFFC12345u; return b; }
    else if (k < 7) f = (float)((int32_t)(rnd() % 2000001) - 1000000) / (float)(1 + rnd() % 1000);
    else if (k < 9) f = (float)((int32_t)rnd()) * ldexpf(1.0f, (int)(rnd() % 40) - 60);
    else return rnd();
    uint32_t v; memcpy(&v, &f, 4); return v;
}
static uint64_t rf64(void) {
    static const double sp[] = { 0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, -2.5, 1e-310, 1.7e308, 2147483647.5, 2147483648.0,
                                 -2147483648.5, -2147483649.0, 1e300, 4503599627370496.5, 0.1, 100.0 };
    uint32_t k = rnd() % 10; double d;
    if (k < 3) d = sp[rnd() % (sizeof sp / sizeof sp[0])];
    else if (k == 3) { uint32_t v = rnd() % 3; return v == 0 ? 0x7FF0000000000000ull : v == 1 ? 0xFFF8000000000000ull : 0x7FF8000012345678ull; }
    else if (k < 7) d = (double)((int32_t)(rnd() % 2000001) - 1000000) / (double)(1 + rnd() % 1000);
    else if (k < 9) d = (double)(int32_t)rnd() * ldexp(1.0, (int)(rnd() % 80) - 60);
    else return ((uint64_t)rnd() << 32) | rnd();
    uint64_t v; memcpy(&v, &d, 8); return v;
}
static uint32_t rint32(void) {
    static const uint32_t sp[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0x10, 0x40000000 };
    uint32_t k = rnd() % 4; return k == 0 ? sp[rnd() % (sizeof sp / sizeof sp[0])] : k == 1 ? rnd() % 64 : k == 2 ? (uint32_t)-(int32_t)(rnd() % 64) : rnd();
}
static void fill_xmm(XMM *x) { if (rnd() & 1) { for (int k = 0; k < 4; k++) x->d[k] = rf32(); } else { for (int k = 0; k < 2; k++) x->q[k] = rf64(); } }

static int nan32(uint32_t v) { return (v & 0x7F800000u) == 0x7F800000u && (v & 0x7FFFFFu); }
static int nan64(uint64_t v) { return (v & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (v & 0xFFFFFFFFFFFFFull); }
/* registers that differ only where both sides hold a NaN (which NaN propagates when both operands are NaN: the C
   compiler may swap the operands of a commutative add or multiply; on arm64 NaN payloads differ from x86 anyway) */
static int nan_only(const XMM *a, const XMM *b) {
    int ok32 = 1, ok64 = 1;
    for (int k = 0; k < 4; k++) if (a->d[k] != b->d[k] && !(nan32(a->d[k]) && nan32(b->d[k]))) ok32 = 0;
    for (int k = 0; k < 2; k++) if (a->q[k] != b->q[k] && !(nan64(a->q[k]) && nan64(b->q[k]))) ok64 = 0;
    return ok32 || ok64;
}
static long nan_payload;

static uint8_t scr[4096] __attribute__((aligned(16))), scr_in[4096], scr_native[4096];
static uint32_t fstack[1024];

static uint32_t flag_mask(const char *f) {        /* the flags an instruction defines (AF 0x10 is not modelled) */
    uint32_t all = 0x8D5 & ~0x10u;                /* CF PF ZF SF OF, and DF 0x400 */
    if (strstr(f, "comis")) return 0x8C5 | 0x400;
    if (!strncmp(f, "bsf", 3) || !strncmp(f, "bsr", 3)) return 0x40 | 0x400;
    return all | 0x400;
}

int main(int argc, char **argv) {
    int trials = argc > 1 ? atoi(argv[1]) : 400, bad_forms = 0, unimpl = 0; long checks = 0;
    for (int k = 0; k < NFORMS; k++) {
        int fails = 0;
        guest_fn fn = sx_lookup(BASE + STUB * (uint32_t)k);
        if (!fn) { printf("FAIL %-40s no lifted function\n", form_name[k]); bad_forms++; continue; }
        for (int t = 0; t < trials; t++) {
            St in; memset(&in, 0, sizeof in);
            for (int r = 0; r < 8; r++) in.r[r] = rint32();
            if (strstr(form_name[k], "word ptr") || strstr(form_name[k], "byte ptr") || strstr(form_name[k], "cvtsi2"))
                for (int r = 0; r < 8; r++) if (rnd() & 1) in.r[r] = (uint32_t)(int32_t)(rnd() % 4001) - 2000;
            in.r[6] = (uint32_t)(uintptr_t)(scr + 16 * (rnd() % 64)); in.r[7] = (uint32_t)(uintptr_t)(scr + 1024 + 16 * (rnd() % 64));
            /* MXCSR rounding: only the conversions that follow it are tested with other modes; the float arithmetic of the
               translation always rounds to nearest (MXCSR's default, which NewDark and the script modules keep) */
            const char *fm_ = form_name[k];
            int rc_ok = !strncmp(fm_, "cvtss2si", 8) || !strncmp(fm_, "cvtsd2si", 8) || !strncmp(fm_, "cvtps2dq", 8) || !strncmp(fm_, "cvtpd2dq", 8) || !strncmp(fm_, "stmxcsr", 7);
            in.mxcsr = 0x1F80u | ((rc_ok && rnd() % 2 ? rnd() % 4 : 0) << 13);
            for (int x = 0; x < 8; x++) fill_xmm(&in.x[x]);
            if (rnd() % 4 == 0) in.x[2] = in.x[1];                          /* equal operands: compares, min/max */
            in.io[0] = 0x202u | (rnd() & 0x8D5u);
            for (int b = 0; b < 4096; b += 8) { uint64_t v = (rnd() & 1) ? rf64() : ((uint64_t)rf32() << 32 | rf32()); memcpy(scr_in + b, &v, 8); }
            if (rnd() % 3 == 0) for (int b = 0; b < 4096; b += 4) { uint32_t v = rint32(); memcpy(scr_in + b, &v, 4); }
            /* native */
            St nat = in; memcpy(scr, scr_in, sizeof scr);
            run_native(tst_base + STUB * k, &nat);
            memcpy(scr_native, scr, sizeof scr);
            /* lifted */
            memcpy(scr, scr_in, sizeof scr);
            St lif = in; lif.io[1] = 0;
            static CPU c; memset(&c, 0, sizeof c);
            c.eax = in.r[0]; c.ecx = in.r[1]; c.edx = in.r[2]; c.ebx = in.r[3]; c.esi = in.r[6]; c.edi = in.r[7];
            c.ebp = (uint32_t)(uintptr_t)lif.io; c.esp = (uint32_t)(uintptr_t)&fstack[1000]; c.cw = 0x27F;
            c.mxcsr_x = in.mxcsr ^ 0x1F80u; memcpy(c.xmm, in.x, sizeof c.xmm);
            PUSH32(&c, 0xDEAD0000u);
            if (setjmp(jb)) { if (fault_kind == RT_FAULT_UNIMPL) { unimpl++; fails = trials; break; } printf("FAIL %-40s fault %d\n", form_name[k], fault_kind); fails++; continue; }
            fn(&c);
            uint32_t lr[8] = { c.eax, c.ecx, c.edx, c.ebx, 0, 0, c.esi, c.edi };
            char why[256] = ""; int n = 0;
            for (int r = 0; r < 8; r++) if (r != 4 && r != 5 && lr[r] != nat.r[r]) n += snprintf(why + n, sizeof why - n, " r%d native %08x lifted %08x;", r, nat.r[r], lr[r]);
            uint32_t fm = flag_mask(form_name[k]);
            if ((nat.io[1] ^ lif.io[1]) & fm) n += snprintf(why + n, sizeof why - n, " flags in %03x native %03x lifted %03x;", in.io[0], nat.io[1] & fm, lif.io[1] & fm);
            for (int x = 0; x < 8; x++) if (memcmp(&nat.x[x], &c.xmm[x], 16) && nan_only(&nat.x[x], &c.xmm[x])) { nan_payload++; c.xmm[x] = nat.x[x]; }
            for (int x = 0; x < 8 && n < 200; x++) if (memcmp(&nat.x[x], &c.xmm[x], 16))
                n += snprintf(why + n, sizeof why - n, " xmm%d in %016llx%016llx native %016llx%016llx lifted %016llx%016llx;", x,
                              (unsigned long long)in.x[x].q[1], (unsigned long long)in.x[x].q[0], (unsigned long long)nat.x[x].q[1],
                              (unsigned long long)nat.x[x].q[0], (unsigned long long)c.xmm[x].q[1], (unsigned long long)c.xmm[x].q[0]);
            if (memcmp(scr, scr_native, sizeof scr)) n += snprintf(why + n, sizeof why - n, " memory differs;");
            checks++;
            if (n) { if (fails < 2) printf("FAIL %-40s (mxcsr %04x)%s\n", form_name[k], in.mxcsr, why); fails++; }
        }
        if (fails) bad_forms++;
        if (fails && fault_kind != RT_FAULT_UNIMPL) printf("     %-40s %d of %d trials differ\n", form_name[k], fails, trials);
    }
    printf("sse_test: %d forms, %ld trials compared, %d forms differ%s; %ld registers differed only in which NaN propagated\n",
           NFORMS, checks, bad_forms, unimpl ? " (some not translated)" : "", nan_payload);
    return bad_forms != 0;
}
