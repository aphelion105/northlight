#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute the production replay admission loop against its previous ordering."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('world_point_rendering.inl').read_text(encoding='utf-8')
start = source.index('            if(withReplays)for(size_t i=0;i<replays.size();++i)')
current = source[start:source.index('\n        }catch(...)', start)]
previous = r'''
if(withReplays)for(size_t i=0;i<replays.size();++i){const auto& bounds=replays[i]->pointBounds;
    {PointReplayRuns runs;const bool owned=!ownGlass.empty()?pointOwnShell(*replays[i],ownShell,runs):(nativeFixture&&pointOwnNativeShell(*replays[i],light,runs,nativeFixture));if(owned){
        if(runs.empty())continue;replayRuns.emplace(i,std::move(runs));}}
    const unsigned mask=NorthlightReplayBounds::outsideSphere(bounds,pointSlot().selected.position,pointSlot().selected.attenuationEnd)?0:
        clip.mask(bounds.low,bounds.high,bounds.valid);
    if(!bounds.valid)(replays[i]->shadowSkinned?pointWork.unboundedSkinned:pointWork.unboundedRigid)+=unsigned(__builtin_popcount(mask&faces));
    pointAppendCandidates(pointReplayCandidates,i,mask,faces);
}
'''
harness = r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
using UINT=unsigned;
struct Bounds{float low[3]={},high[3]={};bool valid=true,outside=false;};
namespace NorthlightReplayBounds{
bool outsideSphere(const Bounds& b,const float*,float){return b.valid&&b.outside;}
}
struct Fixture{
    struct Replay{Bounds pointBounds;bool shadowSkinned=false;unsigned owned=0;};
    struct Clip{unsigned mask(const float* low,const float* high,bool valid,unsigned requested=63)const{return (valid?(high[0]?0:unsigned(low[0])):63)&requested;}} clip;
    struct Light{float position[3]={};float attenuationEnd=1;} light;
    struct Slot{Light selected;} slot;
    struct Work{unsigned unboundedSkinned=0,unboundedRigid=0;} pointWork;
    struct Batch{} ownShell;
    using PointReplayRuns=std::vector<std::pair<UINT,UINT>>;
    std::vector<std::unique_ptr<Replay>> replays;
    std::vector<unsigned> ownGlass;
    std::array<std::vector<size_t>,6> pointReplayCandidates;
    std::map<size_t,PointReplayRuns> replayRuns;
    unsigned proofs=0,pointCulled=0;int fixture=0;const int* nativeFixture=&fixture;
    Slot& pointSlot(){return slot;}
    bool proof(const Replay& p,PointReplayRuns& out){++proofs;if(p.owned==2)out.push_back({3,7});return p.owned!=0;}
    bool pointOwnShell(const Replay& p,const Batch&,PointReplayRuns& out){return proof(p,out);}
    bool pointOwnNativeShell(const Replay& p,const Light&,PointReplayRuns& out,const int*){return proof(p,out);}
    void pointAppendCandidates(std::array<std::vector<size_t>,6>& lists,size_t index,unsigned mask,unsigned faces){
        for(unsigned face=0;face<6;++face)if(faces&(1u<<face)){if(mask&(1u<<face))lists[face].push_back(index);else ++pointCulled;}}
    void add(unsigned mask,bool valid,bool outside,unsigned owned,bool skin=false){
        auto p=std::make_unique<Replay>();p->pointBounds.low[0]=float(mask);p->pointBounds.valid=valid;
        p->pointBounds.outside=outside;p->pointBounds.high[0]=float(outside);p->owned=owned;p->shadowSkinned=skin;replays.push_back(std::move(p));}
    void oldLoop(unsigned faces,bool withReplays){@OLD@}
    void newLoop(unsigned faces,bool withReplays){@NEW@}
};
int main(){
    unsigned cases=0;unsigned long long oldProofs=0,newProofs=0;
    for(unsigned faces=1;faces<64;++faces)for(unsigned mask=0;mask<64;++mask)
    for(unsigned owner=0;owner<3;++owner)for(unsigned shell=0;shell<2;++shell)for(bool replay:{false,true}){
        Fixture old,now;
        for(auto* f:{&old,&now}){
            if(shell)f->ownGlass.push_back(0);
            f->add(mask,true,false,owner);f->add(63,true,true,owner);
            f->add(0,false,true,owner);f->add(0,false,false,0,true);f->add(mask^63,true,false,2);
        }
        old.oldLoop(faces,replay);now.newLoop(faces,replay);
        assert(old.pointReplayCandidates==now.pointReplayCandidates);
        assert(old.pointWork.unboundedRigid==now.pointWork.unboundedRigid);
        assert(old.pointWork.unboundedSkinned==now.pointWork.unboundedSkinned);
        for(const auto& list:old.pointReplayCandidates)for(size_t index:list){
            assert(old.replayRuns.count(index)==now.replayRuns.count(index));
            if(old.replayRuns.count(index))assert(old.replayRuns.at(index)==now.replayRuns.at(index));
        }
        assert(now.proofs<=old.proofs);
        if(replay)assert(now.proofs>=2); // Both invalid bounds still reach ownership/fail-open.
        oldProofs+=old.proofs;newProofs+=now.proofs;++cases;
    }
    assert(newProofs<oldProofs);
    std::printf("POINT_REPLAY_ADMISSION_PASS cases=%u candidatesAndSplits=exact invalidBounds=failOpen oldProofs=%llu newProofs=%llu\n",cases,oldProofs,newProofs);
}
'''.replace('@OLD@', previous).replace('@NEW@', current)
with tempfile.TemporaryDirectory(prefix='northlight-point-admission-') as folder:
    cpp = Path(folder)/'admission.cpp'
    cpp.write_text(harness, encoding='utf-8', newline='\n')
    exe = Path(folder)/('admission.exe' if os.name == 'nt' else 'admission')
    compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
    subprocess.run([*compiler, '-std=c++17',
                    '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    print(subprocess.check_output([str(exe)], text=True), end='')
