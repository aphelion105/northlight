# northlight-test: requires=windows-gpu,zig,client
"""Real D3D9 height upload, pixel and Reset contract."""
import os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
subprocess.run([sys.executable,str(Path(__file__).with_name('test_height_gpu_ownership.py')),'--backend',os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))],check=True)
