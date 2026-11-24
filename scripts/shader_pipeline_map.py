"""Resolve the world pipeline against current source and shipped shaders."""
from collections import defaultdict
from functools import cache
import hashlib
import json
import re
import struct

import northlight_paths as fp
from scripts.shaders.shader_sources import source_hashes, world_source_bytes


REGISTRY = fp.SCRIPTS / 'shader_pipeline_map.json'
REGISTER_ROW = re.compile(r'^//[ \t]+(\w+)[ \t]+([csib]\d+)[ \t]+(\d+)[ \t]*$', re.M)
REGISTER_USE = re.compile(r'(?<![\w.])([csib]\d+)\b')


def must(ok, message):
    if not ok:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path):
    return path.relative_to(fp.REPO).as_posix()


@cache
def source_bundle(name):
    path = (fp.SHADERS / name).resolve()
    must(path.is_relative_to(fp.SHADERS), f'Invalid shader source: {path}')
    return {relative(path.parent / relative_name): digest for relative_name, digest in source_hashes(path).items()}


@cache
def manifest_source_hash(name):
    # The actor compiler pins the base and the actor entry file together.
    if name == 'ao_actor.hlsl':
        effects, actor = (fp.SHADERS / source for source in ('effects.hlsl', name))
        return hashlib.sha256(effects.read_bytes() + b'\n' + actor.read_bytes()).hexdigest()
    if name == 'world_effects.hlsl':
        return hashlib.sha256(world_source_bytes(fp.SHADERS / name)).hexdigest()
    return sha(fp.SHADERS / name)


def registers(path):
    text = path.read_text(encoding='utf-8-sig')
    named = [{'name': name, 'register': reg, 'size': int(size)} for name, reg, size in REGISTER_ROW.findall(text)]
    instructions = [line.split('//', 1)[0].strip() for line in text.splitlines()]
    operations = [line for line in instructions if line and not line.startswith(('dcl', 'def', 'ps_', 'vs_'))]
    reads = sorted(set(REGISTER_USE.findall('\n'.join(operations))), key=lambda reg: (reg[0], int(reg[1:])))
    profile = re.search(r'^\s*((?:ps|vs)_\w+)\s*$', text, re.M)
    must(profile, f'{path.name}: assembly profile missing')
    return {'profile': profile[1], 'named': named, 'read_registers': reads,
            'relative_register_addressing': any(re.search(r'\b[cib]\s*\[', line) for line in operations)}


