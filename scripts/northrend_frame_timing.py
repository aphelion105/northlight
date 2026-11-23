"""Measure ordered CPU Present intervals from the selected live windows."""

import math
from statistics import median


def summarize(windows, *, background=False):
    raw_fields = {'qpcHz', 'lastPresent', 'deltas'}
    if not any(raw_fields.intersection(row) for row in windows):
        return {'state': 'unavailable'}

    points = []
    evidence = []
    identity = None
    previous_end = -1
    for row in windows:
        try:
            hz = int(row['qpcHz'])
            end = int(row['lastPresent'])
            count = int(row['samples'])
            ticks = [int(value) for value in row['deltas'].split(',')]
            device = int(row['device'])
            settings = tuple(row[key] for key in ('gi', 'shadows', 'fog'))
            current = (hz, device, settings)
            start = end - count + 1
            if (row.get('focus') != ('0' if background else '1') or row.get('enabled') != '1' or row.get('complete') == '0' or
                    count != 120 or len(ticks) != count or any(not 0 < tick < 1 << 64 for tick in ticks) or
                    not 0 < hz < 1 << 63 or device <= 0 or end != int(row['frame']) or
                    start <= previous_end or any(value not in ('0', '1') for value in settings) or
                    (identity is not None and current != identity)):
                raise ValueError('invalid or overlapping Present window, or device/profile changed')
            samples = [tick * 1000.0 / hz for tick in ticks]
            ordered = sorted(samples)
            expected = (sum(samples) / count, median(samples), ordered[113], ordered[-1])
            for field, value in zip(('meanMs', 'p50Ms', 'p95Ms', 'maxMs'), expected, strict=True):
                if not math.isclose(float(row[field]), value, rel_tol=0, abs_tol=0.00051):
                    raise ValueError(f'{field} disagrees with the raw intervals')
        except (KeyError, ValueError) as error:
            raise ValueError(f'Invalid raw frame timing: {error}') from error
        identity, previous_end = current, end
        points.extend(enumerate(samples, start=start))
        evidence.append({'first_present': start, 'last_present': end, 'qpc_deltas': ticks})

    ranked = sorted(value for _, value in points)
    count = len(ranked)
    slow = {}
    for fps in (60, 30):
        budget = 1000.0 / fps
        over = sum(value > budget for value in ranked)
        slow[str(fps)] = {'budget_ms': budget, 'frames_over_budget': over, 'fraction': over / count}
    return {'state': 'measured', 'scope': 'CPU Present intervals', 'device': identity[1],
            'window_mode': 'background' if background else 'focused',
            'qpc_hz': identity[0], 'windows': evidence, 'samples': count, 'p50_ms': median(ranked),
            'p95_ms': ranked[(count * 95 + 99) // 100 - 1],
            'p99_ms': ranked[(count * 99 + 99) // 100 - 1], 'max_ms': ranked[-1],
            'slow_frames': slow,
            'worst': [{'present': frame, 'ms': value}
                      for frame, value in sorted(points, key=lambda point: (-point[1], point[0]))[:5]]}
