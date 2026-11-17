# northlight-test: requires=windows-gpu,zig,client
"""Moving foliage cannot leave foreground fog history in clear sky."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

out = fp.output_dir()
source = out / 'fog-disocclusion.cpp'
support = (fp.TESTS / 'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
source.write_text(support + (fp.TESTS / 'fog_temporal_disocclusion_gpu.cpp').read_text(), encoding='utf-8')
exe = out / 'fog-disocclusion.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                '-UNDEBUG', *fp.test_include_flags(), str(source), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backends = [Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll',
            fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll']
for index, backend in enumerate(backends):
    processes()
    result = subprocess.run([str(exe), str(backend)], cwd=out,
                            env={**os.environ, 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(out)},
                            capture_output=True, text=True, timeout=120)
    (out / f'backend-{index}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(backend, result.stdout, result.stderr, flush=True)
    assert result.returncode == 0 and 'FOG_DISOCCLUSION_PASS' in result.stdout
