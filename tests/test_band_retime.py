#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""build_lighting.retime (0.3.178) on synthetic band rows; no client, game or GPU.

Retime moves the key times of relight's private clear-weather bands onto Northlight's sun and moon:
the sunset key to 20:35, night by 21:15, dawn from 04:30, 06:00-12:00 untouched. Values move with
their keys; the only new keys are copies of the 00:00 night value, and only while the band keeps
<= 11 keys. Two-key, constant and unrecognised bands stay byte-identical with a recorded reason.
Writes band-retime-validation.json."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import json
import struct
from collections import Counter
import build_lighting as bl
from build_lighting import DBC, u, rgb

H = 120   # half-minutes per hour
NIGHT, DAWN, DAY, SUNSET = (10, 20, 40), (220, 160, 100), (150, 170, 200), (230, 120, 60)
def c(v): return 0xff000000 | v[0] << 16 | v[1] << 8 | v[2]


def row(keys, floating=False, id=1):
    r = bytearray(34*4)
    struct.pack_into('<II', r, 0, id, len(keys))
    for i, (t, v) in enumerate(keys):
        struct.pack_into('<I', r, (2+i)*4, t)
        struct.pack_into('<f' if floating else '<I', r, (18+i)*4, v if floating else c(v))
    return r


N, D, Y, S = NIGHT, DAWN, DAY, SUNSET
bands = {
    'stock 00 03 06 12 21 22': ([(0, N), (3*H, N), (6*H, D), (12*H, Y), (21*H, S), (22*H, N)], False),
    'STV 00 06 12 21 22': ([(0, N), (6*H, D), (12*H, Y), (21*H, S), (22*H, N)], False),
    'Northrend 00 06 12 21 22': ([(0, (5, 10, 30)), (6*H, (180, 150, 140)), (12*H, (120, 150, 190)), (21*H, (200, 110, 90)), (22*H, (5, 10, 30))], False),
    'Tanaris 00 03 06 12 21': ([(0, N), (3*H, N), (6*H, D), (12*H, Y), (21*H, S)], False),
    '00 03 06 12 18 21': ([(0, N), (3*H, N), (6*H, D), (12*H, Y), (18*H, S), (21*H, N)], False),
    'HD ground ambient (night at 20)': ([(0, N), (4*H, N), (5*H, N), (6*H, D), (17*H, Y), (18*H, (200, 150, 110)), (19*H, S), (20*H, N)], False),
    'HD ground direct (night at 19)': ([(0, N), (4*H, N), (5*H, N), (6*H, D), (17*H, Y), (18*H, S), (19*H, N), (20*H, N)], False),
    'HD Tanaris 00 05 06 12 19': ([(0, N), (5*H, N), (6*H, D), (12*H, Y), (19*H, S)], False),
    'night varies (22 vs 00 within tolerance)': ([(0, N), (3*H, (12, 22, 42)), (6*H, D), (12*H, Y), (21*H, S), (22*H, (13, 23, 44))], False),
    'fog end float 00 03 06 12 21 22': ([(0, 300.), (3*H, 305.), (6*H, 900.), (12*H, 1400.), (21*H, 1000.), (22*H, 310.)], True),
}
# A 16-key float band (HD fog) that still needs the 21:15 night insert: the insert is skipped.
sixteen = [(0, 200.), (1*H, 201.), (2*H, 200.), (3*H, 202.)] + [(h*H, 800.+h) for h in range(6, 18)]
bands['16-key float, no night after sunset'] = (sixteen, True)
unchanged = {
    'two-key 00 12': ([(0, N), (12*H, Y)], False, 'fewer than 3 key times'),
    'constant': ([(0, Y), (6*H, Y), (12*H, Y), (18*H, Y)], False, 'constant'),
    'sunset before 16:00': ([(0, N), (6*H, D), (12*H, Y), (15*H, S), (15*H+60, N)], False, 'evening shape not recognised'),
    'night through 06:00': ([(0, N), (6*H, N), (12*H, Y), (21*H, S), (22*H, N)], False, 'morning shape not recognised'),
}

checks, summary = {}, {}
def night_like(pairs, floating, t):
    night = bl.band_value(pairs, floating, 0)
    spread = max(bl.band_difference(v if floating else rgb(v), night, floating) for _, v in pairs)
    tol = max(1e-3, .1*spread) if floating else max(3, .1*spread)
    return bl.band_difference(bl.band_value(pairs, floating, t), night, floating) <= tol

