"""Join diagnostic capture work at the renderer draw boundary."""

from northrend_compat import strict_zip


CONSUMERS = ('material', 'normals', 'world')
LANES = dict(strict_zip((1, 2, 4), CONSUMERS))
WORK = ('charged_bytes', 'snapshot_hits', 'snapshot_misses', 'packets', 'geometry_reuses',
        'bank_reuses', 'bounds_tests', 'float_get_registers', 'float_handoff_registers',
        'range_read_registers', 'range_handoff_registers')
KNOWN = {'material': WORK[:7], 'normals': WORK, 'world': WORK[:6] + WORK[7:]}
DRAW = ('device', 'epoch', 'frame', 'draw', 'route', 'up', 'indexed', 'topology', 'base',
        'minimum', 'vertices', 'start', 'primitives', 'source', 'sourceBytes', 'pose', 'bank')
DRAW_FIELDS = set(DRAW + CONSUMERS)
CLASSIFIED_FIELDS = DRAW_FIELDS | {'shaderClass', 'alphaTest'}
SHADER_CLASSES = {'terrain', 'terrain_detail', 'wmo', 'm2_skinning', 'm2_other',
                  'm2_projected', 'm2_particle', 'water', 'unknown'}
ALPHA_TEST = {-1: 'unknown', 0: 'off', 1: 'on'}
FRAME = ('device', 'epoch', 'frame', 'rows', 'fpsEligible', 'cpuSourceBytes', 'gpuSceneBytes')
BUFFER = ('device', 'epoch', 'frame', 'draw', 'lane', 'buffer', 'index', 'path', 'offset', 'size', 'ok')
IDENTITY = ('device', 'epoch', 'frame', 'draw')
BUFFER_FIELDS = ('lock_attempts', 'lock_span_bytes', 'fill_attempts', 'fill_span_bytes', 'copy_views', 'copy_span_bytes', 'failed_locks')


def buffer_summary(rows):
    counts = {name: {kind: dict.fromkeys(BUFFER_FIELDS, 0) for kind in ('vb', 'ib')} for name in CONSUMERS}
    shared_draws = shared_lock_draws = 0
    for row in rows:
        spans, locks = {}, {}
        for read in row['buffer_reads']:
            stats = counts[LANES[read['lane']]]['ib' if read['index'] else 'vb']
            path = read['path']
            if path == 'C':
                stats['copy_views'] += 1
                stats['copy_span_bytes'] += read['size']
            else:
                stats['lock_attempts'] += 1
                stats['lock_span_bytes'] += read['size'] if read['ok'] else 0
                stats['failed_locks'] += not read['ok']
                if path == 'F':
                    stats['fill_attempts'] += 1
                    stats['fill_span_bytes'] += read['size'] if read['ok'] else 0
            if not read['ok']:
                continue
            key = (read['buffer'], read['index'], read['offset'], read['size'])
            spans[key] = spans.get(key, 0) | read['lane']
            if path != 'C':
                locks[key] = locks.get(key, 0) | read['lane']
        shared_draws += any(lanes & (lanes - 1) for lanes in spans.values())
        shared_lock_draws += any(lanes & (lanes - 1) for lanes in locks.values())
    return dict(consumers=counts, shared_exact_span_draws=shared_draws,
                shared_exact_lock_span_draws=shared_lock_draws)


def consumer_totals(rows, columns):
    return {name: {item: sum(row[name][item] for row in rows) for item in KNOWN[name] if item in WORK[:columns]}
            for name in CONSUMERS}


def capture_classes(rows, columns):
    domains = {}
    for row in rows:
        domains.setdefault((row['shaderClass'], row['alphaTest']), []).append(row)
    return [dict(shader_class=shader, alpha_test=ALPHA_TEST[alpha], draws=len(group),
                 primitives=sum(row['primitives'] for row in group), up_draws=sum(row['up'] for row in group),
                 consumers=consumer_totals(group, columns), buffer_reads=buffer_summary(group))
            for (shader, alpha), group in sorted(domains.items())]


