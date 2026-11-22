#!/usr/bin/env python3
"""Builds translations/<to>/<lang>/strings.ported.json for a game with no ROM bank (g13jnr): ports the strings of each source game (identical
picture content, see port_strings.py) and namespaces the scene-capture assets per source game ("cap:<src>_<scene>:..." + captures/<src>_<scene>.cap)
because scene names collide between games. usage: port_multi.py --to translations/g13jnr --lang fr golgo13=translations knd=translations/g13knd"""
import sys, os, json, shutil, subprocess, argparse
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--to', required=True); ap.add_argument('--lang', default='fr'); ap.add_argument('srcs', nargs='+'); a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__)); out = {}; ldir = os.path.join(a.to, a.lang); os.makedirs(os.path.join(a.to, 'captures'), exist_ok=True)
    keep = os.path.join(ldir, 'strings.json'); bak = open(keep, 'rb').read() if os.path.exists(keep) else None
    for s in a.srcs:
        pre, sdir = s.split('=', 1)
        subprocess.run([sys.executable, os.path.join(here, 'port_strings.py'), '--lang', a.lang, '--from', sdir, '--to', a.to], check=True)
        d = json.load(open(keep, encoding='utf-8'))['assets']
        for k, v in d.items():
            v = dict(v)
            if 'capture' in v:
                cn = v['capture']; nn = pre + '_' + cn; v['capture'] = nn
                for cd in (os.path.join(sdir, 'captures'), 'translations/captures'):
                    f = os.path.join(cd, cn + '.cap')
                    if os.path.exists(f): shutil.copy(f, os.path.join(a.to, 'captures', nn + '.cap')); break
                else: print('missing capture', cn)
                if k.startswith('cap:'): p = k.split(':', 2); k = 'cap:%s_%s:%s' % (pre, p[1], p[2])
                df = v.get('select', {}).get('distinct_from')
                if df:
                    v['select'] = dict(v['select']); v['select']['distinct_from'] = [pre + '_' + x for x in df]
                    for x in df:
                        for cd in (os.path.join(sdir, 'captures'), 'translations/captures'):
                            f = os.path.join(cd, x + '.cap')
                            if os.path.exists(f): shutil.copy(f, os.path.join(a.to, 'captures', pre + '_' + x + '.cap')); break
            out[k] = v
    if bak is not None: open(keep, 'wb').write(bak)
    json.dump({'assets': out}, open(os.path.join(ldir, 'strings.ported.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(len(out), 'assets in strings.ported.json')
main()