for name, (keys, floating) in bands.items():
    r = row(keys, floating)
    before = bl.band_pairs(r, floating)
    result, reason = bl.retime_plan(r, floating)
    assert result is not None, (name, reason)
    times = [t for t, _ in result]
    ok = all(b > a for a, b in zip(times, times[1:])) and 0 <= times[0] and times[-1] < 2880 and len(result) <= 16
    inserted = len(result)-len(before)
    ok &= inserted == 0 or len(result) <= bl.INSERT_LIMIT
    extra = Counter(v for _, v in result); extra.subtract(v for _, v in before)
    ok &= all(n >= 0 for n in extra.values()) and all(v == bl.band_pairs(r, floating)[0][1] for v in extra.elements())
    ok &= [p for p in result if 720 <= p[0] <= 1440] == [p for p in before if 720 <= p[0] <= 1440]   # 06:00-12:00
    evening_full = reason is None or 'insert' not in reason
    if evening_full:
        ok &= night_like(result, floating, bl.NIGHT_KEY) and night_like(result, floating, bl.NIGHT_KEY+60)
    ok &= night_like(result, floating, 3*H+60) and night_like(result, floating, 4*H+58)
    ok &= bl.band_value(result, floating, 6*H) == bl.band_value(before, floating, 6*H)
    # The old sunset key (the last key before the night run) is reached at 20:35.
    old_sunset = [v for t, v in before if t >= 16*H and not night_like(before, floating, t)][-1]
    ok &= (old_sunset if floating else rgb(old_sunset)) == bl.band_value(result, floating, bl.SUNSET_KEY)
    checks[name] = bool(ok)
    summary[name] = {'before': [t for t, _ in before], 'after': times, 'inserted': inserted, 'note': reason}
for name, (keys, floating, why) in unchanged.items():
    r = row(keys, floating)
    result, reason = bl.retime_plan(r, floating)
    checks[f'{name}: unchanged ({why})'] = result is None and reason == why
    summary[name] = {'unchanged': reason}
checks['16-key float: the 21:15 insert is skipped at the key limit'] = summary['16-key float, no night after sunset']['note'] == 'inserts skipped (key limit): 2550' and summary['16-key float, no night after sunset']['inserted'] == 0
checks['stock: 21->20:35, 22->21:15, 03->04:30, no insert'] = summary['stock 00 03 06 12 21 22']['after'] == [0, 540, 720, 1440, 2470, 2550]
checks['STV: 21->20:35, 22->21:15, night inserted at 04:30'] = summary['STV 00 06 12 21 22']['after'] == [0, 540, 720, 1440, 2470, 2550]
checks['Tanaris: 21->20:35, night inserted at 21:15, 03->04:30'] = summary['Tanaris 00 03 06 12 21']['after'] == [0, 540, 720, 1440, 2470, 2550]
checks['HD ground: 19->20:35, 20->21:15, morning unchanged'] = summary['HD ground ambient (night at 20)']['after'][:4] == [0, 480, 600, 720] and summary['HD ground ambient (night at 20)']['after'][-2:] == [2470, 2550]

# retime() on tables: only the listed profiles change, in place, with a report.
def table(rows, floating):
    size = 34*4
    data = struct.pack('<4s4I', b'WDBC', len(rows), 34, size, 1) + b''.join(rows) + b'\0'
    return DBC(data)
stv = bands['STV 00 06 12 21 22'][0]
ints = table([row(stv, False, id) for id in range(1, 37)], False)
floats = table([row(bands['fog end float 00 03 06 12 21 22'][0], True, id) for id in range(1, 13)], True)
other = ({i: bytes(r) for i, r in ints.index.items() if i > 18}, {i: bytes(r) for i, r in floats.index.items() if i > 6})
report = bl.retime({'LightIntBand': ints, 'LightFloatBand': floats}, [1])
checks['retime(): profile 1 retimed in place, profile 2 byte-identical, report counts'] = (
    report['bands'] == 24 and report['retimed'] == 24 and report['inserted_keys'] == 18
    and all(bytes(ints.index[i]) == b for i, b in other[0].items()) and all(bytes(floats.index[i]) == b for i, b in other[1].items())
    and u(ints.index[1], 1) == 6 and DBC(ints.bytes()).index[1] == ints.index[1])

for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
assert all(checks.values())
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
(out / 'band-retime-validation.json').write_text(json.dumps({'checks': checks, 'bands': summary, 'game_launched': False}, indent=2) + '\n')
print('PASS band retime: synthetic templates, inserts under the key limit, unchanged bands with reasons')
