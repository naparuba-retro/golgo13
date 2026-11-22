#!/usr/bin/env python3
"""One-command setup: from your ROMs to a playable build.

    python tools/setup.py                 # everything for every ROM set found in roms/
    python tools/setup.py --games golgo13 # only some games (golgo13, g13knd, g13jnr)
    python tools/setup.py --no-build      # prepare everything, do not compile the final program

Steps (each one is skipped when already done, so it is safe to run again after adding a ROM):
  1. third-party files (SDL3 on Windows, minimp3) and the menu fonts;
  2. a first program build WITHOUT game code (menu only), which is also used to read your ROMs;
  3. for each ROM set found in roms/: write the ROM regions, analyse the code, generate the C (generated*/ : derived from YOUR ROMs, never published);
  4. the final build with the generated code.
"""
import os, sys, subprocess, shutil, argparse, glob, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PY = sys.executable
WIN = os.name == 'nt'
EXE = os.path.join(ROOT, 'build', 'golgo13.exe') if WIN else os.path.join(ROOT, 'build-linux', 'golgo13')
GAMES = {   # id -> (needs H8 firmware, regions dir, generated dir)
    'golgo13': (True, 'work/regions', 'generated'),
    'g13knd': (True, 'work/g13knd', 'generated_g13knd'),
    'g13jnr': (False, 'work/g13jnr', 'generated_g13jnr'),
}

def say(msg): print('\n=== ' + msg, flush=True)
def run(cmd, env=None, cwd=ROOT):
    e = dict(os.environ); e.update(env or {})
    r = subprocess.run(cmd, cwd=cwd, env=e)
    if r.returncode: sys.exit('FAILED: ' + ' '.join(map(str, cmd)))

def rom_present(gid):
    return any(os.path.exists(os.path.join(ROOT, 'roms', n)) for n in (gid + '.zip', gid))

def build(label):
    say('Build (%s)' % label)
    if WIN: run([PY, 'tools/build/build.py'])
    else:
        run(['cmake', '-S', '.', '-B', 'build-linux', '-DCMAKE_BUILD_TYPE=Release'] + (['-DGOLGO_FETCH_SDL3=ON'] if not shutil.which('pkg-config') else []))
        run(['cmake', '--build', 'build-linux', '--parallel', str(max(1, (os.cpu_count() or 2) // 2))])

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--games', default=','.join(GAMES)); ap.add_argument('--no-build', action='store_true'); a = ap.parse_args()
    wanted = [g for g in a.games.split(',') if g in GAMES]; t0 = time.time()
    os.makedirs(os.path.join(ROOT, 'roms'), exist_ok=True)
    found = [g for g in wanted if rom_present(g)]
    if not found:
        print('No ROM found. Put your ROM sets (golgo13.zip, g13knd.zip, g13jnr.zip) in:\n  %s\nthen run this script again.' % os.path.join(ROOT, 'roms')); return 1
    print('ROM sets found:', ', '.join(found), '| missing:', ', '.join(g for g in wanted if g not in found) or 'none')

    say('1/4 Third-party files and menu fonts')
    run([PY, '-m', 'pip', 'install', '--quiet', 'pillow', 'numpy'])
    run([PY, 'tools/setup_deps.py'])
    if WIN and not os.path.isdir(os.path.join(ROOT, 'third_party', 'SDL3')): run([PY, 'tools/setup_sdl3.py'])
    if subprocess.run([PY, 'tools/ui/bake_font.py'], cwd=ROOT).returncode: print('(menu fonts could not be baked: the plain text menu will be used)')

    say('2/4 Program without game code (also reads your ROMs)')
    if not os.path.exists(EXE): build('menu only')

    for gid in found:
        h8, regions, gen = GAMES[gid]; gen_h8 = gen.replace('generated', 'generated_h8', 1)
        has_mips = bool(glob.glob(os.path.join(ROOT, gen, '*.c'))); has_h8 = (not h8) or bool(glob.glob(os.path.join(ROOT, gen_h8, '*.c')))
        say('3/4 %s: ROM regions, code analysis, C generation (a few minutes)' % gid)
        if has_mips and has_h8:
            print('generated code already present (%s/): skipped. Delete that folder to regenerate.' % gen); continue
        env = {'GOLGO_GAME': gid}
        if not os.path.exists(os.path.join(ROOT, regions, 'maincpu.bin')):
            if gid == 'g13jnr': run([PY, 'tools/jnr/prepare.py'], env)
            else: run([EXE, '--game', gid, '--dump-regions', regions])
        if not has_mips: run([PY, 'tools/recomp_mips/analyze.py'], env); run([PY, 'tools/recomp_mips/gen_all.py'], env)
        if not has_h8: run([PY, 'tools/recomp_h8/flow.py'], env); run([PY, 'tools/recomp_h8/gen.py'], env)

    if a.no_build: print('\nPrepared. Build with: ' + ('python tools/build/build.py' if WIN else 'cmake --build build-linux')); return 0
    say('4/4 Final build with your games')
    build('with game code')
    print('\nDone in %.0f s. Run: %s' % (time.time() - t0, os.path.relpath(EXE, ROOT)))
    return 0

if __name__ == '__main__': sys.exit(main())
