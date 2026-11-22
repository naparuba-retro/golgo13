#!/usr/bin/env python3
"""Directed tests for R3000-specific semantics that a MIPS32 oracle cannot check
(load delay slots, div by zero, overflow trap, branch/link ordering). Expected values follow MAME psxcpu."""
import sys, os, struct, subprocess
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'recomp_mips'))
from recomp import Gen
BASE = 0x80300000
def R(op, rs, rt, rd, sh, f): return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sh << 6) | f
def I(op, rs, rt, imm): return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def J(op, tgt): return (op << 26) | ((tgt >> 2) & 0x3ffffff)
zero, v0, v1, a0, a1, t0, t1, ra = 0, 2, 3, 4, 5, 8, 9, 31
NOP = 0
def addu(d, s, t): return R(0, s, t, d, 0, 0x21)
def lw(rt, off, rs): return I(0x23, rs, rt, off)
def lwl(rt, off, rs): return I(0x22, rs, rt, off)
def lwr(rt, off, rs): return I(0x26, rs, rt, off)
def addiu(rt, rs, imm): return I(9, rs, rt, imm)
def addi(rt, rs, imm): return I(8, rs, rt, imm)
def jr(rs): return R(0, rs, 0, 0, 0, 8)
def beq(rs, rt, off): return I(4, rs, rt, off)
def div(s, t): return R(0, s, t, 0, 0, 0x1a)
def divu(s, t): return R(0, s, t, 0, 0, 0x1b)
def mult(s, t): return R(0, s, t, 0, 0, 0x18)
def mflo(d): return R(0, 0, 0, d, 0, 0x12)
def mfhi(d): return R(0, 0, 0, d, 0, 0x10)
def lui(rt, imm): return I(0x0f, 0, rt, imm)
def ori(rt, rs, imm): return I(0x0d, rs, rt, imm)
def jal(t): return J(3, t)
END = [jr(ra), NOP]

def memw(a):
    x = ((a & ~3) ^ ((7 * 0x9e3779b9) & 0xffffffff)) & 0xffffffff   # same hash as tests/blocktest.c (seed 7)
    x ^= x >> 16; x = (x * 0x85ebca6b) & 0xffffffff; x ^= x >> 13; x = (x * 0xc2b2ae35) & 0xffffffff; x ^= x >> 16
    return x
PTR = 0x80500000
M0, M4 = memw(PTR), memw(PTR + 4)
RET = 0x80309000
# (name, code, initial regs, expected regs, [expected next pc], [expect exception])
tests = [
 ("lw-then-use reads OLD value", [lw(v0, 0, a0), addu(v1, v0, zero)] + END, {v0: 0x11111111, a0: PTR}, {v1: 0x11111111, v0: M0}),
 ("lw-then-alu-write same reg: ALU wins", [lw(v0, 0, a0), addiu(v0, zero, 5)] + END, {a0: PTR}, {v0: 5}),
 ("lw-then-lw same reg: second wins", [lw(v0, 0, a0), lw(v0, 4, a0)] + END, {a0: PTR}, {v0: M4}),
 ("lw, nop, use: sees loaded value", [lw(v0, 0, a0), NOP, addu(v1, v0, zero)] + END, {v0: 0x11111111, a0: PTR}, {v1: M0}),
 ("lwl/lwr chain forwards pending value", [lwl(v0, 3, a0), lwr(v0, 0, a0)] + END, {v0: 0xAAAAAAAA, a0: PTR}, {v0: M0}),
 ("lwl/lwr unaligned (offset 1)", [lwl(v0, 4, a0), lwr(v0, 1, a0)] + END, {v0: 0, a0: PTR},
  {v0: ((M0 >> 8) | (M4 << 24)) & 0xffffffff}),
 ("load then branch: delay slot sees loaded value", [lw(v0, 0, a0), beq(zero, zero, 1), addu(v1, v0, zero), NOP] + END,
  {v0: 0x11111111, a0: PTR}, {v1: M0}, BASE + 0x6000 + 0xc),
 ("div by zero (n>=0): lo=-1 hi=n", [div(t0, zero), mflo(v0), mfhi(v1)] + END, {t0: 5}, {v0: 0xffffffff, v1: 5}),
 ("div by zero (n<0): lo=1 hi=n", [div(t0, zero), mflo(v0), mfhi(v1)] + END, {t0: 0xfffffffb}, {v0: 1, v1: 0xfffffffb}),
 ("divu by zero: lo=-1 hi=n", [divu(t0, zero), mflo(v0), mfhi(v1)] + END, {t0: 7}, {v0: 0xffffffff, v1: 7}),
 ("div INT_MIN/-1: lo=INT_MIN hi=0", [div(t0, t1), mflo(v0), mfhi(v1)] + END, {t0: 0x80000000, t1: 0xffffffff}, {v0: 0x80000000, v1: 0}),
 ("div signed -7/2", [div(t0, t1), mflo(v0), mfhi(v1)] + END, {t0: 0xfffffff9, t1: 2}, {v0: 0xfffffffd, v1: 0xffffffff}),
 ("mult signed -2*3", [mult(t0, t1), mflo(v0), mfhi(v1)] + END, {t0: 0xfffffffe, t1: 3}, {v0: 0xfffffffa, v1: 0xffffffff}),
 ("jal: slot sees new ra", [jal(BASE + 0x100), addu(v0, ra, zero)], {}, {v0: BASE + 0xd000 + 8}, BASE + 0x100),
 ("jr: slot clobbering target reg keeps old target", [jr(v0), addiu(v0, zero, 1)], {v0: BASE + 0x40}, {v0: 1}, BASE + 0x40),
 ("addi overflow -> exception, dest unchanged", [lui(t0, 0x7fff), ori(t0, t0, 0xffff), addi(v0, t0, 1)] + END, {v0: 0x1234}, {v0: 0x1234}, 0xbad00000, True),
]

