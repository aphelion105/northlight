# northlight-test: requires=windows-gpu,zig,client manual
"""Exact SDR/HDR base-lighting comparison against neutral-map PCF."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--backend', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--capture', type=Path)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
source = (ROOT/'src/world/world_renderer.h').read_text()
selector = 'unshadowedLighting?lightingUnshadowedPS:softResolved?lightingSoftPS:lightingPS'
assert source.count(selector) == 1
reference = out/'neutral-map-pcf.h'
reference.write_bytes(source.replace(selector, 'softResolved?lightingSoftPS:lightingPS').encode())
manifest = json.loads((ROOT/'shaders/world-shader-build.json').read_text())['shaders']
general, specialized = (manifest[name] for name in ('WorldLighting', 'WorldLightingUnshadowed'))
assert not ({2, 3, 12} & set(specialized['samplers'])), 'Shadow textures remain in the unshadowed program'
assert specialized['static_instruction_slots'] < general['static_instruction_slots']
assert specialized['temporary_registers'] < general['temporary_registers']
command = [sys.executable, str(ROOT/'tests/profile_base_weather_pipeline.py'),
           '--backend', str(args.backend.resolve()), '--output', str(out/'comparison'),
           '--world-header', str(reference), '--compare-current']
if args.capture:
    command += ['--capture', str(args.capture.resolve())]
subprocess.run(command, cwd=ROOT, check=True)
comparison = json.loads((out/'comparison/comparison.json').read_text())
assert comparison['pass_']
print('WORLD_UNSHADOWED_LIGHTING_PASS color=exact distance=exact fog=exact SDR=1 HDR=1')
