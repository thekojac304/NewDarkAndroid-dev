"""lift_sse.py: SSE/SSE2 instructions for lift.py.

Script modules built with MSVC 2012 or later (for example NVScript.osm of the System Shock 2 Community Patch, MSVC 2017)
use SSE/SSE2 for float and double arithmetic, because /arch:SSE2 is the compiler's default for 32-bit code. The engine
and the modules lifted so far (MSVC 6 to 2008) use the x87 unit only. This file translates the subset such code uses:
scalar and packed float/double arithmetic, moves, conversions, compares, logic, shuffles and the SSE2 integer operations
the CRT's string routines use. The registers live in the CPU struct (runtime/rt.h, c->xmm[0..7], c->mxcsr_x).

Instructions not listed here raise Unsupported, which lift.py turns into rt_fault(RT_FAULT_UNIMPL) as before: SSE3 and
later (the CRT only takes those paths when CPUID reports them, and rt_cpuid reports no SSE at all), AVX, and MXCSR
exception flags (results are IEEE, but no exception status is recorded).
"""
import re
from capstone.x86 import X86_OP_REG, X86_OP_MEM, X86_OP_IMM

SSE_ONLY = {'stmxcsr', 'ldmxcsr', 'cvttss2si', 'cvtss2si', 'cvttsd2si', 'cvtsd2si'}   # no xmm operand with a memory source


def is_sse(i, m):
    return m in SSE_ONLY or any(o.type == X86_OP_REG and i.reg_name(o.reg).startswith('xmm') for o in i.operands)


CMP_PRED = {'eq': 0, 'lt': 1, 'le': 2, 'unord': 3, 'neq': 4, 'nlt': 5, 'nle': 6, 'ord': 7}
F_OPS = {'add': '+', 'sub': '-', 'mul': '*', 'div': '/'}


