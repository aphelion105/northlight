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
start = source.index('            if(withReplays){', source.index('const int64_t candidatesStart='))
current = source[start:source.index('\n            if(pointTimed)', start)]
harness = r'''
#include "point_replay_casters.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using UINT=unsigned;
struct Bounds{float low[3]={},high[3]={};bool valid=true;};
struct Fixture{
    NorthlightPointShadow::ReplayCasters pointReplayCasters;
    struct Replay{Bounds pointBounds;bool shadowSkinned=false;};
    struct Work{unsigned unboundedSkinned=0,unboundedRigid=0;} pointWork;
    std::vector<std::unique_ptr<Replay>> replays;
    std::array<std::vector<size_t>,6> pointReplayCandidates;
    unsigned pointCulled=0,policy=0,pointOrderCalls=0;
    std::vector<size_t> pointReplayOrder;bool pointReplayOrderReady=false;
    void pointOrderReplays(){++pointOrderCalls;pointReplayOrderReady=policy!=0;
        pointReplayOrder.resize(replays.size()-(policy==2));
        for(size_t i=0;i<pointReplayOrder.size();++i)pointReplayOrder[i]=policy==1?replays.size()-i-1:i;}

    void pointAppendCandidates(std::array<std::vector<size_t>,6>& lists,size_t index,unsigned mask,unsigned faces){
        for(unsigned face=0;face<6;++face)if(faces&(1u<<face)){if(mask&(1u<<face))lists[face].push_back(index);else ++pointCulled;}}
    void add(bool valid,bool outside,bool skin=false){
        auto p=std::make_unique<Replay>();p->pointBounds.valid=valid;
        p->pointBounds.low[0]=p->pointBounds.high[0]=outside?10.f:0.f;
        p->shadowSkinned=skin;replays.push_back(std::move(p));}
    void newLoop(unsigned faces,bool withReplays,unsigned admitted=63){
        for(auto& list:pointReplayCandidates)list.clear();
        pointCulled=0;
        pointWork={};
        float matrices[6][16]={};
        for(unsigned face=0;face<6;++face){
            auto& m=matrices[face];
            m[0]=m[5]=m[10]=m[15]=1.f;
            m[12]=(admitted&(1u<<face))?0.f:4.f;
        }
        const NorthlightPointShadow::FaceClip clip(matrices);
        @NEW@
    }
};
int main(){
    unsigned cases=0;
    for(unsigned faces=1;faces<64;++faces)for(unsigned mask=0;mask<64;++mask)for(bool replay:{false,true})for(unsigned policy=0;policy<3;++policy){
        Fixture now;now.policy=policy;now.add(true,false);now.add(true,true);
        now.add(false,true);now.add(false,false,true);now.newLoop(faces,replay,mask);
        unsigned expectedCulled=0;
        for(unsigned face=0;face<6;++face){std::vector<size_t> expected;
            if(replay&&(faces&(1u<<face))){if(mask&(1u<<face))expected.push_back(0);else ++expectedCulled;
                ++expectedCulled;expected.push_back(2);expected.push_back(3);}
            if(policy==1)std::reverse(expected.begin(),expected.end());
            assert(now.pointReplayCandidates[face]==expected);
        }
        const unsigned unbounded=replay?unsigned(__builtin_popcount(faces)):0;
        assert(now.pointWork.unboundedRigid==unbounded&&now.pointWork.unboundedSkinned==unbounded);
        assert(now.pointOrderCalls==unsigned(replay));
        assert(now.pointCulled==expectedCulled);++cases;
    }
    Fixture disabled;disabled.policy=1;disabled.replays.push_back(nullptr);disabled.newLoop(63,false);assert(!disabled.pointOrderCalls);
    Fixture grouped;
    for(unsigned i=0;i<64;++i){
        grouped.add(true,false);
        auto& b=grouped.replays.back()->pointBounds;
        b.low[0]=b.high[0]=float(i/16)*10.f;
    }
    grouped.newLoop(63,true);
    for(const auto& face:grouped.pointReplayCandidates){
        assert(face.size()==16);
        for(size_t i=0;i<face.size();++i)assert(face[i]==i);
    }
    assert(grouped.pointCulled==48*6);
    grouped.newLoop(63,true,0);
    assert(grouped.pointCulled==64*6);
    grouped.pointReplayCasters.invalidate();
    for(auto& replay:grouped.replays)replay->pointBounds.low[0]=replay->pointBounds.high[0]=20.f;
    grouped.newLoop(63,true);
    assert(grouped.pointCulled==64*6);

    std::printf("POINT_REPLAY_ADMISSION_PASS cases=%u allGeometry=1 invalidBounds=failOpen actorOffNoReads=1 permutedOrder=1 staleOrderFallback=1\n",cases);
}
'''.replace('@NEW@', current)
out=fp.output_dir()/'point-replay-admission'
out.mkdir(parents=True,exist_ok=True)
cpp = out/'admission.cpp'
cpp.write_text(harness, encoding='utf-8', newline='\n')
exe = out/('admission.exe' if os.name == 'nt' else 'admission')
compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
subprocess.run([*compiler, '-std=c++17',
                '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(), str(cpp), '-o', str(exe)],
               env=fp.zig_env(), check=True)
print(subprocess.check_output([str(exe)], text=True), end='')
