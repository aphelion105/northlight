"""Join prepared world batch checks to warm producer frames."""

import re


FIELDS = ('producerFrame', 'wmo', 'wmoRanges', 'wmoBounds', 'adt', 'adtRanges',
          'adtOrigins', 'unattributed')
ROW = re.compile(' '.join(['NATIVE_WORLD_BATCH'] + [name + r'=(\d+)' for name in FIELDS]) +
                 r'(?: adtBounds=(?P<bounds>\d+))?')
COVERAGE = re.compile(r'NATIVE_ADT_COVERAGE producerFrame=(\d+) match=([01])')
COVERAGE_SUMMARY = re.compile(r'NATIVE_ADT_COVERAGE producerFrame=(\d+) compared=(\d+) contained=(\d+)')


def observe(lines, warm_frames, required):
    verified = 'NATIVE_WORLD_BATCH wmo12340=1 adt12340=1' in lines
    verified_adt = 'NATIVE_ADT_BOUNDS client12340=1' in lines
    by_frame = {}
    coverage = {}
    for line in lines:
        if line.startswith('NATIVE_ADT_COVERAGE '):
            if match := COVERAGE_SUMMARY.fullmatch(line):
                frame, compared, contained = map(int, match.groups())
                if not 0 < compared or contained > compared:
                    raise ValueError('Inconsistent native ADT coverage')
            elif match := COVERAGE.fullmatch(line):
                frame, contained = map(int, match.groups())
                compared = 1
            else:
                raise ValueError('Malformed native ADT coverage')
            counts = coverage.setdefault(frame, [0, 0])
            counts[0] += compared
            counts[1] += contained
            continue
        if not line.startswith('NATIVE_WORLD_BATCH producerFrame='):
            continue
        match = ROW.fullmatch(line)
        if not match:
            raise ValueError('Malformed native world batch counts')
        row = dict(zip(FIELDS, map(int, match.groups()[:-1]), strict=True))
        row['adtBounds'] = int(match['bounds'] or 0)
        frame = row['producerFrame']
        if (frame in by_frame or row['wmoBounds'] > row['wmoRanges'] or row['wmoRanges'] > row['wmo'] or
                row['adtBounds'] > row['adtOrigins'] or row['adtOrigins'] > row['adtRanges'] or row['adtRanges'] > row['adt']):
            raise ValueError(f'Inconsistent native world batch counts at producer frame {frame}')
        by_frame[frame] = dict(row, world_ready=frame in warm_frames)
    for frame, row in by_frame.items():
        row['adtCompared'], row['adtContained'] = coverage.get(frame, (0, 0))
        if row['adtCompared'] > row['adtBounds']:
            raise ValueError(f'Excess native ADT coverage at producer frame {frame}')
    # Worker coverage can arrive ahead of its producer summary in a partial log.
    samples = list(by_frame.values())
    warm = sum(row['world_ready'] and 0 < row['wmo'] == row['wmoRanges'] == row['wmoBounds'] and
               0 < row['adt'] == row['adtRanges'] == row['adtOrigins'] == row['adtBounds'] ==
               row['adtCompared'] == row['adtContained'] for row in samples)
    return {'verified_client': verified, 'verified_adt_bounds': verified_adt, 'warm_samples': warm,
            'ready': verified and verified_adt and warm >= required,
            'samples': samples}