def build(inventory, registry=None):
    data = registry if registry is not None else json.loads(REGISTRY.read_text(encoding='utf-8'))
    resources, objects = data['resources'], data['objects']
    declared_passes = data['passes']
    variants, passes = {}, {}
    readers, writers = defaultdict(list), defaultdict(list)
    inventory_entries = defaultdict(list)
    for row in inventory['entries']:
        inventory_entries[row['entry']].append(row)
    code, shader_lines, manifests = {}, {}, {}

    def resolve_anchors(site_map):
        sites = []
        for name, anchors in site_map.items():
            if name not in code:
                path = fp.REPO / name
                must(path.is_file() and path.resolve().is_relative_to(fp.SRC), f'Invalid code site: {name}')
                code[name] = path.read_text(encoding='utf-8').splitlines()
            for anchor in anchors:
                match = next((line for line, text in enumerate(code[name], 1) if anchor in text), None)
                must(match is not None, f'Stale pipeline anchor: {name}: {anchor}')
                sites.append({'path': name, 'line': match, 'anchor': anchor})
        return sites

    def resource(name):
        must(name in resources, f'Unknown pipeline resource: {name}')

    def shader(entry, source):
        matches = inventory_entries[entry]
        must(len(matches) == 1, f'Missing or ambiguous pipeline entry: {entry}')
        row = matches[0]
        manifest_path = fp.SHADERS / row['manifest']
        if manifest_path not in manifests:
            manifests[manifest_path] = json.loads(manifest_path.read_text(encoding='utf-8'))
        manifest = manifests[manifest_path]
        compiled = manifest['shaders'][entry]
        binary = fp.COMPILED / f'{entry}.bin'
        assembly = fp.COMPILED / f'{entry}.bin.asm'
        must(binary.is_file() and sha(binary) == row['sha256'], f'{entry}: stale shader bytecode')
        must(assembly.is_file(), f'{entry}: missing shader assembly')
        bundle = source_bundle(source)
        layout = registers(assembly)
        target = compiled.get('target', manifest.get('target', layout['profile']))
        must(target == layout['profile'], f'{entry}: incompatible assembly profile')
        entry_point = entry.removesuffix('-' + target)
        # The volume compiler uses a VOLUME_TRANSITION wrapper and keeps the HLSL entry.
        if row['manifest'] == 'fog-volume-shader-build.json':
            entry_point = entry_point.removesuffix('Transition')
        signature = re.compile(r'^\s*(?!return\b)\w+\s+' + re.escape(entry_point) + r'\s*\(')
        definitions = []
        for path in bundle:
            if path not in shader_lines:
                shader_lines[path] = (fp.REPO / path).read_text(encoding='utf-8-sig').splitlines()
            definitions.extend({'path': path, 'line': line}
                               for line, text in enumerate(shader_lines[path], 1) if signature.match(text))
        must(len(definitions) == 1, f'{entry}: missing or ambiguous HLSL definition')
        pinned_source = compiled.get('source_sha256', manifest.get('source_sha256'))
        includes = manifest.get('sources', {}) | manifest.get('normal_sources', {})
        if pinned_source is not None:
            must(manifest_source_hash(source) == pinned_source, f'{entry}: stale shader source hash')
        if includes:
            must(all(includes.get(path.removeprefix('shaders/')) == digest for path, digest in bundle.items()),
                 f'{entry}: incomplete or stale include hashes')
        else:
            must(pinned_source is not None, f'{entry}: missing shader source hashes')
            bump = 'shaders/auto_bump_normal.hlsl'
            if bump in bundle:
                must(bundle[bump] == manifest.get('bump_include_sha256'), f'{entry}: stale bump include')
        samplers = tuple(item for item in layout['named'] if item['register'].startswith('s'))
        expected_slots = {f's{slot}' for slot in compiled['samplers']}
        actual_slots = {reg for reg in layout['read_registers'] if reg.startswith('s')}
        must(len(samplers) == len(expected_slots) and expected_slots == actual_slots == {s['register'] for s in samplers},
             f'{entry}: incompatible compiled sampler slots')
        must(row['runtime_references'], f'{entry}: generated symbol has no runtime reference')
        return {'manifest': relative(manifest_path), 'entry_point': entry_point, 'target': target, 'sha256': row['sha256'],
                'symbols': row['symbols'], 'compiler_sha256': compiled.get('compiler_sha256', manifest['compiler_sha256']),
                'source': source, 'definition': definitions[0], 'sources': bundle,
                'assembly': relative(assembly), 'assembly_sha256': sha(assembly),
                'registers': layout, 'runtime_references': row['runtime_references'], 'usages': []}, samplers

    shader_samplers = {}
    for name in data['samplers'].values():
        resource(name)
    for pass_id, info in declared_passes.items():
        inputs = set(info.get('reads', []))
        sites = resolve_anchors(info['code'])
        tests = []
        for name in info['tests']:
            path = fp.TESTS / name
            must(path.is_file() and path.resolve().parent == fp.TESTS, f'{pass_id}: missing test {name}')
            tests.append(relative(path))
        entries = info.get('entries', {})
        entry_sources = info.get('entry_sources', {})
        must(entry_sources.keys() <= entries.keys(), f'{pass_id}: unknown entry source override')
        entry_bindings = info.get('entry_bindings', {})
        must(entry_bindings.keys() <= entries.keys(), f'{pass_id}: unknown entry binding override')
        roles = data['samplers'] | info.get('bindings', {})
        alternatives = info.get('binding_alternatives', {})
        matched_overrides = set()
        for entry, admission in entries.items():
            source = entry_sources.get(entry, info.get('source', 'world_effects.hlsl'))
            if entry not in variants:
                variants[entry], shader_samplers[entry] = shader(entry, source)
            variant = variants[entry]
            must(variant['source'] == source, f'{entry}: inconsistent source across passes')
            bindings = []
            overrides = entry_bindings.get(entry, {})
            must(overrides.keys() <= {item['name'] for item in shader_samplers[entry]}, f'{entry}: unused entry sampler override')
            entry_roles = roles | overrides
            for item in shader_samplers[entry]:
                sampler = item['name']
                must(sampler in entry_roles, f'{entry}: unmapped sampler {sampler}')
                matched_overrides.add(sampler)
                inputs.add(entry_roles[sampler])
                binding = dict(item, resource=entry_roles[sampler])
                if sampler in alternatives:
                    binding['alternatives'] = alternatives[sampler]
                    inputs.update(alternatives[sampler])
                bindings.append(binding)
            variant['usages'].append({'pass': pass_id, 'when': admission, 'bindings': bindings})
        must(info.get('bindings', {}).keys() <= matched_overrides, f'{pass_id}: unused sampler override')
        must(alternatives.keys() <= matched_overrides, f'{pass_id}: unused sampler alternative')
        for name in sorted(inputs):
            resource(name)
            readers[name].append(pass_id)
        for name in info['writes']:
            resource(name)
            writers[name].append(pass_id)
        passes[pass_id] = {'owner': info['owner'], 'locations': sites, 'entries': list(entries), 'reads': sorted(inputs),
                           'explicit_reads': info.get('reads', []),
                           'writes': info['writes'], 'fallback': info['fallback'], 'tests': tests}
    for obj, info in objects.items():
        must(info['passes'] and all(p in passes for p in info['passes']), f'{obj}: unknown object consumer')
    modes = data.get('modes', {})
    for mode, info in modes.items():
        for pass_id, entries in info['passes'].items():
            must(pass_id in passes and set(entries) <= set(passes[pass_id]['entries']), f'{mode}: invalid mode pass or entry')
        for name, pass_id in info['outputs'].items():
            must(pass_id in info['passes'] and pass_id in writers[name], f'{mode}: invalid mode producer for {name}')
    for name in resources:
        must(readers[name] or writers[name], f'Unused pipeline resource: {name}')
    for variant in variants.values():
        if len(variant['usages']) == 1:
            variant.update(variant['usages'][0])

    return {'schema': 3, 'scope': 'World objects, shadows, atmosphere and screen effects through HDR output',
            'evidence': 'Manifest, shipped bytecode, named ASM registers and C++ source anchors. Resource edges include conditional routes. GPU tests are referenced separately.',
            'objects': objects, 'modes': modes, 'passes': passes, 'variants': variants,
            'resources': {name: dict(info, readers=readers[name], writers=writers[name]) for name, info in resources.items()}}


