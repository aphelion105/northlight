#!/usr/bin/env python3
"""Import bounded snapshot sidecars and prepare private D3D9 replay inputs.

This never launches a game, installs a wrapper or executes a replay. Actual
apitrace validation and Northlight world-context/resource bridges are separate
acceptance gates; a preparation manifest cannot prove full-scene FPS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib

HEADER_BYTES = 40
MAX_KEYS, MAX_COPIES, ARENA_BYTES = 64, 256, 64 * 1024
MAX_ARCHIVE_BYTES = HEADER_BYTES + MAX_COPIES * 16 + ARENA_BYTES + 4


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def inspect_snapshot(path):
    path = Path(path)
    if not HEADER_BYTES + 4 <= path.stat().st_size <= MAX_ARCHIVE_BYTES:
        raise ValueError('Snapshot archive size is outside the fixed capacity')
    data = path.read_bytes()
    magic, version, size, trigger, keys, copies, payload, draw = struct.unpack_from('<8s6IQ', data)
    if magic != b'NLSNAP1\0' or version != 1:
        raise ValueError('Unsupported snapshot archive magic/version')
    if (size != len(data) or trigger > 3 or not 0 < keys <= MAX_KEYS
            or not keys <= copies <= MAX_COPIES or payload > ARENA_BYTES
            or HEADER_BYTES + copies * 16 + payload + 4 != size):
        raise ValueError('Invalid snapshot archive header bounds')
    if struct.unpack_from('<I', data, size - 4)[0] != zlib.crc32(data[:-4]):
        raise ValueError('Snapshot archive checksum mismatch')
    offset, copied = HEADER_BYTES, 0
    seen = set()
    failed = 0
    for _ in range(copies):
        if offset + 16 > size - 4:
            raise ValueError('Truncated snapshot record')
        address, count, flags = struct.unpack_from('<QII', data, offset)
        offset += 16
        if address > 0xffffffff or count > ARENA_BYTES or flags not in (0, 1) or (flags and offset + count > size - 4):
            raise ValueError('Invalid snapshot record bounds/flags')
        seen.add((address, count))
        if flags:
            offset += count
            copied += count
        else:
            failed += 1
    if offset != size - 4 or copied != payload or len(seen) != keys:
        raise ValueError('Snapshot record/header counts differ')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': size,
            'keys': keys, 'copies': copies, 'payload_bytes': payload,
            'recorded_failures': failed, 'trigger_kind': trigger, 'draw_ordinal': draw}


def pe32(path):
    with Path(path).open('rb') as source:
        header = source.read(64)
        if len(header) != 64 or header[:2] != b'MZ':
            raise ValueError(f'Not a PE image: {path}')
        offset = struct.unpack_from('<I', header, 60)[0]
        if offset > 16 * 1024 * 1024:
            raise ValueError('PE header offset is outside the supported bound')
        source.seek(offset)
        header = source.read(26)
    if len(header) != 26 or header[:4] != b'PE\0\0' or struct.unpack_from('<H', header, 4)[0] != 0x14c or struct.unpack_from('<H', header, 24)[0] != 0x10b:
        raise ValueError(f'The D3D9 scene stand requires x86 PE32 inputs: {path}')


def tool_runtime(root):
    """Keep the official CLI/retracer's bundled DLLs, including D3Dcompiler_47.

    PE32 checks identify architecture only. The acceptance runner separately
    records actual tool parsing, loaded modules and output; this is no readiness
    shortcut and does not assume that copying d3dretrace.exe alone is enough.
    """
    root = Path(root).resolve()
    required = [root / 'bin/apitrace.exe', root / 'bin/d3dretrace.exe',
                root / 'bin/D3Dcompiler_47.dll', root / 'lib/wrappers/d3d9.dll']
    dependencies = sorted((root / 'bin').glob('*.dll'), key=lambda p: p.name)
    for path in dict.fromkeys(required + dependencies):
        if not path.is_file():
            raise ValueError(f'Missing official x86 tool artifact: {path}')
        pe32(path)
    return {'root': str(root), 'files': {str(p): digest(p) for p in dict.fromkeys(required + dependencies)},
            'retrace_dependencies': [str(p) for p in dependencies],
            'runtime_dependency_loading_validated': False}


def prepare(args):
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output must be new; existing evidence is immutable')
    tools = tool_runtime(args.tool_root) if getattr(args, 'tool_root', None) else None
    if tools and args.retracer and args.retracer.resolve() != Path(tools['root']) / 'bin/d3dretrace.exe':
        raise ValueError('The retracer must belong to the identified official tool bundle')
    inputs = [p.resolve() for p in (args.scene_manifest, args.trace, args.snapshot_index,
                                  args.retracer, args.backend_dll, args.northlight_dll, args.quality_ini) if p]
    if tools:
        inputs += [Path(p) for p in tools['files']]
    if any(p == output or p.is_relative_to(output) for p in inputs):
        raise ValueError('Output must not contain/replace source inputs')
    before = {str(p): digest(p) for p in inputs}
    scene = json.loads(args.scene_manifest.read_text(encoding='utf-8')) if args.scene_manifest else None
    trace_path = args.trace.resolve() if args.trace else None
    trace_hash = digest(trace_path) if trace_path else None
    if args.trace and not args.trace.stat().st_size:
        raise ValueError('An empty file is not a captured trace')
    snapshots = []
    index = None
    if args.snapshot_index:
        if not args.trace:
            raise ValueError('A snapshot index requires its actual source trace')
        if args.snapshot_index.stat().st_size > 4 * 1024 * 1024:
            raise ValueError('Snapshot index is too large')
        index = json.loads(args.snapshot_index.read_text(encoding='utf-8'))
        if index.get('format') != 1 or index.get('capture_layer') != args.capture_layer or index.get('trace_sha256') != trace_hash:
            raise ValueError('Snapshot index must identify this exact trace and capture layer')
        records = index.get('records')
        if not isinstance(records, list) or not 0 < len(records) <= 20000:
            raise ValueError('Invalid snapshot index record count')
        previous_call = -1
        for record in records:
            call = record['trace_call']
            if type(call) is not int or call <= previous_call:
                raise ValueError('Snapshot trace calls must be unique and increasing')
            previous_call = call
            relative = Path(record['path'])
            path = (args.snapshot_index.parent / relative).resolve()
            if relative.is_absolute() or not path.is_relative_to(args.snapshot_index.parent.resolve()):
                raise ValueError('Snapshot paths must remain inside the sidecar directory')
            metadata = inspect_snapshot(path)
            if type(record['draw_ordinal']) is not int or metadata['sha256'] != record['sha256'] or metadata['draw_ordinal'] != record['draw_ordinal']:
                raise ValueError('Snapshot bytes/draw provenance differs from the index')
            before[str(path)] = metadata['sha256']
            snapshots.append({'source': str(path), 'trace_call': call, **metadata})
    for path in (args.retracer, args.backend_dll, args.northlight_dll):
        if path:
            pe32(path)
    if args.northlight_dll and args.capture_layer != 'above-northlight':
        raise ValueError('Reapplying Northlight to a baked below-Northlight trace would apply its effects twice')
    if args.northlight_dll and args.backend_dll and not args.backend_kind:
        raise ValueError('The Northlight private chain requires an explicit backend kind')
    output.mkdir(parents=True)
    staged_hashes = {}

    def copy_input(source, destination):
        source = Path(source).resolve()
        expected = before[str(source)]
        shutil.copyfile(source, destination)
        if digest(destination) != expected:
            raise ValueError('A staged artifact differs from its frozen source')
        staged_hashes[destination.relative_to(output).as_posix()] = expected

    if scene:
        copy_input(args.scene_manifest, output / 'static-scene-manifest.json')
    if snapshots:
        (output / 'snapshots').mkdir()
        for number, record in enumerate(snapshots):
            relative = f'snapshots/{number:06d}.nlsnap'
            copy_input(record['source'], output / relative)
            record['path'] = relative
    plans = []
    for mode in ('native-only', 'northlight'):
        if not args.retracer or not args.backend_dll or (mode == 'northlight' and not args.northlight_dll):
            continue
        if mode == 'native-only' and args.capture_layer == 'below-northlight':
            mode = 'baked-backend-replay'
        target = output / mode
        target.mkdir()
        copy_input(args.retracer, target / 'd3dretrace.exe')
        if tools:
            for dependency in tools['retrace_dependencies']:
                copy_input(dependency, target / Path(dependency).name)
        if mode != 'northlight':
            copy_input(args.backend_dll, target / 'd3d9.dll')
        else:
            copy_input(args.northlight_dll, target / 'd3d9.dll')
            copy_input(args.backend_dll, target / 'scene_backend_d3d9.dll')
            config = target / 'northlight-renderer.ini'
            config.write_text(f'[Renderer]\nBackend={args.backend_kind}\nBackendPath=scene_backend_d3d9.dll\n', encoding='utf-8')
            staged_hashes[config.relative_to(output).as_posix()] = digest(config)
            if args.quality_ini:
                copy_input(args.quality_ini, target / 'northlight-quality.ini')
        plans.append({'mode': mode, 'executable': str(target / 'd3dretrace.exe'),
                      'working_directory': str(target),
                      'arguments': ['--headless', '--query-handling=run', str(trace_path)] if trace_path else None,
                      'execution_validated': False, 'northlight_context_validated': False,
                      'tool_runtime_dependencies_validated': False, 'uncapped_replay_validated': False,
                      'presentation_and_msaa_parity_validated': False})
    if any(digest(Path(p)) != sha for p, sha in before.items()):
        raise ValueError('An input changed while preparing the immutable scene')
    manifest = {
        'format': 1, 'capture_layer': args.capture_layer, 'input_sha256': before,
        'trace': str(trace_path) if trace_path else None, 'trace_sha256': trace_hash,
        'staged_sha256': staged_hashes, 'official_tool_bundle': tools,
        'trace_parsed_by_official_tool': False, 'snapshots': snapshots, 'replay_plans': plans,
        'static_scene': scene, 'full_scene_replay_ready': False, 'fps_acceptance_proven': False,
        'game_launched': False, 'replay_executed': False, 'installed_files_modified': False,
        'missing_acceptance': [
            'Actual complete above-Northlight native trace parsed by the official x86 D3D9 retracer',
            'Exact draw/resource identity join between the trace and sidecar (index fields alone are not proof)',
            'Typed world-context/static signature provider; imported missing reads must fail closed',
            'Positive Northlight activation and complete color/depth/MSAA output parity',
            'Same-backend uncapped full-sequence native-only/Northlight timing, without injected sleeps',
            'Original native producer workload or independent CPU evidence for rotation gaps',
        ],
        'limitations': [
            'Below-Northlight captures contain baked extension calls; they cannot measure a new Northlight renderer.',
            'The imported read FIFO does not contain the original code-range/static signature provider.',
            'Preparation never proves that the proxy, backend, external tool or world renderer actually activated.',
            'The source trace remains referenced in place and its SHA-256 must be rechecked before replay.',
        ],
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture-layer', choices=('above-northlight', 'below-northlight'), required=True)
    parser.add_argument('--scene-manifest', type=Path)
    parser.add_argument('--trace', type=Path)
    parser.add_argument('--snapshot-index', type=Path)
    parser.add_argument('--retracer', type=Path)
    parser.add_argument('--tool-root', type=Path, help='Extracted official x86 bundle; preserves bundled retracer DLL dependencies')
    parser.add_argument('--backend-dll', type=Path)
    parser.add_argument('--backend-kind', choices=('native', 'dxvk', 'dxvk2'))
    parser.add_argument('--northlight-dll', type=Path)
    parser.add_argument('--quality-ini', type=Path)
    result = prepare(parser.parse_args())
    print(json.dumps({'prepared': True, 'snapshot_count': len(result['snapshots']),
                      'full_scene_replay_ready': False, 'fps_acceptance_proven': False}))
