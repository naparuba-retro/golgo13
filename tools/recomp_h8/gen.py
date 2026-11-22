#!/usr/bin/env python3
"""H8/300H (advanced mode) static recompiler -> C11.  Input: work/regions/sub.bin + work/h8/flow.json.
Output: generated_h8/h8_NNN.c (one C function per basic block, returns next PC), h8_table.c, h8_blocks.h.

Semantics follow MAME src/devices/cpu/h8 (h8.lst microcode / h8.cpp helpers): ALU/shift/rotate helpers are the
ported ones in runtime/h8/h8_alu.h, so flags are bit-exact. Stack frame (advanced mode, type 'h'): jsr/bsr push a 32-bit
return address; interrupts/rte use a 32-bit frame [CCR][PC23..16][PC15..0].
Unsupported/unused instructions abort generation (never emulated).
"""
import sys, os, json, struct, collections
sys.path.insert(0, os.path.dirname(__file__))
from h8dec import decode, R8, R16, R32, ROOT
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import gamecfg
PFX = gamecfg.PFX; HBP = PFX + 'hb_'

rom = open(os.path.join(gamecfg.REGIONS, 'sub.bin'), 'rb').read()
SIZE = len(rom)
flow = json.load(open(os.path.join(gamecfg.H8FLOW, 'flow.json')))
code = set(flow['code'])
COND = {'bhi': "!(c->ccr & (F_C|F_Z))", 'bls': "(c->ccr & (F_C|F_Z))", 'bcc': "!(c->ccr & F_C)", 'bcs': "(c->ccr & F_C)",
        'bne': "!(c->ccr & F_Z)", 'beq': "(c->ccr & F_Z)", 'bvc': "!(c->ccr & F_V)", 'bvs': "(c->ccr & F_V)",
        'bpl': "!(c->ccr & F_N)", 'bmi': "(c->ccr & F_N)",
        'bge': "(!!(c->ccr & F_N) == !!(c->ccr & F_V))", 'blt': "(!!(c->ccr & F_N) != !!(c->ccr & F_V))",
        'bgt': "(!(c->ccr & F_Z) && (!!(c->ccr & F_N) == !!(c->ccr & F_V)))",
        'ble': "((c->ccr & F_Z) || (!!(c->ccr & F_N) != !!(c->ccr & F_V)))"}
SUF = {'b': 1, 'w': 2, 'l': 4}

class Unsupported(Exception): pass

# ----------------------------------------------------------------- register / operand expressions
def reg_get(kind, i):
    if kind == 'r8': return f"((uint8_t)(c->er[{i}] >> 8))" if i < 8 else f"((uint8_t)c->er[{i - 8}])"
    if kind == 'r16': return f"((uint16_t)c->er[{i}])" if i < 8 else f"((uint16_t)(c->er[{i - 8}] >> 16))"
    return f"c->er[{i}]"
def reg_set(kind, i, v):
    if kind == 'r8':
        return f"c->er[{i}] = (c->er[{i}] & 0xffff00ffu) | ((uint32_t)(uint8_t)({v}) << 8);" if i < 8 else \
               f"c->er[{i - 8}] = (c->er[{i - 8}] & 0xffffff00u) | (uint8_t)({v});"
    if kind == 'r16':
        return f"c->er[{i}] = (c->er[{i}] & 0xffff0000u) | (uint16_t)({v});" if i < 8 else \
               f"c->er[{i - 8}] = (c->er[{i - 8}] & 0x0000ffffu) | ((uint32_t)(uint16_t)({v}) << 16);"
    return f"c->er[{i}] = {v};"
RD = {1: 'h8_rd8', 2: 'h8_rd16', 4: 'h8_rd32'}
WR = {1: 'h8_wr8', 2: 'h8_wr16', 4: 'h8_wr32'}

def addr_expr(op, size):
    if op.kind == 'abs': return f"0x{op.val & 0xffffff:06x}u"
    if op.kind == 'ind': return f"((c->er[{op.reg}] + 0x{op.val & 0xffffffff:x}u) & 0xffffffu)"
    if op.kind in ('postinc', 'predec'): return f"(c->er[{op.reg}] & 0xffffffu)"
    raise Unsupported(f"address of {op.kind}")
def align(a, size): return a if size == 1 else f"(({a}) & 0xfffffeu)"

