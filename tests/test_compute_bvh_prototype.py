# northlight-test: requires=zig,windows-gpu,client timing
"""GPU Morton/LBVH bounds, topology, traces and sun/sky probes; hard negative control."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

path = fp.REPO/'scripts/prototype_gpu_bvh.py'
spec = importlib.util.spec_from_file_location('compute_bvh_experiment', path)
prototype = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prototype)
out = fp.output_dir()
experiment = out/'experiment'
report = prototype.run(experiment, [0, 1, 2, 3], 1)
assert report['pass_'] and report['production_eligible'] is False
# Corrupt the GPU bounds after construction: the actual device readback oracle
# must fail. Diagnostic mode never disables structural validation.
exe = experiment/'gpu-bvh-prototype.exe'
negative = subprocess.run([str(exe), str(experiment), '2', '1', str(fp.client_root()),
                           '--negative-bounds'], capture_output=True, text=True, timeout=120)
(out/'negative-bounds.log').write_text(negative.stdout+negative.stderr)
assert negative.returncode == 1 and 'GPU leaf bounds exclude geometry' in negative.stderr
assert 'GPU_BVH_PASS' not in negative.stdout
(out/'validation.json').write_text(json.dumps(dict(pass_=True, production_eligible=False,
    positive_scenes=[0, 1, 2, 3], negative_bounds_rejected=True,
    experiment_report_sha256=hashlib.sha256((experiment/'results.json').read_bytes()).hexdigest()), indent=2)+'\n')
print('GPU_BVH_ORACLE_PASS: structural/limited sun-sky fixtures; no production migration acceptance')
