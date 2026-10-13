#!/usr/bin/env python3
"""Per-segment effect cost from a northlight-renderer.log; no game or graphics device.

The log is split at `Effects OFF`, `Effects ON` and `Effects components ... master= GI= shadows= fog=` lines. Each
segment reports FRAME interval samples, CPU profile and GPU profile means, the CELESTIAL terrain mask
redraws/reuses, the latest orbit and LOCAL direct lines. A final line compares effects-on with effects-off frames.
Background samples are left out of the means and shown as `background`: with a `focus=` field (newer logs) those are
the focus=0 samples, which run at the client's background frame cap (maxFPSBk, 30 fps by default); in older logs
without it, samples at or above CAP_MS (the ~33.8 ms plateau) are only presumed to be that cap (`presumed`). A focused
sample at or above CAP_MS stays in the means and is counted as `slowFocused`: that one is real cost.
Usage: effects_cost_report.py <northlight-renderer.log> [--min-samples N]"""
import argparse, re, sys
from collections import defaultdict
from pathlib import Path

CAP_MS = 30.0   # the background-window cap plateaus at ~33.8 ms
KV = re.compile(r'(\w+)=(-?[\d.]+)(?:ms)?(?=\s|$)')

def fields(line):
    return {k: float(v) for k, v in KV.findall(line)}

def label(line):
    if line.startswith('Effects OFF'): return 'OFF', False
    if line.startswith('Effects ON'): return 'ON', True
    m = re.search(r'master=(\d)\s+GI=(\w+)\s+shadows=(\w+)\s+fog=(\w+)', line)
    if m: return f'master={m[1]} GI={m[2]} shadows={m[3]} fog={m[4]}', m[1] == '1'
    return None

class Segment:
    def __init__(self, start, text, on):
        self.start, self.text, self.on = start, text, on
        self.frames, self.background, self.presumed, self.slow_focused = [], [], 0, 0
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
            self.frames.append(ms)
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
            cur = Segment(n, tag[0], tag[1]); segments.append(cur)
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
    on = [m for s in segments if s.on for m in s.frames]; off = [m for s in segments if not s.on for m in s.frames]
    print(f'== effects ON  : samples={len(on)} meanMs={mean(on):.3f}\n== effects OFF : samples={len(off)} meanMs={mean(off):.3f}')
    if on and off: print(f'== ON minus OFF: {mean(on) - mean(off):+.3f} ms (focused samples only)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
