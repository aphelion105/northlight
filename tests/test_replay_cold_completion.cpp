// Cold sources must not replay into every light while a finished envelope waits a frame.
#undef NDEBUG
#include <atomic>
#include <chrono>
#ifdef NORTHLIGHT_TEST_CLOCK
struct TestClock {
    using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<TestClock,duration>;
    static constexpr bool is_steady=true;static inline std::int64_t ticks=0,step=1;
    static time_point now(){ticks+=step;return time_point(duration(ticks));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK TestClock
#endif
#include "replay_bounds_job.h"
#include "point_light_shadow.h"
#include "reference_schedule.inc"
#include "terminal_reference.inc"
#include "previous_terminal.inc"
#include "actor_fixture.inc"
#include <cstdio>
#include <cassert>
#include <limits>
using namespace NorthlightReplayBounds;
using Mesh=NorthlightDrawSnapshot::Mesh;
static D3DVERTEXELEMENT9 decl[]={{0,4,2,0,0,0},{0,16,5,0,2,0},{0xff,0,17,0,0,0}};
static Program rigid(){Program p;p.major=3;p.positionRegister=0;p.inputs={{0,0,0}};for(unsigned row=0;row<3;++row){Operation o{};o.code=9;o.destination=0x80000000u|(1u<<(16+row));o.source[0].token=0xa0e40006u+row;o.source[1].token=0x90e40000;p.operations.push_back(o);}Operation w{};w.code=1;w.destination=0x80080000;w.source[0].token=0x90ff0000;p.operations.push_back(w);buildKernel(p);return p;}
static std::shared_ptr<const Mesh> mesh(unsigned n,float x=0,bool indexed=false){
    auto m=std::make_shared<Mesh>();m->vertexCount=n;m->primitiveCount=n/3;m->indexed=indexed;m->streams[0].stride=24;m->streams[0].bytes.resize(size_t(n)*24);
    for(unsigned i=0;i<n;++i){float v[]={x+std::sin(float(i)*.13f),std::cos(float(i)*.07f),float(int(i%11)-5)*.1f};std::memcpy(m->streams[0].bytes.data()+i*24+4,v,12);m->streams[0].bytes[i*24+16]=0;}
    if(indexed){m->indices.resize(size_t(m->primitiveCount)*3);for(size_t i=0;i<m->indices.size();++i)m->indices[i]=unsigned(i%n);}return m;
}
[[maybe_unused]] static bool same(const Bounds& a,const Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
[[maybe_unused]] static void enclosed(const Program& p,const std::shared_ptr<const Mesh>& m,const float* c,const float* inverse,const Bounds& b){
    std::vector<Position> points;assert(b.valid&&worldPositions(p,*m,decl,3,c,inverse,points));
    for(const auto& v:points){const float at[]={v.x,v.y,v.z};for(unsigned k=0;k<3;++k)assert(at[k]>=b.low[k]&&at[k]<=b.high[k]);}
}
[[maybe_unused]] static Status finish(EnvelopeCache& cache,const Program& p,const std::shared_ptr<const Mesh>& m,const D3DVERTEXELEMENT9* d,const float* c,const float* inverse,Bounds& b){
    for(unsigned f=0;f<10000;++f){cache.beginFrame(false);Budget work;work.maxVertices=37;const auto s=cache.calculate(p,*m,m,d,3,c,inverse,work,b);assert(work.vertices<=37&&work.operations<=work.maxOperations);if(s!=Status::Budget)return s;}
    assert(false);return Status::Budget;
}
#ifdef NORTHLIGHT_TEST_CLOCK
int main(){
    float c[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i){inverse[i*5]=1;c[4*(6+i)+i]=1;}for(unsigned b=0;b<75;++b){c[4*(31+3*b)]=1;c[4*(32+3*b)+1]=1;c[4*(33+3*b)+2]=1;}
    const auto p=rigid();auto generic=p;generic.skinned=true; // disables only the rigid source scanner; same interval program.
    unsigned exact=0;
    for(bool indexed:{false,true})for(unsigned n:{3,33,129,2051}){auto m=mesh(n,0,indexed);EnvelopeCache a,b;Bounds x,y;
        for(unsigned frame=0;frame<12;++frame){const float angle=float(frame)*.71f;inverse[0]=inverse[5]=std::cos(angle);inverse[1]=std::sin(angle);inverse[4]=-inverse[1];inverse[12]=10000+float(frame);inverse[13]=-900;c[6*4]=std::cos(angle);c[6*4+1]=std::sin(angle);c[7*4]=-std::sin(angle);c[7*4+1]=std::cos(angle);c[6*4+3]=float(frame)*3;
            assert(finish(a,p,m,decl,c,inverse,x)==Status::Valid);assert(finish(b,generic,m,decl,c,inverse,y)==Status::Valid);assert(same(x,y));enclosed(p,m,c,inverse,x);++exact;}
        auto changed=std::make_shared<Mesh>(*m);const float far=500;std::memcpy(changed->streams[0].bytes.data()+4,&far,4);assert(finish(a,p,changed,decl,c,inverse,x)==Status::Valid);enclosed(p,changed,c,inverse,x);
    }
    // A completed cheap envelope gets this frame's current camera/pose, including indexed sources and a real single-bone program.
    for(unsigned i=0;i<16;++i){inverse[i]=i%5==0?1.f:0.f;c[24+i]=inverse[i];}
    Program skin;const auto words=code(3);assert(compile(words.data(),words.size(),skin));
    for(const auto& program:{p,skin}){
        auto prepared=EnvelopeCache::prepareProgram(program,decl,3);assert(prepared);
        std::vector<NorthlightReplayBoundsJob::Item> oldItems(193),items(193);Cache oldCache,cache;
        for(unsigned i=0;i<items.size();++i){auto m=mesh(3,i?1000+float(i):0,i%2);items[i].owner=oldItems[i].owner=m;items[i].prepared=oldItems[i].prepared=prepared;items[i].constants=oldItems[i].constants=c;}
        oldCache.beginFrame(false,items.size());cache.beginFrame(false,items.size());
        const auto old=NorthlightReplayBoundsJob::referenceSchedule(oldCache,oldItems,inverse);const auto now=NorthlightReplayBoundsJob::schedule(cache,items,inverse);
        assert(old.valid==0&&now.valid==items.size());assert(now.boundVertices==old.boundVertices&&now.boundVertices==193*3);
        unsigned oldFaces=0,newFaces=0;for(size_t i=0;i<items.size();++i){assert(!oldItems[i].bounds.valid);enclosed(program,items[i].owner,c,inverse,items[i].bounds);
            const float center[3]={};oldFaces+=9*6;newFaces+=outsideSphere(items[i].bounds,center,12)?0:9*6;}
        assert(oldFaces==10422&&newFaces==54);std::printf("COLD_COMPLETION_PASS skinned=%u packets=193 sourceVertices=%zu beforeFacePackets=%u afterFacePackets=%u unchangedBudgets=1\n",unsigned(program.skinned),now.boundVertices,oldFaces,newFaces);
    }
    auto prepared=EnvelopeCache::prepareProgram(p,decl,3);
    // An unfinished source fails fastReady, but its exact lookup is still
    // reusable by the cold/material fallback. Neither path builds twice.
    for(auto stage:{EnvelopeCache::Stage::Cheap,EnvelopeCache::Stage::All}){
        auto owned=std::make_shared<Mesh>(*mesh(129));owned->sourceIdentity=NorthlightSourceIdentity::make({91,129});
        std::shared_ptr<const Mesh> source=owned;EnvelopeCache partial;Bounds b;
        partial.beginFrame(false);Budget seed;seed.maxVertices=1;
        assert(partial.calculatePrepared(*prepared,*source,source,c,inverse,seed,b)==Status::Budget&&!b.valid&&seed.vertices==1);
        partial.beginFrame(false);Budget next;next.maxVertices=stage==EnvelopeCache::Stage::All?1:0;WorkInfo info;
        assert(partial.calculatePrepared(*prepared,*source,source,c,inverse,next,b,stage,&info)==Status::Budget&&!b.valid);
        assert(partial.stats().sourceIdentityHits==1&&next.vertices==(stage==EnvelopeCache::Stage::All?1u:0u));
        assert(finish(partial,p,source,decl,c,inverse,b)==Status::Valid);enclosed(p,source,c,inverse,b);
        partial.beginFrame(false);Budget warm;warm.maxVertices=0;
        assert(partial.calculatePrepared(*prepared,*source,source,c,inverse,warm,b,stage)==Status::Valid);
        assert(partial.stats().sourceIdentityHits==1&&!warm.vertices);
    }
    std::puts("COLD_LOOKUP_PASS cheapAndMaterialFallback=one_exact_lookup partialBuild=preserved warmBounds=valid");
    // A cold wave exceeds 65,536 scalar index checks without large vertex
    // work. Every index is still validated; vector predicates fit the same cap.
    {
        std::vector<NorthlightReplayBoundsJob::Item> indexed(128);Cache coldIndices;
        for(unsigned i=0;i<indexed.size();++i){auto m=std::make_shared<Mesh>(*mesh(3,i?1000+float(i):0));
            m->indexed=true;m->primitiveCount=511;m->indices.resize(1533);
            for(unsigned j=0;j<m->indices.size();++j)m->indices[j]=j%3;
            indexed[i].owner=m;indexed[i].prepared=prepared;indexed[i].constants=c;}
        coldIndices.beginFrame(false,indexed.size());const auto result=NorthlightReplayBoundsJob::schedule(coldIndices,indexed,inverse);
#if defined(__SSE2__) && !defined(NORTHLIGHT_REPLAY_BOUNDS_SCALAR_INDICES)
        assert(result.valid==indexed.size()&&coldIndices.envelopeStats().buildIndices==128*1533);
        assert(!coldIndices.envelopeStats().operationDeferred&&!coldIndices.envelopeStats().timeDeferred);
#else
        assert(result.valid<indexed.size()&&coldIndices.envelopeStats().operationDeferred);
#endif
        float matrices[6][16];const float center[3]={};for(unsigned f=0;f<6;++f)assert(NorthlightPointShadow::faceMatrix(center,.1f,12,f,matrices[f],64));
        unsigned faces=0;
        for(auto& item:indexed){EnvelopeCache reference;Bounds expected;
            assert(finish(reference,p,item.owner,decl,c,inverse,expected)==Status::Valid);
            if(item.bounds.valid){assert(same(item.bounds,expected));enclosed(p,item.owner,c,inverse,item.bounds);
                assert(NorthlightPointShadow::faceMask(item.bounds.low,item.bounds.high,true,matrices)==NorthlightPointShadow::faceMask(expected.low,expected.high,true,matrices));}
            faces+=outsideSphere(item.bounds,center,12)?0:unsigned(__builtin_popcount(NorthlightPointShadow::faceMask(item.bounds.low,item.bounds.high,item.bounds.valid,matrices)));
        }
        assert(result.boundOperations<=262144);
        std::printf("COLD_INDEX_WAVE_PASS packets=128 indices=196224 valid=%u buildIndices=%zu operations=%zu facePackets=%u noQuotaIncrease=1\n",result.valid,coldIndices.envelopeStats().buildIndices,result.boundOperations,faces);
        // Corruption in every SIMD lane, tail and chunk boundary fails closed
        // for bounds (so shadow culling remains fail-open), including high bits.
        for(unsigned at:{0u,1u,2u,3u,63u,64u,1532u})for(unsigned bad:{3u,0x80000000u,UINT_MAX}){
            auto changed=std::make_shared<Mesh>(*indexed[0].owner);changed->indices[at]=bad;EnvelopeCache invalid;Bounds b;
            assert(finish(invalid,p,changed,decl,c,inverse,b)==Status::Invalid&&!b.valid);
        }
        std::puts("COLD_INDEX_NEGATIVE_PASS vectorLanes=4 chunkBoundary=1 tail=1 highBits=1 changedCopy=1");
    }
    // A camera turn exposes larger rigid sources too. The clock/operation
    // budgets are unchanged; unused time must not be blocked by the old quota.
    std::vector<NorthlightReplayBoundsJob::Item> wave(24);Cache cold;
    for(unsigned i=0;i<wave.size();++i){wave[i].owner=mesh(513,i?1000+float(i):0);wave[i].prepared=prepared;wave[i].constants=c;}
    cold.beginFrame(false,wave.size());const auto waveTotals=NorthlightReplayBoundsJob::schedule(cold,wave,inverse);
    assert(waveTotals.valid==wave.size()&&waveTotals.boundVertices==24*513);
    for(const auto& item:wave)enclosed(p,item.owner,c,inverse,item.bounds);
    // A cold turn in a smaller scene needs the same unused-time lending as a
    // crowd. The old 256-packet gate stranded 37 sources at 16,384 vertices.
    {
        Cache mixed;std::vector<NorthlightReplayBoundsJob::Item> items(234);
        for(unsigned i=0;i<items.size();++i){auto& item=items[i];item.owner=mesh(i<166?3:513,i?1000+float(i):0);item.prepared=prepared;item.constants=c;
            if(i<166){Bounds b;for(unsigned attempt=0;attempt<8&&!b.valid;++attempt){mixed.beginFrame(false,items.size());Budget work;
                mixed.calculatePreparedPhase(*prepared,*item.owner,item.owner,c,inverse,work,b,EnvelopeCache::Stage::All);}assert(b.valid);}}
        mixed.beginFrame(false,items.size());const auto result=NorthlightReplayBoundsJob::schedule(mixed,items,inverse);
        unsigned faces=0;const float center[3]={};
        for(const auto& item:items){if(item.bounds.valid)enclosed(p,item.owner,c,inverse,item.bounds);faces+=outsideSphere(item.bounds,center,12)?0:42;}
        assert(result.valid>200&&faces<1000&&items[0].bounds.valid&&!outsideSphere(items[0].bounds,center,12));
        assert(result.boundOperations<=262144);
        assert(mixed.phaseNanoseconds(0)+mixed.phaseNanoseconds(1)+mixed.phaseNanoseconds(2)<=AdaptiveTimeBudget::Total);
        std::printf("BASE_BUILD_LENDING_PASS packets=234 warm=166 valid=%u facePackets=%u vertices=%zu totalLimitNs=500000 unchangedCoverage=1\n",result.valid,faces,result.boundVertices);
    }
    // Terminal work preserves every accepted bound and can now publish further
    // current-pose bounds after immutable sources finish.
    for(unsigned count:{1u,816u,969u}){
        Cache control,terminal;std::vector<NorthlightReplayBoundsJob::Item> a(count),b(count);
        auto warm=mesh(3,1000),large=mesh(count==1?12000:32766,2000);
        for(auto* cache:{&control,&terminal}){Bounds ready;cache->beginFrame(false,count);Budget seed;assert(cache->calculatePreparedPhase(*prepared,*warm,warm,c,inverse,seed,ready,EnvelopeCache::Stage::All)==Status::Valid);}
        for(unsigned i=0;i<count;++i){a[i].owner=b[i].owner=i+32<count?warm:large;a[i].prepared=b[i].prepared=prepared;a[i].constants=b[i].constants=c;}
        TestClock::step=1000;control.beginFrame(false,count);terminal.beginFrame(false,count);
        const auto old=NorthlightReplayBoundsJob::beforeTerminalSchedule(control,a,inverse),now=NorthlightReplayBoundsJob::schedule(terminal,b,inverse);
        assert(now.valid>=old.valid&&terminal.envelopeStats().terminalBuildVertices>0&&now.boundVertices>old.boundVertices);
        for(unsigned i=0;i<count;++i){if(a[i].bounds.valid)assert(same(a[i].bounds,b[i].bounds));if(b[i].bounds.valid)enclosed(p,b[i].owner,c,inverse,b[i].bounds);}
        const auto total=AdaptiveTimeBudget::Total+std::min<size_t>(count>256?count-256:0,(AdaptiveTimeBudget::MaxTotal-AdaptiveTimeBudget::Total)/2000)*2000;
        assert(terminal.phaseNanoseconds(0)+terminal.phaseNanoseconds(1)+terminal.phaseNanoseconds(2)<=total+10000);
        assert(now.boundOperations<=262144&&!terminal.canBuild()&&!terminal.canHeavy()&&!terminal.canCheap());
        const auto tailVertices=terminal.envelopeStats().terminalBuildVertices;
        Cache workerCache;workerCache.beginFrame(false,count);Budget seed;Bounds warmBounds;
        assert(workerCache.calculatePreparedPhase(*prepared,*warm,warm,c,inverse,seed,warmBounds,EnvelopeCache::Stage::All)==Status::Valid);
        workerCache.beginFrame(false,count);NorthlightReplayBoundsJob::Worker worker;worker.items()=b;
        for(auto& item:worker.items()){item.bounds={};item.work={};}
        assert(worker.start(workerCache,inverse));const auto result=worker.finish();
        assert(!result.failed&&!result.cancelled&&result.totals.valid==now.valid&&result.totals.boundVertices==now.boundVertices&&result.totals.boundOperations==now.boundOperations);
        for(unsigned i=0;i<count;++i)assert(same(worker.items()[i].bounds,b[i].bounds));
        for(auto* items:{&a,&b})for(auto& item:*items){item.bounds={};item.work={};}
        control.beginFrame(false,count);terminal.beginFrame(false,count);
        const auto nextOld=NorthlightReplayBoundsJob::beforeTerminalSchedule(control,a,inverse),next=NorthlightReplayBoundsJob::schedule(terminal,b,inverse);
        assert(next.valid==count&&nextOld.valid==count&&!next.boundVertices&&nextOld.boundVertices>0);
        for(unsigned i=0;i<count;++i){assert(same(a[i].bounds,b[i].bounds));enclosed(p,b[i].owner,c,inverse,b[i].bounds);}
        std::printf("TERMINAL_SOURCE_PASS packets=%u tailVertices=%zu oldNextBuild=%zu nextBuild=%zu firstFrameBounds=unchanged nextFrameBounds=exact totalLimitNs=%zu\n",count,tailVertices,nextOld.boundVertices,next.boundVertices,size_t(total));
        TestClock::step=1;
    }
    unsigned freshGains=0;
    for(unsigned tick:{1u,10u,50u,100u,250u,1000u}){
        constexpr unsigned count=900;Cache control,terminal;std::vector<NorthlightReplayBoundsJob::Item> a(count),b(count);
        for(unsigned i=0;i<count;++i){a[i].owner=b[i].owner=mesh(129,1000.f+float(i));a[i].prepared=b[i].prepared=prepared;a[i].constants=b[i].constants=c;}
        TestClock::step=tick;control.beginFrame(false,count);terminal.beginFrame(false,count);
        const auto old=NorthlightReplayBoundsJob::previousTerminalSchedule(control,a,inverse),now=NorthlightReplayBoundsJob::schedule(terminal,b,inverse);
        const auto limit=AdaptiveTimeBudget::Total+(count-256)*2000;
        assert(now.valid>=old.valid&&now.boundOperations<=262144);
        assert(terminal.phaseNanoseconds(0)+terminal.phaseNanoseconds(1)+terminal.phaseNanoseconds(2)<=limit+16*tick);
        assert(!terminal.canHeavy()&&!terminal.canBuild()&&!terminal.canCheap());
        assert(control.readyStart(count)==terminal.readyStart(count)&&control.buildStart(count)==terminal.buildStart(count)&&control.heavyStart(count)==terminal.heavyStart(count));
        for(unsigned i=0;i<count;++i){if(a[i].bounds.valid)assert(same(a[i].bounds,b[i].bounds));if(b[i].bounds.valid)enclosed(p,b[i].owner,c,inverse,b[i].bounds);}
        freshGains+=now.valid-old.valid;std::printf("TERMINAL_FRESH_CASE tick=%u before=%u after=%u vertices=%zu ops=%zu\n",tick,old.valid,now.valid,now.boundVertices,now.boundOperations);
    }
    assert(freshGains>1);TestClock::step=1;std::printf("TERMINAL_FRESH_PASS extraValid=%u currentPose=exact rotatingCursors=unchanged caps=unchanged\n",freshGains);
    {Cache warm;auto source=mesh(3);Bounds bound;warm.beginFrame(false,900);Budget seed;
        assert(warm.calculatePreparedPhase(*prepared,*source,source,c,inverse,seed,bound,EnvelopeCache::Stage::All)==Status::Valid);
        std::vector<NorthlightReplayBoundsJob::Item> items(900);for(auto& item:items){item.owner=source;item.prepared=prepared;item.constants=c;}
        warm.beginFrame(false,900);const auto result=NorthlightReplayBoundsJob::schedule(warm,items,inverse);
        assert(result.valid==900&&!result.buildVisited&&warm.canCheap()&&!warm.envelopeStats().terminalBuildVertices);
        std::puts("TERMINAL_WARM_PASS packets=900 extraColdWalk=skipped readyAllowance=retained");}
    // No tail may evade exhausted budgets, use an expired
    // owner or continue after cancellation. Empty/warm tails read no clock.
    for(unsigned why=0;why<5;++why){EnvelopeCache cache;auto source=mesh(12000);Bounds b;Budget seed;seed.maxVertices=1;cache.beginFrame(false);
        assert(cache.calculate(p,*source,source,decl,3,c,inverse,seed,b)==Status::Budget&&!b.valid&&cache.hasPendingBuild());
        Budget tail;tail.maxVertices=16384;const auto before=cache.pendingBuildVertices();
        if(why!=4)cache.finishReservedTurnsAndLend();
        if(why==0)tail.maxVertices=0;if(why==1)tail.maxOperations=0;if(why==2)cache.chargeCheapPreparation(UINT64_MAX);if(why==3)source.reset();
        cache.continuePendingAfterReady(tail);assert(!tail.vertices&&!cache.stats().terminalBuildVertices);
        if(why==3)assert(!cache.hasPendingBuild()&&cache.stats().pendingOwnerExpired==1);else assert(cache.pendingBuildVertices()==before);
    }
    {EnvelopeCache empty;empty.beginFrame(false);empty.finishReservedTurnsAndLend();Budget tail;const auto ticks=TestClock::ticks;empty.continuePendingAfterReady(tail);assert(TestClock::ticks==ticks);}
    {Cache cache;auto source=mesh(12000);Bounds b;Budget seed;seed.maxVertices=1;cache.beginFrame(false);assert(cache.calculatePreparedPhase(*prepared,*source,source,c,inverse,seed,b,EnvelopeCache::Stage::All)==Status::Budget);
        const auto before=cache.pendingBuildVertices();cache.beginFrame(false);std::vector<NorthlightReplayBoundsJob::Item> items(1);items[0].owner=source;items[0].prepared=prepared;items[0].constants=c;std::atomic<bool> cancel{true};
        const auto result=NorthlightReplayBoundsJob::schedule(cache,items,inverse,&cancel);assert(!result.valid&&!result.boundVertices&&cache.pendingBuildVertices()==before&&!cache.envelopeStats().terminalBuildVertices);}
    std::puts("TERMINAL_GUARDS_PASS exhaustedVertexAndOperationCaps=1 saturatedTime=1 expiredOwner=1 closedTurns=1 noPendingClock=0 cancelled=1");
    // Retain three camera views within the existing byte cap. Returning to
    // either view must not rebuild envelopes evicted only by the entry count.
    EnvelopeCache rotation;std::vector<std::shared_ptr<const Mesh>> sources;
    for(unsigned i=0;i<2700;++i){sources.push_back(mesh(3,float(i)));Bounds b;assert(finish(rotation,p,sources.back(),decl,c,inverse,b)==Status::Valid);}
    assert(rotation.entries()==2700&&rotation.reservedBytes()<=8u*1024u*1024u);
    for(const auto& source:sources){rotation.beginFrame(false);Budget work;work.maxVertices=0;Bounds b;
        assert(rotation.calculate(p,*source,source,decl,3,c,inverse,work,b)==Status::Valid&&b.valid&&!work.vertices&&!rotation.stats().evictions);}
    std::printf("COLD_ROTATION_PASS sourceVertices=%zu valid=%u retainedSources=%zu byteCap=8388608\n",waveTotals.boundVertices,waveTotals.valid,rotation.entries());
    // Exhausted direct/material requests cannot evict warm sources even at
    // the count limit. Both independent work caps must block admission first.
    for(unsigned i=2700;i<4096;++i){sources.push_back(mesh(3,float(i)));Bounds b;assert(finish(rotation,p,sources.back(),decl,c,inverse,b)==Status::Valid);}
    const auto bytes=rotation.reservedBytes();auto missing=mesh(3,20000);
    for(bool vertexLimit:{false,true}){rotation.beginFrame(false);Budget exhausted;if(vertexLimit)exhausted.maxVertices=0;else exhausted.maxOperations=0;Bounds b;
        assert(rotation.calculate(p,*missing,missing,decl,3,c,inverse,exhausted,b)==Status::Budget&&!b.valid);
        assert(rotation.entries()==4096&&rotation.reservedBytes()==bytes&&!rotation.stats().evictions&&!rotation.hasPendingBuild());}
    std::puts("COLD_ADMISSION_PASS saturatedEntries=4096 exhaustedVertexAndOperationCaps=no_eviction no_pending_pin=1");
    // Unfinished, expired, unsupported and malformed sources retain invalid
    // bounds: a larger vertex quota cannot bypass the real deadline.
    std::vector<NorthlightReplayBoundsJob::Item> items(2);items[0].owner=mesh(140001);items[0].prepared=prepared;items[0].constants=c;items[1].owner=mesh(3);items[1].constants=c;Cache limited;limited.beginFrame(false,2);auto t=NorthlightReplayBoundsJob::schedule(limited,items,inverse);assert(!t.valid&&!items[0].bounds.valid&&!items[1].bounds.valid);
    EnvelopeCache timed;timed.beginFrame(false);Budget deadline;deadline.maxVertices=131072;Bounds expired;TestClock::step=AdaptiveTimeBudget::Build;
    assert(timed.calculate(p,*items[0].owner,items[0].owner,decl,3,c,inverse,deadline,expired)==Status::Budget&&!expired.valid&&deadline.vertices<=BuildClockStride);TestClock::step=1;
    auto bad=std::make_shared<Mesh>(*mesh(33,0,true));bad->indices.back()=bad->vertexCount;EnvelopeCache invalid;Bounds b;assert(finish(invalid,p,bad,decl,c,inverse,b)==Status::Invalid&&!b.valid);
    for(unsigned why=0;why<8;++why){auto malformed=std::make_shared<Mesh>(*mesh(33));auto d=std::array<D3DVERTEXELEMENT9,3>{decl[0],decl[1],decl[2]};
        if(why==0){const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(malformed->streams[0].bytes.data()+4,&nan,4);}if(why==1)malformed->streams[0].stride=8;if(why==2)d[0].Offset=65535;if(why==3)malformed->streams[0].bytes.resize(8);if(why==4)inverse[15]=2;if(why==5)inverse[0]=INFINITY;if(why==6)c[6*4]=INFINITY;if(why==7)d[0].Type=D3DDECLTYPE_FLOAT4;
        EnvelopeCache negative;const auto status=finish(negative,p,malformed,d.data(),c,inverse,b);assert(status!=Status::Valid&&!b.valid);inverse[15]=inverse[0]=c[6*4]=1;
    }
    std::printf("RIGID_SOURCE_PASS exactBounds=%u cameraAndRevision=1 malformedFailOpen=1 partialBuildPreserved=1\n",exact);return 0;
}
#else
int main(){
    auto p=rigid(),generic=p;generic.skinned=true;float c[1024]={},inv[16]={};for(unsigned i=0;i<4;++i){inv[i*5]=1;c[4*(6+i)+i]=1;}
    double times[2]={};size_t vertices[2]={};
    for(unsigned block=0;block<12;++block){const bool fast=block%4==1||block%4==2;EnvelopeCache cache;auto m=mesh(32766,0,true);Bounds b;const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<10000;++frame){cache.beginFrame(false);Budget work;work.maxVertices=32768;const auto status=cache.calculate(fast?p:generic,*m,m,decl,3,c,inv,work,b);vertices[fast]+=work.vertices;if(status!=Status::Budget){assert(status==Status::Valid&&b.valid);break;}}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();times[fast]+=ms;std::printf("RIGID_SOURCE_TIMING block=%u optimized=%u ms=%.6f\n",block,unsigned(fast),ms);
    }
    assert(vertices[0]==vertices[1]);std::printf("RIGID_SOURCE_TIMING_TOTAL referenceMs=%.6f optimizedMs=%.6f sourceVertices=%zu\n",times[0],times[1],vertices[0]);
}
#endif
