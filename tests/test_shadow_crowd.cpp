#include <atomic>
#include <chrono>
#include <cstdint>
struct CrowdClock {
    using duration=std::chrono::nanoseconds;
    using time_point=std::chrono::time_point<CrowdClock,duration>;
    static inline std::atomic<int64_t> ticks{0},step{0};
    static time_point now(){return time_point(duration(ticks.fetch_add(step.load())));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK CrowdClock
#include "replay_bounds_job.h"
#include "point_light_shadow.h"
#include <cassert>
#include <cstdio>

using namespace NorthlightReplayBounds;
using namespace NorthlightReplayBoundsJob;
using NorthlightDrawSnapshot::Mesh;

int main(){
    AdaptiveTimeBudget time;
    for(size_t count:{size_t(0),size_t(256),size_t(1222),size_t(4096),SIZE_MAX}){
        time.reset(count);
        assert(time.totalLimit>=AdaptiveTimeBudget::Total&&time.totalLimit<=AdaptiveTimeBudget::MaxTotal);
        assert(time.cheapLimit+AdaptiveTimeBudget::Heavy+AdaptiveTimeBudget::Build==time.totalLimit);
        time.finishReservedTurns(10000,20000);
        assert(time.cheapLimit==time.totalLimit-30000&&time.lent()==190000);
        const auto limit=time.cheapLimit;time.finishReservedTurns(0,0);assert(time.cheapLimit==limit);
    }
    time.reset();assert(time.cheapLimit==AdaptiveTimeBudget::Cheap);
    time.finishReservedTurns(UINT64_MAX,UINT64_MAX);assert(time.cheapLimit==0);

    NorthlightActorDeformation::Program program;program.major=3;program.positionRegister=0;program.inputs.push_back({0,0,0});
    NorthlightActorDeformation::Operation mov;mov.code=1;mov.destination=0x800f0000;mov.source[0].token=0x90e40000;
    NorthlightActorDeformation::Operation add;add.code=2;add.destination=0x80070000;add.source[0].token=0x80e40000;add.source[1].token=0xa0e40000;
    program.operations={mov,add};
    D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    auto mesh=std::make_shared<Mesh>();mesh->vertexCount=30;mesh->primitiveCount=10;mesh->indexed=false;mesh->topology=D3DPT_TRIANGLELIST;
    mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(30*12);
    for(unsigned i=0;i<30;++i){float p[]={float(i%3),float(i%2),2};std::memcpy(mesh->streams[0].bytes.data()+12*i,p,12);}
    auto prepared=EnvelopeCache::prepareProgram(program,decl,2);assert(prepared);
    constexpr size_t Count=1222;
    std::vector<std::array<float,1024>> constants(Count);
    std::vector<Item> input(Count);
    for(size_t i=0;i<Count;++i){constants[i][0]=i<10?0:100+float(i);input[i].owner=mesh;input[i].prepared=prepared;input[i].constants=constants[i].data();}
    float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const auto warm=[&](Cache& cache){
        CrowdClock::step=0;cache.beginFrame();Budget budget;Bounds b;WorkInfo work;
        cache.buildEnclosed(*prepared,*mesh,mesh,constants[0].data(),inverse,budget,b);
        cache.beginFrame();budget={};assert(cache.calculateCheap(*prepared,*mesh,mesh,constants[0].data(),inverse,budget,b,work)==Status::Valid);
    };
    Cache baseline,current,reference,async;warm(baseline);warm(current);warm(reference);warm(async);
    auto expected=input;reference.beginFrame();auto ref=schedule(reference,expected,inverse);assert(ref.valid==Count);
    // Charge enough per batched clock read to exhaust the fixed allowance.
    CrowdClock::step=5000;
    auto old=input;baseline.beginFrame();const auto before=schedule(baseline,old,inverse);
    auto updated=input;current.beginFrame(true,Count);const auto after=schedule(current,updated,inverse);
    assert(before.valid<Count&&after.valid==Count);
    assert(current.envelopeReadyMilliseconds()<4.1&&after.boundVertices==0);
    Worker worker;worker.items()=input;async.beginFrame(true,Count);assert(worker.start(async,inverse));const auto result=worker.finish();
    assert(!result.failed&&!result.cancelled&&result.totals.valid==Count);
    const auto same=[](const Bounds& a,const Bounds& b){return a.valid==b.valid&&
        !std::memcmp(a.low,b.low,sizeof a.low)&&!std::memcmp(a.high,b.high,sizeof a.high);};
    for(size_t i=0;i<Count;++i){
        assert(same(updated[i].bounds,expected[i].bounds));
        assert(same(worker.items()[i].bounds,expected[i].bounds));
    }
    // Same shared bounds feed all 18 cubes. Unknowns still draw on every face;
    // bounded off-light geometry must disappear without losing near casters.
    float matrices[6][16],center[3]={};
    for(unsigned f=0;f<6;++f)assert(NorthlightPointShadow::faceMatrix(center,.1f,12,f,matrices[f],256));
    const NorthlightPointShadow::FaceClip clip(matrices);
    const auto draws=[&](const auto& items){unsigned n=0;for(const auto& p:items){const auto& b=p.bounds;
        const unsigned mask=outsideSphere(b,center,12)?0:clip.mask(b.low,b.high,b.valid);
        for(unsigned f=0;f<6;++f)n+=bool(mask&(1u<<f));}return n*18;};
    const unsigned oldDraws=draws(old),newDraws=draws(updated);
    assert(newDraws==draws(expected)&&newDraws>0&&oldDraws>newDraws*50);
    // Fresh transforms/camera, unknown programs and expired input must never
    // inherit a previous frame's valid result.
    constants[Count-1][0]=0;inverse[12]=1;
    auto changed=input;changed[3].prepared.reset();changed[4].owner.reset();current.beginFrame(true,Count);
    const auto changedTotals=schedule(current,changed,inverse);assert(changedTotals.valid==Count-2);
    assert(!changed[3].bounds.valid&&!changed[4].bounds.valid&&clip.mask(changed[3].bounds.low,changed[3].bounds.high,false)==63);
    assert(changed.back().bounds.valid&&!outsideSphere(changed.back().bounds,center,12));
    std::atomic<bool> cancel{true};auto cancelled=input;current.beginFrame(true,Count);
    assert(schedule(current,cancelled,inverse,&cancel).valid==0);
    for(const auto& p:cancelled)assert(!p.bounds.valid);
    std::printf("SHADOW_CROWD_PASS packets=%zu baselineValid=%u valid=%u baselineDraws=%u draws=%u asyncExact=1 currentPose=1 failOpen=1\n",Count,before.valid,after.valid,oldDraws,newDraws);
}