def query(report, term):
    term = term.casefold()
    entry_reads = {}
    mode = next((name for name in report['modes'] if name.casefold() == term), None)
    if mode:
        info = report['modes'][mode]
        heading = {'mode': mode, **info}
        pending, direction = list(info['passes']), 'mode'
    else:
        for name, info in report['objects'].items():
            if term in {key.casefold() for key in (name, *info.get('aliases', []))}:
                heading = {'object': name, 'admission': info['admission'], 'fallback': info['fallback']}
                pending, direction = list(info['passes']), 'downstream'
                break
        else:
            matches = [name for name in report['variants'] if name.casefold() == term]
            must(len(matches) == 1, f'Unknown pipeline entry/object/mode: {term}')
            name = matches[0]
            heading = {'entry': name, **report['variants'][name]}
            entry_reads = {use['pass']: binding_resources(use) for use in heading['usages']}
            pending, direction = list(entry_reads), 'upstream'

    passes, resources = {}, {}
    while pending:
        pass_id = pending.pop(0)
        if pass_id in passes:
            continue
        info = report['passes'][pass_id]
        entries = heading['passes'][pass_id] if mode else info['entries']
        uses = {entry: next(use for use in report['variants'][entry]['usages'] if use['pass'] == pass_id)
                for entry in entries}
        if mode or pass_id in entry_reads:
            reads = set().union(*(binding_resources(use) for use in uses.values())) if mode else entry_reads[pass_id]
            info = dict(info, reads=sorted(set(info['explicit_reads']) | reads))
        admissions = {entry: use['when'] for entry, use in uses.items()}
        passes[pass_id] = dict(info, variants=admissions)
        for name in (*info['reads'], *info['writes']):
            resources[name] = report['resources'][name]
        if direction != 'mode':
            upstream = direction == 'upstream'
            for name in info['reads'] if upstream else info['writes']:
                pending.extend(resources[name]['writers' if upstream else 'readers'])
    result = {'direction': direction, 'selection': heading, 'passes': passes, 'resources': resources}
    if evidence := report.get('replay_shadow_products'):
        observed = {frame: dict(row, resources={name: item for name, item in row['resources'].items() if name in resources},
                               unavailable={name: status for name, status in row['unavailable'].items() if name in resources})
                    for frame, row in evidence['frames'].items()}
        binding_resources_for_frame = {'rain_scrub': {'rain-mask'}, 'water_resolve': {'water-mask'},
            'sky_background': {'sky-layer-color', 'sky-background-copy'}, 'hdr_sky_decode': {'hdr-sky'},
            'late_weather': {'late-weather-color'}}
        for row in observed.values():
            for field, related in binding_resources_for_frame.items():
                if not related.intersection(resources):
                    row.pop(field, None)
            if 'live_bindings' in row:
                row['live_bindings'] = {key: value for key, value in row['live_bindings'].items()
                                        if binding_resources_for_frame[key].intersection(resources)}
        result['replay_shadow_products'] = dict(evidence, frames=observed)
    return result


