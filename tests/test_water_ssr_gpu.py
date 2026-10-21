# northlight-test: requires=windows-gpu,zig,client
"""Run the actual water renderer against late world surfaces on a hidden D3D9 device."""
from pathlib import Path
import os, subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
output=fp.out()/'water-ssr-gpu';output.mkdir(parents=True,exist_ok=True)
exe=output/'water-ssr-gpu.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                *fp.test_include_flags(),str(fp.TESTS/'water_ssr_pipeline_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
env=dict(os.environ,DXVK_CONFIG='d3d9.customVendorId = 1002; d3d9.supportDFFormats = True')
subprocess.run([str(exe),backend],cwd=output,env=env,check=True,timeout=60)