def census(lines, frame):
    rows = []
    trailer = None
    columns = None
    classified = None
    pending = []
    buffer_rows = 0
    for line in lines:
        tag, _, tail = line.partition(' ')
        if tag not in ('CAPTURE_WORK_DRAW', 'CAPTURE_WORK_FRAME', 'CAPTURE_WORK_BUFFER'):
            continue
        try:
            items = [item.split('=', 1) for item in tail.split()]
            fields = dict(items)
            if tag == 'CAPTURE_WORK_FRAME':
                if (trailer is not None or pending or len(items) != len(fields) or
                        fields.keys() not in (set(FRAME), set(FRAME + ('bufferRows',)))):
                    raise ValueError('Invalid capture work frame marker')
                trailer = {key: int(value) for key, value in fields.items()}
                continue
            if tag == 'CAPTURE_WORK_BUFFER':
                if trailer is not None or len(items) != len(fields) or fields.keys() != set(BUFFER):
                    raise ValueError('Invalid capture buffer row')
                read = {key: (value if key == 'path' else int(value, 16 if key == 'buffer' else 10))
                        for key, value in fields.items()}
                if (read['path'] not in ('B', 'F', 'C') or read['index'] not in (0, 1) or
                        read['ok'] not in (0, 1) or read['lane'] not in LANES or
                        not 0 < read['buffer'] <= 0xffffffffffffffff or not 0 <= read['offset'] <= 0xffffffff or
                        not 0 < read['size'] <= 0xffffffff or (read['path'] == 'C' and not read['ok']) or
                        (read['path'] == 'F' and read['offset'])):
                    raise ValueError('Invalid capture buffer span')
                read['buffer'] = f'{read["buffer"]:08x}'
                pending.append(read)
                buffer_rows += 1
                continue
            if trailer is not None or len(items) != len(fields) or fields.keys() not in (DRAW_FIELDS, CLASSIFIED_FIELDS):
                raise ValueError('Invalid capture work draw row')
            has_class = fields.keys() == CLASSIFIED_FIELDS
            if classified is not None and classified != has_class:
                raise ValueError('Only some capture work draws have a class')
            classified = has_class
            row = {key: int(fields[key], 16 if key == 'source' else 10) for key in DRAW}
            if (row['frame'] != frame or row['device'] <= 0 or row['draw'] <= 0 or
                    any(row[key] < 0 for key in DRAW if key != 'base') or
                    row['route'] not in (4, 6, 7, 15) or
                    any(row[key] not in (0, 1) for key in ('up', 'indexed', 'pose', 'bank')) or
                    (rows and row['draw'] <= rows[-1]['draw'])):
                raise ValueError('Invalid or repeated capture work draw identity')
            row['source'] = f'{row["source"]:016x}' if row['source'] else None
            if classified:
                shader, alpha = fields['shaderClass'], int(fields['alphaTest'])
                if shader not in SHADER_CLASSES or alpha not in ALPHA_TEST:
                    raise ValueError('Invalid capture shader class or alpha test state')
                row.update(shaderClass=shader, alphaTest=alpha)
            for consumer in CONSUMERS:
                values = tuple(map(int, fields[consumer].split(',')))
                if columns is None:
                    columns = len(values)
                counters = dict(zip(WORK, values))
                if (columns not in (9, len(WORK)) or len(values) != columns or any(value < 0 for value in values) or
                        any(counters[name] for name in counters.keys() - set(KNOWN[consumer]))):
                    raise ValueError('Invalid or regressed capture work counters')
                row[consumer] = {name: counters[name] for name in KNOWN[consumer] if name in counters}
            if any(any(read[key] != row[key] for key in IDENTITY) or not row['route'] & read['lane']
                   for read in pending):
                raise ValueError('Capture buffer row is outside its draw or consumer')
            row['buffer_reads'], pending = pending, []
            rows.append(row)
        except (KeyError, ValueError) as error:
            raise ValueError(f'Malformed capture work log: {error}') from error
    if (not trailer or not rows or trailer['rows'] != len(rows) or trailer['fpsEligible'] != 0 or
            min(trailer.values()) < 0 or trailer['frame'] != frame or
            trailer.get('bufferRows', 0) != buffer_rows or
            ('bufferRows' in trailer and columns != len(WORK)) or (classified and 'bufferRows' not in trailer) or
            pending or
            any(any(row[key] != trailer[key] for key in ('device', 'epoch', 'frame')) for row in rows)):
        raise ValueError('Missing or incomplete capture work frame')
    summary = dict(trailer, consumers=consumer_totals(rows, columns),
                   multi_consumer_packet_draws=sum(sum(row[name]['packets'] > 0 for name in CONSUMERS) > 1 for row in rows),
                   multi_consumer_charge_draws=sum(sum(row[name]['charged_bytes'] > 0 for name in CONSUMERS) > 1 for row in rows),
                   identified_draws=sum(row['source'] is not None for row in rows))
    if 'bufferRows' in trailer:
        summary['buffer_reads'] = buffer_summary(rows)
    else:
        for row in rows:
            del row['buffer_reads']
    if classified:
        summary['capture_classes'] = capture_classes(rows, columns)
    format_version = 4 if classified else 3 if 'bufferRows' in trailer else 1 if columns == 9 else 2
    return {'format': format_version, 'summary': summary, 'draws': rows,
            'draw_ordinal': 'renderer draw sequence on this device', 'fps_eligible': False}
