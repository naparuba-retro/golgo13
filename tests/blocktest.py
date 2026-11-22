#!/usr/bin/env python3
"""Differential test: generated C blocks (build/blocktest.exe) vs Unicorn MIPS32 on random states.

Blocks excluded from the oracle (semantics differ between R3000 and MIPS32 or need devices):
COP0/COP2, syscall/break, div/divu, add/addi/sub (overflow trap), reserved ops, loads whose
result is read/written by the very next instruction (R3000 load delay), load/branch-in-slot.
"""
import sys, os, struct, random, subprocess, json, collections
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools', 'recomp_mips'))
from recomp import Gen
from unicorn import *
from unicorn.mips_const import *

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
BASE = 0x80300000; WIN_LO = 0x80400000; WIN_HI = 0x80800000
rom = open(os.path.join(ROOT, 'work/regions/maincpu.bin'), 'rb').read()
img = rom[0x28000:0x28000 + 0xf0000]
fl = json.load(open(os.path.join(ROOT, 'work/flow/game.json')))
g = Gen(img, BASE, fl['code'], fl['funcs'], fl['jump_tables'], 'game')
leaders = g.find_leaders()

def fmix(h):
    h &= 0xffffffff
    h ^= h >> 16; h = (h * 0x85ebca6b) & 0xffffffff; h ^= h >> 13; h = (h * 0xc2b2ae35) & 0xffffffff; h ^= h >> 16
    return h
def mem_word(a, seed): return fmix((a & ~3) ^ ((seed * 0x9e3779b9) & 0xffffffff))

def bytes_of(writes):
    # final memory image (word-granular) after applying the writes onto the fixed base memory
    d = {}
    for a, sz, v in writes:
        for i in range(sz):
            ba = a + i; wa = ba & ~3
            w = d.get(wa, mem_word(wa, 7)); sh = 8 * (ba & 3)
            d[wa] = (w & ~(0xff << sh)) | (((v >> (8 * i)) & 0xff) << sh)
    return d

def usable(pcs):
    prev_load = None
    for pc in pcs:
        w = g.W(pc); op = w >> 26; f = w & 0x3f; rs = (w >> 21) & 31; rt = (w >> 16) & 31
        if op in (0x10, 0x12, 0x32, 0x3a): return False
        if op == 0 and f in (0x0c, 0x0d, 0x1a, 0x1b, 0x20, 0x22): return False
        if op == 8: return False
        # reserved/unknown
        wr, isl, lwx = g.effects(w)
        if op in (0x13, 0x14, 0x15, 0x16, 0x17, 0x1c, 0x1d, 0x1e, 0x1f, 0x2c, 0x2d, 0x2f, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f): return False
        if op == 0 and f in (1, 5, 10, 11, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31) or (op == 0 and f in range(0x28, 0x2a)) or (op == 0 and f > 0x2b): return False
        if prev_load is not None:
            # next instruction after a load must not touch the loaded register (lwl/lwr chains are fine)
            reads = set()
            if op == 0: reads = {rs, rt}
            elif op in (4, 5): reads = {rs, rt}
            elif op in (1, 6, 7): reads = {rs}
            elif op in (0x28, 0x29, 0x2a, 0x2b, 0x2e): reads = {rs, rt}
            else: reads = {rs}
            if op not in (0x22, 0x26) and (prev_load in reads or wr == prev_load): return False
            if op in (0x22, 0x26) and prev_load in {rs}: return False
            prev_load = None
        if isl and wr: prev_load = wr
    return prev_load is None

