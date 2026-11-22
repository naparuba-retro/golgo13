"""Helper to write translations/<game>/<lang>/strings.<pack>.json: mk(pack, [(ja, fr[, style]), ...]) in layout order (style: caption for 512x84, plate otherwise)."""
import json, os, sys
def mk(pack, items, root='translations/g13knd/fr'):
    a = {}
    for i, it in enumerate(items):
        if it is None: continue
        ja, fr = it[0], it[1]; style = it[2] if len(it) > 2 else None
        spec = {'ja': ja}
        if isinstance(fr, list): spec['lines'] = fr
        else: spec['text'] = fr
        spec['style'] = style or 'caption'
        a['%s/%02d' % (pack, i)] = spec
    json.dump({'assets': a}, open(os.path.join(root, 'strings.%s.json' % pack), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(pack, len(a), 'entries')
