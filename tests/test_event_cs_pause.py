# northlight-test: requires=zig,windows-exe
"""Compare the production EVENT scheduler against its prior CPU-spin policy."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='event-cs-pause-') as folder:
    folder=Path(folder)
    def build(name, includes):
        exe=folder/(name+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                    '-Wall','-Wextra','-Werror',*includes,*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),
                    '-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
        return exe
    subprocess.run([str(build('current',[]))],check=True,timeout=60)
    header=fp.src('query_poll.h').read_text()
    guard='if((backend==EventPendingKind::Untouched&&elapsed>=200000)||expired'
    assert header.count(guard)==1
    (folder/'query_poll.h').write_text(header.replace(guard,'if((false&&backend==EventPendingKind::Untouched)||expired'))
    control=subprocess.run([str(build('old-policy',['-I',str(folder)]))],capture_output=True,text=True,timeout=60)
    assert control.returncode!=0 and 'elapsed<200000' in control.stderr,control.stdout+control.stderr
    print('EVENT_CS_NEGATIVE_PASS removing_only_cpu_pending_rule_fails_200us_spin_bound',flush=True)
