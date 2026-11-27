"""Measure ordered CPU Present intervals from the selected live windows."""

import math
from statistics import median
from northrend_compat import strict_zip


def summarize(windows, *, background=False, enabled=True):
    raw_fields = {'qpcHz', 'lastPresent', 'deltas'}
    if not any(raw_fields.intersection(row) for row in windows):
        return {'state': 'unavailable'}

    points = []
    evidence = []
    identity = None
    previous_end, previous_qpc = -1, None
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
            if (row.get('focus') != ('0' if background else '1') or row.get('enabled') != str(int(enabled)) or row.get('complete') == '0' or
                    count != 120 or len(ticks) != count or any(not 0 < tick < 1 << 64 for tick in ticks) or
                    not 0 < hz < 1 << 63 or device <= 0 or end != int(row['frame']) or
                    start <= previous_end or any(value not in ('0', '1') for value in settings) or
                    (identity is not None and current != identity)):
                raise ValueError('invalid or overlapping Present window, or device/profile changed')
            anchor = int(row['lastQpc']) if 'lastQpc' in row else None
            if anchor is not None:
                elapsed = sum(ticks)
                if not elapsed <= anchor < 1 << 63:
                    raise ValueError('invalid Present QPC anchor')
                if previous_qpc is not None and start == previous_end + 1 and anchor - elapsed != previous_qpc:
                    raise ValueError('Present QPC anchors disagree with the raw intervals')
            samples = [tick * 1000.0 / hz for tick in ticks]
            ordered = sorted(samples)
            expected = (sum(samples) / count, median(samples), ordered[113], ordered[-1])
            for field, value in strict_zip(('meanMs', 'p50Ms', 'p95Ms', 'maxMs'), expected):
                if not math.isclose(float(row[field]), value, rel_tol=0, abs_tol=0.00051):
                    raise ValueError(f'{field} disagrees with the raw intervals')
        except (KeyError, ValueError) as error:
            raise ValueError(f'Invalid raw frame timing: {error}') from error
        identity, previous_end, previous_qpc = current, end, anchor
        points.extend(enumerate(samples, start=start))
        window = {'first_present': start, 'last_present': end, 'qpc_deltas': ticks}
        if anchor is not None:
            window['last_qpc'] = anchor
        evidence.append(window)

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