def binding_resources(use):
    return {name for binding in use['bindings'] for name in (binding['resource'], *binding.get('alternatives', {}))}


def frame_binding_journal(images, frame, route):
    journal = images / f'capture-{frame}-frame-parity-{route}.jsonl'
    if not journal.is_file():
        return None
    rows = [json.loads(line) for line in journal.read_text(encoding='utf-8').splitlines()]
    must(len(rows) >= 3 and rows[0].get('event') == 'begin' and rows[0].get('renderer_frame') == frame and
         rows[0].get('scope') == f'{route}-pass-bindings' and rows[-1].get('event') == 'end' and
         rows[-1].get('render_completed') is True and rows[-1].get('export_complete') is True,
         f'Present {frame}: invalid {route} bindings journal')
    return journal, rows


def read_pass_bindings(observation, entries, reasons, frame):
    journal, rows = observation
    routes = [row for row in rows if row.get('entry') in entries]
    must(len(routes) == 1, f'Present {frame}: expected one of {sorted(entries)}')
    route = routes[0]
    evidence = {'entry': route['entry'], 'journal_sha256': sha(journal)}
    if route.get('event') == 'binding-skip':
        must(route.get('reason') in reasons, f'Present {frame}: unknown {route["entry"]} fallback reason')
        return dict(evidence, state='skipped', reason=route['reason'])
    must(route.get('event') == 'binding-pass' and route.get('draw_HRESULT') == '00000000',
         f'Present {frame}: {route["entry"]} draw failed')
    selected = [row for row in rows if row.get('pass') == route['pass']]
    targets = [row for row in selected if row.get('event') == 'binding-target']
    shaders = [row for row in selected if row.get('event') == 'shader-binding']
    must(len(targets) == len(shaders) == 1, f'Present {frame}: missing or ambiguous shader/target')
    shader_path = journal.with_name(f'{journal.stem}-{int(shaders[0]["shader"])}.bin')
    result = dict(evidence, state='executed', shader_sha256=sha(shader_path), target=targets[0],
                  samplers=[row for row in selected if row.get('event') == 'binding-sampler'])
    for event, field in (('binding-resolve-source', 'resolve_source'), ('binding-depth', 'depth')):
        sources = [row for row in selected if row.get('event') == event]
        if sources:
            must(len(sources) == 1, f'Present {frame}: ambiguous {event}')
            result[field] = sources[0]
    return result


def rain_scrub_bindings(images, frame, width, height):
    if not (observation := frame_binding_journal(images, frame, 'atmosphere')):
        return None
    result = read_pass_bindings(observation, {'RainScrub'}, {'no-rain-mask-draw', 'inputs-unavailable',
                                'resolve-failed', 'state-unavailable', 'target-unavailable'}, frame)
    if result['state'] == 'skipped':
        return result
    target, samplers = result['target'], result['samplers']
    must((target['slot'], target['width'], target['height'], target['format'], target['samples']) ==
         (0, width, height, 21, 0), f'Present {frame}: unexpected RainScrub target')
    must(sorted(row['slot'] for row in samplers) == [0, 1] and all(
        row['present'] is True and (row['width'], row['height'], row['minFilter'], row['magFilter']) ==
        (width, height, 1, 1) for row in samplers), f'Present {frame}: unexpected RainScrub samplers')
    must(result['shader_sha256'] == sha(fp.COMPILED / 'RainScrub.bin'), f'Present {frame}: unexpected RainScrub bytecode')
    return result


