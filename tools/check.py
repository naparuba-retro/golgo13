#!/usr/bin/env python3
"""Phase 1+2+3+5a end-to-end check: regenerate C from the ROM, compile it, run all differential/semantic tests.
Run with the project venv:  .venv/Scripts/python tools/check.py"""
import subprocess, sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PY = sys.executable
def run(title, cmd, **kw):
    print(f"\n=== {title}", flush=True)
    r = subprocess.run(cmd, cwd=ROOT, **kw)
    if r.returncode: print(f"FAILED: {title} (exit {r.returncode})"); sys.exit(r.returncode)
run("flow analysis (game, bios, kernel images)", [PY, 'tools/recomp_mips/analyze.py'], stdout=subprocess.DEVNULL)
run("generate C for all images", [PY, 'tools/recomp_mips/gen_all.py'], stdout=subprocess.DEVNULL)
run("H8 flow analysis + C generation", [PY, 'tools/recomp_h8/flow.py'], stdout=subprocess.DEVNULL)
run("H8 generate C", [PY, 'tools/recomp_h8/gen.py'], stdout=subprocess.DEVNULL)
run("compile generated C (MSVC /std:c11 /O2 /W3)", ['cmd', '/c', os.path.join(ROOT, 'tools', 'build', 'compile_generated.bat')], stdout=subprocess.DEVNULL)
run("build block test harness", ['cmd', '/c', os.path.join(ROOT, 'tools', 'build', 'build_blocktest.bat')], stdout=subprocess.DEVNULL)
run("build native runner (incremental)", [PY, 'tools/build/build.py'], stdout=subprocess.DEVNULL)
run("native boot vs MAME RAM dumps (frames 900, 3600)", [PY, 'tests/boot_test.py'])
run("C352 audio sanity (headless, 3700 frames)", [PY, 'tests/audio_test.py'])
run("block differential test vs Unicorn (R3000 blocks)", [PY, 'tests/blocktest.py', '30000'])
run("R3000 semantics (load delay, div, overflow, links)", [PY, 'tests/semantics_test.py'])
run("build GPU rasteriser test", ['cmd', '/c', os.path.join(ROOT, 'tools', 'build', 'build_gpu_test.bat')], stdout=subprocess.DEVNULL)
run("GPU rasteriser coverage (top-left rule, no seams)", [os.path.join(ROOT, 'build', 'gpu_test.exe')])
run("build MDEC test", [PY, 'tools/build/build_mdec_test.py'], stdout=subprocess.DEVNULL)
run("MDEC differential vs MAME mdec.cpp", [os.path.join(ROOT, 'build', 'mdec_test.exe'), '600'])
run("build GTE test", ['cmd', '/c', os.path.join(ROOT, 'tools', 'build', 'build_gte_test.bat')], stdout=subprocess.DEVNULL)
run("GTE differential vs MAME gte.cpp", [os.path.join(ROOT, 'build', 'gte_test.exe'), '300000'])
print("\nALL PHASE 1+2+3+5a CHECKS PASSED")
