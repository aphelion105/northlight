#!/usr/bin/env python3
"""Per-segment effect cost from a northlight-renderer.log; no game or graphics device.

The log is split at `Effects OFF`, `Effects ON` and `Effects components ... master= GI= shadows= fog=` lines. Each
segment reports FRAME interval samples, CPU profile and GPU profile means, the CELESTIAL terrain mask
redraws/reuses, the latest orbit and LOCAL direct lines. The closing summary classifies every FRAME interval sample by
the state on its own line (enabled, gi, shadows, fog; F10 does not reset the F7-F9 components, so a segment label is
not enough): all on (enabled=1 gi=1 shadows=1 fog=1) against all off (enabled=0), with each partial state apart.
Background samples are left out of the means and shown as `background`: with a `focus=` field (newer logs) those are
the focus=0 samples, which run at the client's background frame cap (maxFPSBk, 30 fps by default); in older logs
without it, samples at or above CAP_MS (the ~33.8 ms plateau) are only presumed to be that cap (`presumed`, and the
summary warns that such a log cannot tell a real slowdown below ~33 fps from it). A focused sample at or above CAP_MS
stays in the means and is counted as `slowFocused`: that one is real cost.
Usage: effects_cost_report.py <northlight-renderer.log> [--min-samples N]"""
import argparse, re, sys
from collections import defaultdict
from pathlib import Path

CAP_MS = 30.0   # the background-window cap plateaus at ~33.8 ms
KV = re.compile(r'(\w+)=(-?[\d.]+)(?:ms)?(?=\s|$)')

def fields(line):
    return {k: float(v) for k, v in KV.findall(line)}

def label(line):
    if line.startswith('Effects OFF'): return 'OFF'
    if line.startswith('Effects ON'): return 'ON'
    m = re.search(r'master=(\d)\s+GI=(\w+)\s+shadows=(\w+)\s+fog=(\w+)', line)
    if m: return f'master={m[1]} GI={m[2]} shadows={m[3]} fog={m[4]}'
    return None

def state(f):
    """'all on', 'all off' or the partial components from a FRAME interval line's own fields."""
    if not f.get('enabled', 1): return 'all off'
    off = [k for k in ('gi', 'shadows', 'fog') if not f.get(k, 1)]
    return 'all on' if not off else ' '.join(k + '=0' for k in off)

class Segment:
    def __init__(self, start, text):
        self.start, self.text = start, text
        self.frames, self.states, self.background, self.presumed, self.slow_focused = [], [], [], 0, 0
        self.cpu, self.gpu, self.direct = defaultdict(list), defaultdict(list), defaultdict(list)
        self.redraws, self.reuses, self.triangles = [0, 0], [0, 0], []
        self.orbit = self.local = None

    def add(self, line):
        if line.startswith('FRAME interval'):
            f = fields(line); ms = f.get('meanMs')
            if ms is None: return
            if 'focus' in f:
                if not f['focus']: self.background.append(ms); return
                if ms >= CAP_MS: self.slow_focused += 1
            elif ms >= CAP_MS: self.background.append(ms); self.presumed += 1; return
            self.frames.append(ms); self.states.append(state(f))
        elif line.startswith('CPU profile'):
            f = fields(line)
            for k in ('drawGate', 'capture', 'effects', 'cleanup', 'total', 'drawHooks'):
                if f.get(k, -1) >= 0: self.cpu[k].append(f[k])
        elif line.startswith('GPU profile'):
            for k, v in fields(line).items():
                if k != 'frame': self.gpu[k].append(v)
        elif line.startswith('CELESTIAL terrain mask'):
            m = re.search(r'redraws=(\d+),(\d+) reuses=(\d+),(\d+)', line)
            if m:
                for i in range(2): self.redraws[i] += int(m[1 + i]); self.reuses[i] += int(m[3 + i])
            if 'triangles' in fields(line): self.triangles.append(fields(line)['triangles'])
        elif line.startswith('CELESTIAL orbit'): self.orbit = line
        elif line.startswith('LOCAL direct'):
            self.local = fields(line)
            if 'selectUs' in self.local: self.direct['selectUs'].append(self.local['selectUs'])

def mean(v): return sum(v) / len(v) if v else float('nan')

def parse(lines):
    segments, cur = [], None
    for n, line in enumerate(lines, 1):
        line = line.strip()
        tag = label(line) if line.startswith('Effects ') else None
        if tag:
            cur = Segment(n, tag); segments.append(cur)
        elif cur: cur.add(line)
    return segments

def show(s):
    out = [f'== line {s.start}: {s.text}', f'  frames={len(s.frames)} meanMs={mean(s.frames):.3f} slowFocused={s.slow_focused} '
           f'background={len(s.background)} (presumed={s.presumed}) backgroundMeanMs={mean(s.background):.3f}']
    if s.cpu: out.append('  CPU ms: ' + ' '.join(f'{k}={mean(v):.3f}' for k, v in s.cpu.items()))
    if s.gpu:
        top = sorted(((mean(v), k) for k, v in s.gpu.items() if k != 'total'), reverse=True)[:6]
        out.append(f'  GPU ms: total={mean(s.gpu.get("total", [])):.3f} ' + ' '.join(f'{k}={m:.3f}' for m, k in top))
    if s.triangles or any(s.redraws + s.reuses):
        out.append(f'  terrain mask: redraws={s.redraws[0]},{s.redraws[1]} reuses={s.reuses[0]},{s.reuses[1]} meanTriangles={mean(s.triangles):.0f}')
    if s.orbit:
        sun = re.search(r'sun=\S*->(-?[\d.]+)', s.orbit); moon = re.search(r'moon=\S*->(-?[\d.]+)', s.orbit)
        w = re.search(r'weights=(\S+)', s.orbit)
        out.append(f'  orbit: sun={sun[1] if sun else "?"} moon={moon[1] if moon else "?"} weights={w[1] if w else "?"}')
    if s.local:
        out.append(f'  local direct: lights={s.local.get("lights", -1):.0f} available={s.local.get("available", -1):.0f} meanSelectUs={mean(s.direct["selectUs"]):.1f}')
    return out

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('log'); ap.add_argument('--min-samples', type=int, default=1, help='hide segments with fewer frame samples')
    args = ap.parse_args(argv)
    segments = parse(Path(args.log).read_text(errors='replace').splitlines())
    for s in segments:
        if len(s.frames) + len(s.background) >= args.min_samples: print('\n'.join(show(s)))
    by = defaultdict(list)
    for s in segments:
        for ms, st in zip(s.frames, s.states): by[st].append(ms)
    for st in ['all on', 'all off'] + sorted(k for k in by if k not in ('all on', 'all off')):
        print(f'== {st:<8}: samples={len(by[st])} meanMs={mean(by[st]):.3f}')
    if by['all on'] and by['all off']: print(f'== all on minus all off: {mean(by["all on"]) - mean(by["all off"]):+.3f} ms (focused samples only)')
    if any(s.presumed for s in segments):
        print('== warning: no focus= field; samples >= %.0f ms were presumed to be the background cap, so a real slowdown '
              'below ~33 fps cannot be told apart in this log' % CAP_MS)
    return 0

if __name__ == '__main__':
    sys.exit(main())
