#!/usr/bin/env python3
"""Records a scene exactly as the game draws it (every 4/8-bit sprite quad with its decoded pixels) into translations/captures/<scene>.cap.
The capture holds pixels of the game: it is git-ignored and must be regenerated from your own ROM.

usage: capture_scene.py <scene> [--exe build/golgo13.exe] [--rom roms/golgo13] [--translations translations]
Scenes are described in translations/scenes.json:
  {"card_m1": {"note": "...", "frame": 8100, "presses": ["2000:6:coin", "2030:6:coin", "2600:30:start", "4000:8:trigger"]}}
`presses` are the scripted inputs (frame:duration:button) that bring the game to the scene; `frame` is the video frame to record."""
import sys, os, json, argparse, subprocess

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('scene'); ap.add_argument('--exe', default='build/golgo13.exe')
    ap.add_argument('--rom', default='roms/golgo13'); ap.add_argument('--translations', default='translations'); ap.add_argument('--game', default=None); a = ap.parse_args()
    scenes = json.load(open(os.path.join(a.translations, 'scenes.json'), encoding='utf-8'))
    sc = scenes[a.scene]; os.makedirs(os.path.join(a.translations, 'captures'), exist_ok=True)
    out = os.path.join(a.translations, 'captures', a.scene + '.cap')
    if os.path.exists(out): os.remove(out)
    cmd = [os.path.abspath(a.exe)] + (['--game', a.game] if a.game else []) + (['--roms', a.rom] if a.rom else []) + ['--headless', '--frames', str(sc['frame'] + 2)]
    if sc.get('replay'): cmd += ['--replay', os.path.join(a.translations, sc['replay'])]
    for p in sc.get('presses', []): cmd += ['--press', p]
    if sc.get('aim'): cmd += ['--aim', sc['aim']]
    cmd += ['--quadcap', '%s:%d' % (os.path.abspath(out).replace('\\', '/'), sc['frame'])]
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if not os.path.exists(out): sys.exit('no capture produced (frame %d never drew a sprite quad?)\n%s' % (sc['frame'], r.stderr[-400:]))
    print('%s: %d bytes -> %s' % (a.scene, os.path.getsize(out), out))

if __name__ == '__main__':
    main()
