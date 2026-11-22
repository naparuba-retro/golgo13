#!/usr/bin/env python3
"""Appends translations to translations/g13jnr/fr/strings.jnr.json. usage: tr_add.py < lines   (each line:  id | style | fr  ; style = c(aption) p(late) o(bjective) t(itle) or a style name)"""
import sys, json, os
P = 'translations/g13jnr/fr/strings.jnr.json'
ST = {'c': 'caption', 'p': 'plate', 'o': 'objective', 't': 'story_title'}
d = json.load(open(P, encoding='utf-8')) if os.path.exists(P) else {'assets': {}}
n = 0
for line in sys.stdin.buffer.read().decode('utf-8').splitlines():
    line = line.strip()
    if not line or line.startswith('#'): continue
    aid, st, fr = [x.strip() for x in line.split('|', 2)]
    d['assets'][aid] = {'text': fr, 'style': ST.get(st, st)}; n += 1
json.dump(d, open(P, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(n, 'added,', len(d['assets']), 'total')
