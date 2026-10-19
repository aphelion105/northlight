from pathlib import Path
import subprocess,tempfile,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='northlight-frame-stall-') as tmp:
    for label,flags in [('O2',['-O2']),('debug',['-O1','-g'])]:
        binary=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
