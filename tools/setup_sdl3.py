#!/usr/bin/env python3
"""Fetch the official SDL3 development package for Windows (MSVC) into third_party/SDL3 (project-local, git-ignored).
Verifies the SHA-256 published by GitHub for the release asset. On Linux/macOS install SDL3 from the system package manager or build it."""
import os, sys, json, hashlib, urllib.request, zipfile, io
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
TAG = 'release-3.4.20'; NAME = 'SDL3-devel-3.4.20-VC.zip'
api = f'https://api.github.com/repos/libsdl-org/SDL/releases/tags/{TAG}'
rel = json.load(urllib.request.urlopen(api))
asset = next(a for a in rel['assets'] if a['name'] == NAME)
want = (asset.get('digest') or '').replace('sha256:', '')
data = urllib.request.urlopen(asset['browser_download_url']).read()
got = hashlib.sha256(data).hexdigest()
print(NAME, len(data), 'bytes, sha256', got)
if want and want != got: sys.exit('SHA-256 mismatch (expected %s)' % want)
dest = os.path.join(ROOT, 'third_party')
with zipfile.ZipFile(io.BytesIO(data)) as z: z.extractall(dest)
src = os.path.join(dest, 'SDL3-3.4.20')
tgt = os.path.join(dest, 'SDL3')
if os.path.isdir(src) and not os.path.isdir(tgt): os.rename(src, tgt)
print('SDL3 installed in', tgt)