def lift(F, i, m, Unsupported):
    """C statements for SSE instruction i (mnemonic m without lock prefix). ea is already computed for a memory operand."""
    ops = i.operands
    def xn(op): return int(i.reg_name(op.reg)[3:])
    def X(op): return f'c->xmm[{xn(op)}]'
    def isx(op): return op.type == X86_OP_REG and i.reg_name(op.reg).startswith('xmm')
    def s128(op):                                       # a 128-bit source, copied (the destination may be the same register)
        return f'XMM s = {X(op)};' if isx(op) else 'XMM s = XLD128(ea);'
    def s64lo(op):                                      # the low 64 bits of a source (xmm or m64), upper half zero
        return f'XMM s = {X(op)};' if isx(op) else 'XMM s; s.q[0] = RD64(ea); s.q[1] = 0;'
    def f32(op): return f'{X(op)}.f[0]' if isx(op) else 'XF32(RD32(ea))'
    def f64(op): return f'{X(op)}.g[0]' if isx(op) else 'XF64(RD64(ea))'
    def gp(op): return F.rd(i, op, 4)
    d = ops[0] if ops else None
    s = ops[1] if len(ops) > 1 else None
    imm = next((o.imm for o in ops if o.type == X86_OP_IMM), None)

    # ---------------------------------------------------------------- MXCSR
    if m == 'stmxcsr': return ['WR32(ea, c->mxcsr_x ^ 0x1F80u);']
    if m == 'ldmxcsr': return ['c->mxcsr_x = RD32(ea) ^ 0x1F80u;']

    # ---------------------------------------------------------------- moves
    if m == 'movss':
        if isx(d) and isx(s): return [f'{X(d)}.d[0] = {X(s)}.d[0];']
        if isx(d): return [f'{X(d)}.d[0] = RD32(ea); {X(d)}.d[1] = {X(d)}.d[2] = {X(d)}.d[3] = 0;']
        return [f'WR32(ea, {X(s)}.d[0]);']
    if m == 'movsd':
        if isx(d) and isx(s): return [f'{X(d)}.q[0] = {X(s)}.q[0];']
        if isx(d): return [f'{X(d)}.q[0] = RD64(ea); {X(d)}.q[1] = 0;']
        return [f'WR64(ea, {X(s)}.q[0]);']
    if m in ('movaps', 'movups', 'movapd', 'movupd', 'movdqa', 'movdqu', 'movntps', 'movntpd', 'movntdq', 'lddqu'):
        if isx(d) and isx(s): return [f'{X(d)} = {X(s)};']
        if isx(d): return [f'{X(d)} = XLD128(ea);']
        return [f'XST128(ea, {X(s)});']
    if m == 'movd':
        if isx(d): return [f'{X(d)}.d[0] = {gp(s)}; {X(d)}.d[1] = {X(d)}.d[2] = {X(d)}.d[3] = 0;']
        return [F.wr(i, d, f'{X(s)}.d[0]')]
    if m in ('movq', 'movq2dq'):
        if isx(d) and isx(s): return [f'{X(d)}.q[0] = {X(s)}.q[0]; {X(d)}.q[1] = 0;']
        if isx(d) and s.type == X86_OP_MEM: return [f'{X(d)}.q[0] = RD64(ea); {X(d)}.q[1] = 0;']
        if d.type == X86_OP_MEM and isx(s): return [f'WR64(ea, {X(s)}.q[0]);']
        raise Unsupported(m)
    if m in ('movlpd', 'movlps', 'movhpd', 'movhps'):
        h = 1 if m.startswith('movh') else 0
        if isx(d) and s.type == X86_OP_MEM: return [f'{X(d)}.q[{h}] = RD64(ea);']
        if d.type == X86_OP_MEM and isx(s): return [f'WR64(ea, {X(s)}.q[{h}]);']
        raise Unsupported(m)
    if m == 'movlhps': return [f'{X(d)}.q[1] = {X(s)}.q[0];']
    if m == 'movhlps': return [f'{X(d)}.q[0] = {X(s)}.q[1];']
    if m in ('movmskps', 'movmskpd'):
        n = 4 if m == 'movmskps' else 2
        v = ' | '.join(f'(({X(s)}.{"d" if n == 4 else "q"}[{k}] >> {31 if n == 4 else 63}) & 1u) << {k}' for k in range(n))
        return [F.reg_wr(i.reg_name(d.reg), f'(uint32_t)({v})')]
    if m == 'pmovmskb':
        return [f'uint32_t r = 0; for (int k = 0; k < 16; k++) r |= (uint32_t)({X(s)}.b[k] >> 7) << k;',
                F.reg_wr(i.reg_name(d.reg), 'r')]

    # ---------------------------------------------------------------- float arithmetic
    mm = re.fullmatch(r'(add|sub|mul|div|min|max|sqrt)(ss|sd|ps|pd)', m)
    if mm:
        op, ty = mm.groups()
        lane, n = ('f', 1 if ty == 'ss' else 4) if ty in ('ss', 'ps') else ('g', 1 if ty == 'sd' else 2)
        def comb(a, b):
            if op in F_OPS: return f'{a} {F_OPS[op]} {b}'
            if op == 'min': return f'{a} < {b} ? {a} : {b}'             # either NaN, or both zero: the source, as SSE does
            if op == 'max': return f'{a} > {b} ? {a} : {b}'
            return f'sqrtf({b})' if lane == 'f' else f'sqrt({b})'
        T = 'float' if lane == 'f' else 'double'
        if n == 1:
            src = f32(s) if lane == 'f' else f64(s)
            return [f'{T} a = {X(d)}.{lane}[0], b = {src};', f'{X(d)}.{lane}[0] = {comb("a", "b")};']
        L = [s128(s)]
        for k in range(n):
            L.append(f'{{ {T} a = {X(d)}.{lane}[{k}], b = s.{lane}[{k}]; {X(d)}.{lane}[{k}] = {comb("a", "b")}; }}')
        return L

    # ---------------------------------------------------------------- logic (bitwise on the whole register)
    mm = re.fullmatch(r'(and|andn|or|xor)(ps|pd)|p(and|andn|or|xor)', m)
    if mm:
        op = mm.group(1) or mm.group(3)
        if op == 'xor' and isx(s) and xn(s) == xn(d): return [f'{X(d)}.q[0] = {X(d)}.q[1] = 0;']
        e = {'and': '{a} & {b}', 'andn': '~{a} & {b}', 'or': '{a} | {b}', 'xor': '{a} ^ {b}'}[op]
        return [s128(s)] + [f'{X(d)}.q[{k}] = ' + e.format(a=f'{X(d)}.q[{k}]', b=f's.q[{k}]') + ';' for k in (0, 1)]

    # ---------------------------------------------------------------- compares
    if m in ('comiss', 'ucomiss'): return [f'SSE_COMI({X(d)}.f[0], {f32(s)});']
    if m in ('comisd', 'ucomisd'): return [f'SSE_COMI({X(d)}.g[0], {f64(s)});']
    mm = re.fullmatch(r'cmp(eq|lt|le|unord|neq|nlt|nle|ord)?(ss|sd|ps|pd)', m)
    if mm:
        p = CMP_PRED[mm.group(1)] if mm.group(1) else (imm & 7 if imm is not None else None)
        if p is None or (imm is not None and imm > 7): raise Unsupported(m)
        ty = mm.group(2)
        if ty == 'ss': return [f'{X(d)}.d[0] = SSE_CMP({X(d)}.f[0], {f32(s)}, {p}) ? 0xFFFFFFFFu : 0u;']
        if ty == 'sd': return [f'{X(d)}.q[0] = SSE_CMP({X(d)}.g[0], {f64(s)}, {p}) ? ~0ull : 0ull;']
        if ty == 'ps': return [s128(s)] + [f'{X(d)}.d[{k}] = SSE_CMP({X(d)}.f[{k}], s.f[{k}], {p}) ? 0xFFFFFFFFu : 0u;' for k in range(4)]
        return [s128(s)] + [f'{X(d)}.q[{k}] = SSE_CMP({X(d)}.g[{k}], s.g[{k}], {p}) ? ~0ull : 0ull;' for k in range(2)]

    # ---------------------------------------------------------------- conversions
    if m in ('cvttss2si', 'cvtss2si', 'cvttsd2si', 'cvtsd2si'):
        v = f32(s) if 'ss' in m else f64(s)
        e = f'SSE_CVTT32((double){v})' if m.startswith('cvtt') else f'SSE_CVT32(c, (double){v})'
        return [F.reg_wr(i.reg_name(d.reg), e)]
    if m == 'cvtsi2ss': return [f'{X(d)}.f[0] = (float)(int32_t){gp(s)};']
    if m == 'cvtsi2sd': return [f'{X(d)}.g[0] = (double)(int32_t){gp(s)};']
    if m == 'cvtss2sd': return [f'float v = {f32(s)};', f'{X(d)}.g[0] = (double)v;']
    if m == 'cvtsd2ss': return [f'double v = {f64(s)};', f'{X(d)}.f[0] = (float)v;']
    if m == 'cvtps2pd': return [s64lo(s), f'{X(d)}.g[0] = (double)s.f[0]; {X(d)}.g[1] = (double)s.f[1];']
    if m == 'cvtpd2ps': return [s128(s), f'{X(d)}.f[0] = (float)s.g[0]; {X(d)}.f[1] = (float)s.g[1]; {X(d)}.q[1] = 0;']
    if m == 'cvtdq2ps': return [s128(s)] + [f'{X(d)}.f[{k}] = (float)s.sd[{k}];' for k in range(4)]
    if m == 'cvtdq2pd': return [s64lo(s), f'{X(d)}.g[0] = (double)s.sd[0]; {X(d)}.g[1] = (double)s.sd[1];']
    if m in ('cvttps2dq', 'cvtps2dq'):
        f = 'SSE_CVTT32((double)s.f[{k}])' if m.startswith('cvtt') else 'SSE_CVT32(c, (double)s.f[{k}])'
        return [s128(s)] + [f'{X(d)}.d[{k}] = ' + f.format(k=k) + ';' for k in range(4)]
    if m in ('cvttpd2dq', 'cvtpd2dq'):
        f = 'SSE_CVTT32(s.g[{k}])' if m.startswith('cvtt') else 'SSE_CVT32(c, s.g[{k}])'
        return [s128(s)] + [f'{X(d)}.d[{k}] = ' + f.format(k=k) + ';' for k in range(2)] + [f'{X(d)}.q[1] = 0;']

    # ---------------------------------------------------------------- shuffles and unpacks
    def rebuild(parts, field):                          # the destination from lanes of the old destination (o) and s
        return ['XMM o = ' + X(d) + ';'] + [f'{X(d)}.{field}[{k}] = {p};' for k, p in enumerate(parts)]
    if m == 'unpcklps': return [s128(s)] + rebuild(['o.d[0]', 's.d[0]', 'o.d[1]', 's.d[1]'], 'd')
    if m == 'unpckhps': return [s128(s)] + rebuild(['o.d[2]', 's.d[2]', 'o.d[3]', 's.d[3]'], 'd')
    if m in ('unpcklpd', 'punpcklqdq'): return [s128(s)] + rebuild(['o.q[0]', 's.q[0]'], 'q')
    if m in ('unpckhpd', 'punpckhqdq'): return [s128(s)] + rebuild(['o.q[1]', 's.q[1]'], 'q')
    if m == 'punpckldq': return [s128(s)] + rebuild(['o.d[0]', 's.d[0]', 'o.d[1]', 's.d[1]'], 'd')
    if m == 'punpckhdq': return [s128(s)] + rebuild(['o.d[2]', 's.d[2]', 'o.d[3]', 's.d[3]'], 'd')
    if m == 'punpcklwd': return [s128(s)] + rebuild([f'{"os"[k & 1]}.w[{k >> 1}]' for k in range(8)], 'w')
    if m == 'punpcklbw': return [s128(s)] + rebuild([f'{"os"[k & 1]}.b[{k >> 1}]' for k in range(16)], 'b')
    if m == 'shufps':
        return [s128(s)] + rebuild([f'o.d[{imm & 3}]', f'o.d[{(imm >> 2) & 3}]', f's.d[{(imm >> 4) & 3}]', f's.d[{(imm >> 6) & 3}]'], 'd')
    if m == 'shufpd': return [s128(s)] + rebuild([f'o.q[{imm & 1}]', f's.q[{(imm >> 1) & 1}]'], 'q')
    if m == 'pshufd': return [s128(s)] + [f'{X(d)}.d[{k}] = s.d[{(imm >> 2 * k) & 3}];' for k in range(4)]
    if m == 'pshuflw': return [s128(s)] + [f'{X(d)}.w[{k}] = s.w[{(imm >> 2 * k) & 3}];' for k in range(4)] + [f'{X(d)}.q[1] = s.q[1];']
    if m == 'pshufhw': return [s128(s), f'{X(d)}.q[0] = s.q[0];'] + [f'{X(d)}.w[{4 + k}] = s.w[{4 + ((imm >> 2 * k) & 3)}];' for k in range(4)]
    if m == 'pextrw': return [F.reg_wr(i.reg_name(d.reg), f'(uint32_t){X(s)}.w[{imm & 7}]')]
    if m == 'pinsrw':
        v = F.reg_rd(i.reg_name(s.reg)) if s.type == X86_OP_REG else 'RD16(ea)'
        return [f'{X(d)}.w[{imm & 7}] = (uint16_t)({v});']

    # ---------------------------------------------------------------- SSE2 integer
    mm = re.fullmatch(r'p(add|sub)(b|w|d|q)', m)
    if mm:
        op, w = mm.groups(); n = {'b': 16, 'w': 8, 'd': 4, 'q': 2}[w]; f = {'b': 'b', 'w': 'w', 'd': 'd', 'q': 'q'}[w]
        o = '+' if op == 'add' else '-'
        return [s128(s)] + [f'{X(d)}.{f}[{k}] {o}= s.{f}[{k}];' for k in range(n)]
    mm = re.fullmatch(r'pcmp(eq|gt)(b|w|d)', m)
    if mm:
        op, w = mm.groups(); n = {'b': 16, 'w': 8, 'd': 4}[w]; u = {'b': 'b', 'w': 'w', 'd': 'd'}[w]; sg = {'b': 'sb', 'w': 'sw', 'd': 'sd'}[w]
        e = f'{X(d)}.{u}[{{k}}] == s.{u}[{{k}}]' if op == 'eq' else f'{X(d)}.{sg}[{{k}}] > s.{sg}[{{k}}]'
        return [s128(s)] + [f'{X(d)}.{u}[{k}] = ({e.format(k=k)}) ? ({ {"b": "uint8_t", "w": "uint16_t", "d": "uint32_t"}[w] })~0u : 0;' for k in range(n)]
    mm = re.fullmatch(r'p(max|min)(sw|ub)', m)
    if mm:
        op, w = mm.groups(); n, f = (8, 'sw') if w == 'sw' else (16, 'b'); cmpop = '>' if op == 'max' else '<'
        return [s128(s)] + [f'if (s.{f}[{k}] {cmpop} {X(d)}.{f}[{k}]) {X(d)}.{f}[{k}] = s.{f}[{k}];' for k in range(n)]
    mm = re.fullmatch(r'ps(ll|rl|ra)(w|d|q)', m)
    if mm:
        kind, w = mm.groups(); n, f, bits = {'w': (8, 'w', 16), 'd': (4, 'd', 32), 'q': (2, 'q', 64)}[w]
        if kind == 'ra' and w == 'q': raise Unsupported(m)
        L = [f'uint64_t n = {imm & 0xFF}u;' if s.type == X86_OP_IMM else
             (f'uint64_t n = {X(s)}.q[0];' if isx(s) else 'uint64_t n = RD64(ea);')]
        sf = {'w': 'sw', 'd': 'sd'}.get(w)
        for k in range(n):
            if kind == 'll': L.append(f'{X(d)}.{f}[{k}] = n >= {bits} ? 0 : ({X(d)}.{f}[{k}] << n);')
            elif kind == 'rl': L.append(f'{X(d)}.{f}[{k}] = n >= {bits} ? 0 : ({X(d)}.{f}[{k}] >> n);')
            else: L.append(f'{X(d)}.{sf}[{k}] = {X(d)}.{sf}[{k}] >> (n >= {bits} ? {bits - 1} : n);')
        return L
    if m in ('psrldq', 'pslldq'):
        n = imm & 0xFF
        L = ['XMM o = ' + X(d) + ';']
        for k in range(16):
            src = k + n if m == 'psrldq' else k - n
            L.append(f'{X(d)}.b[{k}] = {f"o.b[{src}]" if 0 <= src < 16 else "0"};')
        return L

    raise Unsupported(m)