def operand_read(op, size, uid):
    """returns (pre, expr, post)"""
    k = op.kind
    if k in ('r8', 'r16', 'r32'): return [], reg_get(k, op.reg), []
    if k == 'imm': return [], f"0x{op.val & ((1 << (8 * size)) - 1):x}u", []
    if k in ('abs', 'ind'): return [], f"{RD[size]}(c, {align(addr_expr(op, size), size)})", []
    if k == 'postinc': return [], f"{RD[size]}(c, {align(addr_expr(op, size), size)})", [f"c->er[{op.reg}] += {size};"]
    if k == 'predec': return [f"c->er[{op.reg}] -= {size};"], f"{RD[size]}(c, {align(addr_expr(op, size), size)})", []
    raise Unsupported(f"read of {k}")

def operand_write(op, size, val):
    k = op.kind
    if k in ('r8', 'r16', 'r32'): return [reg_set(k, op.reg, val)]
    if k in ('abs', 'ind'): return [f"{WR[size]}(c, {align(addr_expr(op, size), size)}, {val});"]
    if k == 'postinc': return [f"{WR[size]}(c, {align(addr_expr(op, size), size)}, {val});", f"c->er[{op.reg}] += {size};"]
    if k == 'predec': return [f"c->er[{op.reg}] -= {size};", f"{WR[size]}(c, {align(addr_expr(op, size), size)}, {val});"]
    raise Unsupported(f"write of {k}")

def u(size): return {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}[size]
def sz_suffix(size): return {1: '8', 2: '16', 4: '32'}[size]