def water_resolve_bindings(images, frame, width, height):
    from renderer.water_shader_patch import resolve_shader

    if not (observation := frame_binding_journal(images, frame, 'atmosphere')):
        return None
    variants = {'WaterCorrectDistance': (114, 114, (50,), True),
                'WaterMergeDistanceMS': (115, 114, (50, 114), True),
                'WaterMergeDistance': (115, 114, (114,), False),
                'WaterResolveLegacy': (115, 115, (115,), None)}
    if not any(row.get('entry') in variants or row.get('entry') == 'WaterMaskResolve' for row in observation[1]):
        return None  # Earlier journals only recorded rain.
    result = read_pass_bindings(observation, {*variants, 'WaterMaskResolve'},
                                {'mask-not-requested', 'no-resolve-draw', 'resolve-failed'}, frame)
    if result['state'] == 'skipped':
        return result
    entry, target, samplers = result['entry'], result['target'], result['samplers']
    target_format, source_format, sampler_formats, multisampled = variants[entry]
    source = result.get('resolve_source', {})
    must((target['slot'], target['width'], target['height'], target['format'], target['samples']) ==
         (0, width, height, target_format, 0), f'Present {frame}: unexpected {entry} target')
    must((source.get('width'), source.get('height'), source.get('format')) == (width, height, source_format),
         f'Present {frame}: unexpected {entry} resolve source')
    samples = source['samples']
    has_msaa = samples != 0 and (samples != 1 or source['sample_quality'] != 0)
    must(multisampled is None or has_msaa == multisampled, f'Present {frame}: unexpected {entry} source MSAA')
    must(len(samplers) == len(sampler_formats) and all(
        row['present'] is True and (row['slot'], row['width'], row['height'], row['format'], row['minFilter'], row['magFilter']) ==
        (slot, width, height, fmt, 1, 1) for slot, (row, fmt) in enumerate(zip(samplers, sampler_formats))),
        f'Present {frame}: unexpected {entry} samplers')
    expected = hashlib.sha256(resolve_shader()).hexdigest() if entry == 'WaterResolveLegacy' else sha(fp.COMPILED / f'{entry}.bin')
    must(result['shader_sha256'] == expected, f'Present {frame}: unexpected {entry} bytecode')
    return result


def sky_background_bindings(images, frame, width, height):
    if not (observation := frame_binding_journal(images, frame, 'sky')):
        return None
    result = read_pass_bindings(observation, {'CelestialSkyPS'},
                                {'palette', 'camera', 'resources', 'state', 'copy', 'draw-failed'}, frame)
    if result['state'] == 'skipped':
        return result
    target, depth, samplers = result['target'], result.get('depth', {}), result['samplers']
    must((target['slot'], target['width'], target['height']) == (0, width, height) and
         all(depth.get(key) == target[key] for key in ('width', 'height', 'samples', 'sample_quality')),
         f'Present {frame}: sky depth and target are incompatible')
    must([row['slot'] for row in samplers] == [0, 1, 6] and all(
        row['present'] and (row['width'], row['height'], row['minFilter'], row['magFilter']) ==
        (width, height, 1, 1) for row in samplers[1:]), f'Present {frame}: invalid sky scene/layer samplers')
    must(samplers[0]['present'] is False or (samplers[0]['width'], samplers[0]['height']) == (width, height),
         f'Present {frame}: invalid sky depth sampler')
    must(result['shader_sha256'] == sha(fp.COMPILED / 'CelestialSkyPS.bin'), f'Present {frame}: wrong sky bytecode')
    return result


def hdr_sky_bindings(images, frame, width, height):
    if not (observation := frame_binding_journal(images, frame, 'atmosphere')):
        return None
    if not any(row.get('entry') == 'HdrDecodeWorldSky' for row in observation[1]):
        return None
    result = read_pass_bindings(observation, {'HdrDecodeWorldSky'},
                                {'sdr-frame', 'no-captured-sky', 'no-sky-decode-draw'}, frame)
    if result['state'] == 'skipped':
        return result
    target, source, samplers = result['target'], result.get('resolve_source', {}), result['samplers']
    must(target['slot'] == 0 and (target['width'], target['height'], target['format'], target['samples']) ==
         (width, height, 113, 0), f'Present {frame}: wrong HDR sky target')
    must((source.get('width'), source.get('height'), source.get('format')) == (width, height, 113),
         f'Present {frame}: missing HDR sky source')
    must([row['slot'] for row in samplers] == [0, 1] and all(
        row['present'] and (row['width'], row['height'], row['minFilter'], row['magFilter']) == (width, height, 1, 1)
        for row in samplers), f'Present {frame}: wrong HDR sky color/depth samplers')
    must(result['shader_sha256'] == sha(fp.COMPILED / 'HdrDecodeWorld.bin'), f'Present {frame}: wrong HDR sky decode bytecode')
    return result


