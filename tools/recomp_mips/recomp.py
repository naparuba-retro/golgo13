#!/usr/bin/env python3
"""MIPS R3000 (PS1/System 12) static recompiler -> C11.

Input : ROM image bytes + base address, flow-analysis json (code words, funcs, jump tables).
Output: generated/<name>_NNN.c  (one C function per basic block, returning the next guest PC),
        generated/<name>_table.c (guest PC -> block function), generated/<name>_blocks.h

Semantics implemented (checked against MAME psxcpu):
 * branch delay slots (condition / target read BEFORE the slot executes, link written before it)
 * load delay slots: the instruction after a load reads the OLD register value; the load result
   is committed after that instruction unless it writes the same register; lwl/lwr see the
   pending value (pipeline forwarding)
 * unaligned lwl/lwr/swl/swr, add/addi/sub overflow exceptions, break/syscall, COP0, COP2 (GTE)
 * no interpreter: unknown targets are reported by the runtime (rt_unknown_pc)
"""
import argparse, json, os, struct, sys, collections

def sx16(v): return v - 0x10000 if v & 0x8000 else v
def u32(v): return v & 0xffffffff

class Gen:
    def __init__(self, image, base, code, funcs, jtabs, name, lead_code=None, all_entries=False):
        self.img = image; self.base = base; self.name = name; self.bp = 'b_' if not name.startswith('g13') else name.split('_')[0] + '_b_'
        self.code = set(code); self.funcs = set(funcs)
        self.lead_code = set(lead_code) if lead_code is not None else self.code
        self.all_entries = all_entries
        self.max_block = 40 if all_entries else 100000
        self.jtabs = jtabs
        self.end = base + len(image)
        self.warn = collections.Counter()
        self.warn_examples = collections.defaultdict(list)

    def W(self, a):
        return struct.unpack_from('<I', self.img, a - self.base)[0]

    # ---------------------------------------------------------------- leaders
    def find_leaders(self):
        L = set(self.funcs)
        for a in sorted(self.lead_code):
            w = self.W(a); op = w >> 26; f = w & 0x3f; rt = (w >> 16) & 31
            if op == 2 or op == 3:
                L.add(((w & 0x3ffffff) << 2) | (a & 0xf0000000))
                if op == 3: L.add(a + 8)
            elif op in (4, 5, 6, 7) or (op == 1 and rt in (0, 1, 16, 17)):
                L.add(a + 4 + (sx16(w & 0xffff) << 2)); L.add(a + 8)
                if op == 1 and rt in (16, 17): pass
            elif op == 0 and f == 9: L.add(a + 8)
            elif op == 0 and f in (0x0c, 0x0d): L.add(a + 4)
        for t in self.jtabs.values():
            for x in t['targets']: L.add(int(x, 16))
        # keep only leaders that are real code
        self.unknown_leaders = sorted(x for x in L if x not in self.code)
        return set(x for x in L if x in self.code)

    # ----------------------------------------------------------- instruction
    @staticmethod
    def R(n): return "0u" if n == 0 else f"c->r[{n}]"

    def effects(self, w):
        """(written_reg or None, is_load, is_lwx)"""
        op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; f = w & 0x3f
        if op == 0:
            if f in (0x00,0x02,0x03,0x04,0x06,0x07,0x10,0x12,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2a,0x2b): return rd, False, False
            if f == 9: return rd, False, False
            return None, False, False
        if op in (8,9,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f): return rt, False, False
        if op == 3: return 31, False, False
        if op == 1 and rt in (16, 17): return 31, False, False
        if op == 0x10 and rs == 0: return rt, False, False       # mfc0
        if op == 0x12 and not (w >> 25) & 1 and rs in (0, 2): return rt, False, False  # mfc2/cfc2 (delayed on real HW, treated as ALU write like MAME)
        if op in (0x20,0x21,0x23,0x24,0x25): return rt, True, False
        if op in (0x22, 0x26): return rt, True, True
        return None, False, False

    def emit(self, pc, w, pend, in_slot, bpc):
        """Return (lines, new_pending). pend = (reg, varname) or None. Does NOT handle branches."""
        op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31
        sh = (w >> 6) & 31; f = w & 0x3f; imm = w & 0xffff; simm = sx16(imm)
        R = self.R; L = []
        epc = bpc if in_slot else pc
        exc = lambda code: f"return rt_exception(c, {code}, 0x{epc:08x}u, {1 if in_slot else 0}u);"
        def setr(n, expr):
            if n != 0: L.append(f"c->r[{n}] = {expr};")
            else: L.append(f"(void)({expr});")
        wr_reg, is_load, is_lwx = self.effects(w)
        newpend = None
        ld = lambda expr: None
        def load(expr, signed_cast=None):
            nonlocal newpend
            var = f"l_{pc:08x}"
            L.append(f"uint32_t {var} = {expr};")
            if rt != 0: newpend = (rt, var)
        if op == 0:
            if f == 0x00: setr(rd, f"{R(rt)} << {sh}")
            elif f == 0x02: setr(rd, f"{R(rt)} >> {sh}")
            elif f == 0x03: setr(rd, f"(uint32_t)((int32_t){R(rt)} >> {sh})")
            elif f == 0x04: setr(rd, f"{R(rt)} << ({R(rs)} & 31)")
            elif f == 0x06: setr(rd, f"{R(rt)} >> ({R(rs)} & 31)")
            elif f == 0x07: setr(rd, f"(uint32_t)((int32_t){R(rt)} >> ({R(rs)} & 31))")
            elif f == 0x0c: L.append(exc("EXC_SYS")); self.syscall_end = True
            elif f == 0x0d: L.append(exc("EXC_BP")); self.syscall_end = True
            elif f == 0x10: setr(rd, "c->hi")
            elif f == 0x11: L.append(f"c->hi = {R(rs)};")
            elif f == 0x12: setr(rd, "c->lo")
            elif f == 0x13: L.append(f"c->lo = {R(rs)};")
            elif f == 0x18:
                L.append(f"{{ int64_t p = (int64_t)(int32_t){R(rs)} * (int64_t)(int32_t){R(rt)}; c->lo = (uint32_t)p; c->hi = (uint32_t)((uint64_t)p >> 32); }}")
            elif f == 0x19:
                L.append(f"{{ uint64_t p = (uint64_t){R(rs)} * (uint64_t){R(rt)}; c->lo = (uint32_t)p; c->hi = (uint32_t)(p >> 32); }}")
            elif f == 0x1a:
                L.append(f"{{ int32_t n = (int32_t){R(rs)}, d = (int32_t){R(rt)};"
                         f" if (d == 0) {{ c->lo = (n < 0) ? 1u : 0xffffffffu; c->hi = (uint32_t)n; }}"
                         f" else if (n == (int32_t)0x80000000 && d == -1) {{ c->lo = 0x80000000u; c->hi = 0; }}"
                         f" else {{ c->lo = (uint32_t)(n / d); c->hi = (uint32_t)(n % d); }} }}")
            elif f == 0x1b:
                L.append(f"{{ uint32_t n = {R(rs)}, d = {R(rt)};"
                         f" if (d == 0) {{ c->lo = 0xffffffffu; c->hi = n; }} else {{ c->lo = n / d; c->hi = n % d; }} }}")
            elif f == 0x20:
                L.append(f"{{ uint32_t a = {R(rs)}, b = {R(rt)}, s = a + b; if (((a ^ s) & (b ^ s)) >> 31) {exc('EXC_OV')} "
                         f"{('c->r[%d] = s;' % rd) if rd else ''} }}")
            elif f == 0x21: setr(rd, f"{R(rs)} + {R(rt)}")
            elif f == 0x22:
                L.append(f"{{ uint32_t a = {R(rs)}, b = {R(rt)}, s = a - b; if (((a ^ b) & (a ^ s)) >> 31) {exc('EXC_OV')} "
                         f"{('c->r[%d] = s;' % rd) if rd else ''} }}")
            elif f == 0x23: setr(rd, f"{R(rs)} - {R(rt)}")
            elif f == 0x24: setr(rd, f"{R(rs)} & {R(rt)}")
            elif f == 0x25: setr(rd, f"{R(rs)} | {R(rt)}")
            elif f == 0x26: setr(rd, f"{R(rs)} ^ {R(rt)}")
            elif f == 0x27: setr(rd, f"~({R(rs)} | {R(rt)})")
            elif f == 0x2a: setr(rd, f"(uint32_t)((int32_t){R(rs)} < (int32_t){R(rt)})")
            elif f == 0x2b: setr(rd, f"(uint32_t)({R(rs)} < {R(rt)})")
            else:
                L.append(exc("EXC_RI")); self.syscall_end = True
                self.note('reserved-special', pc)
        elif op == 0x08:
            L.append(f"{{ uint32_t a = {R(rs)}, b = 0x{u32(simm):08x}u, s = a + b; if (((a ^ s) & (b ^ s)) >> 31) {exc('EXC_OV')} "
                     f"{('c->r[%d] = s;' % rt) if rt else ''} }}")
        elif op == 0x09: setr(rt, f"{R(rs)} + 0x{u32(simm):08x}u")
        elif op == 0x0a: setr(rt, f"(uint32_t)((int32_t){R(rs)} < (int32_t)0x{u32(simm):08x}u)")
        elif op == 0x0b: setr(rt, f"(uint32_t)({R(rs)} < 0x{u32(simm):08x}u)")
        elif op == 0x0c: setr(rt, f"{R(rs)} & 0x{imm:x}u")
        elif op == 0x0d: setr(rt, f"{R(rs)} | 0x{imm:x}u")
        elif op == 0x0e: setr(rt, f"{R(rs)} ^ 0x{imm:x}u")
        elif op == 0x0f: setr(rt, f"0x{(imm << 16):08x}u")
        elif op == 0x10:
            if rs == 0: setr(rt, f"rt_cop0_read(c, {rd})")
            elif rs == 4: L.append(f"rt_cop0_write(c, {rd}, {R(rt)});")
            elif rs == 0x10 and f == 0x10: L.append("rt_rfe(c);")
            else: L.append(exc("EXC_RI")); self.syscall_end = True; self.note('bad-cop0', pc)
        elif op == 0x12:
            if w & (1 << 25): L.append(f"rt_gte_cmd(c, 0x{w & 0x1ffffff:07x}u);")
            elif rs == 0: setr(rt, f"rt_gte_mfc2(c, {rd})")
            elif rs == 2: setr(rt, f"rt_gte_cfc2(c, {rd})")
            elif rs == 4: L.append(f"rt_gte_mtc2(c, {rd}, {R(rt)});")
            elif rs == 6: L.append(f"rt_gte_ctc2(c, {rd}, {R(rt)});")
            else: L.append(exc("EXC_RI")); self.syscall_end = True; self.note('bad-cop2', pc)
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25):
            a = f"{R(rs)} + 0x{u32(simm):08x}u"
            e = {0x20: f"(uint32_t)(int32_t)(int8_t)rt_rd8(c, {a})",
                 0x24: f"rt_rd8(c, {a})",
                 0x21: f"(uint32_t)(int32_t)(int16_t)rt_rd16(c, {a})",
                 0x25: f"rt_rd16(c, {a})",
                 0x23: f"rt_rd32(c, {a})"}[op]
            load(e)
        elif op in (0x22, 0x26):
            src = pend[1] if (pend and pend[0] == rt) else R(rt)
            L.append(f"{{ uint32_t a = {R(rs)} + 0x{u32(simm):08x}u, m = rt_rd32(c, a & ~3u), k = a & 3u;")
            if op == 0x22: L.append(f"  l_{pc:08x} = (m << (24 - 8*k)) | ({src} & (0x00ffffffu >> (8*k))); ")
            else:          L.append(f"  l_{pc:08x} = (m >> (8*k)) | ({src} & (0xffffff00u << (24 - 8*k))); ")
            L.append("}")
            L.insert(len(L) - 3, f"uint32_t l_{pc:08x};")
            if rt != 0: newpend = (rt, f"l_{pc:08x}")
        elif op in (0x28, 0x29, 0x2b):
            a = f"{R(rs)} + 0x{u32(simm):08x}u"
            fn = {0x28: "rt_wr8", 0x29: "rt_wr16", 0x2b: "rt_wr32"}[op]
            L.append(f"{fn}(c, {a}, {R(rt)});")
        elif op in (0x2a, 0x2e):
            L.append(f"{{ uint32_t a = {R(rs)} + 0x{u32(simm):08x}u, m = rt_rd32(c, a & ~3u), k = a & 3u, v = {R(rt)};")
            if op == 0x2a: L.append("  m = (m & (0xffffff00u << (8*k))) | (v >> (24 - 8*k));")
            else:          L.append("  m = (m & (0x00ffffffu >> (24 - 8*k))) | (v << (8*k));")
            L.append("  rt_wr32(c, a & ~3u, m); }")
        elif op == 0x32:  # lwc2
            L.append(f"rt_gte_mtc2(c, {rt}, rt_rd32(c, {R(rs)} + 0x{u32(simm):08x}u));")
        elif op == 0x3a:  # swc2
            L.append(f"rt_wr32(c, {R(rs)} + 0x{u32(simm):08x}u, rt_gte_mfc2(c, {rt}));")
        else:
            L.append(exc("EXC_RI")); self.syscall_end = True; self.note('reserved-op', pc)

        # load-delay bookkeeping
        out = list(L)
        if pend and not (wr_reg is not None and wr_reg == pend[0]) and not self.syscall_end:
            out.append(f"c->r[{pend[0]}] = {pend[1]};")
        return out, newpend

    def note(self, kind, pc):
        self.warn[kind] += 1
        if len(self.warn_examples[kind]) < 5: self.warn_examples[kind].append(f"{pc:08x}")

    # ----------------------------------------------------------------- block
    def gen_block(self, start, leaders):
        lines = []; pc = start; n = 0; pend = None
        self.syscall_end = False; self.cur = []
        while True:
            if pc not in self.code:
                if pend: lines.append(f"c->r[{pend[0]}] = {pend[1]};")
                lines.append(f"return rt_unknown_pc(c, 0x{pc:08x}u);")
                break
            w = self.W(pc); op = w >> 26; f = w & 0x3f; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31
            is_br = (op in (2, 3, 4, 5, 6, 7) or (op == 1 and rt in (0, 1, 16, 17)) or (op == 0 and f in (8, 9)))
            lines.append(f"/* {pc:08x}: {w:08x} */")
            self.cur.append(pc)
            if is_br:
                if pc + 4 in self.code: self.cur.append(pc + 4)
                n += 1
                R = self.R
                tgt = None; link = None; tvar = None
                if op == 2 or op == 3:
                    tgt = ((w & 0x3ffffff) << 2) | (pc & 0xf0000000)
                    if op == 3: link = 31
                    pre = []; ret = f"0x{tgt:08x}u"
                elif op == 0:  # jr / jalr
                    pre = [f"uint32_t t = {R(rs)};"]; ret = "t"
                    if f == 9: link = rd
                else:
                    tg = pc + 4 + (sx16(w & 0xffff) << 2); ft = pc + 8
                    if op == 4: cond = f"{R(rs)} == {R(rt)}"
                    elif op == 5: cond = f"{R(rs)} != {R(rt)}"
                    elif op == 6: cond = f"(int32_t){R(rs)} <= 0"
                    elif op == 7: cond = f"(int32_t){R(rs)} > 0"
                    elif rt in (0, 16): cond = f"(int32_t){R(rs)} < 0"
                    else: cond = f"(int32_t){R(rs)} >= 0"
                    if rt in (16, 17) and op == 1: link = 31
                    pre = [f"uint32_t t = ({cond}) ? 0x{tg:08x}u : 0x{ft:08x}u;"]; ret = "t"
                lines.append("{")
                lines += pre
                if pend: lines.append(f"c->r[{pend[0]}] = {pend[1]};"); pend = None
                if link is not None and link != 0: lines.append(f"c->r[{link}] = 0x{pc + 8:08x}u;")
                # delay slot
                sp = pc + 4
                if sp not in self.code:
                    lines.append(f"return rt_unknown_pc(c, 0x{sp:08x}u); /* slot not in code */")
                else:
                    sw = self.W(sp); self.syscall_end = False
                    sop = sw >> 26; sf = sw & 0x3f; srt = (sw >> 16) & 31
                    if (sop in (2, 3, 4, 5, 6, 7) or (sop == 1 and srt in (0, 1, 16, 17)) or (sop == 0 and sf in (8, 9))):
                        lines.append(f"return rt_unknown_pc(c, 0x{sp:08x}u); /* branch in delay slot */")
                        self.note('branch-in-slot', pc)
                    else:
                        lines.append(f"/* {sp:08x}: {sw:08x} (slot) */")
                        sl, np_ = self.emit(sp, sw, None, True, pc)
                        lines += sl
                        if np_:
                            lines.append(f"c->r[{np_[0]}] = {np_[1]};")
                            self.note('load-in-slot', sp)
                        lines.append(f"return {ret};")
                n += 1
                lines.append("}")
                break
            # normal instruction
            self.syscall_end = False
            il, pend = self.emit(pc, w, pend, False, pc)
            n += 1
            lines += il
            if self.syscall_end:
                break
            pc += 4
            if (pc in leaders or n >= self.max_block) and not pend:
                lines.append(f"return 0x{pc:08x}u;")
                break
            if pend and pc not in self.code:
                lines.append(f"c->r[{pend[0]}] = {pend[1]};")
                lines.append(f"return rt_unknown_pc(c, 0x{pc:08x}u);")
                break
        return n, lines

    # ------------------------------------------------------------------ main
    def run(self, outdir, shard=1200):
        os.makedirs(outdir, exist_ok=True)
        leaders = self.find_leaders()
        order = sorted(leaders | (self.code if self.all_entries else set()))
        nins = 0; files = []
        decl = []
        shards = [order[i:i + shard] for i in range(0, len(order), shard)]
        for si, blks in enumerate(shards):
            fn = f"{self.name}_{si:03d}.c"
            with open(os.path.join(outdir, fn), 'w', newline='\n') as f:
                f.write('/* GENERATED by tools/recomp_mips/recomp.py - derived from user-supplied ROM data; do not redistribute. */\n')
                f.write('#include "rt.h"\n\n')
                for b in blks:
                    n, ls = self.gen_block(b, leaders)
                    nins += n
                    f.write(f"uint32_t {self.bp}{b:08x}(cpu_t *c)\n{{\n    c->cycles += {n};\n")
                    for l in ls: f.write("    " + l + "\n")
                    f.write("}\n\n")
                    decl.append(b)
            files.append(fn)
        with open(os.path.join(outdir, f"{self.name}_blocks.h"), 'w', newline='\n') as f:
            f.write('/* GENERATED */\n#pragma once\n#include "rt.h"\n')
            for b in decl: f.write(f"uint32_t {self.bp}{b:08x}(cpu_t *c);\n")
        lo = self.base
        nent = (self.end - self.base) >> 2
        with open(os.path.join(outdir, f"{self.name}_table.c"), 'w', newline='\n') as f:
            f.write('/* GENERATED */\n#include "rt.h"\n#include "%s_blocks.h"\n\n' % self.name)
            f.write(f"#define IMG_BASE 0x{self.base & 0x1fffffff:08x}u\n#define IMG_WORDS {nent}u\n\n")
            f.write("static const blk_fn tbl[IMG_WORDS] = {\n")
            for b in decl:
                f.write(f"    [(0x{b & 0x1fffffff:08x}u - IMG_BASE) >> 2] = {self.bp}{b:08x},\n")
            f.write("};\n\n")
            f.write("blk_fn %s_lookup(uint32_t pc)\n{\n    uint32_t p = pc & 0x1fffffffu;\n"
                    "    if (p < IMG_BASE || (p & 3u)) return 0;\n    p = (p - IMG_BASE) >> 2;\n"
                    "    return p < IMG_WORDS ? tbl[p] : 0;\n}\n" % self.name)
            # copy this image's entries into a runtime flat table covering [dst_base, dst_base + dst_words*4) (physical addresses)
            f.write("\nvoid %s_export(blk_fn *dst, uint32_t dst_base, uint32_t dst_words)\n{\n"
                    "    for (uint32_t i = 0; i < IMG_WORDS; i++) {\n"
                    "        uint32_t p = IMG_BASE + i * 4u;\n"
                    "        if (tbl[i] && p >= dst_base && ((p - dst_base) >> 2) < dst_words) dst[(p - dst_base) >> 2] = tbl[i];\n"
                    "    }\n}\n" % self.name)
        files.append(f"{self.name}_table.c")
        return dict(blocks=len(order), instructions=nins, files=files,
                    unknown_leaders=len(self.unknown_leaders), warnings=dict(self.warn),
                    warn_examples=dict(self.warn_examples))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True, help='main CPU ROM region bin (4 MB, LE)')
    ap.add_argument('--rom-offset', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--size', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--base', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--flow', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--name', default='game')
    a = ap.parse_args()
    rom = open(a.rom, 'rb').read()
    img = rom[a.rom_offset:a.rom_offset + a.size]
    fl = json.load(open(a.flow))
    g = Gen(img, a.base, fl['code'], fl['funcs'], fl['jump_tables'], a.name)
    rep = g.run(a.out)
    json.dump(rep, open(os.path.join(a.out, f"{a.name}_report.json"), 'w'), indent=1)
    print(json.dumps({k: v for k, v in rep.items() if k != 'files'}, indent=1))

if __name__ == '__main__':
    main()