# ----------------------------------------------------------------- per-instruction emission
def emit_insn(i, nxt):
    """Returns (lines, terminator) ; terminator None (fall through) or ('ret', cexpr) for block-ending instructions."""
    n = i.name; ops = i.ops
    base, _, sfx = n.partition('.')
    size = SUF.get(sfx)
    L = []
    def bit_target():
        # returns (read_stmts producing byte in 'b', write_stmts taking 'b') for the byte operand
        o = ops[-1]
        if o.kind == 'r8': return [f"uint8_t b = {reg_get('r8', o.reg)};"], reg_set('r8', o.reg, 'b')
        if o.kind in ('abs', 'ind'):
            a = addr_expr(o, 1); return [f"uint32_t ba = {a}; uint8_t b = h8_rd8(c, ba);"], f"h8_wr8(c, ba, b);"
        raise Unsupported(f"bit target {o.kind}")
    def bit_no():
        s = ops[0]
        if s.kind == 'imm': return f"{s.val & 7}"
        if s.kind == 'r8': return f"({reg_get('r8', s.reg)} & 7)"
        raise Unsupported("bit number operand")

    if n == 'nop': return L, None
    if base == 'mov' and size and len(ops) == 2:
        pre, e, post = operand_read(ops[0], size, i.pc)
        L += pre; L.append(f"{u(size)} t = {e};"); L += post
        L += operand_write(ops[1], size, 't')
        L.append(f"set_nzv{sz_suffix(size)}(c, t);")
        return [("{ " + " ".join(L) + " }")], None
    if base in ('add', 'sub', 'cmp', 'and', 'or', 'xor', 'addx', 'subx') and size and len(ops) == 2:
        pre, e, post = operand_read(ops[0], size, i.pc)
        dpre, d, dpost = operand_read(ops[1], size, i.pc)
        if ops[1].kind not in ('r8', 'r16', 'r32'): raise Unsupported(f"{n} to memory")
        L += pre; L.append(f"{u(size)} s = {e};"); L += post
        s8 = sz_suffix(size)
        if base == 'add': r = f"do_add{s8}(c, {d}, s)"
        elif base == 'sub': r = f"do_sub{s8}(c, {d}, s)"
        elif base == 'cmp': r = f"do_sub{s8}(c, {d}, s)"
        elif base == 'addx': r = f"do_addx8(c, {d}, s)"
        elif base == 'subx': r = f"do_subx8(c, {d}, s)"
        else:
            op_ = {'and': '&', 'or': '|', 'xor': '^'}[base]
            r = f"({u(size)})({d} {op_} s)"
        if base == 'cmp': L.append(f"(void){r};")
        elif base in ('and', 'or', 'xor'):
            L.append(f"{u(size)} t = {r};"); L.append(reg_set(ops[1].kind, ops[1].reg, 't')); L.append(f"set_nzv{s8}(c, t);")
        else:
            L.append(f"{u(size)} t = {r};"); L.append(reg_set(ops[1].kind, ops[1].reg, 't'))
        return [("{ " + " ".join(L) + " }")], None
    if base in ('adds', 'subs') and size == 4:
        d = ops[1]; op_ = '+' if base == 'adds' else '-'
        return [f"c->er[{d.reg}] {op_}= {ops[0].val};"], None
    if base in ('inc', 'dec') and size:
        d = ops[1]; s8 = sz_suffix(size)
        v = ops[0].val
        r = f"do_inc{s8}(c, {reg_get(d.kind, d.reg)}, {v})" if base == 'inc' else f"do_dec{s8}(c, {reg_get(d.kind, d.reg)}, {v})"
        return [reg_set(d.kind, d.reg, r)], None
    if base in ('neg', 'not') and size:
        d = ops[0]; s8 = sz_suffix(size)
        if base == 'neg': return [reg_set(d.kind, d.reg, f"do_sub{s8}(c, 0, {reg_get(d.kind, d.reg)})")], None
        return ["{ " + f"{u(size)} t = ({u(size)})~{reg_get(d.kind, d.reg)}; set_nzv{s8}(c, t); {reg_set(d.kind, d.reg, 't')}" + " }"], None
    if base in ('shal', 'shar', 'shll', 'shlr', 'rotl', 'rotr', 'rotxl', 'rotxr') and size:
        d = ops[-1]; s8 = sz_suffix(size)
        two = len(ops) == 2 and ops[0].kind == 'imm' and ops[0].val == 2
        fn = f"do_{base}{'2_' if two else ''}{s8}"
        return [reg_set(d.kind, d.reg, f"{fn}(c, {reg_get(d.kind, d.reg)})")], None
    if base in ('exts', 'extu') and size:
        d = ops[0]; s8 = sz_suffix(size)
        src = f"(uint8_t){reg_get(d.kind, d.reg)}" if size == 2 else f"(uint16_t){reg_get(d.kind, d.reg)}"
        if base == 'exts': src = f"(int8_t)({reg_get(d.kind, d.reg)})" if size == 2 else f"(int16_t)({reg_get(d.kind, d.reg)})"
        return ["{ " + f"{u(size)} t = ({u(size)})({src}); set_nzv{s8}(c, t); {reg_set(d.kind, d.reg, 't')}" + " }"], None
    if n in ('movfpe', 'movtpe'):
        a = ops[0] if n == 'movfpe' else ops[1]; r = ops[1] if n == 'movfpe' else ops[0]
        if n == 'movfpe': return ["{ " + f"uint8_t t = h8_rd8(c, 0x{a.val & 0xffffff:06x}u); set_nzv8(c, t); {reg_set('r8', r.reg, 't')}" + " }"], None
        return ["{ " + f"uint8_t t = {reg_get('r8', r.reg)}; set_nzv8(c, t); h8_wr8(c, 0x{a.val & 0xffffff:06x}u, t);" + " }"], None
    if n in ('daa.b', 'das.b'):
        d = ops[0]
        if n == 'daa.b':
            body = ("uint8_t a = %s, adj = 0; if (c->ccr & F_C) { if (c->ccr & F_H) { if ((a & 0xf0) <= 0x30 && (a & 0x0f) <= 3) adj = 0x66; } "
                    "else { if ((a & 0xf0) <= 0x20) adj = (a & 0x0f) <= 9 ? 0x60 : 0x66; } } else { if (c->ccr & F_H) { if ((a & 0x0f) <= 3) adj = (a & 0xf0) <= 0x90 ? 0x06 : 0x66; } "
                    "else { if ((a & 0x0f) <= 9) adj = (a & 0xf0) <= 0x90 ? 0x00 : 0x60; else adj = (a & 0xf0) <= 0x80 ? 0x06 : 0x66; } } ") % reg_get('r8', d.reg)
        else:
            body = ("uint8_t a = %s, adj = 0; if (c->ccr & F_C) { if (c->ccr & F_H) { if ((a & 0xf0) >= 0x60 && (a & 0x0f) >= 6) adj = 0x9a; } "
                    "else { if ((a & 0xf0) >= 0x70 && (a & 0x0f) <= 9) adj = 0xa0; } } else { if (c->ccr & F_H) { if ((a & 0xf0) <= 0x80 && (a & 0x0f) >= 6) adj = 0xfa; } } ") % reg_get('r8', d.reg)
        return ["{ " + body + reg_set('r8', d.reg, "do_add8(c, a, adj)") + " }"], None
    if n in ('mulxs.b', 'mulxs.w'):
        s, d = ops
        if n == 'mulxs.b': return ["{ " + f"int32_t t = (int32_t)(int8_t){reg_get('r16', d.reg)} * (int32_t)(int8_t){reg_get('r8', s.reg)}; set_nz16(c, (uint16_t)t); {reg_set('r16', d.reg, '(uint16_t)t')}" + " }"], None
        return ["{ " + f"int32_t t = (int32_t)(int16_t)c->er[{d.reg}] * (int32_t)(int16_t){reg_get('r16', s.reg)}; set_nz32(c, (uint32_t)t); c->er[{d.reg}] = (uint32_t)t;" + " }"], None
    if n in ('divxs.b', 'divxs.w'):
        s, d = ops
        wide = n == 'divxs.w'
        a = f"(int32_t)c->er[{d.reg}]" if wide else f"(int32_t)(int16_t){reg_get('r16', d.reg)}"
        b = f"(int32_t)(int16_t){reg_get('r16', s.reg)}" if wide else f"(int32_t)(int8_t){reg_get('r8', s.reg)}"
        res = (f"c->er[{d.reg}] = ((uint32_t)q & 0xffff) | (((uint32_t)r & 0xffff) << 16);" if wide
               else reg_set('r16', d.reg, "(uint16_t)(((uint32_t)q & 0xff) | (((uint32_t)r & 0xff) << 8))"))
        return ["{ " + f"int32_t a = {a}, b = {b}; c->ccr &= ~(F_Z|F_N); if (!b) c->ccr |= F_Z; else {{ int32_t q = a / b, r = a % b; if (q < 0) c->ccr |= F_N; {res} }}" + " }"], None
    if n in ('mulxu.b', 'mulxu.w'):
        s, d = ops
        if n == 'mulxu.b': return [reg_set('r16', d.reg, f"(uint16_t)((uint8_t){reg_get('r16', d.reg)} * {reg_get('r8', s.reg)})")], None
        return [reg_set('r32', d.reg, f"(uint32_t)((uint16_t)c->er[{d.reg}] * {reg_get('r16', s.reg)})")], None
    if n in ('divxu.b', 'divxu.w'):
        s, d = ops
        if n == 'divxu.b':
            return ["{ " + f"uint32_t a = {reg_get('r16', d.reg)}, b = {reg_get('r8', s.reg)}; c->ccr &= ~(F_Z|F_N); if (b & 0x80) c->ccr |= F_N; "
                    f"if (!b) c->ccr |= F_Z; else {{ uint32_t q = a / b, r = a % b; {reg_set('r16', d.reg, '(q & 0xff) | ((r & 0xff) << 8)')} }}" + " }"], None
        return ["{ " + f"uint32_t a = c->er[{d.reg}], b = {reg_get('r16', s.reg)}; c->ccr &= ~(F_Z|F_N); if (b & 0x80) c->ccr |= F_N; "
                f"if (!b) c->ccr |= F_Z; else {{ uint32_t q = a / b, r = a % b; c->er[{d.reg}] = (q & 0xffff) | ((r & 0xffff) << 16); }}" + " }"], None
    if n in ('ldc', 'stc', 'andc', 'orc', 'xorc'):
        if n == 'stc': return [reg_set('r8', ops[1].reg, 'c->ccr')], None
        if n == 'ldc':
            v = f"0x{ops[0].val:x}u" if ops[0].kind == 'imm' else reg_get('r8', ops[0].reg)
            return [f"c->ccr = (uint8_t)({v});"], None
        op_ = {'andc': '&=', 'orc': '|=', 'xorc': '^='}[n]
        return [f"c->ccr {op_} 0x{ops[0].val:x};"], None
    if base in ('bset', 'bclr', 'bnot', 'bst', 'bist'):
        rd, wr = bit_target(); bn = bit_no()
        m = f"(1u << {bn})"
        if base == 'bset': body = f"b |= {m};"
        elif base == 'bclr': body = f"b &= ~{m};"
        elif base == 'bnot': body = f"b ^= {m};"
        elif base == 'bst': body = f"if (c->ccr & F_C) b |= {m}; else b &= ~{m};"
        else: body = f"if (!(c->ccr & F_C)) b |= {m}; else b &= ~{m};"
        return ["{ " + " ".join(rd) + f" {body} " + wr + " }"], None
    if base in ('btst', 'bld', 'bild', 'band', 'biand', 'bor', 'bior', 'bxor', 'bixor'):
        rd, _ = bit_target(); bn = bit_no(); t = f"(b & (1u << {bn}))"
        if base == 'btst': body = f"if ({t}) c->ccr &= ~F_Z; else c->ccr |= F_Z;"
        elif base == 'bld': body = f"if ({t}) c->ccr |= F_C; else c->ccr &= ~F_C;"
        elif base == 'bild': body = f"if (!{t}) c->ccr |= F_C; else c->ccr &= ~F_C;"
        elif base == 'bor': body = f"if ({t}) c->ccr |= F_C;"
        elif base == 'bior': body = f"if (!{t}) c->ccr |= F_C;"
        elif base == 'bxor': body = f"if ({t}) c->ccr ^= F_C;"
        elif base == 'bixor': body = f"if (!{t}) c->ccr ^= F_C;"
        elif base == 'band': body = f"if (!{t}) c->ccr &= ~F_C;"
        else: body = f"if ({t}) c->ccr &= ~F_C;"
        return ["{ " + " ".join(rd) + f" {body} " + " }"], None
    # ---- control flow
    ret = nxt
    if n in COND: return [], ('ret', f"({COND[n]}) ? 0x{ops[0].val:06x}u : 0x{nxt:06x}u")
    if n == 'bt': return [], ('ret', f"0x{ops[0].val:06x}u")
    if n == 'bf': return [], ('ret', f"0x{nxt:06x}u")
    def push(retaddr):
        return [f"c->er[7] -= 4; h8_wr16(c, c->er[7] & 0xfffffeu, 0x{(retaddr >> 16) & 0xff:x}); h8_wr16(c, (c->er[7] + 2) & 0xfffffeu, 0x{retaddr & 0xffff:x});"]
    if n == 'bsr': return push(nxt), ('ret', f"0x{ops[0].val:06x}u")
    if n in ('jmp', 'jsr'):
        o = ops[0]
        if o.kind == 'abs': tgt = f"0x{o.val:06x}u"; pre = []
        elif o.kind == 'r32': tgt = "t"; pre = [f"uint32_t t = c->er[{o.reg}] & 0xffffffu;"]
        elif o.kind == 'memind':
            tgt = "t"; pre = [f"uint32_t t = ((uint32_t)h8_rd16(c, {o.val}u) << 16 | h8_rd16(c, {o.val + 2}u)) & 0xffffffu;"]
        else: raise Unsupported(f"{n} {o.kind}")
        L = pre + (push(nxt) if n == 'jsr' else [])
        return L, ('ret', tgt)
    if n == 'rts':
        return ["uint32_t t = (((uint32_t)h8_rd16(c, c->er[7] & 0xfffffeu) << 16) | h8_rd16(c, (c->er[7] + 2) & 0xfffffeu)) & 0xffffffu; c->er[7] += 4;"], ('ret', "t")
    if n == 'rte':
        return ["uint16_t w0 = h8_rd16(c, c->er[7] & 0xfffffeu), w1 = h8_rd16(c, (c->er[7] + 2) & 0xfffffeu); c->er[7] += 4; "
                "c->ccr = (uint8_t)(w0 >> 8); uint32_t t = (((uint32_t)(w0 & 0xff)) << 16) | w1;"], ('ret', "t")
    if n == 'sleep': return ["c->halted = 1;"], ('ret', f"0x{nxt:06x}u")
    if n == 'illegal': return [], ('ret', f"h8_illegal(c, 0x{i.pc:06x}u)")
    raise Unsupported(f"{n} {ops}")

