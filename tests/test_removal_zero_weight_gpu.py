# northlight-test: requires=windows-gpu,zig,client
"""Exact current/frozen lighting history after eliding zero-weight texture reads."""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'removal-zero-weight'
out.mkdir(parents=True, exist_ok=True)
reference = out / 'before.bin'
reference.write_bytes(subprocess.check_output([
    'git', 'show', 'b725ae54de12db3254ea1cf0a6f72b17e6de98c6:shaders/compiled/TemporalLightMerged.bin'], cwd=fp.REPO))
subprocess.run([sys.executable, str(fp.TESTS / 'test_removal_temporal_merge_gpu.py'),
                '--reference-fused', str(reference), '--candidate', str(fp.COMPILED / 'TemporalLightMergedZeroWeight.bin'), '--output', str(out / 'gpu')], check=True)
