#!/usr/bin/env python3
"""Phase 2 regression: native boot (BIOS + kernel + game init, up to the first H8 handshake) vs a MAME RAM dump.
Needs work/trace/ram_f900.bin (MAME RAM at frame 900, produced locally by work/trace/trace.lua) and build/golgo13.exe."""
import subprocess, sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
exe = os.path.join(ROOT, 'build', 'golgo13.exe'); out = os.path.join(ROOT, 'work', 'ram_run900.bin')
r = subprocess.run([exe, '--roms', os.path.join(ROOT, 'roms', 'golgo13'), '--frames', '900', '--dump-ram', out], capture_output=True, text=True)
sys.stderr.write("\n".join(l for l in r.stderr.splitlines() if 'write to ROM' not in l) + "\n")
if r.returncode: print("FAIL: runner exited with", r.returncode); sys.exit(1)
mine = open(out, 'rb').read(); ref = open(os.path.join(ROOT, 'work', 'trace', 'ram_f900.bin'), 'rb').read()
def ratio(a, b):
    m, r_ = mine[a:b], ref[a:b]
    nz = sum(1 for x, y in zip(m, r_) if x or y); eq = sum(1 for x, y in zip(m, r_) if (x or y) and x == y)
    return nz, (eq / nz if nz else 1.0)
bad = 0
def check(tag, mine, ref, rows):
    global bad
    for name, a, b, thr in rows:
        m, r_ = mine[a:b], ref[a:b]
        nz = sum(1 for x, y in zip(m, r_) if x or y); eq = sum(1 for x, y in zip(m, r_) if (x or y) and x == y); q = eq / nz if nz else 1.0
        ok = q >= thr
        print(f"{'PASS' if ok else 'FAIL'} [{tag}] {name:22s} nonzero bytes {nz:7d}  equal {q:.4f} (>= {thr})"); bad += not ok
# frame 3600 (60 s): the whole boot incl. the slow H8 handshakes; native H8 firmware
out3 = os.path.join(ROOT, 'work', 'ram_run3600.bin')
r3 = subprocess.run([exe, '--roms', os.path.join(ROOT, 'roms', 'golgo13'), '--frames', '3600', '--dump-ram', out3], capture_output=True, text=True)
if r3.returncode: print("FAIL: runner (3600 frames) exited with", r3.returncode); sys.exit(1)
ref3 = os.path.join(ROOT, 'work', 'trace', 'ram_f3600.bin')
if os.path.exists(ref3):
    check('f3600', open(out3, 'rb').read(), open(ref3, 'rb').read(),
          (("kernel RAM", 0, 0x9600, 1.0), ("game text/rodata", 0x300000, 0x39ed30, 1.0), ("RAM 0x9600-0x300000", 0x9600, 0x300000, 0.99), ("game data/bss", 0x39ed30, 0x3f0000, 0.8), ("stacks", 0x3f0000, 0x400000, 0.98)))
out5 = os.path.join(ROOT, 'work', 'ram_run5400.bin')
r5 = subprocess.run([exe, '--roms', os.path.join(ROOT, 'roms', 'golgo13'), '--frames', '5400', '--dump-ram', out5], capture_output=True, text=True)
if r5.returncode: print("FAIL: runner (5400 frames) exited with", r5.returncode); sys.exit(1)
ref5 = os.path.join(ROOT, 'work', 'trace', 'ram_f5400.bin')
if os.path.exists(ref5):
    check('f5400', open(out5, 'rb').read(), open(ref5, 'rb').read(),
          (("kernel RAM", 0, 0x9600, 1.0), ("game text/rodata", 0x300000, 0x39ed30, 1.0), ("RAM 0x9600-0x300000", 0x9600, 0x300000, 0.95), ("game data/bss", 0x39ed30, 0x3f0000, 0.9), ("stacks", 0x3f0000, 0x400000, 0.98)))
for name, a, b, thr in (("kernel RAM 0x0-0x9600", 0, 0x9600, 1.0), ("game text/rodata", 0x300000, 0x39ed30, 1.0), ("game data/bss", 0x39ed30, 0x3f0000, 0.99), ("stacks", 0x3f0000, 0x400000, 0.85)):
    nz, q = ratio(a, b); ok = q >= thr
    print(f"{'PASS' if ok else 'FAIL'} {name:24s} nonzero bytes {nz:7d}  equal {q:.4f} (>= {thr})"); bad += not ok
sys.exit(1 if bad else 0)
