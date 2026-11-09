#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile actual loader/device gates, including fallback and lock-policy independence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p = argparse.ArgumentParser(__doc__)
p.add_argument('--before', type=Path)
a = p.parse_args()
out = fp.output_dir()
renderer = fp.src('renderer.cpp')
source = renderer.read_text(encoding='utf-8')


def gates(text):
    begin = text.index('    materialCacheAsyncReadback=')
    if '    const bool loadedDxvk=' in text:
        begin = text.index('    const bool loadedDxvk=')
    end = text.index('    NorthlightStream::dxvk311EventPayload.store(', begin)
    setup = re.search(r'world->setHdrBaseCompositeEnabled\([^\n]+', text).group()
    warm = re.search(r'HRESULT warmPointPipelinesAtDeviceInit\(\)\{\s*([^\n]+)', text).group(1)
    return text[begin:end], setup, warm


harness = r'''#include "backend_policy.h"
#include <atomic>
#include <cassert>
#include <cstdio>
using HRESULT=int;
constexpr int S_OK=0;
namespace NorthlightUpload {std::atomic<bool> ReadBackNoOverwrite{false};}
bool materialCacheAsyncReadback=false,backendHdrBaseComposite=false,backendPointPipelinePrewarm=false;
template<class... A> void logf(const char*,A...){}
struct Info {bool dxvk;std::string dxvkVersion;};
struct Attempt {Info info;};
struct Result {bool fallback;};
void load(bool module,const Result& result,const Attempt& last){LOADER}
struct World {bool hdr=false;unsigned warms=0;
 void setHdrBaseCompositeEnabled(bool on){hdr=on;}
 HRESULT warmPointPipelinesAtDeviceInit(){++warms;return 42;}
};
struct Device {World state;World* world=&state;
 NorthlightBackend::Kind selectedBackend=NorthlightBackend::Kind::Legacy;
 void setup(){SETUP}
 HRESULT warmPointPipelinesAtDeviceInit(){WARM}
};
int main(){
 struct Case {const char* version;bool hdr,warm,readback;};
 const Case cases[]={{"",false,false,false},{"garbage",false,false,false},
 {"v1.10.2",false,false,false},{"v1.10.3",false,true,true},
 {"v2.7.0",false,false,false},{"v2.7.1",true,true,true},
 {"v3.1.1",true,true,true},{"v3.2.0",true,true,false}};
 unsigned count=0;
 for(const auto& c:cases)for(unsigned mask=0;mask<8;++mask)for(unsigned kind=0;kind<4;++kind)for(bool lock:{false,true}){
  const bool module=mask&1,fallback=mask&2,dxvk=mask&4,loaded=module&&!fallback&&dxvk;
  NorthlightUpload::ReadBackNoOverwrite.store(lock);
  materialCacheAsyncReadback=backendHdrBaseComposite=backendPointPipelinePrewarm=false;
  load(module,{fallback},{{dxvk,c.version}});
  Device d;d.selectedBackend=NorthlightBackend::Kind(kind);d.setup();const auto hr=d.warmPointPipelinesAtDeviceInit();
  assert(d.state.hdr==(loaded&&c.hdr));assert(d.state.warms==unsigned(loaded&&c.warm));
  assert(hr==(loaded&&c.warm?42:S_OK));assert(materialCacheAsyncReadback==(loaded&&c.readback));
  assert(NorthlightUpload::ReadBackNoOverwrite.load()==lock);++count;
 }
 std::printf("BACKEND_WORK_POLICY_PASS cases=%u actualLoadedRuntime=1 fallbackDisabled=1 nativeUnchanged=1 lockFlagsIndependent=1\n",count);
}
'''


def run(label, text):
    loader, setup, warm = gates(text)
    cpp, exe = out / (label+'.cpp'), out / (label+'.exe')
    cpp.write_text(harness.replace('LOADER', loader).replace('SETUP', setup).replace('WARM', warm), encoding='utf-8', newline='\n')
    command = list(map(str, [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                             '-I', fp.src('backend_policy.h').parent, cpp, '-o', exe]))
    subprocess.run(command, env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (out / (label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
    return result


result = run('current', source)
print(result.stdout+result.stderr, end='')
result.check_returncode()
if a.before:
    negative = run('old-backend-gates', a.before.read_text(encoding='utf-8'))
    assert negative.returncode and 'Assertion failed:' in negative.stdout+negative.stderr
    print('BACKEND_WORK_NEGATIVE_PASS old-backend-gates')
assert renderer.read_text(encoding='utf-8') == source
(out/'validation.json').write_text(json.dumps(dict(pass_=True, renderer_sha256=hashlib.sha256(renderer.read_bytes()).hexdigest(),
    negative_control=bool(a.before), scope='Actual loader assignments and Device selectors; GPU pixel/state equivalence tested separately.'), indent=2)+'\n', encoding='utf-8')
