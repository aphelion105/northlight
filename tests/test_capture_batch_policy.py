#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile the actual device setup: batching is independent of read-back flags."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path);a=p.parse_args()
out=fp.output_dir();renderer=fp.src('renderer.cpp');source=renderer.read_text(encoding='utf-8')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def setup(text):
    material=re.search(r'^\s*autoBump->setDeferredCapture\([^\n]+',text,re.M).group().strip()
    start=text.index('        // Resource proxies and the command-stream writer serialize')
    end=text.index('        mirrorState.beforeDisable=',start)
    return material,text[start:end]
material,guard=setup(source)
harness=r'''#include <atomic>
#include <cassert>
#include <cstdio>
#include <initializer_list>
namespace NorthlightBackend {enum class Kind {Native,Dxvk,Dxvk2,Legacy};}
namespace NorthlightStream {std::atomic<bool> streamActive{false};}
namespace NorthlightUpload {std::atomic<bool> ReadBackNoOverwrite{false};unsigned readBackLock(){return 16u|(ReadBackNoOverwrite.load()?4096u:0u);}}
struct Capture {bool deferred=false,guard=false;unsigned calls=0;void* context=nullptr;
    void setDeferredCapture(bool on,void(*)(void*)=nullptr,void* ctx=nullptr){deferred=on;context=ctx;++calls;}
    void setTextureWriteGuard(bool on,void(*)(void*),void* ctx){guard=on;context=ctx;++calls;}};
struct Mirror {bool enabled=false;bool active()const{return enabled;}};
struct Device {Capture material,normals;Capture* autoBump=&material;Capture* modelNormals=&normals;
    Mirror mirrorState;bool spellOn=false;NorthlightBackend::Kind selectedBackend=NorthlightBackend::Kind::Native;
    static void materialPendingChanged(void*){}
    template<class... A> static void logf(const char*,A...){}
    void setup(){MATERIAL_SETUP
GUARDED_SETUP
}};
int main(){unsigned cases=0;for(unsigned backend=0;backend<4;++backend){for(bool readback:{false,true}){for(unsigned mask=0;mask<8;++mask){
    Device d;d.selectedBackend=NorthlightBackend::Kind(backend);d.spellOn=mask&1;d.mirrorState.enabled=mask&2;
    NorthlightStream::streamActive.store(mask&4);NorthlightUpload::ReadBackNoOverwrite.store(readback);const auto flags=NorthlightUpload::readBackLock();
    d.setup();assert(d.material.deferred);const bool guarded=mask==7;assert(d.normals.deferred==guarded&&d.material.guard==guarded);
    assert(d.material.calls==2&&d.normals.calls==1&&d.material.context==&d&&d.normals.context==&d);
    assert(NorthlightUpload::readBackLock()==flags&&NorthlightUpload::ReadBackNoOverwrite.load()==readback);++cases;
}}}std::printf("CAPTURE_BATCH_POLICY_PASS cases=%u native=1 dxvk1=1 dxvk2=1 dxvk3=1 legacy=1 textureContract=preserved readBackFlags=unchanged\n",cases);}
'''
def run(name,material,guard):
    cpp=out/(name+'.cpp');exe=out/(name+'.exe');cpp.write_text(harness.replace('MATERIAL_SETUP',material).replace('GUARDED_SETUP',guard),encoding='utf-8',newline='\n')
    command=list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',cpp,'-o',exe]))
    subprocess.run(command,env=fp.zig_env(),check=True);r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');return r,command
r,command=run('current',material,guard);print(r.stdout+r.stderr,end='');r.check_returncode()
controls=[]
if a.before:
    oldMaterial,oldGuard=setup(a.before.read_text(encoding='utf-8'))
    for name,m,g in [('old-material-gate',oldMaterial,guard),('old-normal-gate',material,oldGuard)]:
        control,_=run(name,m,g);assert control.returncode!=0 and 'Assertion failed:' in control.stderr+control.stdout,(name,control.stdout,control.stderr)
        controls.append(name);print('CAPTURE_BATCH_NEGATIVE_PASS',name)
assert sha(renderer)==hashlib.sha256(source.encode()).hexdigest()
(out/'validation.json').write_text(json.dumps({'pass':True,'renderer_sha256':sha(renderer),'runner_sha256':sha(Path(__file__)),'command':command,'negative_controls':controls,'scope':'Actual production setup statements, backend/guard combinations and unchanged read-back policy; GPU equivalence is checked separately.'},indent=2)+'\n',encoding='utf-8',newline='\n')