def late_weather_bindings(images, frame, width, height):
    if not (observation := frame_binding_journal(images, frame, 'late-weather')):
        return None
    precipitation = {'RainQuality': (3, 5), 'RainPerformance': (3, 5),
                     'SnowQuality': (2, 3, 5), 'SnowPerformance': (2, 3, 5)}
    variants = {**precipitation, 'WeatherLensParticle': (0, 2, 4), 'FrostVignette': (0, 1), 'SandLens': (4,)}
    routes = [row['entry'] for row in observation[1] if row.get('event') in ('binding-pass', 'binding-skip')]
    must(routes and len(set(routes)) == len(routes) and set(routes) <= {*variants, 'LateWeather'},
         f'Present {frame}: invalid late weather routes')
    if routes == ['LateWeather']:
        return read_pass_bindings(observation, {'LateWeather'}, {'weather-pass-unavailable', 'no-active-weather',
            'surface-context-unavailable', 'supplemental-weather-disabled', 'camera-sheltered', 'state-save-failed',
            'no-supplemental-draw'}, frame)
    must('LateWeather' not in routes, f'Present {frame}: late weather skip contradicts draws')
    passes = []
    for entry in routes:
        result = read_pass_bindings(observation, {entry}, set(), frame)
        target, samplers = result['target'], result['samplers']
        must((target['slot'], target['width'], target['height']) == (0, width, height),
             f'Present {frame}: invalid {entry} target')
        must(tuple(row['slot'] for row in samplers) == variants[entry], f'Present {frame}: invalid {entry} sampler slots')
        present = {row['slot']: row for row in samplers if row['present']}
        required = {3} if entry in precipitation else {0, 1} if entry == 'FrostVignette' else {4} if entry == 'SandLens' else set()
        if entry.startswith('Snow'):
            required.add(2)
        must(required <= present.keys() and (entry != 'WeatherLensParticle' or {0, 4} & present.keys()),
             f'Present {frame}: missing {entry} sampler input')
        for slot, sample in present.items():
            expected_filter = 1 if slot in (3, 5) else 2
            must((sample['minFilter'], sample['magFilter']) == (expected_filter, expected_filter),
                 f'Present {frame}: invalid {entry} sampler filter')
            if slot in (0, 3):
                must((sample['width'], sample['height']) == (width, height), f'Present {frame}: invalid {entry} image size')
        must(result['shader_sha256'] == sha(fp.COMPILED / f'{entry}.bin'), f'Present {frame}: wrong {entry} bytecode')
        passes.append(result)
    return {'state': 'executed', 'passes': passes}


