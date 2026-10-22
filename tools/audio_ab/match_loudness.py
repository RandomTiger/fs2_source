# Copies a capture folder with each scene's DirectSound and EAX takes set to the same loudness (RMS), so a blind A/B
# compares placement, not volume (Tom, 2026-10-21). The louder take is turned down to the quieter one, so nothing clips.
# Usage: match_loudness.py <indir> <outdir>
import os, sys, glob, wave, numpy as np
src, dst = sys.argv[1:3]
os.makedirs(dst, exist_ok=True)
def rd(p):
    w = wave.open(p); return np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float), w.getparams()
for p in sorted(glob.glob(os.path.join(src, 'Audio*_DirectSound.wav'))):
    s = os.path.basename(p).split('_')[0]
    takes = {b: rd(os.path.join(src, '%s_%s.wav' % (s, b))) for b in ('DirectSound', 'EAX')}
    rms = {b: np.sqrt((t[0] ** 2).mean()) for b, t in takes.items()}
    quiet = min(rms.values())
    for b, (a, prm) in takes.items():
        g = quiet / rms[b] if rms[b] > 0 else 1.0
        with wave.open(os.path.join(dst, '%s_%s.wav' % (s, b)), 'wb') as w:
            w.setparams(prm); w.writeframes(np.clip(a * g, -32768, 32767).astype('<i2').tobytes())
        print(s, b, 'gain %.1f dB' % (20 * np.log10(g)))
