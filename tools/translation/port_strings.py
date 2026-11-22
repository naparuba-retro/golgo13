#!/usr/bin/env python3
"""Ports a language from one game to another: every asset of the source language whose picture content is identical in the target game
(same tile pixels, same canvas) is copied under its new id. Pictures that only exist in the target game are listed for translation.
usage: port_strings.py --lang fr --from translations --to translations/g13knd
The target dir needs manifest.json (tools/translation/manifest.py); the source dir has manifest.json + <lang>/strings*.json."""
import sys, os, json, glob, argparse, collections

def key(x): return (x['content_crc32'], tuple(x['canvas']), x['tiles'])

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--lang', required=True); ap.add_argument('--from', dest='src', default='translations'); ap.add_argument('--to', required=True)
    a = ap.parse_args()
    ma = json.load(open(os.path.join(a.src, 'manifest.json'), encoding='utf-8'))['assets']
    mb = json.load(open(os.path.join(a.to, 'manifest.json'), encoding='utf-8'))['assets']
    by_src = collections.defaultdict(list)
    for x in ma: by_src[key(x)].append(x['id'])
    by_dst = collections.defaultdict(list)
    for x in mb: by_dst[key(x)].append(x['id'])
    # source id -> list of target ids (identical content); several identical pictures: keep all of them
    ids = {}
    for k, sids in by_src.items():
        if k in by_dst:
            for s in sids: ids[s] = by_dst[k]
    sdir = os.path.join(a.src, a.lang); out = {}; used = set(); skipped = []
    files = [os.path.join(sdir, 'strings.json')] + sorted(glob.glob(os.path.join(sdir, 'strings.*.json')))
    for f in files:
        d = json.load(open(f, encoding='utf-8'))
        for aid, spec in d.get('assets', {}).items():
            if aid.startswith('cap:'):                       # scene captures are recognised by sprite pixels: copied as they are
                out[aid] = spec; continue
            if aid in ids:
                for t in ids[aid]: out[t] = spec; used.add(t)
            else: skipped.append(aid)
    meta = json.load(open(files[0], encoding='utf-8')).get('meta', {})
    os.makedirs(os.path.join(a.to, a.lang), exist_ok=True)
    json.dump({'meta': meta, 'assets': out}, open(os.path.join(a.to, a.lang, 'strings.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('%d assets ported, %d source assets without identical picture in the target (%s ...)' % (len(used), len(skipped), skipped[:6]))

if __name__ == '__main__':
    main()