def replay_shadow_products(folder):
    from build_renderer import source_identity
    from capture_files import read_bytes
    from compile_native_replay_plan import load_json
    from northrend_point_shadows import cube_products
    from pathlib import Path
    from prepare_native_scene import digest

    folder = folder.resolve()
    validation = folder / 'validation.json'
    validation_sha = sha(validation)
    report = load_json(validation, limit=1 << 20)
    must(report.get('stand_identity') == source_identity(), 'Replay renderer source identity differs from this tree')
    must(report.get('status') in ('PASS', 'FAIL') and report.get('host_exit_code') == 0 and
         (report.get('host_result') or '').startswith('NORTHLIGHT_NATIVE_BRIDGE PASS ') and
         all(report[key] is True for key in ('trace_join_validated', 'stand_world_observed', 'stand_static_mesh_ready')),
         'Replay did not certify trace and World execution')
    selected = report['trace_summary']['selected_presents']
    plan = load_json(folder / 'plan/validation.json', limit=32 << 20)
    trace = Path(report['capture']) / 'wow.trace'
    capture_validation = trace.parent / 'validation.json'
    capture_sha = sha(capture_validation)
    capture = load_json(capture_validation)
    # The plan can also select checkpoint or work-audit Presents without
    # writing images. Only the certified parity frames have screen products.
    parity_frames = report['parity']['frames']
    calls = {str(frame['present_ordinal']): frame['trace_call'] for frame in parity_frames}
    must(capture.get('status') == 'PASS' and calls and len(calls) == len(parity_frames) and
         calls.keys() == capture['parity_frames'].keys() and all(selected.get(frame) == call for frame, call in calls.items()),
         'Replay Presents differ from the validated capture')
    trace_sha = capture['trace']['sha256']
    stand = folder / 'stand/northlight-stand.dll'
    must(plan['pass'] is True and plan['authored_transport_only'] is False and
         plan['summary'] == report['trace_summary'] and plan['inputs'].get(str(trace)) == trace_sha == digest(trace) and
         plan['inputs'].get(str(stand)) == report['stand_sha256'] == sha(stand) and
         report['stand_assets_sha256'] == sha(folder / 'stand/northlight-assets.bin') and
         report['host_sha256'] == sha(folder / 'northlight-native-retrace.exe'),
         'Replay plan, trace or runtime payload differs from the validated input')
    images = folder / 'frame-parity'
    live_images = trace.parent / 'frame-parity'
    same_source = (capture.get('observation', {}).get('identity') == report['stand_identity'] and
                   capture.get('log_sha256') == sha(trace.parent / 'northlight-renderer.log'))
    cubes = {}
    if 'point_cubes' in report:
        cubes = cube_products(images, map(int, calls))
        must(cubes == report['point_cubes'] and all(source['complete'] for frame in cubes.values() for source in frame.values()),
             'Point cube images changed after replay validation or are incomplete')
    width, height = map(int, report['stand_output']['resolution'].split('x'))
    products = {'cascade-near': ('shadow-near', 1024, 1024, 114, 4),
                'cascade-far': ('shadow-far', 1024, 1024, 114, 4),
                'local-light': ('current-local-light', width // 2, height // 2, 113, 8)}
    atmosphere = {'fog-color': ('fog', width // 2, height // 2, 113, 8),
                  'current-distance': ('distance', width // 2, height // 2, 114, 4)}
    binding_readers = {'rain_scrub': rain_scrub_bindings, 'water_resolve': water_resolve_bindings,
                       'sky_background': sky_background_bindings, 'hdr_sky_decode': hdr_sky_bindings,
                       'late_weather': late_weather_bindings}
    frames = {}
    for frame, call in calls.items():
        rows = [json.loads(line) for line in
                (images / f'capture-{frame}-products.jsonl').read_text(encoding='utf-8').splitlines()]
        observed = {}
        unavailable = dict.fromkeys(('water-mask', 'rain-mask', 'sky-layer-color', 'hdr-sky', 'late-weather-color'), 'not-exported')
        if frame in cubes:
            observed['point-cube'] = cubes[frame]
        else:
            unavailable['point-cube'] = 'not-exported'
        for resource, (name, w, h, format_id, stride) in (products | atmosphere).items():
            matches = [row for row in rows if row.get('product') == name]
            if resource in atmosphere and not matches:
                unavailable[resource] = 'not-exported'
                continue
            must(len(matches) == 1 and matches[0].get('renderer_frame') == int(frame),
                 f'Present {frame}: missing or ambiguous {name} journal row')
            item = matches[0]
            if resource in atmosphere and item.get('state') in ('skipped', 'unavailable'):
                unavailable[resource] = item['state']
                continue
            must(item.get('state') == 'current' and item.get('saved') is True and item.get('zero_filled') is False,
                 f'Present {frame}: no current {name} output')
            data = read_bytes(images / f'capture-{frame}-{name}.fgr')
            header = struct.pack('<4s4I', b'FGR1', w, h, format_id, stride)
            must(data.startswith(header) and len(data) == len(header) + w * h * stride,
                 f'Present {frame}: invalid {name} format or size')
            observed[resource] = {'width': w, 'height': h, 'format': format_id, 'samples': 1,
                                  'sha256': hashlib.sha256(data).hexdigest()}
        row = {'trace_call': call, 'resources': observed, 'unavailable': unavailable}
        for field, reader in binding_readers.items():
            if bindings := reader(images, int(frame), width, height):
                row[field] = bindings
            if same_source and (live := reader(live_images, int(frame), width, height)):
                row.setdefault('live_bindings', {})[field] = live
        frames[frame] = row
    must(validation_sha == sha(validation) and capture_sha == sha(capture_validation),
         'Replay or capture validation changed while reading the products')
    return {'scope': 'Exported world products and observed environmental bindings on certified Presents.',
            'validation': str(validation), 'validation_sha256': validation_sha, 'trace_sha256': trace_sha,
            'capture_validation_sha256': capture_sha, 'capture_identity_matches': same_source,
            'stand_identity': report['stand_identity'], 'stand_sha256': report['stand_sha256'], 'world_clock': report['world_clock'],
            'output': report['stand_output'], 'frames': frames, 'fps_eligible': False}


def markdown(report):
    def link(name, path, line=None):
        target = (fp.REPO / path).as_posix() + (f':{line}' if line else '')
        return f'[{name}](<{target}>)'

    lines = ['# World shader pipeline', '', report['evidence'], '',
             '| Pass | Owner | Shader variants | Tests |', '| --- | --- | --- | --- |']
    for name, info in report['passes'].items():
        site = info['locations'][0]
        owner = link(info['owner'], site['path'], site['line'])
        links = ', '.join(link(test.split('/')[-1], test) for test in info['tests'])
        variants = ', '.join(link(entry, **report['variants'][entry]['definition'])
                             for entry in info['entries']) or 'Native / CPU'
        lines.append(f"| {name} | {owner} | {variants} | {links} |")
    lines += ['', '## Resources', '']
    for name, info in report['resources'].items():
        lines.append(f"- **{name}** — {info['shape']}. {info['lifetime']}")
    lines += ['', '## Modes', '']
    for name, info in report['modes'].items():
        producers = ', '.join(f'`{resource}` from `{owner}`' for resource, owner in info['outputs'].items())
        lines.append(f"- **{name}**. {info['admission']} Selected outputs are {producers}. {info['fallback']}")
    if replay := report.get('replay_shadow_products'):
        lines += ['', '## Replay products', '', replay['scope'], '',
                  '| Present | Trace call | Exported resources | Ready point cubes | Missing observations |',
                  '| --- | --- | --- | --- | --- |']
        for frame, row in replay['frames'].items():
            names = ', '.join(row['resources'])
            unavailable = ', '.join(row['unavailable'])
            count = len(row['resources'].get('point-cube', {}))
            lines.append(f"| {frame} | {row['trace_call']} | {names} | {count} | {unavailable} |")
        lines += ['', '## Runtime shader bindings', '', '| Present | Runtime | Entry | Observation |',
                  '| --- | --- | --- | --- |']
        for frame, row in replay['frames'].items():
            fields = ('water_resolve', 'rain_scrub', 'sky_background', 'hdr_sky_decode', 'late_weather')
            for runtime, data in (('Replay', row), ('Live', row.get('live_bindings', {}))):
                for field in fields:
                    if field not in data:
                        continue
                    observation = data[field]
                    for record in observation.get('passes', [observation]):
                        if record['state'] == 'skipped':
                            details = f"Skipped ({record['reason']})"
                        else:
                            target = record['target']
                            slots = ', '.join(f"s{sampler['slot']}" for sampler in record['samplers'] if sampler['present'])
                            details = f"RT0 format {target['format']}, D3D sample type {target['samples']}, samplers {slots}"
                        lines.append(f"| {frame} | {runtime} | `{record['entry']}` | {details} |")
    return '\n'.join(lines) + '\n'


def mermaid(report):
    lines = ['flowchart LR']
    objects = {name: f'o{i}' for i, name in enumerate(report['objects'])}
    passes = {name: f'p{i}' for i, name in enumerate(report['passes'])}
    resources = {name: f'r{i}' for i, name in enumerate(report['resources'])}
    for ids in (objects, passes, resources):
        lines.extend(f'    {node}["{name}"]' for name, node in ids.items())
    for name, info in report['objects'].items():
        lines.extend(f'    {objects[name]} --> {passes[consumer]}' for consumer in info['passes'])
    for name, info in report['passes'].items():
        lines.extend(f'    {resources[resource]} --> {passes[name]}' for resource in info['reads'])
        lines.extend(f'    {passes[name]} --> {resources[resource]}' for resource in info['writes'])
    return '\n'.join(lines) + '\n'
