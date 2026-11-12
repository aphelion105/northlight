#!/usr/bin/env python3
# northlight-test: requires=zig timing
"""Exact LRU/atlas equivalence and allocation counts for bounded probe churn."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
source = fp.src('world_probe_cache.h').read_text()
first = source.index('class ProbeCache {')
last = source.index('// Fixed world slots', first)
reference = source[first:last].replace('ProbeCache', 'ReferenceProbeCache')
start = reference.index('\n', reference.index('    void putMissing(')) + 1
end = reference.index('    // Membership without touching', start)
# Frozen pre-recycling insertion; every other cache operation is identical.
reference = reference[:start] + '''        entries_.push_front({key,value});
        try{lookup_.emplace(key,entries_.begin());}
        catch(...){entries_.pop_front();throw;}
        if(entries_.size()>capacity_) {
            lookup_.erase(entries_.back().key);entries_.pop_back();
        }
    }
''' + reference[end:]
(out/'probe_cache_reference.h').write_text('namespace NorthlightGI {\n' + reference + '}\n')
for mode in ('O0', 'O2'):
    exe = out/('probe-cache-' + mode + ('.exe' if os.name == 'nt' else ''))
    command = [str(fp.zig()), 'c++', '-std=c++17', '-' + mode, '-UNDEBUG',
               '-Wall', '-Wextra', '-Werror', '-I', str(out), *fp.test_include_flags(),
               str(Path(__file__).with_suffix('.cpp')), '-o', str(exe)]
    if os.name == 'nt':
        command[2:2] = ['-target', 'x86-windows-gnu']
    subprocess.run(command, env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (out/(mode + '.log')).write_text(result.stdout + result.stderr)
    print(mode, result.stdout + result.stderr, flush=True)
    result.check_returncode()
