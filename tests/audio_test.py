#!/usr/bin/env python3
"""Audio sanity: run the native build headless for 3700 frames and check the C352 output (88.2 kHz stereo):
silent during the slow H8 boot, then non-silent music/effects with a plausible level and no clipping."""
import subprocess, sys, os, wave, struct, math
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
out = os.path.join(ROOT, 'work', 'audio_test.wav')
r = subprocess.run([os.path.join(ROOT, 'build', 'golgo13.exe'), '--roms', os.path.join(ROOT, 'roms', 'golgo13'), '--frames', '3700', '--wav', out], capture_output=True, text=True)
if r.returncode: print("FAIL: runner exit", r.returncode); sys.exit(1)
w = wave.open(out, 'rb'); sr, n = w.getframerate(), w.getnframes(); data = struct.unpack('<%dh' % (n * 2), w.readframes(n))
def rms(a, b): seg = data[int(a * sr) * 2:int(b * sr) * 2]; return math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))
ok = True
early, late = rms(0, 30), rms(50, 61)
peak = max(abs(x) for x in data)
for name, cond, val in (("rate 88200 Hz", sr == 88200, sr), ("duration >= 60 s", n / sr >= 60, round(n / sr, 1)), ("silent before 30 s", early < 5, round(early, 1)),
                        ("audible 50-61 s (rms 100..5000)", 100 < late < 5000, round(late)), ("no clipping (peak < 32000)", peak < 32000, peak)):
    print(("PASS " if cond else "FAIL ") + name, val); ok &= cond
sys.exit(0 if ok else 1)
