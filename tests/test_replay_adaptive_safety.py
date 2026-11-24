#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""Deterministic lending/fairness and immutable prepared-identity regressions."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

stub = literal(HERE / 'test_terrain_snapshot.py', 'stub')
fixture = literal(HERE / 'test_replay_skin_safety.py', 'harness').split('int main(')[0]
fixture = fixture.replace('static Status finish(', '[[maybe_unused]] static Status finish(')
harness = fixture + r'''
#include "replay_bounds_schedule.h"
#include <new>
using Stage=EnvelopeCache::Stage;
using Kind=EnvelopeCache::Kind;
using Info=EnvelopeCache::WorkInfo;
using Prepared=EnvelopeCache::Prepared;
static Budget cheapBudget(){Budget b;b.maxVertices=0;b.maxOperations=163840;return b;}
static Budget heavyBudget(){Budget b;b.maxVertices=0;b.maxOperations=32768;return b;}
static Budget buildBudget(){Budget b;b.maxVertices=2048;b.maxOperations=65536;return b;}
static Program rigid(){
    Program p;p.major=3;p.positionRegister=0;p.inputs.push_back({0,0,0});
    Operation op;op.code=1;op.destination=0x800f0000;op.source[0].token=0x90e40000;p.operations.push_back(op);
    op={};op.code=2;op.destination=0x80070000;op.source[0].token=0x80e40000;op.source[1].token=0xa0e4000a;p.operations.push_back(op);return p;
}
static size_t budgets(){
    const uint64_t values[]={0,1,69999,70000,149999,150000,200000,499999,500000,UINT64_MAX};size_t checked=0;
    for(uint64_t heavy:values)for(uint64_t build:values){AdaptiveTimeBudget b;assert(b.cheapLimit==280000&&!b.reservedClosed&&b.lent()==0);
        b.finishReservedTurns(heavy,build);const uint64_t expected=heavy>=500000||build>=500000-heavy?0:500000-heavy-build;
        assert(b.cheapLimit==expected&&b.reservedClosed&&b.lent()<=220000);
        if(heavy<=500000&&build<=500000-heavy)assert(b.cheapLimit+heavy+build==500000);else assert(!b.cheapLimit);
        const uint64_t once=b.cheapLimit;b.finishReservedTurns(0,0);assert(b.cheapLimit==once);b.finishReservedTurns(UINT64_MAX,UINT64_MAX);assert(b.cheapLimit==once);
        b.reset();assert(b.cheapLimit==280000&&!b.reservedClosed&&b.lent()==0);++checked;
    }
    AdaptiveTimeBudget normal,pointWave;
    normal.reset(1220);pointWave.reset(1220);
    normal.lendToBuild(500000,0);pointWave.lendToBuild(500000,0,true);
    assert(pointWave.totalLimit==AdaptiveTimeBudget::MaxTotal&&pointWave.cheapLimit==normal.cheapLimit);
    assert(pointWave.buildLimit-normal.buildLimit==pointWave.totalLimit-normal.totalLimit);
    pointWave.finishReservedTurns(0,pointWave.buildLimit);
    assert(pointWave.cheapLimit+pointWave.buildLimit==AdaptiveTimeBudget::MaxTotal);
    pointWave.reset(1220);assert(pointWave.totalLimit==normal.totalLimit&&pointWave.buildLimit==AdaptiveTimeBudget::Build);
    return checked+1;
}
// Token-limited fake phases exercise REAL production cursor semantics without
// depending on native CPU speed. Only deadline duration is replaced by tokens.
static bool scheduling(std::vector<char> kinds,unsigned firstQuota,unsigned extraQuota,bool persistExtra=false,bool growing=false){
    size_t cursor=0,hcursor=0,bcursor=0;std::vector<bool> serviced(kinds.size());
    for(unsigned frame=0;frame<120;++frame){
        if(growing&&frame<24&&frame%3==0){kinds.push_back(frame%2?'H':'B');serviced.push_back(false);}
        std::vector<char> hints(kinds.size(),'?');std::vector<bool> valid(kinds.size());AdaptiveTimeBudget time;unsigned available=firstQuota;
        auto classify=[&](size_t i){if(valid[i])return false;assert(available);--available;hints[i]=kinds[i];if(kinds[i]=='C'){valid[i]=true;serviced[i]=true;}return kinds[i]!='C';};
        const size_t next=NorthlightReplaySchedule::pass(kinds.size(),cursor,[&](){return available!=0;},classify);
        cursor=next;assert(!time.reservedClosed&&time.cheapLimit==280000);
        unsigned heavyVisits=0,buildVisits=0;
        hcursor=NorthlightReplaySchedule::pass(kinds.size(),hcursor,[&](){return heavyVisits<1;},[&](size_t i){if(hints[i]!='H')return false;++heavyVisits;serviced[i]=valid[i]=true;return false;});
        bcursor=NorthlightReplaySchedule::pass(kinds.size(),bcursor,[&](){return buildVisits<1;},[&](size_t i){if(hints[i]!='B')return false;++buildVisits;serviced[i]=true;kinds[i]='C';return false;});
        time.finishReservedTurns(heavyVisits*10000,buildVisits*20000);assert(time.reservedClosed);
        available=extraQuota;
        const size_t extraEnd=NorthlightReplaySchedule::pass(kinds.size(),next,[&](){return available!=0;},classify);
        if(persistExtra)cursor=extraEnd; // Deliberately bad control for regression proof.
    }
    return std::all_of(serviced.begin(),serviced.end(),[](bool v){return v;});
}
static void warm(EnvelopeCache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& mesh,const float* c,const float* inverse){
    for(unsigned frame=0;frame<10000;++frame){cache.beginFrame(false);auto budget=cheapBudget();Info info;Bounds bounds;
        cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,budget,bounds,Stage::Cheap,&info);
        if(info.kind==Kind::Cheap||info.kind==Kind::Heavy)return;
        assert(info.kind==Kind::Cold||info.kind==Kind::Unknown);auto build=buildBudget();
        cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,build,bounds,Stage::Build);assert(!bounds.valid);
    }assert(false);
}
static Bounds evaluate(EnvelopeCache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& mesh,const float* c,const float* inverse,bool diagnostics){
    for(unsigned attempt=0;attempt<100;++attempt){cache.beginFrame(diagnostics);auto budget=cheapBudget();Info info;Bounds bounds;
        auto s=cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,budget,bounds,Stage::Cheap,&info);
        if(s==Status::Valid)return bounds;
        if(info.kind==Kind::Heavy){auto heavy=heavyBudget();s=cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,heavy,bounds,Stage::Heavy,&info);if(s==Status::Valid)return bounds;}
        assert(s==Status::Budget);
    }assert(false);return {};
}
static void cold(EnvelopeCache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& mesh,const float* c,const float* inverse){
    for(Stage stage:{Stage::Cheap,Stage::Heavy}){cache.beginFrame(false);auto b=stage==Stage::Cheap?cheapBudget():heavyBudget();Info info;Bounds out;out.valid=true;const size_t entries=cache.entries();
        assert(cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,b,out,stage,&info)==Status::Budget);
        assert(info.kind==Kind::Cold&&!out.valid&&cache.entries()==entries&&b.vertices==0&&b.operations==0);
    }
}
int main(int argc,char** argv){
    assert(argc==2);const size_t budgetCases=budgets();
    assert(scheduling({'H','C','B'},2,1));assert(!scheduling({'H','C','B'},2,1,true));
    assert(scheduling(std::vector<char>(17,'H'),3,5));assert(scheduling(std::vector<char>(17,'B'),3,5));
    assert(scheduling({'H','C','B','H','B','C'},2,3,false,true));

    auto original=rigid(),source=original;D3DVERTEXELEMENT9 sourceDecl[4];std::memcpy(sourceDecl,declaration,sizeof declaration);
    auto prepared=EnvelopeCache::prepareProgram(source,sourceDecl,4);assert(prepared&&prepared->bytes()>sizeof(Prepared));
    auto duplicate=EnvelopeCache::prepareProgram(original,declaration,4);assert(duplicate&&duplicate!=prepared);
    auto changedDefinition=original;changedDefinition.definitions.push_back({10,{200,0,0,0}});
    auto definition=EnvelopeCache::prepareProgram(changedDefinition,declaration,4);assert(definition);
    auto changedDecl=std::vector<D3DVERTEXELEMENT9>(declaration,declaration+4);changedDecl[0].Offset=4;
    auto layout=EnvelopeCache::prepareProgram(original,changedDecl.data(),4);assert(layout);
    // Neither subsequent input-array reuse nor destruction mutates Prepared.
    source={};std::memset(sourceDecl,0xff,sizeof sourceDecl);changedDecl.clear();changedDecl.shrink_to_fit();
    float c[1024];palette(c,0);float inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;
    auto mesh=std::make_shared<const Mesh>(geometry(12));EnvelopeCache cache;warm(cache,*prepared,mesh,c,inverse);
    const size_t entryCount=cache.entries();auto a=evaluate(cache,*duplicate,mesh,c,inverse,false);assert(cache.entries()==entryCount);enclosed(original,*mesh,c,inverse,a);
    cold(cache,*definition,mesh,c,inverse);cold(cache,*layout,mesh,c,inverse);
    warm(cache,*definition,mesh,c,inverse);auto defBounds=evaluate(cache,*definition,mesh,c,inverse,false);enclosed(changedDefinition,*mesh,c,inverse,defBounds);assert(defBounds.low[0]>a.high[0]+190);
    auto owner2=std::make_shared<const Mesh>(*mesh);cold(cache,*prepared,owner2,c,inverse);
    c[40]=30;inverse[12]=400;auto moved=evaluate(cache,*prepared,mesh,c,inverse,false);enclosed(original,*mesh,c,inverse,moved);assert(moved.low[0]>a.high[0]+420);
    auto instrumented=evaluate(cache,*prepared,mesh,c,inverse,true);assert(!std::memcmp(moved.low,instrumented.low,12)&&!std::memcmp(moved.high,instrumented.high,12));
    c[40]=0;inverse[12]=0;

    // Lending is closed to donors and idempotent; the next frame restores them.
    cache.beginFrame(false);assert(cache.borrowedMicroseconds()==0&&cache.canHeavy()&&cache.canBuild());
    cache.finishReservedTurnsAndLend();assert(cache.borrowedMicroseconds()==220&&!cache.canHeavy()&&!cache.canBuild()&&cache.canReady());
    auto heavyBudgetNow=heavyBudget();Info info;Bounds out;out.valid=true;
    assert(cache.calculatePrepared(*prepared,*mesh,mesh,c,inverse,heavyBudgetNow,out,Stage::Heavy,&info)==Status::Budget&&!out.valid);
    auto buildNow=buildBudget();assert(cache.calculatePrepared(*prepared,*mesh,mesh,c,inverse,buildNow,out,Stage::Build,&info)==Status::Budget&&!out.valid);
    cache.finishReservedTurnsAndLend();assert(cache.borrowedMicroseconds()==220);
    cache.beginFrame(false);assert(cache.borrowedMicroseconds()==0&&cache.canHeavy()&&cache.canBuild());

    // Preparation misses consume the same bank, including saturating charges.
    cache.chargeCheapPreparation(280000);assert(!cache.canReady());
    cache.finishReservedTurnsAndLend();assert(cache.canReady()&&cache.borrowedMicroseconds()==220&&!cache.canHeavy()&&!cache.canBuild());
    cache.chargeCheapPreparation(219999);assert(cache.canReady());
    cache.chargeCheapPreparation(1);assert(!cache.canReady());
    cache.finishReservedTurnsAndLend();assert(!cache.canReady());
    cache.beginFrame(false);assert(cache.canReady()&&cache.canHeavy()&&cache.canBuild());
    cache.chargeCheapPreparation(UINT64_MAX);cache.chargeCheapPreparation(1);assert(!cache.canReady());
    cache.finishReservedTurnsAndLend();assert(!cache.canReady());
    cache.beginFrame(false);assert(cache.canReady()&&cache.borrowedMicroseconds()==0);

    // Real prepared-path four-pass sequence, with finite per-pass operation
    // quotas ensuring continuation executes meaningfully without a speed test.
    auto real=load(argv[1]);Operation op;op.code=1;op.destination=0x80070001;op.source[0].token=0x80e40001;real.operations.push_back(op);
    auto heavyPrepared=EnvelopeCache::prepareProgram(real,declaration,4);assert(heavyPrepared);auto heavyMesh=std::make_shared<const Mesh>(geometry(24));warm(cache,*heavyPrepared,heavyMesh,c,inverse);
    // Distinct immutable owners plus a fresh pose each frame keep this an
    // operation-budget continuation test even when exact result memoization
    // makes repeated identical packets free of deformation work.
    std::shared_ptr<const Mesh> cheapMeshes[4];
    for(auto& owner:cheapMeshes){owner=std::make_shared<const Mesh>(*mesh);warm(cache,*prepared,owner,c,inverse);}
    auto coldMesh=std::make_shared<const Mesh>(geometry(66));size_t extraValid=0,coldBuilt=0,heavyAttempts=0,cursor=0,hcursor=0,bcursor=0;
    for(unsigned frame=0;frame<80;++frame){c[40]=float(frame+1);cache.beginFrame(false);Info hints[6];Bounds bounds[6];auto cheap=cheapBudget(),heavy=heavyBudget(),build=buildBudget();cheap.maxOperations=4;
        auto run=[&](size_t i,Stage stage,Budget& b){const auto& identity=i==0?heavyPrepared:prepared;const auto& geometry=i==0?heavyMesh:i==5?coldMesh:cheapMeshes[i-1];
            return cache.calculatePrepared(*identity,*geometry,geometry,c,inverse,b,bounds[i],stage,stage==Stage::Build?nullptr:&hints[i])==Status::Budget;};
        const size_t next=NorthlightReplaySchedule::pass(6,cursor,[&](){return cache.canReady()&&cheap.operations<cheap.maxOperations;},[&](size_t i){return run(i,Stage::Cheap,cheap);});cursor=next;
        hcursor=NorthlightReplaySchedule::pass(6,hcursor,[&](){return cache.canHeavy()&&heavy.operations<heavy.maxOperations;},[&](size_t i){return hints[i].kind==Kind::Heavy&&!bounds[i].valid?run(i,Stage::Heavy,heavy):false;});
        bcursor=NorthlightReplaySchedule::pass(6,bcursor,[&](){return cache.canBuild()&&build.vertices<build.maxVertices&&build.operations<build.maxOperations;},[&](size_t i){return hints[i].kind==Kind::Cold&&!bounds[i].valid?run(i,Stage::Build,build):false;});
        heavyAttempts+=cache.stats().heavyAttempts;coldBuilt+=cache.stats().buildVertices;cache.finishReservedTurnsAndLend();assert(!cache.canHeavy()&&!cache.canBuild());
        cheap.maxOperations=262144-heavy.operations-build.operations;
        NorthlightReplaySchedule::pass(6,next,[&](){return cache.canReady()&&cheap.operations<cheap.maxOperations;},[&](size_t i){if(bounds[i].valid||hints[i].kind==Kind::Heavy||hints[i].kind==Kind::Cold||hints[i].kind==Kind::Unsupported)return false;
            const bool pending=run(i,Stage::Cheap,cheap);if(bounds[i].valid)++extraValid;return pending;});
        assert(cheap.operations+heavy.operations+build.operations<=262144);
        for(size_t i=0;i<6;++i)if(bounds[i].valid)enclosed(i==0?real:original,i==0?*heavyMesh:i==5?*coldMesh:*cheapMeshes[i-1],c,inverse,bounds[i]);
    }
    assert(extraValid>0&&coldBuilt==66&&heavyAttempts>0);

    cache.clear();cold(cache,*prepared,mesh,c,inverse);warm(cache,*prepared,mesh,c,inverse);
    alignas(Mesh) unsigned char storage[sizeof(Mesh)];auto create=[&](){return std::shared_ptr<const Mesh>(new(storage) Mesh(geometry(12)),[](const Mesh* p){const_cast<Mesh*>(p)->~Mesh();});};
    auto first=create();const auto* address=first.get();warm(cache,*prepared,first,c,inverse);std::weak_ptr<const Mesh> weak=first;first.reset();assert(weak.expired());auto replacement=create();assert(replacement.get()==address);cold(cache,*prepared,replacement,c,inverse);replacement.reset();
    EnvelopeCache eviction;warm(eviction,*prepared,mesh,c,inverse);std::vector<std::shared_ptr<const Mesh>> retained;
    for(unsigned n=0;n<4300;++n){retained.push_back(std::make_shared<const Mesh>(geometry(3)));warm(eviction,*prepared,retained.back(),c,inverse);assert(eviction.entries()<=4096&&eviction.reservedBytes()<=8u*1024u*1024u);}
    cold(eviction,*prepared,mesh,c,inverse);assert(mesh.use_count()==1);
    std::printf("PASS adaptive: %zu deterministic budget/overflow/reset/idempotence cases; mixed/allheavy/allcold/growing queue fairness and badcursor regression; %zu prepared continuation results, heavy attempts%zu, cold vertices%zu. Immutable program+decl copies, exactDEF/layout/newowner/reset/ABA/eviction, currentpose/camera and diagnosticparity validated.\n",budgetCases,extraValid,heavyAttempts,coldBuilt);
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-adaptive-safety-') as tmp:
    root = Path(tmp)
    (root / 'd3d9.h').write_text(stub)
    (root / 'test.cpp').write_text(harness)
    for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']):
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(root), *fp.test_include_flags(), str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test'), str(client_fixtures.four_bone_vs3())], check=True)
