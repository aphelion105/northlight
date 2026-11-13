# northlight-test: requires=windows-gpu,zig,client
"""Actual history helper must preserve the following alpha-tested caster image."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import windows_gpu_guard

windows_gpu_guard.processes()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
support = (fp.TESTS/'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
source = out/'history-state.cpp'
source.write_text(support+(fp.TESTS/'shadow_history_state_gpu.cpp').read_text(), encoding='utf-8', newline='\n')
exe = out/'history-state.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.test_include_flags(), '-I'+str(fp.TESTS), str(source),
                str(fp.src('world_gi.cpp')), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
backend = Path(os.environ.get('NORTHLIGHT_TEST_D3D9', str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')))
run = subprocess.run([str(exe), str(backend), str(fp.COMPILED)], cwd=out, env=fp.zig_env(), capture_output=True, text=True)
(out/'gpu.log').write_text(run.stdout+run.stderr, encoding='utf-8')
print(run.stdout, end='')
run.check_returncode()
