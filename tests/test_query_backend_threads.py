# northlight-test: requires=zig,windows-exe
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='query-threads-') as folder:
    for name,flags in [('O2',['-O2']),('debug',['-O0','-g'])]:
        exe=Path(folder)/(name+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True,timeout=30)
