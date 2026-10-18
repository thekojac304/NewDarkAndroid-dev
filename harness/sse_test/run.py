#!/usr/bin/env python3
"""harness/sse_test/run.py: differential test of lift_sse.py (and cmov/bsf/bsr/xadd/cmpxchg) against the real CPU.

    python harness/sse_test/run.py [--trials N] [--keep DIR]

Needs an x86 or x86-64 Windows PC (the test program is 32-bit x86 and runs the instructions natively) and Zig
(python -m pip install ziglang). Every instruction form below is assembled once into a stub

    push dword [ebp]; popfd; <instruction>; pushfd; pop dword [ebp+4]; ret

(position independent: EBP points at the flags in/out words), the stubs are lifted with lift.py into C, and one program
runs each stub natively and as recompiled C from the same random states (general registers, flags, the eight XMM
registers, MXCSR rounding mode, and a scratch buffer that ESI/EDI point into), then compares registers, flags (the ones
the instruction defines), XMM registers and memory. The lifted side is built with -mfpmath=sse, so its float maths is
IEEE single/double as on the arm64 and x86-64 hosts the port runs on.
"""
import argparse, os, random, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, ROOT)
import lift

STUB = 32
BASE = 0x00400000          # where the lifter is told the stubs live (the lifted code never refers to it)

X = [f'xmm{k}' for k in range(8)]
# memory operands: ESI and EDI point into the 4 KB scratch buffer, 16-byte aligned (movaps/movdqa need that)
M32, M64, M128 = 'dword ptr [esi + 16]', 'qword ptr [edi + 8]', 'xmmword ptr [esi + 32]'

def forms():
    F = []
    def both(m, mem):                                   # xmm, xmm and xmm, mem
        F.extend([f'{m} xmm1, xmm2', f'{m} xmm3, xmm3', f'{m} xmm0, {mem}'])
    for m in ('addss', 'subss', 'mulss', 'divss', 'minss', 'maxss', 'sqrtss'): both(m, M32)
    for m in ('addsd', 'subsd', 'mulsd', 'divsd', 'minsd', 'maxsd', 'sqrtsd'): both(m, M64)
    for m in ('addps', 'subps', 'mulps', 'divps', 'minps', 'maxps', 'sqrtps', 'addpd', 'subpd', 'mulpd', 'divpd', 'minpd',
              'maxpd', 'sqrtpd', 'andps', 'andnps', 'orps', 'xorps', 'andpd', 'andnpd', 'orpd', 'xorpd', 'pand', 'pandn', 'por',
              'pxor', 'unpcklps', 'unpckhps', 'unpcklpd', 'unpckhpd', 'punpcklqdq', 'punpckhqdq', 'punpckldq', 'punpckhdq',
              'punpcklwd', 'punpcklbw', 'paddb', 'paddw', 'paddd', 'paddq', 'psubb', 'psubw', 'psubd', 'psubq', 'pcmpeqb',
              'pcmpeqw', 'pcmpeqd', 'pcmpgtb', 'pcmpgtw', 'pcmpgtd', 'pmaxsw', 'pminsw', 'pmaxub', 'pminub', 'cvtdq2ps',
              'cvtpd2ps', 'cvttps2dq', 'cvtps2dq', 'cvttpd2dq', 'cvtpd2dq'): both(m, M128)
    for m in ('cvtps2pd', 'cvtdq2pd'): both(m, M64)
    for m in ('comiss', 'ucomiss'): F.extend([f'{m} xmm1, xmm2', f'{m} xmm0, {M32}'])
    for m in ('comisd', 'ucomisd'): F.extend([f'{m} xmm1, xmm2', f'{m} xmm0, {M64}'])
    for p in range(8):
        F.extend([f'cmpss xmm1, xmm2, {p}', f'cmpsd xmm1, {M64}, {p}', f'cmpps xmm4, xmm5, {p}', f'cmppd xmm6, {M128}, {p}'])
    for m, mem in (('cvttss2si', M32), ('cvtss2si', M32)): F.extend([f'{m} eax, xmm1', f'{m} ecx, {mem}'])
    for m, mem in (('cvttsd2si', M64), ('cvtsd2si', M64)): F.extend([f'{m} edx, xmm2', f'{m} ebx, {mem}'])
    F.extend(['cvtsi2ss xmm1, eax', f'cvtsi2ss xmm2, {M32}', 'cvtsi2sd xmm3, ecx', f'cvtsi2sd xmm4, {M32}',
              'cvtss2sd xmm1, xmm2', f'cvtss2sd xmm0, {M32}', 'cvtsd2ss xmm1, xmm2', f'cvtsd2ss xmm0, {M64}'])
    F.extend(['movss xmm1, xmm2', f'movss xmm0, {M32}', f'movss {M32}, xmm5', 'movsd xmm1, xmm2', f'movsd xmm0, {M64}',
              f'movsd {M64}, xmm5'])
    for m in ('movaps', 'movups', 'movapd', 'movupd', 'movdqa', 'movdqu'):
        F.extend([f'{m} xmm1, xmm2', f'{m} xmm0, {M128}', f'{m} {M128}, xmm7'])
    F.extend(['movd xmm1, eax', f'movd xmm2, {M32}', 'movd ecx, xmm3', f'movd {M32}, xmm4', 'movq xmm1, xmm2',
              f'movq xmm0, {M64}', f'movq {M64}, xmm6', f'movlpd xmm1, {M64}', f'movlpd {M64}, xmm2', f'movhpd xmm1, {M64}',
              f'movhpd {M64}, xmm2', f'movlps xmm3, {M64}', f'movhps {M64}, xmm3', 'movlhps xmm1, xmm2', 'movhlps xmm1, xmm2',
              'movmskps eax, xmm1', 'movmskpd ecx, xmm2', 'pmovmskb edx, xmm3'])
    for imm in (0x1B, 0xE4, 0x4E, 0x00, 0xFF):
        F.extend([f'pshufd xmm1, xmm2, {imm}', f'pshuflw xmm1, xmm2, {imm}', f'pshufhw xmm3, {M128}, {imm}',
                  f'shufps xmm1, xmm2, {imm}', f'shufps xmm3, xmm3, {imm}'])
    F.extend(['shufpd xmm1, xmm2, 1', 'shufpd xmm1, xmm2, 2', 'pextrw eax, xmm1, 3', 'pextrw ecx, xmm2, 7',
              'pinsrw xmm1, eax, 2', f'pinsrw xmm2, word ptr [esi + 16], 5'])
    for m in ('psllw', 'psrlw', 'psraw', 'pslld', 'psrld', 'psrad', 'psllq', 'psrlq'):
        F.extend([f'{m} xmm1, 3', f'{m} xmm2, 40', f'{m} xmm3, xmm4'])
    F.extend(['psrldq xmm1, 3', 'psrldq xmm2, 15', 'pslldq xmm3, 5', 'pslldq xmm4, 16'])
    F.extend([f'stmxcsr {M32}'])
    # integer instructions added with lift_sse.py
    for cc in ('o', 'no', 'b', 'ae', 'e', 'ne', 'be', 'a', 's', 'ns', 'p', 'np', 'l', 'ge', 'le', 'g'):
        F.extend([f'cmov{cc} eax, ecx', f'cmov{cc} edx, dword ptr [esi + 16]'])
    F.extend(['cmove ax, cx', 'bsf eax, ecx', 'bsr edx, ebx', 'bsf ax, cx', 'bsr dx, bx', f'bsr eax, dword ptr [esi + 16]',
              'xadd dword ptr [esi + 16], eax', 'xadd ecx, edx', 'xadd eax, eax', 'lock xadd dword ptr [esi + 16], ecx',
              'xadd word ptr [esi + 16], ax', 'xadd byte ptr [esi + 16], cl',
              'cmpxchg dword ptr [esi + 16], ecx', 'lock cmpxchg dword ptr [esi + 16], edx', 'cmpxchg ebx, ecx',
              'cmpxchg word ptr [esi + 16], cx', 'cmpxchg byte ptr [esi + 16], dl'])
    return F