def main():
    out = os.path.join(ROOT, 'work', 'syn_gen'); os.makedirs(out, exist_ok=True)
    for f in os.listdir(out): os.remove(os.path.join(out, f))
    img = bytearray(0x1000 * (len(tests) + 1)); funcs = []; code = []
    for i, t in enumerate(tests):
        a = BASE + 0x1000 * i
        for k, w in enumerate(t[1]): struct.pack_into('<I', img, a - BASE + 4 * k, w); code.append(a + 4 * k)
        funcs.append(a)
    g = Gen(bytes(img), BASE, code, funcs, {}, 'syn')
    g.run(out, shard=1000)
    shim = ['#include "rt.h"', 'blk_fn syn_lookup(uint32_t);', 'blk_fn game_lookup(uint32_t pc) { return syn_lookup(pc); }', '']
    with open(os.path.join(out, 'shim.c'), 'w') as f: f.write("\n".join(shim))
    bat = os.path.join(ROOT, 'work', 'build_syn.bat')
    lines = ['@echo off',
             r'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1',
             r'cd /d C:\SOURCES\golgo\work',
             r'cl /nologo /std:c11 /W3 /Od /I..\runtime\include /Isyn_gen /Fosyn_gen\ ..\tests\blocktest.c syn_gen\syn_000.c syn_gen\syn_table.c syn_gen\shim.c /Fe:syn_gen\syn.exe > syn_gen\build.log 2>&1']
    with open(bat, 'w') as f: f.write("\r\n".join(lines) + "\r\n")
    subprocess.run(['cmd', '/c', bat], check=False)
    exe = os.path.join(out, 'syn.exe')
    if not os.path.exists(exe):
        print(open(os.path.join(out, 'build.log'), errors='replace').read()[-3000:]); return 2
    inp = os.path.join(ROOT, 'work', 'syn_in.bin'); outp = os.path.join(ROOT, 'work', 'syn_out.bin')
    with open(inp, 'wb') as f:
        for i, t in enumerate(tests):
            regs = [0] * 32
            for r, v in t[2].items(): regs[r] = v
            regs[ra] = regs[ra] or RET
            f.write(struct.pack('<34I', BASE + 0x1000 * i, *regs[1:], 0, 0) + struct.pack('<2I', 7, 0))
    subprocess.run([exe, inp, outp], check=True)
    data = open(outp, 'rb').read(); off = 0; fails = 0
    for i, t in enumerate(tests):
        nx, wild, hi, lo, nw = struct.unpack_from('<5I', data, off); off += 20
        regs = struct.unpack_from('<32I', data, off); off += 128 + 12 * nw
        exp = t[3]; exp_nx = t[4] if len(t) > 4 else RET; exc = len(t) > 5
        ok = nx == exp_nx and all(regs[r] == v for r, v in exp.items()) and (bool(wild) == exc)
        print(("PASS " if ok else "FAIL ") + t[0])
        if not ok:
            fails += 1
            print("    got", {r: hex(regs[r]) for r in exp}, "nx", hex(nx), "wild", wild, "| expected", {r: hex(v) for r, v in exp.items()}, "nx", hex(exp_nx))
    print(f"{len(tests) - fails}/{len(tests)} passed")
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main())