def main():
    ncase = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    rnd = random.Random(1234)
    cands = []
    for b in sorted(leaders):
        n, _ = g.gen_block(b, leaders)
        pcs = list(g.cur)
        if usable(pcs) and len(pcs) == n: cands.append((b, pcs))
    print(f"blocks total {len(leaders)}, oracle-testable {len(cands)}")
    rnd.shuffle(cands)
    cases = []
    def jr_reg(pcs):
        if len(pcs) < 2: return None
        w = g.W(pcs[-2])
        if (w >> 26) == 0 and (w & 0x3f) in (8, 9): return (w >> 21) & 31
        return None
    kept = []
    for b, pcs in cands:
        r_ = jr_reg(pcs)
        if r_ is not None:
            sw = g.W(pcs[-1]); wr, _, _ = g.effects(sw)
            if wr == r_: continue
        kept.append((b, pcs))
    cands = kept
    for b, pcs in cands[:ncase]:
        for k in range(4):
            regs = [0] * 32
            for i in range(1, 32):
                regs[i] = (WIN_LO + rnd.randrange(0, WIN_HI - WIN_LO)) & ~3 if rnd.random() < 0.55 else rnd.getrandbits(32)
                if rnd.random() < 0.1: regs[i] = rnd.choice([0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 31, 32])
            jr = jr_reg(pcs)
            if jr: regs[jr] = (0x80309000 + 4 * rnd.randrange(0, 0x30000 // 4 * 4 // 4)) & ~3
            cases.append(dict(pc=b, regs=regs, hi=rnd.getrandbits(32), lo=rnd.getrandbits(32), seed=7, n=len(pcs), pcs=pcs))
    # ---- run C
    inp = os.path.join(ROOT, 'work/bt_in.bin'); outp = os.path.join(ROOT, 'work/bt_out.bin')
    with open(inp, 'wb') as f:
        for c in cases:
            f.write(struct.pack('<34I', c['pc'], *c['regs'][1:], c['hi'], c['lo']) + struct.pack('<2I', c['seed'], 0))
    r = subprocess.run([os.path.join(ROOT, 'build/blocktest.exe'), inp, outp])
    assert r.returncode == 0
    data = open(outp, 'rb').read(); off = 0; res = []
    for c in cases:
        nx, wild, hi, lo, nw = struct.unpack_from('<5I', data, off); off += 20
        regs = list(struct.unpack_from('<32I', data, off)); off += 128
        ws = [struct.unpack_from('<3I', data, off + 12 * i) for i in range(nw)]; off += 12 * nw
        res.append(dict(nx=nx, wild=wild, hi=hi, lo=lo, regs=regs, ws=ws))
    # ---- run Unicorn
    FIXED_SEED = 7
    memimg = b''.join(struct.pack('<I', mem_word(0x80400000 + o, FIXED_SEED)) for o in range(0, 0x400000, 4))
    writes = []; st = {'path': []}
    def hook_w(uc_, access, addr, size, value, ud):
        writes.append((addr if addr >= 0x80000000 else (addr | 0x80000000), size, value & ((1 << (8 * size)) - 1)))
    def hook_c(uc_, address, size, ud): st['path'].append(address)
    import unicorn.mips_const as MC
    regids = [None] + [getattr(MC, f'UC_MIPS_REG_{i}') for i in range(1, 32)]
    tested = bad = skipped = 0; fails = collections.Counter(); examples = []
    for c, rr in zip(cases, res):
        if rr['wild']: skipped += 1; continue
        writes.clear(); st['path'] = []
        uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
        uc.mem_map(0x300000, 0x100000); uc.mem_write(0x300000, img)
        uc.mem_map(0x400000, 0x400000); uc.mem_write(0x400000, memimg)
        uc.hook_add(UC_HOOK_MEM_WRITE, hook_w); uc.hook_add(UC_HOOK_CODE, hook_c)
        for i in range(1, 32): uc.reg_write(regids[i], c['regs'][i])
        uc.reg_write(UC_MIPS_REG_HI, c['hi']); uc.reg_write(UC_MIPS_REG_LO, c['lo'])
        try:
            if os.environ.get('G13_DEBUG'): print('case', hex(c['pc']), c['n'], json.dumps(c['regs']), c['hi'], c['lo'], flush=True)
            if rr['nx'] == c['pc']: skipped += 1; continue
            uc.emu_start(c['pc'], rr['nx'], count=c['n'] + 2)
        except UcError as e:
            skipped += 1; continue
        exp_regs = [0] + [uc.reg_read(regids[i]) for i in range(1, 32)]
        if st['path'] != c['pcs'] and not (0x80300000 <= rr['nx'] < 0x803f0000): skipped += 1; continue
        exp_pc = rr['nx'] if st['path'] == c['pcs'] else 0xffffffff; exp_hi = uc.reg_read(UC_MIPS_REG_HI); exp_lo = uc.reg_read(UC_MIPS_REG_LO)
        tested += 1
        ok = (exp_pc == rr['nx'] and exp_hi == rr['hi'] and exp_lo == rr['lo'] and exp_regs == rr['regs'] and bytes_of(writes) == bytes_of(rr['ws']))
        if not ok:
            bad += 1
            kind = 'pc' if exp_pc != rr['nx'] else 'regs' if exp_regs != rr['regs'] else 'hilo' if (exp_hi, exp_lo) != (rr['hi'], rr['lo']) else 'writes'
            fails[kind] += 1
            if False: print('  DIFF', hex(c['pc']), sorted((hex(a),v) for a,v in bytes_of(writes).items()), '| C:', sorted((hex(a),v) for a,v in bytes_of(rr['ws']).items()), 'regs a1,t3', hex(c['regs'][5]), hex(c['regs'][11]))
            if len(examples) < 6: examples.append((hex(c['pc']), kind, hex(exp_pc), hex(rr['nx']), [i for i in range(32) if exp_regs[i] != rr['regs'][i]][:4]))
    print(f"cases {len(cases)}  compared {tested}  skipped(wild/err) {skipped}  MISMATCH {bad}  {dict(fails)}")
    for e in examples: print("  ", e)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
