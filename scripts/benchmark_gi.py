#!/usr/bin/env python3
"""Frozen CPU baseline and GPU GI variants on identical cached geometry; no gameplay FPS claim."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'scripts')]
import northlight_paths as fp
import prototype_gpu_bvh as prototype


def run(out, reference, rounds):
    out.mkdir(parents=True, exist_ok=False)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = [ROOT/'scripts'/name for name in ('benchmark_gi.py', 'prototype_gpu_bvh.py',
        'gpu_bvh_prototype.cpp', 'gpu_bvh_prototype.hlsl')]
    inputs += [fp.src('world_gi.cpp'), *sorted((ROOT/'src').rglob('*.h'))]
    identity = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    git = lambda *args: subprocess.check_output(['git', *args], cwd=ROOT)
    head = git('rev-parse', 'HEAD').decode().strip()
    frozen = git('rev-parse', reference+'^{commit}').decode().strip()
    report = dict(status='BUILDING', production_eligible=False, commit=head, reference=frozen,
        input_sha256=identity, records=[], scope='Isolated x86 CPU / hardware CS5 GI, 512 probes, 32 rays, 3 bounces. No game, D3D9 publication or FPS proof.')
    save = lambda: (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    save()
    binaries = {}
    for mode in ('reference', 'candidate'):
        folder = out/mode
        folder.mkdir()
        cpu_source = None
        if mode == 'reference':
            files = git('ls-tree', '-r', '--name-only', frozen, 'src/gi').decode().splitlines()
            for name in files:
                if name.endswith('.h') or name == 'src/gi/world_gi.cpp':
                    (folder/Path(name).name).write_bytes(git('show', frozen+':'+name))
            cpu_source = folder/'world_gi.cpp'
            report['reference_sha256'] = {p.name: sha(p) for p in folder.iterdir()}
        binaries[mode], compiler = prototype.build(folder, cpu_source)
        report.setdefault('executables', {})[mode] = sha(binaries[mode])
        report['compiler'] = compiler
    report['status'] = 'MEASURING'
    save()
    for layout in ('surface', 'grid'):
        expected = None
        for index, mode in enumerate(('reference', 'candidate', 'candidate', 'reference')):
            folder = out/mode
            flag = '--gi-variants-grid' if layout=='grid' else '--gi-variants'
            command = [str(binaries[mode]), str(folder), '6', str(rounds), str(fp.client_root()), flag]
            result = subprocess.run(command, capture_output=True, timeout=600)
            log = out/f'{layout}-{index}-{mode}.log'
            log.write_bytes(result.stdout+result.stderr)
            rows = [json.loads(line) for line in result.stdout.decode().splitlines() if line.startswith('{')]
            if result.returncode not in (0, 2) or not any(r.get('gi_variants_bit_identical') for r in rows):
                raise RuntimeError(f'Variant validation failed: {log}')
            values = (folder/f'cpu-{layout}.bin').read_bytes()
            if expected is None:
                expected = values
            if values != expected:
                raise RuntimeError(f'CPU probe float bits changed: {log}')
            report['records'].append(dict(layout=layout, mode=mode, index=index,
                exit_code=result.returncode, log=log.name, log_sha256=sha(log), rows=rows,
                cpu_output_sha256=hashlib.sha256(values).hexdigest()))
            save()
            print(f'{layout}: {mode} {index}, CPU exact, GPU variants exact, CPU/GPU gate exit={result.returncode}', flush=True)
    report['medians'] = {}
    for layout in ('surface', 'grid'):
        values = report['medians'][layout] = {}
        for mode in ('reference', 'candidate'):
            rows = [row for rec in report['records'] if rec['layout']==layout and rec['mode']==mode for row in rec['rows']]
            values[mode] = {phase:{key:statistics.median(r[key] for r in rows if r.get('phase')==phase)
                for key in ('wall_ms', 'gpu_ms', 'cpu_cycles')}
                for phase in sorted({r['phase'] for r in rows if 'phase' in r})}
    assert identity == {str(p.relative_to(ROOT)): sha(p) for p in inputs}, 'Inputs changed during measurement'
    assert head == git('rev-parse', 'HEAD').decode().strip(), 'HEAD changed during measurement'
    report.update(status='COMPONENT_COMPARISON', cpu_bit_identical=True, gpu_variants_bit_identical=True,
        cpu_gpu_all_layouts_pass=all(r['exit_code']==0 for r in report['records']))
    save()
    print(json.dumps(report['medians'], indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference', default='5a7daa33')
    parser.add_argument('--rounds', type=int, default=8)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 20:
        parser.error('rounds must be 1..20')
    run(args.output.resolve(), args.reference, args.rounds)
