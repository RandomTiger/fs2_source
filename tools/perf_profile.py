#!/usr/bin/env python
"""Summarize one profile run written by the Godot wrapper (godot/src/perf_prof.cpp): <dir>/<tag>_game.csv, _godot.csv, _totals.txt, _samples.txt.
Usage: python -I perf_profile.py <dir> <tag> [--spike-ms 8.0] [--warm 300] [--top 25]
Numbers only; the markdown report is written by hand from this output."""
import csv, sys, os, collections, statistics, argparse

ap = argparse.ArgumentParser()
ap.add_argument("dir"); ap.add_argument("tag")
ap.add_argument("--spike-ms", type=float, default=8.0)   # a game frame over this misses the 120 Hz period (8.33 ms)
ap.add_argument("--warm", type=int, default=300)         # game frames at the start (mission load) reported apart
ap.add_argument("--top", type=int, default=25)
a = ap.parse_args()

def rd(name):
    p = os.path.join(a.dir, a.tag + name)
    if not os.path.exists(p): return None
    with open(p, newline="") as f: return list(csv.DictReader(f))

def pct(v, q):
    if not v: return 0.0
    v = sorted(v); k = (len(v) - 1) * q; lo = int(k); hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)

def mean(v): return sum(v) / len(v) if v else 0.0

game = rd("_game.csv"); godot = rd("_godot.csv")
zones = [c[2:] for c in game[0].keys() if c.startswith("z_")]
ctrs = [c[2:] for c in game[0].keys() if c.startswith("c_")]
for r in game:
    for k in r:
        r[k] = float(r[k])
    r["sink"] = sum(r["z_" + z] for z in zones)
    r["game_only"] = r["total_us"] - r["sink"]
    r["tex"] = r["z_tex_create"] + r["z_tex_free"]
    r["readback"] = r["z_read_region"] + r["z_capture"]
    r["acc"] = r["c_frame_accepted"] > 0

print(f"== {a.tag}")
n = len(game)
body = [r for r in game if r["frame"] >= a.warm]
load = [r for r in game if r["frame"] < a.warm]
print(f"game frames {n} (warm {len(load)}, body {len(body)}); drawn (accepted) {sum(1 for r in game if r['acc'])}")
def line(name, v, unit="us"):
    print(f"  {name:28s} mean {mean(v):9.1f}  p50 {pct(v,.5):9.1f}  p90 {pct(v,.9):9.1f}  p99 {pct(v,.99):9.1f}  max {max(v) if v else 0:9.1f} {unit}")
print("per game frame (body):")
line("total", [r["total_us"] for r in body])
line("game-side (total - sink)", [r["game_only"] for r in body])
line("sink (all zones, excl)", [r["sink"] for r in body])
acc = [r for r in body if r["acc"]]
non = [r for r in body if not r["acc"]]
print(f"  drawn frames {len(acc)}, skipped-draw frames {len(non)}")
line("total, drawn frames", [r["total_us"] for r in acc])
line("sink, drawn frames", [r["sink"] for r in acc])
line("game-side, drawn frames", [r["game_only"] for r in acc])
line("total, skipped-draw frames", [r["total_us"] for r in non])
print("sink split, drawn frames (mean us per drawn frame, excl):")
for z in zones:
    m = mean([r["z_" + z] for r in acc])
    if m > 0.05: print(f"  {z:14s} {m:9.1f}")
print("counters per drawn frame (mean / p99 / max):")
for c in ctrs:
    v = [r["c_" + c] for r in acc]
    if v and max(v) > 0: print(f"  {c:16s} {mean(v):12.1f} {pct(v,.99):12.1f} {max(v):12.0f}")
