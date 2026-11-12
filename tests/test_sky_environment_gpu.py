# northlight-test: requires=windows-gpu,zig,client
"""Imported sky must preserve clouds, HDR emission and opaque geometry."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

processes()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out/'sky-environment.exe'
source = fp.src('celestial_disc_renderer.h').read_text(encoding='utf-8')
start = source.index('    bool renderSkyBackground(')
end = source.index('\n    }', start) + len('\n    }')
# Exercise the exact production device setup, not just the shader with a
# separately enabled depth guard (the caller deliberately passes no snapshot).
(out/'sky-background-production.h').write_text(
    '#include "saved_state.h"\n#include "celestial_disc_compiled_shaders.h"\n'
    'struct SkyBackgroundProduction {IDirect3DDevice9* d;IDirect3DPixelShader9* skyBackgroundPS=nullptr;\n'
    + source[start:end] + '\n};\n', encoding='utf-8', newline='\n')
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', '-I'+str(out), *fp.test_include_flags(), str(fp.TESTS/'sky_environment_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
for backend in (Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
                fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
                fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'):
    subprocess.run([str(exe), str(fp.COMPILED/'CelestialSkyPS.bin'), str(backend)], cwd=out, check=True, timeout=60)
