#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile the actual AO submission instrumentation: no disabled clocks or extra draws."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text()
contact = fp.src('contact_ao_pass.h').read_text()
start = contact.index('const bool timeAOBlur=')
end = contact.index('\n    }\n    return true;', start)
region = contact[start:end]
timing = contact[contact.index('struct BlurTiming {'):contact.index('struct Outputs {')]
scope = source[source.index('struct CpuScope {'):source.index('static NorthlightMemoryDiagnostics::Sample')]
assert 'logf(' not in region
assert source.index('logf("AO_SUBMIT') > source.index('if(sampledFrame&&cpuFrequency.QuadPart>0)')
folder = fp.output_dir() / 'ao-submit-profile'
folder.mkdir(parents=True, exist_ok=True)
cpp = folder / 'actual-region.cpp'
cpp.write_text('''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
using LONGLONG=long long;using HRESULT=int;
struct LARGE_INTEGER {LONGLONG QuadPart=0;};
static unsigned clocks=0;
static bool QueryPerformanceCounter(LARGE_INTEGER* value){value->QuadPart=++clocks;return true;}
''' + scope + timing + '''
struct GPU {
    bool on=false;std::vector<std::string> labels;
    bool recording()const{return on;}
    std::uint64_t sampledFrame()const{return on?9876:0;}
    void mark(const char* label){if(on)labels.emplace_back(label);}
};
struct Extension {
    bool fail=false;std::vector<int> calls;
    HRESULT SetRenderTarget(unsigned slot,int target){calls.push_back(int(slot)+target*100);return fail?-1:0;}
    void SetTexture(unsigned slot,int texture){calls.push_back(1000+int(slot)+texture*100);}
    void SetTexture(unsigned slot,std::nullptr_t){SetTexture(slot,0);}
    void SetPixelShader(int shader){calls.push_back(2000+shader);}
};
enum class Bucket {AO};
struct Buckets {unsigned marks=0;void mark(Bucket){++marks;}};
struct Device {
    GPU gpu;GPU* gpuProfile=&gpu;Extension extension;Extension* ext=&extension;Buckets effectsBuckets;
    unsigned width=137,height=79,aoMethod=2,draws=0,bindings=0;bool failDraw=false,cpuClockAvailable=true;
    int aoSurface=3,aoRaw=4,aoBlurPS=5,scene=6,waterMask=0,aoBlurNoWaterPS=0;
    BlurTiming blurTiming;
    bool diagnosticAo=false;
    bool error(HRESULT hr,const char*){return hr<0;}
    void bindBlurInputs(){++bindings;}
    HRESULT quad(unsigned w,unsigned h,bool viewport){assert(w==68&&h==39&&!viewport);++draws;return failDraw?-1:0;}
    bool run(){
''' + region + '''
        return true;
    }
};
int main(){
    for(bool recording:{false,true})for(bool frequency:{false,true})for(bool targetFailure:{false,true})for(bool drawFailure:{false,true})for(bool water:{false,true})for(bool specialized:{false,true}){
        Device d;d.gpu.on=recording;d.cpuClockAvailable=frequency;d.extension.fail=targetFailure;d.failDraw=drawFailure;
        d.waterMask=water;d.aoBlurNoWaterPS=specialized?7:0;
        clocks=0;CpuScope::reads=0;assert(d.run()==(!targetFailure&&!drawFailure));
        const bool timed=recording&&frequency;
        assert(clocks==(timed?(targetFailure?2u:4u):0u)&&CpuScope::reads==clocks);
        assert(d.draws==(targetFailure?0u:1u)&&d.bindings==(targetFailure?0u:1u));
        const auto& timing=d.blurTiming;
        assert(timing.pending==(timed&&!targetFailure&&!drawFailure));assert(d.diagnosticAo==(!targetFailure&&!drawFailure));
        if(timing.pending){assert(timing.frame==9876&&timing.method==2&&timing.width==68&&timing.height==39);assert(timing.setup==1&&timing.submit==1);}
        const int shader=!water&&specialized?2007:2005;
        const std::vector<int> states=targetFailure?std::vector<int>{300}:drawFailure?std::vector<int>{300,1402,1400,shader}:std::vector<int>{300,1402,1400,shader,1002,1600};
        assert(d.extension.calls==states);
        const auto count=recording?(targetFailure?0u:drawFailure?1u:2u):0u;assert(d.gpu.labels.size()==count);
        if(count)assert(d.gpu.labels[0]=="AOBlurSetup");if(count==2)assert(d.gpu.labels[1]=="AOBlur");
    }
}
''')
exe = folder / 'actual-region.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
print('PASS actual AO instrumentation: 64 success/failure/water/shader gates, off=0 clocks, sampled=4 clocks, unchanged states and one draw')
ring = folder / 'query-ring.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-I' + str(ROOT / 'tests/support/gpu_profile'), *fp.test_include_flags(),
                str(ROOT / 'tests/test_gpu_profile.cpp'), '-o', str(ring)], env=fp.zig_env(), check=True)
subprocess.run([str(ring)], check=True)
