#!/usr/bin/env python3
"""Fetch the official SDL3 development package for Windows (MSVC) into third_party/SDL3 (project-local, git-ignored).
Verifies the SHA-256 published by GitHub for the release asset. On Linux/macOS install SDL3 from the system package manager or build it."""
import os, sys, json, hashlib, urllib.request, zipfile, io
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
TAG = 'release-3.4.20'; NAME = 'SDL3-devel-3.4.20-VC.zip'
url = f'https://github.com/libsdl-org/SDL/releases/download/{TAG}/{NAME}'     # direct asset URL: no API call (unauthenticated API calls are rate-limited on CI runners)
want = ''
try:                                                                              # the published SHA-256, when the API answers (optional)
    api = f'https://api.github.com/repos/libsdl-org/SDL/releases/tags/{TAG}'
    rel = json.load(urllib.request.urlopen(api, timeout=30))
    want = (next(a for a in rel['assets'] if a['name'] == NAME).get('digest') or '').replace('sha256:', '')
except Exception as e: print('SHA-256 lookup skipped:', e)
for attempt in range(4):
    try: data = urllib.request.urlopen(url, timeout=120).read(); break
    except Exception as e:
        print('download failed:', e)
        if attempt == 3: raise
        import time; time.sleep(5 * (attempt + 1))
got = hashlib.sha256(data).hexdigest()
print(NAME, len(data), 'bytes, sha256', got)
if want and want != got: sys.exit('SHA-256 mismatch (expected %s)' % want)
dest = os.path.join(ROOT, 'third_party')
with zipfile.ZipFile(io.BytesIO(data)) as z: z.extractall(dest)
src = os.path.join(dest, 'SDL3-3.4.20')
tgt = os.path.join(dest, 'SDL3')
if os.path.isdir(src) and not os.path.isdir(tgt): os.rename(src, tgt)
print('SDL3 installed in', tgt)
