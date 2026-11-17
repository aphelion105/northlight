# northlight-test: requires=zig,windows-exe timing
"""Point-shadow broad phase versus the full conservative face classifier."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'point-batch-tree'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'point-batch-tree.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           *fp.test_include_flags(), fp.TESTS / 'point_batch_tree.cpp', '-o', exe]
run = subprocess.run(list(map(str, command)), env=fp.zig_env(), capture_output=True, text=True)
(out / 'compile.log').write_text(run.stdout + run.stderr)
if run.returncode:
    print(run.stdout + run.stderr)
run.check_returncode()
run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
(out / 'validation.log').write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr)
run.check_returncode()
assert 'POINT_BATCH_TREE_PASS ' in run.stdout
