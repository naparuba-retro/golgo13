#!/usr/bin/env python3
"""Build build/mdec_test.exe: C port vs MAME's mdec.cpp (copied to build/mdec_ref with its dma.h/endianness.h includes removed)."""
import os, re, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
VCVARS = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
src = os.path.join(ROOT, 'third_party', 'mame', 'src', 'devices', 'cpu', 'psx')
dst = os.path.join(ROOT, 'build', 'mdec_ref'); os.makedirs(dst, exist_ok=True)
for f in ('mdec.cpp', 'mdec.h'):
    t = open(os.path.join(src, f), encoding='utf-8').read()
    t = re.sub(r'#include "(dma|endianness)\.h"\n', '', t)
    open(os.path.join(dst, f), 'w', encoding='utf-8').write(t)
lines = ['@echo off', f'call "{VCVARS}" >nul 2>&1', f'cd /d {ROOT}',
         r'cl /nologo /std:c++20 /EHsc /O2 /Itests\shim_mdec /Ibuild\mdec_ref /Fobuild\mdec_ref\ /c tests\mdec_ref.cpp tests\mdec_test.cpp || exit /b 1',
         r'cl /nologo /std:c11 /O2 /Iruntime\video /Fobuild\mdec_ref\ /c runtime\video\mdec.c || exit /b 1',
         r'link /nologo /OUT:build\mdec_test.exe build\mdec_ref\mdec_ref.obj build\mdec_ref\mdec_test.obj build\mdec_ref\mdec.obj']
bat = os.path.join(ROOT, 'build', '_mdec.bat')
with open(bat, 'w') as f: f.write('\r\n'.join(lines) + '\r\n')
r = subprocess.run(['cmd', '/c', bat], capture_output=True, text=True, errors='replace')
if r.returncode: print(r.stdout[-3000:]); sys.exit(1)
print("built build/mdec_test.exe")
