# northlight-test: requires=zig,windows-exe
"""Compile the actual renderer timer gate; profiling-off must read no sample clock."""
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

method = re.search(r'bool sampledDrawTimers\(\)const\{[^}]+\}', fp.src('renderer.cpp').read_text())[0]
out = fp.out()/'production-timers'
out.mkdir(exist_ok=True)
cpp = out/'check.cpp'
cpp.write_text('''#include <cassert>
#include <initializer_list>
namespace NorthlightRenderThreadProbe { bool on; bool profiling(){return on;} }
struct Device {
    bool sample=false, gateUntimed=false;
    mutable unsigned sampleReads=0;
    bool sampled() const {++sampleReads;return sample;}
    '''+method+'''
};
int main(){
    for(bool profile:{false,true})for(bool sample:{false,true})for(bool untimed:{false,true}){
        NorthlightRenderThreadProbe::on=profile;
        Device device;device.sample=sample;device.gateUntimed=untimed;
        for(unsigned i=0;i<100000;++i)assert(device.sampledDrawTimers()==(profile&&sample&&!untimed));
        assert(device.sampleReads==(profile?100000u:0u));
    }
}
''')
exe = out/'check.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', str(cpp), '-o', str(exe)],
               env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
print('PASS 800000 production timer gate cases; profiling-off performs no sampled() calls')
