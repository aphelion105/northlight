# northlight-test: requires=zig
"""Exercise the real shelter worker against floor and roof BVHs, without D3D or game."""
from pathlib import Path
import os,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='northlight-shelter-',dir=fp.output_dir()) as tmp:
    exe=Path(tmp)/'shelter.exe'
    subprocess.run([str(fp.zig()),'c++',*(['-target','x86-windows-gnu'] if os.name=='nt' else []),'-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
