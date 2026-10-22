# Back-to-back A/B player for the audio captures (Tom, 2026-10-20). Usage: player.py <capture dir>
# Per scene: 1 = play A, 2 = play B, Space = A then B with no gap, T = A and B swapped every 3 s at the same
# moment of the mission, S = stop, Enter = answer. A and B are random per scene; one scene is the same take twice
# as a check. The order goes to order.txt, to be opened afterwards.
import os, sys, glob, wave, random, msvcrt, winsound, datetime
import numpy as np

d = sys.argv[1]
RATE = 48000
scenes = sorted({os.path.basename(p).split('_')[0] for p in glob.glob(os.path.join(d, 'Audio*_DirectSound.wav'))})
tmp = os.path.join(d, 'play'); os.makedirs(tmp, exist_ok=True)
stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M')
answers = os.path.join(d, 'answers_%s.txt' % stamp); order = os.path.join(d, 'order_%s.txt' % stamp)

def rd(p):
    with wave.open(p) as w:
        return np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, 2).astype(np.float32)

def wr(p, x):
    with wave.open(p, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(np.clip(x, -32768, 32767).astype('<i2').tobytes())

def fade(x, n=480):
    x = x.copy(); r = np.linspace(0, 1, min(n, len(x)))[:, None]
    x[:len(r)] *= r; x[len(x) - len(r):] *= r[::-1]; return x

def play(p):
    winsound.PlaySound(None, 0)
    winsound.PlaySound(p, winsound.SND_FILENAME | winsound.SND_ASYNC)

rng = random.Random()
check = rng.choice(scenes)
with open(order, 'w') as f:
    f.write('Audio A/B %s, captures in %s, check scene %s (same take twice)\n' % (stamp, d, check))
with open(answers, 'w') as f:
    f.write('Audio A/B %s: A better, B better or S same, per scene\n' % stamp)

print('%d scenes. Keys: 1 = A, 2 = B, Space = A then B, T = swap every 3 s, S = stop, Enter = answer.' % len(scenes))
for s in scenes:
    pair = ['DirectSound', 'EAX']; rng.shuffle(pair)
    if s == check:
        pair = [pair[0], pair[0]]
    a, b = rd(os.path.join(d, '%s_%s.wav' % (s, pair[0]))), rd(os.path.join(d, '%s_%s.wav' % (s, pair[1])))
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    pa, pb, pab, pt = [os.path.join(tmp, s + x + '.wav') for x in ('_A', '_B', '_AB', '_T')]
    wr(pa, a); wr(pb, b)
    wr(pab, np.concatenate([fade(a), fade(b)]))
    seg = 3 * RATE
    wr(pt, np.concatenate([fade((a if (i // seg) % 2 == 0 else b)[i:i + seg]) for i in range(0, n, seg)]))
    print('\n%s (%.0f s). A then B is %.0f s; swap mode starts on A: A 0-3 s, B 3-6 s, A 6-9 s ...' % (s, n / RATE, 2 * n / RATE))
    while True:
        k = msvcrt.getwch()
        if k == '1': play(pa); print('  playing A')
        elif k == '2': play(pb); print('  playing B')
        elif k == ' ': play(pab); print('  playing A then B')
        elif k in 'tT': play(pt); print('  playing swap mode')
        elif k in 'sS': winsound.PlaySound(None, 0); print('  stopped')
        elif k == '\r': break
    winsound.PlaySound(None, 0)
    ans = ''
    while ans not in ('A', 'B', 'S'):
        ans = input('  A, B or S (same): ').strip().upper()
    note = input('  Note (Enter for none): ').strip()
    with open(answers, 'a') as f: f.write('%s\t%s\t%s\n' % (s, ans, note))
    with open(order, 'a') as f: f.write('%s\tA=%s\tB=%s%s\n' % (s, pair[0], pair[1], '\tcheck: same take twice' if s == check else ''))
print('\nDone. Answers: %s\nOrder (open after): %s' % (answers, order))
