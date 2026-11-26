"""Validate a stationary baseline and two opposite camera sweeps against CPU Presents."""

import math
import re
from statistics import median

from northrend_fixture_server import RATCHET_PHASES
from northrend_frame_timing import summarize


def markers(events):
    rows = []
    for line in events:
        if not line.startswith('RATCHET_CAMERA '):
            continue
        marker = re.fullmatch(r'RATCHET_CAMERA phase=(\w+) tick=(\d+)', line)
        if not marker:
            raise ValueError('Invalid Ratchet camera marker')
        rows.append((marker[1], int(marker[2])))
    if (tuple(name for name, _ in rows) != RATCHET_PHASES[:len(rows)] or
            any(end <= start for (_, start), (_, end) in zip(rows, rows[1:]))):
        raise ValueError('Ratchet camera markers are out of order')
    return rows


def assess(windows, phases, camera, spawn, *, background):
    start, stop = phases[0]['start'], phases[-1]['end']
    recovery_end = stop + 3000
    selected = []
    for row in windows:
        end = int(row['tick'])
        count = int(row['samples'])
        first = end - count * float(row['meanMs'])
        if end >= start and first <= recovery_end:
            selected.append(row)
    if not selected or int(selected[-1]['tick']) < recovery_end:
        return None
    timing = summarize(selected, background=background)
    if timing['state'] != 'measured':
        raise ValueError('The camera run needs raw Present interval evidence')

    previous = None
    measurements = []
    for row, raw in zip(selected, timing['windows']):
        if previous is not None and raw['first_present'] != previous + 1:
            raise ValueError('A Present window is missing during camera measurement')
        previous = raw['last_present']
        end = float(row['tick'])
        for present, delta in reversed(list(enumerate(raw['qpc_deltas'], raw['first_present']))):
            ms = delta * 1000 / timing['qpc_hz']
            measurements.append((end - ms, end, ms, present))
            end -= ms

    def stats(phase):
        moving = phase['name'] in ('left', 'right')
        # Assign a frame crossing a key event to the motion phase. This keeps
        # the first stalled frame after the input in the measurement.
        eligible = [(ms, present) for begin, end, ms, present in measurements
                    if (end > phase['start'] and begin < phase['end'] if phase['name'] != 'idle' else
                        begin >= phase['start'] and end <= phase['end'])]
        if len(eligible) < 24:
            raise ValueError(f'Too few Present intervals for {phase["name"]}')
        values = sorted(ms for ms, _ in eligible)
        p95 = values[(95 * len(values) + 99) // 100 - 1]
        result = dict(phase, frames=len(values), median_ms=median(values), p95_ms=p95, maximum_ms=values[-1],
                      worst=[{'present': present, 'ms': ms} for ms, present in sorted(eligible, reverse=True)[:5]])
        if moving:
            samples = [sample for sample in camera if phase['start'] <= sample['tick'] <= phase['end']]
            if len(samples) < 6 or any(math.dist(sample['eye'], spawn[:3]) > 32 for sample in samples):
                raise ValueError(f'Missing local camera observations for {phase["name"]}')
            headings = [math.atan2(sample['forward'][1], sample['forward'][0]) for sample in samples]
            steps = [math.remainder(b - a, math.tau) for a, b in zip(headings, headings[1:])]
            yaw = math.degrees(sum(steps))
            if abs(yaw) < 300 or abs(yaw) < .85 * sum(abs(math.degrees(step)) for step in steps):
                raise ValueError(f'Camera did not complete a smooth {phase["name"]} sweep ({yaw:.0f} degrees)')
            result['yaw_degrees'] = yaw
        return result

    if min(begin for begin, _, _, _ in measurements) > start + 2:
        raise ValueError('The idle baseline starts before the first recorded Present interval')
    idle, left, right = map(stats, phases)
    recovery = stats({'name': 'recovery', 'start': stop, 'end': recovery_end})
    if left['yaw_degrees'] * right['yaw_degrees'] >= 0:
        raise ValueError('Camera sweeps did not reverse direction')
    max_frame = min(50., max(1000 / 30, 2 * idle['median_ms']))
    max_p95 = max(1000 / 30, 1.35 * idle['p95_ms'])
    def over_budget(row):
        return row['maximum_ms'] > max_frame or row['p95_ms'] > max_p95

    failure = [row['name'] for row in (left, right) if over_budget(row)]
    return {'pass': not failure and not over_budget(recovery), 'limits_ms': {'max': max_frame, 'p95': max_p95},
            'failed_sweeps': failure, 'failed_recovery': over_budget(recovery),
            'idle': idle, 'sweeps': [left, right], 'recovery': recovery, 'raw_timing': timing}


def event_wait_evidence(lines, report):
    frames = {row['present'] for phase in (*report['sweeps'], report['recovery']) for row in phase['worst']}
    required = {'replayed', 'replayMs', 'beforeReplayMs', 'afterReplayMs', 'backendUntouched', 'backendFalse'}
    events = []
    for line in lines:
        if not line.startswith('CSTREAM QUERYFRAME '):
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if fields.get('type') != '8' or not required <= fields.keys() or int(fields['replayed']) not in frames:
            continue
        events.append({'present': int(fields['replayed']), 'replay_ms': float(fields['replayMs']),
                       'before_replay_ms': float(fields['beforeReplayMs']), 'after_replay_ms': float(fields['afterReplayMs']),
                       'untouched_trips': int(fields['backendUntouched']), 'gpu_pending_trips': int(fields['backendFalse'])})
    return events
