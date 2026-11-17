# northlight-test: requires=zig,windows-exe
"""Production fog admission: retained targets, address reserve and failed cold allocation."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

world = fp.src('world_renderer.h').read_text()
first = world.index('    bool fogVolumeResources(')
method = world[first:world.index('    // 0.3.200', first)]
harness = r'''
#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
struct Fixture {
    struct Volume {
        void* texture[3]={};bool refused=false,fail=false;unsigned creates=0;
        bool resources(void*,unsigned,unsigned){++creates;if(fail){refused=true;return false;}texture[0]=this;return true;}
        void reset(){texture[0]=nullptr;refused=false;}
    } fogVolume;
    void* d=nullptr;bool memoryPressure=false;unsigned reads=0;
    NorthlightGeometryMemory::Sample memory{1024ull<<20,64ull<<20,true};
    NorthlightGeometryMemory::Sample geometryAdmission(NorthlightGeometryMemory::Budget){++reads;return memory;}
METHOD
};
int main(){
    Fixture f;assert(f.fogVolumeResources(3840,2160));assert(!f.reads&&f.fogVolume.creates==1);
    const auto identity=f.fogVolume.texture[0];f.memoryPressure=true;f.memory={0,0,false};
    for(unsigned frame=0;frame<10000;++frame)assert(f.fogVolumeResources(3840,2160)&&f.fogVolume.texture[0]==identity);
    assert(!f.reads&&f.fogVolume.creates==1);
    f.fogVolume.reset();assert(!f.fogVolumeResources(3840,2160)&&f.reads==1&&f.fogVolume.creates==1&&!f.fogVolume.refused);
    unsigned cases=0;
    for(bool valid:{false,true})for(uint64_t mib:{0ull,128ull,255ull,257ull,258ull,400ull,1024ull})for(uint64_t largest:{0ull,1ull<<20,16ull<<20}){
        f.fogVolume.reset();f.memory={mib<<20,largest,valid};const auto creates=f.fogVolume.creates;
        const bool accepted=f.fogVolumeResources(3840,2160);
        if(!valid||mib<258||largest<(1ull<<20)){assert(!accepted&&f.fogVolume.creates==creates);}
        else{assert(accepted&&f.fogVolume.creates==creates+1);}
        ++cases;
    }
    f.fogVolume.reset();f.memory={1024ull<<20,64ull<<20,true};f.fogVolume.fail=true;
    assert(!f.fogVolumeResources(3840,2160));const auto creates=f.fogVolume.creates,reads=f.reads;
    assert(!f.fogVolumeResources(3840,2160)&&f.fogVolume.creates==creates&&f.reads==reads);
    f.fogVolume.reset();f.fogVolume.fail=false;assert(f.fogVolumeResources(3840,2160));
    std::printf("FOG_ADMISSION_PASS retainedFrames=10000 newAllocations=0 pressureCases=%u reserveMiB=256 coldMetadataMiB=2 unknown=deny failure=fallback reset=recover\n",cases);
}
'''.replace('METHOD', method)
out = fp.output_dir() / 'fog-volume-admission'
out.mkdir(parents=True, exist_ok=True)
cpp, exe = out/'fixture.cpp', out/'fixture.exe'
cpp.write_text(harness)
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           *fp.test_include_flags(), str(cpp), '-o', str(exe)]
run = subprocess.run(command, env=fp.zig_env(), capture_output=True, text=True)
(out/'compile.log').write_text(run.stdout+run.stderr)
if run.returncode:
    print(run.stdout+run.stderr)
run.check_returncode()
run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
(out/'validation.log').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr)
run.check_returncode()
assert 'FOG_ADMISSION_PASS ' in run.stdout