# ----------------------------------------------------------------- blocks
def main():
    out = gamecfg.OUT_H8; os.makedirs(out, exist_ok=True)
    for f in os.listdir(out): os.remove(os.path.join(out, f))
    insns = {a: decode(rom, a) for a in sorted(code)}
    leaders = set(flow['funcs']) | set(int(v) for v in flow['vectors'].values())
    ENDS = set(COND) | {'bt', 'bf', 'bsr', 'jmp', 'jsr', 'rts', 'rte', 'sleep', 'illegal'}
    for a, i in insns.items():
        if i.name in COND or i.name in ('bt', 'bf', 'bsr'): leaders.add(i.ops[0].val)
        if i.name in ENDS: leaders.add(a + i.size)
    leaders = sorted(x for x in leaders if x in insns)
    lset = set(leaders)
    # every even address of the code area may be an indirect-jump target (function pointers kept in RAM / tables):
    # compile a fallback entry for each one (runs up to the next natural leader or MAXB instructions). No interpreter, ever.
    ALL_LO, ALL_HI, MAXB = 0x100, 0x7800, 48
    for a in range(ALL_LO, ALL_HI, 2):
        if a not in insns: insns[a] = decode(rom, a)
    entries = sorted(set(leaders) | set(range(ALL_LO, ALL_HI, 2)))
    unsup = collections.Counter(); blocks = []
    for b in entries:
        pc = b; lines = []; states = 0; term = None; ninstr = 0
        while True:
            i = insns.get(pc)
            if i is None:
                lines.append(f"return h8_unknown_pc(c, 0x{pc:06x}u);"); break
            nxt = pc + i.size
            try:
                L, term = emit_insn(i, nxt)
            except Unsupported as e:
                unsup[f"{i.name} {[repr(o) for o in i.ops]}"] += 1
                L, term = [f"return h8_illegal(c, 0x{pc:06x}u); /* unsupported: {i.name} */"], ('ret', "0")
            lines.append(f"/* {pc:06x} {i.name} {', '.join(map(repr, i.ops))} */")
            lines += L
            states += 2 * (i.size // 2) + (2 if any(o.kind in ('abs', 'ind', 'postinc', 'predec') for o in i.ops) else 0)
            if term is not None:
                lines.append(f"c->cycles += {states}; return {term[1]};"); break
            pc = nxt; ninstr += 1
            if pc in lset or ninstr >= MAXB:
                lines.append(f"c->cycles += {states}; return 0x{pc:06x}u;"); break
        blocks.append((b, lines))
    shard = 800
    names = []
    for si in range(0, len(blocks), shard):
        fn = f"{PFX}h8_{si // shard:03d}.c"
        with open(os.path.join(out, fn), 'w', newline='\n') as f:
            f.write('/* GENERATED by tools/recomp_h8/gen.py - derived from user-supplied ROM data; do not redistribute. */\n')
            f.write('#include "h8_state.h"\n#include "h8_alu.h"\n\n')
            for b, lines in blocks[si:si + shard]:
                f.write(f"uint32_t {HBP}{b:06x}(h8_t *c)\n{{\n")
                for l in lines: f.write("    " + l + "\n")
                f.write("}\n\n")
        names.append(fn)
    with open(os.path.join(out, PFX + 'h8_blocks.h'), 'w', newline='\n') as f:
        f.write('/* GENERATED */\n#pragma once\n#include "h8_state.h"\n')
        for b, _ in blocks: f.write(f"uint32_t {HBP}{b:06x}(h8_t *c);\n")
    with open(os.path.join(out, PFX + 'h8_table.c'), 'w', newline='\n') as f:
        f.write('/* GENERATED */\n#include "h8_state.h"\n#include "' + PFX + 'h8_blocks.h"\n\n')
        f.write(f"#define H8_WORDS {SIZE // 2}u\nstatic const h8_blk_fn tbl[H8_WORDS] = {{\n")
        for b, _ in blocks: f.write(f"    [0x{b >> 1:x}] = {HBP}{b:06x},\n")
        f.write("};\n\nh8_blk_fn " + PFX + "h8_lookup(uint32_t pc)\n{\n    pc &= 0xffffffu;\n    if ((pc & 1u) || (pc >> 1) >= H8_WORDS) return 0;\n    return tbl[pc >> 1];\n}\n")
    print(f"blocks {len(blocks)}, instructions {len(insns)}; unsupported: {dict(unsup)}")
    if unsup: print("NOTE: unsupported instructions are compiled as 'illegal' traps (implement before running those paths)")

if __name__ == '__main__':
    main()
