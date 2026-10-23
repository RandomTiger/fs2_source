# Captures each audio scene's game sound per backend, cut to the mission itself (log lines "Entered mission" to
# "REPLAY OK"), for back-to-back listening (Tom, 2026-10-20). Usage: capture.py <repo> <gamedir> <outdir> [scenes,...]
import os, sys, time, wave, threading, subprocess, glob
import numpy as np, soundcard as sc
import ctypes
from ctypes import wintypes

repo, game, out = sys.argv[1:4]
out = os.path.abspath(out)  # the game runs in its own folder, so -ini needs a full path
scenes = sys.argv[4].split(',') if len(sys.argv) > 4 else sorted(os.path.basename(p)[:-7] for p in glob.glob(os.path.join(repo, 'recordings', 'audio', 'Audio*.fs2rec')))
os.makedirs(out, exist_ok=True)
RATE = 48000
exe = os.path.join(game, 'FS2_audio_r.exe')
base_ini = open(os.path.join(game, 'fs2.ini'), encoding='latin-1').read().splitlines()
ini = {}
for b in ('DirectSound', 'EAX'):
    ini[b] = os.path.join(out, 'fs2_%s.ini' % b)
    with open(ini[b], 'w', encoding='latin-1') as f:
        f.write('\n'.join('Soundcard=' + b if l.startswith('Soundcard=') else l for l in base_ini) + '\n')

spk = sc.default_speaker()
mic = sc.get_microphone(id=str(spk.name), include_loopback=True)

def keep_alive(stop):
    # plays silence so the loopback stream delivers frames even when the game is silent (sample count = wall time)
    with spk.player(samplerate=RATE, channels=2) as p:
        z = np.zeros((RATE // 10, 2), dtype=np.float32)
        while not stop.is_set():
            p.play(z)

u32 = ctypes.windll.user32

def focus(pid):
    # the game is launched from a background process, so Windows will not activate its window on its own, and
    # DirectSound (exclusive level, no GLOBALFOCUS) mutes a game that is not in the foreground: silent takes
    found = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        q = wintypes.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(q))
        if q.value == pid and u32.IsWindowVisible(h):
            found.append(h)
        return True
    u32.EnumWindows(cb, 0)
    for h in found:
        u32.SetForegroundWindow(h)
        if u32.GetForegroundWindow() != h:
            u32.keybd_event(0x12, 0, 0, 0); u32.SetForegroundWindow(h); u32.keybd_event(0x12, 0, 2, 0)  # Alt unlocks SetForegroundWindow
    return bool(found) and u32.GetForegroundWindow() in found

def run(scene, backend):
    rec = os.path.join(repo, 'recordings', 'audio', scene + '.fs2rec')
    log = rec + '.replay.log'
    if os.path.exists(log):
        os.remove(log)
    trace = os.path.join(out, '%s_%s.trace' % (scene, backend))
    stop = threading.Event()
    chunks, marks = [], {}
    count = [0]
    def grab():
        with mic.recorder(samplerate=RATE, channels=2) as r:
            while not stop.is_set():
                d = r.record(numframes=RATE // 100)
                chunks.append(d)
                count[0] += len(d)
    ka = threading.Thread(target=keep_alive, args=(stop,)); ka.start()
    g = threading.Thread(target=grab); g.start()
    time.sleep(0.5)
    env = dict(os.environ, FS2_REPLAY_SOUND_MODEL='1')
    p = subprocess.Popen([exe, '-window', '-autotest', '-nomusic', '-replay', rec, '-replay_pace', '-trace', trace, '-ini', ini[backend]], cwd=game, env=env)
    text = ''
    focused, next_try = False, 0
    while p.poll() is None:
        if not focused and time.time() >= next_try:
            focused, next_try = focus(p.pid), time.time() + 0.25
        if os.path.exists(log):
            text = open(log, encoding='latin-1').read()
            if 'start' not in marks and 'Entered mission' in text:
                marks['start'] = count[0]
            if 'end' not in marks and ('REPLAY OK' in text or 'REPLAY FAILED' in text):
                marks['end'] = count[0]
        time.sleep(0.005)
    time.sleep(0.2)
    stop.set(); g.join(); ka.join()
    if os.path.exists(log):
        text = open(log, encoding='latin-1').read()
        os.remove(log)
    ok = 'REPLAY OK' in text and 'start' in marks and 'end' in marks
    data = np.concatenate(chunks)[marks.get('start', 0):marks.get('end', None)]
    path = os.path.join(out, '%s_%s.wav' % (scene, backend))
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes((np.clip(data, -1, 1) * 32767).astype('<i2').tobytes())
    peak = float(np.abs(data).max()) if len(data) else 0.0
    print('%s %s exit %s %s, %.1f s, peak %.3f%s' % (scene, backend, p.returncode, 'OK' if ok else 'FAILED', len(data) / RATE, peak, '' if focused else ', NO FOCUS'), flush=True)

for s in scenes:
    for b in os.environ.get('CAPTURE_BACKENDS', 'DirectSound,EAX').split(','):
        run(s, b)
print('DONE', flush=True)
