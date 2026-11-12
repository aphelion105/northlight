# northlight-test: requires=windows-gpu,zig,client,stormlib timing
"""Stock M2 source handoff, failed-proof reuse and bounded cold-source progress."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

subprocess.run([
    sys.executable, str(fp.TESTS / 'test_material_equal_depth_gpu.py'),
    '--output', str(fp.output_dir()),
    '--backend', str(fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'),
    '--benchmark', '--m2-admission',
], check=True)
