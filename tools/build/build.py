#!/usr/bin/env python3
"""Incremental build of build/golgo13.exe with MSVC (compiles only stale files per group, then links)."""
import os, sys, subprocess, glob
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
def find_vcvars():                                # VCVARS env var, else the first Visual Studio / Build Tools install found
    if os.environ.get('VCVARS'): return os.environ['VCVARS']
    for pf in (os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), os.environ.get('ProgramFiles', r'C:\Program Files')):
        for year in ('2022', '2019'):
            for ed in ('BuildTools', 'Community', 'Professional', 'Enterprise'):
                p = os.path.join(pf, 'Microsoft Visual Studio', year, ed, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
                if os.path.exists(p): return p
    vw = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if os.path.exists(vw):                         # any installed version (2019 / 2022 / newer), including the CI runners'
        try:
            path = subprocess.run([vw, '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
                                  capture_output=True, text=True).stdout.strip().splitlines()
            if path:
                p = os.path.join(path[0], 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
                if os.path.exists(p): return p
        except OSError: pass
    sys.exit('Visual Studio / Build Tools (MSVC) not found: set the VCVARS environment variable to vcvars64.bat')
VCVARS = find_vcvars()
SDL = os.path.join(ROOT, 'third_party', 'SDL3')
INC = ['third_party/SDL3/include', 'runtime/include', 'runtime/core', 'runtime/gte', 'runtime/h8', 'runtime/gpu', 'runtime/platform', 'runtime/audio', 'runtime/video', 'runtime/ui', 'generated', 'generated_h8'] + [d for d in ('generated_g13knd', 'generated_h8_g13knd', 'generated_g13jnr') if os.path.isdir(os.path.join(ROOT, d))]
GROUPS = {   # name -> (sources, header dirs that invalidate it)
    'mips': (sorted(glob.glob(os.path.join(ROOT, 'generated', '*.c'))), ['generated', 'runtime/include']),
    'h8': (sorted(glob.glob(os.path.join(ROOT, 'generated_h8', '*.c'))), ['generated_h8', 'runtime/h8']),
    'mips_g13knd': (sorted(glob.glob(os.path.join(ROOT, 'generated_g13knd', '*.c'))), ['generated_g13knd', 'runtime/include']),
    'h8_g13knd': (sorted(glob.glob(os.path.join(ROOT, 'generated_h8_g13knd', '*.c'))), ['generated_h8_g13knd', 'runtime/h8']),
    'mips_g13jnr': (sorted(glob.glob(os.path.join(ROOT, 'generated_g13jnr', '*.c'))), ['generated_g13jnr', 'runtime/include']),
    'rt': ([os.path.join(ROOT, p) for p in ('runtime/core/main.c', 'runtime/core/machine.c', 'runtime/core/loader.c', 'runtime/core/inflate.c', 'runtime/core/translate.c', 'runtime/gte/gte.c', 'runtime/gte/rt_gte.c', 'runtime/h8/h8_machine.c', 'runtime/gpu/gpu.c', 'runtime/audio/c352.c', 'runtime/video/mdec.c', 'runtime/platform/platform_sdl3.c', 'runtime/platform/wiimote.c', 'runtime/core/games_code.c', 'runtime/core/sys10.c', 'runtime/audio/spu.c', 'runtime/audio/mp3.c', 'runtime/ui/ui.c', 'runtime/ui/options.c', 'runtime/ui/menu.c') + (() if glob.glob(os.path.join(ROOT, 'generated', '*.c')) else ('runtime/core/no_game_code.c',))],
           ['runtime/include', 'runtime/core', 'runtime/gte', 'runtime/h8', 'runtime/gpu', 'runtime/platform', 'runtime/audio', 'runtime/video', 'runtime/ui']),
}
JOBS = max(1, (os.cpu_count() or 2) // 2)       # half of the cores: keep the machine usable while building
if os.name == 'nt':                               # children inherit a below-normal priority
    import ctypes
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
def newest_header(dirs):
    t = 0
    for d in dirs:
        for h in glob.glob(os.path.join(ROOT, d, '*.h')): t = max(t, os.path.getmtime(h))
    return t
def run(cmd):
    bat = os.path.join(ROOT, 'build', '_cl.bat')
    os.makedirs(os.path.dirname(bat), exist_ok=True)
    with open(bat, 'w') as f: f.write(f'@echo off\r\ncall "{VCVARS}" >nul 2>&1\r\ncd /d {ROOT}\r\n{cmd}\r\n')
    return subprocess.run(['cmd', '/c', bat], capture_output=True, text=True, errors='replace')
objs = []; failed = False
for name, (srcs, hdirs) in GROUPS.items():
    od = os.path.join(ROOT, 'build', 'obj', name); os.makedirs(od, exist_ok=True)
    hn = newest_header(hdirs); stale = []
    for s in srcs:
        o = os.path.join(od, os.path.splitext(os.path.basename(s))[0] + '.obj'); objs.append(o)
        if not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(s), hn): stale.append(s)
    if stale:
        inc = ' '.join('/I' + i for i in INC)
        r = run(f'cl /nologo /utf-8 /std:c11 /W3 /O2 /MP{JOBS} /c /D_CRT_SECURE_NO_WARNINGS {"/DRT_INLINE_MEM" if name.startswith("mips") else ""} {"/DHAVE_G13KND" if name == "rt" and os.path.isdir(os.path.join(ROOT, "generated_g13knd")) else ""} {"/DHAVE_G13JNR" if name == "rt" and os.path.isdir(os.path.join(ROOT, "generated_g13jnr")) else ""} {inc} /Fo{od}\ ' + ' '.join(f'"{s}"' for s in stale))
        err = [l for l in (r.stdout + r.stderr).splitlines() if ' error ' in l or 'error C' in l]
        if r.returncode or err: print("COMPILE FAILED in group", name); print("\n".join(err[:15])); failed = True; break
        print(f"[{name}] compiled {len(stale)}/{len(srcs)}")
if failed: sys.exit(1)
rsp = os.path.join(ROOT, 'build', '_link.rsp')
with open(rsp, 'w') as f: f.write(chr(10).join('"%s"' % o for o in objs))      # response file: the object list is too long for the command line
r = run('link /nologo /OUT:build' + chr(92) + os.environ.get('GOLGO13_OUT', 'golgo13.exe') + ' @"' + rsp + '" /LIBPATH:"' + os.path.join(SDL, 'lib', 'x64') + '" SDL3.lib hid.lib setupapi.lib bthprops.lib')
import shutil
try:
    if os.path.exists(os.path.join(SDL, 'lib', 'x64', 'SDL3.dll')): shutil.copy(os.path.join(SDL, 'lib', 'x64', 'SDL3.dll'), os.path.join(ROOT, 'build', 'SDL3.dll'))
except PermissionError: pass      # a running copy of the game holds the dll: it is the same file anyway
if r.returncode: print((r.stdout + r.stderr)[-2500:]); sys.exit(1)
print('built build/' + os.environ.get('GOLGO13_OUT', 'golgo13.exe'))
