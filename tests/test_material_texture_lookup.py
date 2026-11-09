#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compare metadata lookup hints with linear lookup, including miss invalidation."""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()

def once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)

def run(output):
    output.mkdir(parents=True, exist_ok=True)
    source = fp.src('particle_texture_stats.h').read_text()
    reference = once(source, '    // An absent identity stays absent until slot() inserts a new record.\n    const void* missing_=nullptr;\n', '')
    reference = once(reference, 'if(missing_==raw)return nullptr;', '')
    reference = once(reference, 'missing_=raw;', '')
    reference = once(reference, 'recent_=selected;missing_=nullptr;', '')
    reference = reference.replace('NorthlightParticleTextures', 'MaterialLookupReferenceParticles').replace('NorthlightMaterialTextures', 'MaterialLookupReference')
    (output/'material_texture_lookup_reference.h').write_text(reference)
    results = []
    for label, flags in [('release', ['-O2']), ('debug', ['-O0']), ('stale-miss-control', ['-O2'])]:
        include = output
        if label == 'stale-miss-control':
            include = output/'stale-miss'
            include.mkdir(exist_ok=True)
            (include/'particle_texture_stats.h').write_text(once(source, 'recent_=selected;missing_=nullptr;', 'recent_=selected;'))
        exe = output/(label+'.exe')
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', *flags, '-UNDEBUG',
                        '-I'+str(include), '-I'+str(output), *fp.test_include_flags(),
                        str(fp.TESTS/'material_texture_lookup.cpp'), '-o', str(exe)], check=True, env=fp.zig_env())
        command = [str(exe)] + (['--benchmark'] if args.benchmark and label == 'release' else [])
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        log = output/(label+'.log')
        log.write_text(result.stdout+result.stderr)
        if label == 'stale-miss-control':
            assert result.returncode and 'a.version(raw)==b.version(raw)' in result.stderr, result.stdout+result.stderr
            print('MATERIAL_LOOKUP_STALE_MISS_CONTROL_PASS', flush=True)
        else:
            assert result.returncode == 0 and 'MATERIAL_LOOKUP_PASS comparisons=48000' in result.stdout, result.stdout+result.stderr
            print(label, result.stdout.strip(), flush=True)
        results.append({'mode': label, 'exit_code': result.returncode, 'log': str(log)})
    digest = lambda text: hashlib.sha256(text.encode()).hexdigest()
    (output/'validation.json').write_text(json.dumps({'pass': True, 'source_sha256': hashlib.sha256(fp.src('particle_texture_stats.h').read_bytes()).hexdigest(),
        'reference_sha256': digest(reference), 'runs': results}, indent=2)+'\n')

if args.output:
    run(args.output.resolve())
else:
    with tempfile.TemporaryDirectory(prefix='material-texture-lookup-') as temp:
        run(Path(temp))