# derived hypotheses numbers
tb = mean([r["c_tex_bytes"] for r in body])
print(f"texture uploads per game frame (all body frames): {mean([r['c_tex_create'] for r in body]):.3f}, {tb:.0f} B; frames with an upload {sum(1 for r in body if r['c_tex_create']>0)} ({100*sum(1 for r in body if r['c_tex_create']>0)/max(1,len(body)):.2f}%)")
print(f"pixel reads per game frame: {mean([r['c_read_region'] + r['c_capture'] for r in body]):.4f}")
dc = mean([r["c_draw_calls"] for r in acc]); bt = mean([r["c_batches"] for r in acc])
print(f"draw calls (gf poly) per drawn frame {dc:.0f}, batches {bt:.0f}, verts/draw {mean([r['c_verts'] for r in acc])/max(1,dc):.1f}, tris/batch {mean([r['c_indices'] for r in acc])/3/max(1,bt):.1f}")

# spikes
sp_us = a.spike_ms * 1000
spikes = [r for r in body if r["total_us"] >= sp_us]
print(f"spikes: body frames >= {a.spike_ms} ms: {len(spikes)} ({100*len(spikes)/max(1,len(body)):.2f}%)")
if spikes:
    sg = [r["game_only"] for r in spikes]; ss = [r["sink"] for r in spikes]
    print(f"  spike mean total {mean([r['total_us'] for r in spikes]):.0f} us = game-side {mean(sg):.0f} + sink {mean(ss):.0f}")
    print("  spike frames: drawn %d, skipped-draw %d" % (sum(1 for r in spikes if r["acc"]), sum(1 for r in spikes if not r["acc"])))
    print("  sink zones in spike frames vs drawn normal frames (mean us):")
    normal = [r for r in acc if r["total_us"] < sp_us]
    for z in zones:
        ms = mean([r["z_" + z] for r in spikes]); mn = mean([r["z_" + z] for r in normal])
        if ms > 0.5 or mn > 0.5: print(f"    {z:14s} spike {ms:9.1f}   normal {mn:9.1f}")
    for c in ("draw_calls", "batches", "meshes", "layers", "tex_create", "tex_bytes", "read_region", "canvas_items", "tex_free"):
        print(f"    counter {c:12s} spike {mean([r['c_'+c] for r in spikes]):12.1f}   normal {mean([r['c_'+c] for r in normal]):12.1f}")
    # spike that has a texture upload / frame with high game-only
    up = sum(1 for r in spikes if r["c_tex_create"] > 0); print(f"  spikes with a texture upload: {up}")
    gh = sum(1 for r in spikes if r["game_only"] > r["sink"]); print(f"  spikes where game-side > sink: {gh}")
    gaps = [spikes[i + 1]["frame"] - spikes[i]["frame"] for i in range(len(spikes) - 1)]
    if gaps: print(f"  frames between spikes: p50 {pct(gaps,.5):.0f}  p90 {pct(gaps,.9):.0f}  min {min(gaps):.0f}  max {max(gaps):.0f}")
    top = sorted(body, key=lambda r: -r["total_us"])[:8]
    print("  worst body frames: " + "; ".join(f"#{int(r['frame'])} {r['total_us']/1000:.1f}ms (game {r['game_only']/1000:.1f} sink {r['sink']/1000:.1f} tex {r['tex']/1000:.1f})" for r in top))
big = sorted(load + body, key=lambda r: -r["total_us"])[:5]
print("  biggest frames overall: " + "; ".join(f"#{int(r['frame'])} {r['total_us']/1000:.1f}ms (game {r['game_only']/1000:.1f} sink {r['sink']/1000:.1f} tex {r['tex']/1000:.1f})" for r in big))
hit = [r for r in game if r["total_us"] >= 30000]
print(f"  frames >= 30 ms: {len(hit)} at frames {[int(r['frame']) for r in hit][:12]}")

