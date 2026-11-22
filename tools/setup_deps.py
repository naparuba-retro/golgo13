#!/usr/bin/env python3
"""Fetches the small third-party source this project needs but does not ship: minimp3 (CC0, https://github.com/lieff/minimp3) into
third_party/minimp3/. The file hashes are pinned. (SDL3: tools/setup_sdl3.py on Windows, system package or CMake FetchContent elsewhere.)"""
import os, sys, hashlib, urllib.request
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BASE = 'https://raw.githubusercontent.com/lieff/minimp3/142823796ab8efa55e1bf11406028f3be77ecceb/'
FILES = {'minimp3.h': 'f9592be69e8b94044e9da6f45f993c9708000abae38cf6f44b70c699de55b509', 'LICENSE': '6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01'}
dest = os.path.join(ROOT, 'third_party', 'minimp3'); os.makedirs(dest, exist_ok=True)
for name, want in FILES.items():
    path = os.path.join(dest, name)
    if os.path.exists(path) and hashlib.sha256(open(path, 'rb').read()).hexdigest() == want: print(name, 'ok'); continue
    data = urllib.request.urlopen(BASE + name).read(); got = hashlib.sha256(data).hexdigest()
    if got != want: sys.exit('%s: unexpected SHA-256 %s (expected %s): upstream changed, update the pin after review' % (name, got, want))
    open(path, 'wb').write(data); print(name, 'downloaded')
