#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Check production replay admission against an independent per-face oracle."""
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('world_point_rendering.inl').read_text(encoding='utf-8')
start = source.index('            if(withReplays)for(size_t i=0;i<replays.size();++i)')
current = source[start:source.index('\n            if(pointTimed)', start)]
harness = r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using UINT=unsigned;
struct Bounds{float low[3]={},high[3]={};bool valid=true;};
struct Fixture{
    struct Replay{Bounds pointBounds;bool shadowSkinned=false;};
    struct Clip{unsigned mask(const float* low,const float* high,bool valid,unsigned requested=63)const{return (valid?(high[0]?0:unsigned(low[0])):63)&requested;}} clip;
    struct Work{unsigned unboundedSkinned=0,unboundedRigid=0;} pointWork;
    std::vector<std::unique_ptr<Replay>> replays;
    std::array<std::vector<size_t>,6> pointReplayCandidates;
    unsigned pointCulled=0;
    void pointAppendCandidates(std::array<std::vector<size_t>,6>& lists,size_t index,unsigned mask,unsigned faces){
        for(unsigned face=0;face<6;++face)if(faces&(1u<<face)){if(mask&(1u<<face))lists[face].push_back(index);else ++pointCulled;}}
    void add(unsigned mask,bool valid,bool outside,bool skin=false){
        auto p=std::make_unique<Replay>();p->pointBounds.low[0]=float(mask);p->pointBounds.valid=valid;
        p->pointBounds.high[0]=float(outside);p->shadowSkinned=skin;replays.push_back(std::move(p));}
    void newLoop(unsigned faces,bool withReplays){@NEW@}
};
int main(){
    unsigned cases=0;
    for(unsigned faces=1;faces<64;++faces)for(unsigned mask=0;mask<64;++mask)for(bool replay:{false,true}){
        Fixture now;now.add(mask,true,false);now.add(63,true,true);
        now.add(0,false,true);now.add(0,false,false,true);now.newLoop(faces,replay);
        unsigned expectedCulled=0;
        for(unsigned face=0;face<6;++face){std::vector<size_t> expected;
            if(replay&&(faces&(1u<<face))){if(mask&(1u<<face))expected.push_back(0);else ++expectedCulled;
                ++expectedCulled;expected.push_back(2);expected.push_back(3);}
            assert(now.pointReplayCandidates[face]==expected);
        }
        const unsigned unbounded=replay?unsigned(__builtin_popcount(faces)):0;
        assert(now.pointWork.unboundedRigid==unbounded&&now.pointWork.unboundedSkinned==unbounded);
        assert(now.pointCulled==expectedCulled);++cases;
    }
    Fixture disabled;disabled.replays.push_back(nullptr);disabled.newLoop(63,false);
    std::printf("POINT_REPLAY_ADMISSION_PASS cases=%u allGeometry=1 invalidBounds=failOpen actorOffNoReads=1\n",cases);
}
'''.replace('@NEW@', current)
out=fp.output_dir()/'point-replay-admission'
out.mkdir(parents=True,exist_ok=True)
cpp = out/'admission.cpp'
cpp.write_text(harness, encoding='utf-8', newline='\n')
exe = out/('admission.exe' if os.name == 'nt' else 'admission')
compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
subprocess.run([*compiler, '-std=c++17',
                '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
               env=fp.zig_env(), check=True)
print(subprocess.check_output([str(exe)], text=True), end='')
