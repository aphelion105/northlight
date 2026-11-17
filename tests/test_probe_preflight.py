# northlight-test: requires=zig,windows-exe
"""Exact old/new probe, response, moments and moving results across validity boundaries."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()/'probe-preflight'
out.mkdir(parents=True, exist_ok=True)
source = fp.src('world_gi.cpp').read_text()
switch = 'static constexpr bool ProbeRejectBeforeTransport=true;'
assert source.count(switch) == 1
outputs = []
for mode in ('reference', 'candidate'):
    cpp = out/(mode+'.cpp')
    reference = source.replace(switch, switch.replace('true', 'false'))
    cpp.write_text(reference if mode=='reference' else source)
    exe = out/(mode+'.exe')
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
               '-UNDEBUG', *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')), str(cpp), '-o', str(exe)]
    built = subprocess.run(command, env=fp.zig_env(), capture_output=True, text=True)
    (out/(mode+'-compile.log')).write_text(built.stdout+built.stderr)
    built.check_returncode()
    result = subprocess.run([str(exe)], capture_output=True, timeout=60)
    (out/(mode+'.log')).write_bytes(result.stdout+result.stderr)
    result.check_returncode()
    assert b'PREFLIGHT_CASES' in result.stdout
    outputs.append(result.stdout)
assert outputs[0] == outputs[1], 'Probe preflight changed float bits or validity'
(out/'validation.json').write_text(json.dumps(dict(pass_=True, bit_identical=True,
    source_sha256=hashlib.sha256(source.encode()).hexdigest(),
    output_sha256=hashlib.sha256(outputs[1]).hexdigest()), indent=2)+'\n')
print('PROBE_PREFLIGHT_PASS: every float bit, validity, response and moving result agrees')