def coff_section(path, name):
    d = open(path, 'rb').read()
    nsec, opt = struct.unpack_from('<H', d, 2)[0], struct.unpack_from('<H', d, 16)[0]
    for k in range(nsec):
        h = 20 + opt + 40 * k
        if d[h:h + 8].rstrip(b'\0') == name:
            size, ptr = struct.unpack_from('<II', d, h + 16)
            return d[ptr:ptr + size]
    sys.exit(f'{name} not in {path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--trials', type=int, default=400); ap.add_argument('--keep')
    a = ap.parse_args()
    work = a.keep or tempfile.mkdtemp(prefix='sse_test')
    os.makedirs(work, exist_ok=True)
    zig = [sys.executable, '-m', 'ziglang', 'cc', '-target', 'x86-windows-gnu']
    F = forms()
    s = ['.intel_syntax noprefix', '.section .tst,"xr"', '.globl _tst_base', '.p2align 5', '_tst_base:']
    for k, f in enumerate(F):
        s += [f'.p2align 5', f't{k}:', '    push dword ptr [ebp]', '    popfd', f'    {f}', '    pushfd',
              '    pop dword ptr [ebp + 4]', '    ret']
    open(os.path.join(work, 'stubs.s'), 'w').write('\n'.join(s) + '\n')
    obj = os.path.join(work, 'stubs.o')
    subprocess.run(zig + ['-c', os.path.join(work, 'stubs.s'), '-o', obj], check=True)
    blob = coff_section(obj, b'.tst')
    if not STUB * (len(F) - 1) < len(blob) <= STUB * len(F): sys.exit('stubs did not assemble to the expected layout')
    for k in range(len(F)):                              # each stub must fit its 32 bytes
        st = blob[STUB * k:STUB * (k + 1)]
        if st.rstrip(b'\x00\x90\xcc')[-1:] != b'\xc3' and b'\xc3' not in st: sys.exit(f'stub {k} ({F[k]}) does not fit')
    gen = os.path.join(work, 'gen'); shutil.rmtree(gen, ignore_errors=True); os.makedirs(gen)
    img = lift.SynthImage(BASE, blob, [BASE + STUB * k for k in range(len(F))])
    meta = lift.Lifter(None, 'sx', img=img).run(gen, per_file=400)
    if meta['unimpl']: print('lifter could not translate:', meta['unimpl'])
    with open(os.path.join(work, 'forms.h'), 'w') as f:
        f.write(f'#define NFORMS {len(F)}\nstatic const char *const form_name[] = {{\n')
        f.write(''.join('    "' + x + '",\n' for x in F) + '};\n')
    exe = os.path.join(work, 'sse_test.exe')
    srcs = [os.path.join(HERE, 'driver.c'), obj] + [os.path.join(gen, x) for x in sorted(os.listdir(gen)) if x.endswith('.c')]
    subprocess.run(zig + ['-O1', '-msse2', '-mfpmath=sse', '-DRT_IDENTITY', f'-I{os.path.join(ROOT, "runtime")}', f'-I{gen}', f'-I{work}',
                          '-Wno-unused', '-Wno-parentheses-equality', '-o', exe] + srcs, check=True)
    r = subprocess.run([exe, str(a.trials)])
    if not a.keep: shutil.rmtree(work, ignore_errors=True)
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