# godot frames
if godot:
    for r in godot:
        for k in r: r[k] = float(r[k])
    gb = godot[a.warm // 4:] if len(godot) > a.warm else godot
    print(f"godot frames {len(godot)}; body {len(gb)}")
    line("interval", [r["interval_us"] for r in gb if r["interval_us"] > 0])
    line("step (game+sink) per call", [r["step_us"] for r in gb])
    line("sink per call", [r["sink_us"] for r in gb])
    print(f"  game frames per call mean {mean([r['games'] for r in gb]):.2f}")
    print(f"  RS draw calls/frame mean {mean([r['rs_draw_calls'] for r in gb]):.0f}  objects {mean([r['rs_objects'] for r in gb]):.0f}  prims {mean([r['rs_prims'] for r in gb]):.0f}")
    line("screen viewport render cpu", [r["vp_cpu_ms"] * 1000 for r in gb])
    line("screen viewport render gpu", [r["vp_gpu_ms"] * 1000 for r in gb])
    iv = [r["interval_us"] for r in gb if r["interval_us"] > 0]
    if iv:
        med = pct(iv, .5)
        print(f"  Godot frames > 1.5x median ({med*1.5/1000:.1f} ms): {sum(1 for x in iv if x > med*1.5)} ({100*sum(1 for x in iv if x > med*1.5)/len(iv):.2f}%)")
    fps = 1e6 / pct(iv, .5) if iv else 0
    print(f"  median Godot fps {fps:.1f}")

# whole-run zone totals
tot = {}
p = os.path.join(a.dir, a.tag + "_totals.txt")
if os.path.exists(p):
    for l in open(p):
        t = l.split()
        if t[0] == "zone": tot[t[1]] = (int(t[3]), int(t[5]), int(t[7]))
    print("zone totals (calls, excl ms, incl ms): " + ", ".join(f"{k} {v[0]}/{v[1]/1e6:.0f}/{v[2]/1e6:.0f}" for k, v in tot.items() if v[0]))

# samples
sp = os.path.join(a.dir, a.tag + "_samples.txt")
if os.path.exists(sp):
    names = {}; samples = []
    for l in open(sp, errors="replace"):
        if l.startswith("Y "):
            _, pc, nm = l.rstrip("\n").split(" ", 2); names[pc] = nm
        elif l.startswith("S "):
            t = l.split(); samples.append((float(t[1]), int(t[2]), int(t[3]), t[4:]))
    spike_frames = {int(r["frame"]) for r in spikes}
    def fn(pc):
        nm = names.get(pc, pc)
        return nm
    def mod(nm): return nm.split("!")[0].lower()
    def report(title, sset):
        print(f"{title}: {len(sset)} samples")
        leaf = collections.Counter(); incl = collections.Counter(); mods = collections.Counter()
        for s in sset:
            if not s[3]: continue
            leaf[fn(s[3][0])] += 1; mods[mod(fn(s[3][0]))] += 1
            for f in set(fn(x) for x in s[3]): incl[f] += 1
        tot_s = max(1, len(sset))
        print("  by leaf module: " + ", ".join(f"{k} {100*v/tot_s:.1f}%" for k, v in mods.most_common(8)))
        print("  inclusive (function appears on stack):")
        for k, v in incl.most_common(a.top): print(f"    {100*v/tot_s:5.1f}%  {k}")
        print("  leaf (self):")
        for k, v in leaf.most_common(a.top): print(f"    {100*v/tot_s:5.1f}%  {k}")
    interval = (samples[-1][0] - samples[0][0]) / max(1, len(samples) - 1) if len(samples) > 1 else 0
    print(f"samples {len(samples)}, mean period {interval:.3f} ms; in game step {sum(1 for s in samples if s[1])}")
    report("in game step (all)", [s for s in samples if s[1] and s[2] >= a.warm])
    report("outside game step (Godot)", [s for s in samples if not s[1]])
    report("in game step, spike frames", [s for s in samples if s[1] and s[2] in spike_frames])
    report("in game step, load (warm) frames", [s for s in samples if s[1] and s[2] < a.warm])
