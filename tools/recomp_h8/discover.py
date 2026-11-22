#!/usr/bin/env python3
"""Iteratively run the native build and add every 'unknown code address' reported by the H8 runtime as a new root
(never emulated: each is recompiled). Usage: discover.py [frames] [max_iter]"""
import subprocess, sys, os, re, json
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PY = sys.executable
er = os.path.join(ROOT, 'work', 'h8', 'extra_roots.json')
frames = sys.argv[1] if len(sys.argv) > 1 else '600'
roots = json.load(open(er)) if os.path.exists(er) else []
for it in range(int(sys.argv[2]) if len(sys.argv) > 2 else 40):
    for step in ([PY, 'tools/recomp_h8/flow.py'], [PY, 'tools/recomp_h8/gen.py'], [PY, 'tools/build/build.py']):
        r = subprocess.run(step, cwd=ROOT, capture_output=True, text=True)
        if r.returncode: print("step failed:", step, r.stdout[-800:], r.stderr[-800:]); sys.exit(1)
    r = subprocess.run([os.path.join(ROOT, 'build', 'golgo13.exe'), '--roms', 'roms/golgo13', '--frames', frames], cwd=ROOT, capture_output=True, text=True, errors='replace')
    err = r.stderr
    m = re.search(r'\[h8 fatal\] jump to unknown code address ([0-9a-f]+)', err)
    if m:
        a = int(m.group(1), 16)
        if a in roots or a % 2: print("cannot add", hex(a)); print(err[-1500:]); sys.exit(2)
        roots.append(a); json.dump(sorted(roots), open(er, 'w')); print(f"iter {it}: + root {a:06x}"); continue
    print(f"iter {it}: no unknown H8 address.  exit={r.returncode}"); print("\n".join(l for l in err.splitlines() if 'write to ROM' not in l)[-3000:])
    break
