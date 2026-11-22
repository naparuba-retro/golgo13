"""Generic static control-flow discovery for R3000 code images (used for game, BIOS ROM, kernel RAM images).

Image = bytes at virtual `base`. Addresses are matched alias-agnostically (low 29 bits) so that
0x00xxxxxx / 0x80xxxxxx / 0xA0xxxxxx / 0xBFCxxxxx / 0x9FCxxxxx constants resolve to the same image;
discovered addresses are expressed with the image's own alias (`base`'s top bits).

Passes: roots + recursive descent (delay slots, jump tables by constant propagation), tables of code
pointers in data, lui/addiu(ori) constants pointing into text, orphan function prologues.
"""
import struct, collections

def sx16(v): return v - 0x10000 if v & 0x8000 else v

class Flow:
    def __init__(self, img, base, text, roots, extra_roots=(), prologue_pass=True):
        self.img = img; self.base = base; self.size = len(img)
        self.phys0 = base & 0x1fffffff
        self.alias = base & 0xe0000000
        self.t0, self.t1 = text
        self.code = set(); self.funcs = set(); self.work = []
        self.jtabs = {}; self.unres = []
        self.roots = list(roots) + list(extra_roots)
        self.prologue_pass = prologue_pass

    # --- address helpers
    def canon(self, v):
        """Map any alias of an address inside the image to the image's own alias, else None."""
        p = v & 0x1fffffff
        if self.phys0 <= p < self.phys0 + self.size and v % 4 == 0:
            return self.base + (p - self.phys0)
        return None
    def inimg(self, a): return self.canon(a) is not None
    def intext(self, a): return self.t0 <= a < self.t1 and a % 4 == 0
    def W(self, a): return struct.unpack_from('<I', self.img, a - self.base)[0]

    def decode_flow(self, a):
        w = self.W(a); op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; f = w & 0x3f
        if op == 0 and f == 8: return ('jr', rs, None)
        if op == 0 and f == 9: return ('jalr', rs, None)
        if op in (2, 3):
            t = ((w & 0x3ffffff) << 2) | (a & 0xf0000000)
            return ('j' if op == 2 else 'jal', None, self.canon(t) or t)
        if op in (4, 5, 6, 7) or (op == 1 and rt in (0, 1)):
            return ('br', None, a + 4 + (sx16(w & 0xffff) << 2))
        if op == 1 and rt in (16, 17):
            return ('bral', None, a + 4 + (sx16(w & 0xffff) << 2))
        return None

    def add_func(self, a):
        if self.intext(a) and a not in self.funcs:
            self.funcs.add(a); self.work.append(a)

    def jumptable(self, a):
        w = self.W(a); rx = (w >> 21) & 31
        best = None
        for win in (16, 28, 48):
            const = {}; sym = {}; tab = None
            for b in range(max(self.base, a - 4 * win), a, 4):
                x = self.W(b); op = x >> 26; rs = (x >> 21) & 31; rt = (x >> 16) & 31; rd = (x >> 11) & 31; f = x & 0x3f; imm = x & 0xffff
                if op == 0x0f: const[rt] = imm << 16; sym.pop(rt, None); continue
                if op in (9, 8) and rs in const: const[rt] = (const[rs] + sx16(imm)) & 0xffffffff; sym.pop(rt, None); continue
                if op == 0x0d and rs in const: const[rt] = const[rs] | imm; sym.pop(rt, None); continue
                if op == 0 and f in (0x21, 0x20):
                    ca = const.get(rs); cb = const.get(rt)
                    if ca is not None and cb is None and self.inimg(ca): sym[rd] = ca; const.pop(rd, None); continue
                    if cb is not None and ca is None and self.inimg(cb): sym[rd] = cb; const.pop(rd, None); continue
                    if ca is not None and cb is not None: const[rd] = (ca + cb) & 0xffffffff; continue
                    const.pop(rd, None); sym.pop(rd, None); continue
                if op == 0x23:
                    if rs in sym:
                        t = (sym[rs] + sx16(imm)) & 0xffffffff
                        if self.inimg(t): tab = (b, t, rt)
                    const.pop(rt, None); sym.pop(rt, None); continue
                if op == 0 and f not in (8, 9): const.pop(rd, None); sym.pop(rd, None)
                elif op not in (0x2b, 0x28, 0x29, 0x2a, 0x2e, 4, 5, 6, 7, 1, 2, 3): const.pop(rt, None); sym.pop(rt, None)
            if tab and tab[2] == rx: best = tab; break
            if tab and best is None: best = tab
        if best is None: return None
        _, base, _ = best
        base = self.canon(base)
        n = None
        for k in range(1, 40):
            if a - 4 * k < self.base: break
            c = self.W(a - 4 * k)
            if (c >> 26) == 0x0b: n = c & 0xffff; break
        ents = []; i = 0
        while True:
            if n is not None and i >= n: break
            ad = base + 4 * i
            if ad + 4 > self.base + self.size: break
            t = self.canon(self.W(ad))
            if t is None or not self.intext(t) or abs(t - a) >= 0x10000: break
            ents.append(t); i += 1
            if i > 512: break
        if not ents: return None
        return base, ents, n

    def const_target(self, a):
        """jr/jalr rX where rX was built by lui+addiu/ori shortly before: return canonical target or None."""
        rx = (self.W(a) >> 21) & 31
        const = {}
        for b in range(max(self.base, a - 4 * 8), a, 4):
            x = self.W(b); op = x >> 26; rs = (x >> 21) & 31; rt = (x >> 16) & 31; imm = x & 0xffff
            if op == 0x0f: const[rt] = imm << 16
            elif op in (8, 9) and rs in const: const[rt] = (const[rs] + sx16(imm)) & 0xffffffff
            elif op == 0x0d and rs in const: const[rt] = const[rs] | imm
            elif op in (8, 9, 0x0d, 0x0f, 0x23, 0x20, 0x21, 0x24, 0x25): const.pop(rt, None)
            elif op == 0:
                const.pop((x >> 11) & 31, None)
        v = const.get(rx)
        if v is None: return None
        return self.canon(v) or (v if self.intext(v) else None)

    def run(self, start):
        pc = start
        while True:
            if not self.intext(pc) or pc in self.code: return
            fl = self.decode_flow(pc)
            self.code.add(pc)
            if fl is None: pc += 4; continue
            k, rs, t = fl
            self.code.add(pc + 4)
            if k == 'jal': self.add_func(t); pc += 8; continue
            if k == 'jalr':
                ct = self.const_target(pc)
                if ct is not None: self.add_func(ct)
                pc += 8; continue
            if k == 'bral': self.add_func(t); pc += 8; continue
            if k == 'j':
                if self.intext(t): self.work.append(t)
                return
            if k == 'br': self.work.append(t); pc += 8; continue
            if k == 'jr':
                if rs == 31: return
                ct = self.const_target(pc)
                if ct is not None:
                    self.work.append(ct); return
                r = self.jumptable(pc)
                if r:
                    self.jtabs[pc] = r
                    for e in r[1]: self.work.append(e)
                else: self.unres.append(pc)
                return

    def drain(self):
        while self.work:
            a = self.work.pop()
            if a not in self.code: self.run(a)

    def analyze(self):
        for r in self.roots: self.add_func(r)
        self.drain()
        n1 = len(self.code)
        # tables of code pointers in data
        ptrs = {}
        for a in range(self.base, self.base + self.size, 4):
            if a in self.code: continue
            t = self.canon(self.W(a))
            if t is not None and self.intext(t): ptrs[a] = t
        runs = []; cur = []
        for a in sorted(ptrs):
            if cur and a == cur[-1] + 4: cur.append(a)
            else:
                if len(cur) >= 2: runs.append(cur)
                cur = [a]
        if len(cur) >= 2: runs.append(cur)
        added = 0
        for r in runs:
            tg = [ptrs[a] for a in r]
            ok = sum(1 for t in tg if (self.W(t) >> 16) == 0x27bd or t in self.funcs)
            if ok >= max(2, len(tg) // 2):
                for t in tg:
                    if t not in self.funcs: self.add_func(t); added += 1
        self.drain()
        # lui+addiu/ori constants into text
        la = 0
        for a in sorted(self.code):
            x = self.W(a)
            if (x >> 26) != 0x0f: continue
            r = (x >> 16) & 31; hi = (x & 0xffff) << 16
            for k in range(1, 5):
                if a + 4 * k >= self.base + self.size: break
                y = self.W(a + 4 * k)
                if (y >> 26) in (9, 0x0d) and ((y >> 21) & 31) == r:
                    v = (hi + sx16(y & 0xffff)) & 0xffffffff if (y >> 26) == 9 else hi | (y & 0xffff)
                    t = self.canon(v)
                    if t is not None and self.intext(t) and t not in self.code and t not in self.funcs:
                        self.add_func(t); la += 1
                    break
        self.drain()
        orphans = 0
        if self.prologue_pass:
            for a in range(self.t0, self.t1, 4):
                if a not in self.code and a + 4 <= self.base + self.size and (self.W(a) >> 16) == 0x27bd and (self.W(a) & 0x8000):
                    self.add_func(a); orphans += 1
            self.drain()
        tot = (self.t1 - self.t0) // 4
        return dict(code_words=len(self.code), funcs=len(self.funcs), jump_tables=len(self.jtabs), unresolved_jr=len(self.unres),
                    ptr_roots=added, la_roots=la, orphan_prologues=orphans, text_words=tot,
                    coverage=round(len(self.code) / tot, 4))

    def export(self):
        return dict(base=self.base, size=self.size, funcs=sorted(self.funcs), code=sorted(self.code),
                    jump_tables={hex(k): {'base': hex(v[0]), 'n': v[2], 'targets': [hex(x) for x in v[1]]} for k, v in self.jtabs.items()},
                    unresolved_jr=[hex(x) for x in self.unres])
