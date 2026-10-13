#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib slow
"""0.3.143 replay bounds worker: the asynchronous pass must reproduce the 0.3.142
synchronous pass bit for bit (bounds, work classes, visit counts, cache state)
under a deterministic bounds clock, whether the render thread joins early or
late; stress mode adds cancellation, mesh churn during the job, unknown
programs and a protocol violation. Builds -O2, ASan+UBSan and TSan, then a
real-clock main-thread microbenchmark (legacy wall vs kick+join)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast
import json
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

stub = literal(HERE / 'test_terrain_snapshot.py', 'stub')
fixture = literal(HERE / 'test_replay_skin_safety.py', 'harness').split('int main(')[0]
fixture = fixture.replace('static Status finish(', '[[maybe_unused]] static Status finish(').replace('static Mesh geometry(', '[[maybe_unused]] static Mesh geometry(').replace('static void palette(', '[[maybe_unused]] static void palette(').replace('static size_t enclosed(', '[[maybe_unused]] static size_t enclosed(')
point = fp.src('world_point_rendering.inl').read_text()
# The renderer's own kick/join/abandon glue, compiled verbatim below.
glue = point[point.index('    NorthlightReplayBoundsJob::Worker replayBoundsWorker;'):point.index('    void pointCalculateReplayBounds(){')]
# 0.3.142 pointCalculateReplayBounds pass, verbatim (logging tail excluded).
LEGACY = r"""        const auto start=std::chrono::steady_clock::now();
        using WorkKind=NorthlightReplayBounds::WorkKind;
        NorthlightReplayBounds::Budget cheapBudget,heavyBudget,buildBudget;
        cheapBudget.maxVertices=0;cheapBudget.maxOperations=163840;
        heavyBudget.maxVertices=0;heavyBudget.maxOperations=32768;
        buildBudget.maxVertices=2048;buildBudget.maxOperations=65536;
        const bool diagnostics=NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics);replayBoundsMetadata.beginFrame();
        // Hints contain no entry/declaration pointers. Current pose bounds and
        // classifications never survive to another frame or recycled packet.
        for(auto& p:replays){p->pointBounds={};p->boundsWork={};p->boundsPrepared.reset();}
        size_t cheapVisited=0,heavyVisited=0,buildVisited=0;
        const auto visit=[&](size_t index,unsigned phase){
            auto& p=replays[index];if(p->pointBounds.valid)return false;
            // This check precedes declaration/program/cache lookups. In a warm
            // crowd the cold pass consequently performs no repeated lookups.
            if(phase==1&&p->boundsWork.kind!=WorkKind::Heavy)return false;
            if(phase==2&&p->boundsWork.kind!=WorkKind::Cold)return false;
            // The borrowed cheap turn cannot spend already-closed heavy/build
            // allowances. Newly built cold packets evaluate on a later frame.
            if(phase==0&&(p->boundsWork.kind==WorkKind::Heavy||p->boundsWork.kind==WorkKind::Cold||p->boundsWork.kind==WorkKind::Unsupported))return false;
            if(!p->shared){p->boundsWork.kind=WorkKind::Unsupported;return false;}
            if(phase==0&&!p->boundsPrepared){
                p->boundsPrepared=replayBoundsMetadata.get(p->originalShader,p->decl,[&]()->std::shared_ptr<const NorthlightReplayBounds::Prepared>{
                    // Key preparation used to run inside each cache lookup.
                    // Charge misses to the same budget; hits need no new timer.
                    struct PreparationCost {
                        NorthlightReplayBounds::Cache& cache;
                        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                        ~PreparationCost(){cache.chargeCheapPreparation(std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()));}
                    } cost{replayBoundsCache};
                    auto program=actorPrograms.find(p->originalShader);
                    if(program==actorPrograms.end())return {};
                    const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                    if(!declarationCache.get(p->decl,elements,count))return {};
                    return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*program->second,elements,count);
                });
            }
            NorthlightReplayBounds::Status status;
            if(p->boundsPrepared){
                const auto& prepared=*p->boundsPrepared;
                if(phase==0){++cheapVisited;status=replayBoundsCache.calculateCheap(prepared,p->mesh(),p->shared,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(prepared,p->mesh(),p->shared,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(prepared,p->mesh(),p->shared,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }else{
                // Allocation/metadata-cap failures retain the audited full key
                // path and do not turn missing metadata into missing shadows.
                auto program=actorPrograms.find(p->originalShader);
                if(program==actorPrograms.end()){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                if(!declarationCache.get(p->decl,elements,count)){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                if(phase==0){++cheapVisited;status=replayBoundsCache.calculateCheap(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }
            if(status==NorthlightReplayBounds::Status::Valid)++pointBoundsValid;
            return status==NorthlightReplayBounds::Status::Budget;
        };
        replayBoundsCache.readyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);}));
        replayBoundsCache.heavyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.heavyStart(replays.size()),
            [&](){return replayBoundsCache.canHeavy()&&heavyBudget.operations<heavyBudget.maxOperations;},[&](size_t index){return visit(index,1);}));
        replayBoundsCache.continuePendingBuild(buildBudget);
        replayBoundsCache.buildResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.buildStart(replays.size()),
            [&](){return replayBoundsCache.canBuild()&&buildBudget.vertices<buildBudget.maxVertices&&buildBudget.operations<buildBudget.maxOperations;},[&](size_t index){return visit(index,2);}));
        const size_t reservedCheapVisited=cheapVisited;
        replayBoundsCache.finishReservedTurnsAndLend();
        cheapBudget.maxOperations=262144-heavyBudget.operations-buildBudget.operations;
        // Preserve the first turn's cursor for the next frame. Advancing it
        // through this bonus turn could strand cold/heavy packets exclusively
        // after their reserved phases on every frame of a periodic draw list.
        NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);});
        replayBoundsCache.settleClock();
        pointBoundVertices=buildBudget.vertices;pointBoundOperations=cheapBudget.operations+heavyBudget.operations+buildBudget.operations;
"""

clock = r"""
#include <atomic>
#include <chrono>
#include <cstdint>
struct TestClock {
    using duration=std::chrono::nanoseconds;using rep=duration::rep;using period=duration::period;
    using time_point=std::chrono::time_point<TestClock,duration>;static constexpr bool is_steady=true;
    static std::atomic<std::int64_t> ticks,step;
    static time_point now()noexcept{return time_point(duration(ticks.fetch_add(step.load(std::memory_order_relaxed),std::memory_order_relaxed)));}
};
inline std::atomic<std::int64_t> TestClock::ticks{1000000},TestClock::step{1000};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK TestClock
#define NORTHLIGHT_TEST_CLOCK 1
"""

harness = r"""
#include "replay_bounds_job.h"
#include "replay_bounds_metadata.h"
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
""" + fixture + r"""
using Prepared=NorthlightReplayBounds::Prepared;using WorkInfo=NorthlightReplayBounds::WorkInfo;using Totals=NorthlightReplayBoundsJob::Totals;
struct IDirect3DVertexShader9 {unsigned AddRef(){return 1;}unsigned Release(){return 1;}};
struct FakeDecl:IDirect3DVertexDeclaration9 {
    std::vector<D3DVERTEXELEMENT9> elements;
    unsigned AddRef()override{return 1;}unsigned Release()override{return 1;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return D3D_OK;}
};
#include "diagnostics_switch.h" /* 0.3.192: draw_snapshot.h includes it (LOCK METER read-back classes); off until a frame turns it on */
static const bool diagnosticsOff=(NorthlightDiagnostics::configure(false),true);
struct Packet {
    IDirect3DVertexShader9* originalShader=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
    NorthlightReplayBounds::Bounds pointBounds;WorkInfo boundsWork;std::shared_ptr<const Prepared> boundsPrepared;
    std::shared_ptr<const Mesh> shared;Mesh snapshot;const float* constants=nullptr;unsigned otherState=0;bool captureRootValid=false;float captureRoot[3]={};bool anchorValid=false;float anchor[3]={};
    const float* position()const{return captureRootValid?captureRoot:anchorValid?anchor:nullptr;}
    const Mesh& mesh()const{return shared?*shared:snapshot;}
};
struct DeclarationCache {bool get(IDirect3DVertexDeclaration9* d,const D3DVERTEXELEMENT9*& e,UINT& n){if(!d)return false;auto* f=static_cast<FakeDecl*>(d);e=f->elements.data();n=UINT(f->elements.size());return true;}};
struct Context {float inverseView[16]={};};
struct World {
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>>& actorPrograms; /* 0.3.177: the renderer's program handles */
    std::vector<std::unique_ptr<Packet>> replays;
    NorthlightReplayBounds::Cache replayBoundsCache;
    NorthlightReplayMetadata::Cache<Prepared,IDirect3DVertexShader9,IDirect3DVertexDeclaration9> replayBoundsMetadata;
    DeclarationCache declarationCache;Context context;unsigned frames=0,logs=0;float actorMatrices[2][16]={};bool actorValid[2]={};unsigned replayNearSize=0;unsigned long replayNearStops=0; /* the actor maps as last drawn: none, no near-first order */
    struct History {unsigned hold=0;float at[3]={};unsigned selfHold()const{return hold;}const float* selfAt()const{return at;}} actorShadowHistory;
    unsigned pointBoundsValid=0;size_t pointBoundVertices=0,pointBoundOperations=0;Totals visits;
    explicit World(std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>>& programs):actorPrograms(programs){}
    void pointLogReplayBounds(const Totals& t,double,double,double,double,double,bool){visits=t;++logs;}
    void legacyPass(){
""" + LEGACY + r"""
        visits={cheapVisited,heavyVisited,buildVisited,reservedCheapVisited,pointBoundVertices,pointBoundOperations,pointBoundsValid};(void)start;
    }
""" + glue + r"""
};
static bool same(const NorthlightReplayBounds::Bounds& a,const NorthlightReplayBounds::Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
[[maybe_unused]] static bool sameVisits(const Totals& a,const Totals& b){return a.cheapVisited==b.cheapVisited&&a.heavyVisited==b.heavyVisited&&a.buildVisited==b.buildVisited&&a.reservedCheapVisited==b.reservedCheapVisited&&a.boundVertices==b.boundVertices&&a.boundOperations==b.boundOperations&&a.valid==b.valid;}
[[maybe_unused]] static bool sameWork(const WorkInfo& a,const WorkInfo& b){return a.kind==b.kind&&a.programHash==b.programHash&&a.groups==b.groups&&a.operations==b.operations&&a.skin==b.skin&&a.elapsedUs==b.elapsedUs;}
static Mesh randomMesh(std::mt19937& rng,unsigned vertices,unsigned bones){
    Mesh m;m.vertexCount=vertices;m.primitiveCount=vertices/3;m.topology=D3DPT_TRIANGLELIST;m.indexed=false;
    m.streams[0].stride=32;m.streams[0].bytes.resize(size_t(vertices)*32);std::uniform_real_distribution<float> u(-1,1),w(0,1);
    const unsigned first=rng()%(76-bones);
    for(unsigned n=0;n<vertices;++n){float v[7]={u(rng)*2,u(rng)*2,u(rng)*2,w(rng),w(rng),w(rng),w(rng)};float sum=v[3]+v[4]+v[5]+v[6];
        for(unsigned k=3;k<7;++k)v[k]/=sum;auto* t=m.streams[0].bytes.data()+n*32;std::memcpy(t,v,28);
        for(unsigned k=0;k<4;++k)t[28+k]=(unsigned char)(first+(bones>1?rng()%bones:0));}
    return m;
}
static void pose(std::mt19937& rng,float* c,unsigned actor,unsigned frame){
    std::uniform_real_distribution<float> u(-1,1);for(unsigned i=0;i<124;++i)c[i]=u(rng);
    for(unsigned b=0;b<75;++b){const float a=float(b+actor)*.031f+float(frame)*.01f,e=float(b*3+actor)*.017f;
        const float r[3][3]={{std::cos(a),-std::sin(a)*std::cos(e),std::sin(a)*std::sin(e)},{std::sin(a),std::cos(a)*std::cos(e),-std::cos(a)*std::sin(e)},{0,std::sin(e),std::cos(e)}};
        for(unsigned i=0;i<3;++i){float* row=c+4*(31+3*b+i);for(unsigned j=0;j<3;++j)row[j]=r[i][j];row[3]=float(actor%40)*3.1f+u(rng)*.05f;}}
}
static void view(float* inverse,unsigned frame){
    const float a=float(frame)*.013f,e=.3f+float(frame%50)*.002f;std::fill(inverse,inverse+16,0.f);
    const float r[3][3]={{std::cos(a),std::sin(a),0},{-std::sin(a)*std::cos(e),std::cos(a)*std::cos(e),std::sin(e)},{std::sin(a)*std::sin(e),-std::cos(a)*std::sin(e),std::cos(e)}};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)inverse[4*i+j]=r[i][j];inverse[12]=100+float(frame)*.2f;inverse[13]=-50;inverse[14]=20;inverse[15]=1;
}
static Program rigid(){
    Program p;p.major=3;p.positionRegister=0;p.inputs.push_back({0,0,0});
    Operation op;op.code=1;op.destination=0x800f0000;op.source[0].token=0x90e40000;p.operations.push_back(op);
    op={};op.code=2;op.destination=0x80070000;op.source[0].token=0x80e40000;op.source[1].token=0xa0e4000a;p.operations.push_back(op);return p;
}
struct Scene {
    IDirect3DVertexShader9 shaders[4];FakeDecl decls[2];std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>> programs;
    std::vector<std::shared_ptr<const Mesh>> pool;std::vector<unsigned> kinds;std::mt19937 rng{143};
    std::vector<std::array<float,1024>> banks;unsigned heavyBones=3,heavyShare=2;
    explicit Scene(const Program& skin,size_t meshes,unsigned bones=3,unsigned share=2):heavyBones(bones),heavyShare(share){
        Program heavy=skin;Operation copy;copy.code=1;copy.destination=0x80070001;copy.source[0].token=0x80e40001;heavy.operations.push_back(copy);
        programs[&shaders[0]]=std::make_shared<const Program>(skin);programs[&shaders[1]]=std::make_shared<const Program>(heavy);programs[&shaders[2]]=std::make_shared<const Program>(rigid()); /* shaders[3]: unknown program */
        for(auto& d:decls)d.elements.assign(declaration,declaration+4);
        for(size_t i=0;i<meshes;++i)add();
    }
    void add(){const unsigned kind=unsigned(rng()%10);kinds.push_back(kind<8-heavyShare?0:kind<8?1:2);
        const unsigned bones=kinds.back()==1?1+rng()%heavyBones:1+rng()%40;pool.push_back(std::make_shared<const Mesh>(randomMesh(rng,kinds.back()==2?12+rng()%60:30+rng()%700,bones)));}
    void churn(size_t count){for(size_t n=0;n<count;++n){const size_t i=rng()%pool.size();const unsigned bones=kinds[i]==1?1+rng()%heavyBones:1+rng()%40;
        pool[i]=std::make_shared<const Mesh>(randomMesh(rng,kinds[i]==2?12+rng()%60:30+rng()%700,bones));}}
    // Identical packet lists for both worlds: same meshes, same frozen banks.
    void frame(unsigned f,size_t count,bool unknown,std::vector<World*> worlds){
        const size_t actors=count/4+1;banks.resize(actors);for(size_t a=0;a<actors;++a)pose(rng,banks[a].data(),unsigned(a),f);
        for(auto* w:worlds){w->replays.clear();view(w->context.inverseView,f);w->frames=f%7?1:0;} /* frames==0: sampled diagnostics */
        std::mt19937 local(f*2654435761u+17);
        for(size_t i=0;i<count;++i){const size_t actor=i/4;const size_t mesh=(actor*7+i%4+(f/200))%pool.size();
            const bool owned=local()%33==0,odd=unknown&&local()%29==0;
            for(auto* w:worlds){auto p=std::make_unique<Packet>();p->originalShader=&shaders[odd?3:kinds[mesh]];p->decl=&decls[(actor/3)%2];
                if(!owned)p->shared=pool[mesh];else{p->snapshot.vertexCount=3;p->snapshot.primitiveCount=1;}
                p->constants=banks[actor].data();w->replays.push_back(std::move(p));}
        }
    }
};
static void warm(World& w,Scene& s){ // metadata warm for every pair (<=4 new per frame)
    for(unsigned round=0;round<8;++round){w.replayBoundsMetadata.beginFrame();
        for(unsigned sh=0;sh<3;++sh)for(auto& d:s.decls)w.replayBoundsMetadata.get(&s.shaders[sh],&d,[&]{return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*s.programs[&s.shaders[sh]],d.elements.data(),d.elements.size());});}
}
[[maybe_unused]] static void compareCaches(const World& a,const World& b,size_t n){
    const auto& x=a.replayBoundsCache;const auto& y=b.replayBoundsCache;
    assert(!std::memcmp(&x.envelopeStats(),&y.envelopeStats(),sizeof x.envelopeStats()));
    assert(x.readyStart(n)==y.readyStart(n)&&x.heavyStart(n)==y.heavyStart(n)&&x.buildStart(n)==y.buildStart(n));
    assert(x.envelopeEntries()==y.envelopeEntries()&&x.envelopeBytes()==y.envelopeBytes()&&x.envelopeValid()==y.envelopeValid()&&x.deferred()==y.deferred());
    assert(x.envelopeReadyMilliseconds()==y.envelopeReadyMilliseconds()&&x.envelopeHeavyMilliseconds()==y.envelopeHeavyMilliseconds()&&x.envelopeBuildMilliseconds()==y.envelopeBuildMilliseconds()&&x.envelopeClassifyMilliseconds()==y.envelopeClassifyMilliseconds());
    assert(x.hasPendingBuild()==y.hasPendingBuild()&&x.pendingBuildVertices()==y.pendingBuildVertices()&&x.borrowedMicroseconds()==y.borrowedMicroseconds());
}
static void spin(double us){const auto end=std::chrono::steady_clock::now()+std::chrono::nanoseconds(std::int64_t(us*1000));while(std::chrono::steady_clock::now()<end){}}
int main(int argc,char** argv){
    assert(argc>=3);const std::string mode=argv[2];auto skin=load(argv[1]);assert(SkinEnvelope::supports(skin));
#ifdef NORTHLIGHT_TEST_CLOCK
    if(mode=="priority"){
        // A dense crowd through the real Worker (schedule with a priority list): 1500 packets, 300 of them where a caster can be in the actor map
        // (the self's 12 parts, 40 in the pivot's neighbourhood, 150 in the middle, 98 in the edge band), 1200 far away, all shuffled in one draw list
        // that is the same every frame, the near ones the largest meshes. The clock step is large, so the time budgets (not the operation budgets)
        // end every phase after a few packets: the near walk (half of a phase) covers less than the 300 (the outer ring most of the time not at all). Per frame, how many packets of each ring and
        // of the far group have valid bounds. Asserted: after warm-up the self's parts and the pivot's neighbourhood are covered in EVERY frame,
        // every packet gets bounds (also the outer ring, which the rotating pass keeps), the middle ring is covered most of the time, the far coverage per frame stays at
        // least about half of what the plain rotation gives, and a position beyond the actor map's reach is never listed.
        const unsigned ringSize[4]={12,40,150,98},nearCount=300,farCount=1200,total=nearCount+farCount,frames=argc>3?unsigned(std::atoi(argv[3])):80,warmup=24;
        Scene scene(skin,600,3,2);
        std::vector<size_t> cheap; /* the plain skinned meshes (shaders[0]): all of them reach bounds */
        for(size_t m=0;m<scene.pool.size();++m)if(scene.kinds[m]==0)cheap.push_back(m);
        std::sort(cheap.begin(),cheap.end(),[&](size_t a,size_t b){return scene.pool[a]->vertexCount>scene.pool[b]->vertexCount;});assert(cheap.size()>=nearCount+20);
        // logical packet -> slot in the draw list (shuffled), group: 0..3 the rings, 4 far
        std::vector<unsigned> slotOf(total),group(total);{unsigned g=0,k=0;for(unsigned i=0;i<total;++i){group[i]=i<nearCount?g:4;if(i<nearCount&&++k==ringSize[g]){++g;k=0;}}}
        {std::vector<unsigned> order(total);for(unsigned i=0;i<total;++i)order[i]=i;std::mt19937 shuffle(7);std::shuffle(order.begin(),order.end(),shuffle);for(unsigned i=0;i<total;++i)slotOf[i]=order[i];}
        const float centre[3]={100,50,10};
        float matrix[16]={};matrix[0]=matrix[5]=1.f/12;matrix[10]=.5f;matrix[12]=-centre[0]/12;matrix[13]=-centre[1]/12;matrix[14]=.25f;matrix[15]=1; /* clip x,y = (world - centre)/12 */
        const auto position=[&](unsigned i,float* at){ /* ring 0: within 1 yd of the self, ring 1: 3.3..4.3 yd, ring 2: 5..9.4 yd, ring 3: 10..13 yd, far: 40+ yd */
            const unsigned g=group[i];unsigned first=0;for(unsigned r=0;r<g&&r<4;++r)first+=ringSize[r];const unsigned k=i-(g<4?first:nearCount);
            const float angle=float(k)*2.399963f;const float radius=g==0?1.f:g==1?3.3f+1.f*float(k)/40.f:g==2?5.f+4.4f*float(k)/150.f:g==3?10.f+3.f*float(k)/98.f:40.f+float(k)*.05f;
            const float c=std::cos(angle),n=std::sin(angle),square=std::max(std::fabs(c),std::fabs(n)); /* the rings are squares (Chebyshev distance) */
            at[0]=centre[0]+radius*c/square;at[1]=centre[1]+radius*n/square;at[2]=centre[2];};
        struct Run {std::vector<unsigned> valid[5];unsigned maxGap[5]={};size_t never=0,listSize=0,firstSelf=0;double farMean=0;unsigned minSelf=~0u,minInner=~0u;unsigned ringSizes[4]={};};
        const auto run=[&](bool ordered){
            World w(scene.programs);warm(w,scene);TestClock::ticks=1000000;
                        Run r;std::vector<unsigned> last(total,0),gap(total,0);
            if(ordered){w.actorValid[0]=true;std::memcpy(w.actorMatrices[0],matrix,sizeof matrix);w.actorShadowHistory.hold=1;std::memcpy(w.actorShadowHistory.at,centre,sizeof centre);}
            for(unsigned f=1;f<=frames+8;++f){
                const bool settle=f<=8;TestClock::step=settle?1:(argc>4?std::atoi(argv[4]):1000); /* 8 frames with room for everything build the meshes' envelopes; the crowd is then in its steady state */
                scene.frame(0,total,false,{&w}); /* the same draw list every frame */
                for(unsigned i=0;i<total;++i){auto& p=*w.replays[slotOf[i]];position(i,p.captureRoot);p.captureRootValid=true;
                    p.shared=scene.pool[i<nearCount?cheap[i]:cheap[nearCount+i%(cheap.size()-nearCount)]];p.originalShader=&scene.shaders[0];} /* the largest meshes are the near packets */
                w.replayBoundsKick();w.replayBoundsJoin();if(settle)continue;
                const auto& list=w.replayBoundsWorker.nearList();r.listSize=list.size();for(unsigned g=0;g<4;++g)r.ringSizes[g]=unsigned(list.ring[g].size());
                unsigned count[5]={};
                for(unsigned i=0;i<total;++i){const bool valid=w.replays[slotOf[i]]->pointBounds.valid;count[group[i]]+=valid;
                    if(valid)last[i]=f-8;if(f-8>warmup)gap[i]=std::max(gap[i],f-8-last[i]);}
                for(unsigned g=0;g<5;++g)r.valid[g].push_back(count[g]);
            }
            for(unsigned i=0;i<total;++i){r.never+=last[i]==0;r.maxGap[group[i]]=std::max(r.maxGap[group[i]],gap[i]);}
            for(unsigned f=warmup;f<frames;++f){r.minSelf=std::min(r.minSelf,r.valid[0][f]);r.minInner=std::min(r.minInner,r.valid[1][f]);r.farMean+=r.valid[4][f];}
            r.farMean/=double(frames-warmup);return r;
        };
        const Run on=run(true),off=run(false);
        double mean[5]={};for(unsigned g=0;g<5;++g){for(unsigned f=warmup;f<frames;++f)mean[g]+=on.valid[g][f];mean[g]/=double(frames-warmup);}
        std::printf("{\"mode\":\"priority\",\"nearListSize\":%zu,\"rings\":[%u,%u,%u,%u],\"on\":{\"minSelf\":%u,\"minInner\":%u,\"meanPerRing\":[%.1f,%.1f,%.1f,%.1f],\"maxGapPerRing\":[%u,%u,%u,%u],\"farMean\":%.1f,\"farMaxGap\":%u},\"off\":{\"minSelf\":%u,\"farMean\":%.1f,\"maxGapPerRing\":[%u,%u,%u,%u,%u]},\"neverValid\":%zu}\n",
            on.listSize,on.ringSizes[0],on.ringSizes[1],on.ringSizes[2],on.ringSizes[3],on.minSelf,on.minInner,mean[0],mean[1],mean[2],mean[3],on.maxGap[0],on.maxGap[1],on.maxGap[2],on.maxGap[3],on.farMean,on.maxGap[4],off.minSelf,off.farMean,off.maxGap[0],off.maxGap[1],off.maxGap[2],off.maxGap[3],off.maxGap[4],on.never);
        assert(on.listSize==nearCount&&on.ringSizes[0]==ringSize[0]&&on.ringSizes[1]==ringSize[1]&&on.ringSizes[2]==ringSize[2]&&on.ringSizes[3]==ringSize[3]); /* the far 1200 are not listed */
        assert(on.never==0&&off.never==0);
        assert(on.minSelf==ringSize[0]&&on.minInner==ringSize[1]); /* the self and the pivot's neighbourhood: bounds in every frame after warm-up */
        assert(mean[0]+mean[1]+mean[2]+mean[3]<double(nearCount)); /* the budget share did not cover the list: the rings really compete */
        assert(mean[2]>=.5*ringSize[2]&&on.maxGap[2]<=off.maxGap[2]); /* the middle ring is covered most of the time and no packet waits longer than in the plain rotation */
        assert(on.farMean>=.5*off.farMean);     /* the rotating pass keeps at least about half of its coverage */
        return 0;
    }
    if(mode=="equivalence"||mode=="stress"){
        const bool stress=mode=="stress";const unsigned frames=argc>3?unsigned(std::atoi(argv[3])):600;
        Scene scene(skin,stress?260:220);World legacy(scene.programs),async(scene.programs);warm(legacy,scene);warm(async,scene);
        size_t packets=0,valid=0,budgetFrames=0,cancelled=0,violations=0,enclosedVertices=0,lateJoins=0,differentValid=0,unsupported=0;std::mt19937 pick(5);
        for(unsigned f=0;f<frames;++f){
            const size_t count=400+(size_t(f)*7919)%601;scene.frame(f,count,stress,{&legacy,&async});packets+=count;
            NorthlightDiagnostics::configure(true);const bool sampled=f%7==0;TestClock::step=200+std::int64_t((f*2654435761u)%3800);
            legacy.legacyPass();
            async.replayBoundsKick();assert(async.replayBoundsWorker.pending());
            for(auto& p:async.replays)assert(!p->pointBounds.valid&&p->boundsWork.kind==NorthlightReplayBounds::WorkKind::Unknown);
            const bool cancel=stress&&f%11==5,violate=stress&&f%13==3;size_t victim=0;
            if(violate){victim=pick()%async.replays.size();async.replays[victim]->constants=scene.banks[0].data()+4;} /* protocol breach: pointer swap */
            if(stress){for(auto& p:async.replays)p->otherState+=1;scene.churn(3);} /* concurrent render-thread work */
            switch(f%4){case 0:lateJoins++;break;case 1:spin(150);break;case 2:std::this_thread::yield();break;default:spin(900);}
            if(cancel){async.replayBoundsAbandon();++cancelled;
                for(auto& p:async.replays)assert(!p->pointBounds.valid);for(auto& item:async.replayBoundsWorker.items())assert(!item.owner&&!item.prepared);
                async.legacyPass();continue;} /* the synchronous pass then runs, as in render(); caches diverge from here */
            async.replayBoundsJoin();assert(!async.replayBoundsWorker.pending());
            for(auto& item:async.replayBoundsWorker.items())assert(!item.owner&&!item.prepared);
            assert(async.replays.size()==legacy.replays.size());
            if(!stress){
                assert(async.pointBoundsValid==legacy.pointBoundsValid&&async.pointBoundVertices==legacy.pointBoundVertices&&async.pointBoundOperations==legacy.pointBoundOperations);
                if(sampled&&!(async.logs&&sameVisits(async.visits,legacy.visits))){auto&a=async.visits;auto&b=legacy.visits;std::printf("f=%u logs=%u %zu/%zu %zu/%zu %zu/%zu %zu/%zu %zu/%zu %zu/%zu %u/%u\n",f,async.logs,a.cheapVisited,b.cheapVisited,a.heavyVisited,b.heavyVisited,a.buildVisited,b.buildVisited,a.reservedCheapVisited,b.reservedCheapVisited,a.boundVertices,b.boundVertices,a.boundOperations,b.boundOperations,a.valid,b.valid);assert(false);}
                compareCaches(legacy,async,count);
            }
            for(size_t i=0;i<count;++i){const auto& a=*async.replays[i];const auto& l=*legacy.replays[i];
                if(violate&&i==victim){assert(!a.pointBounds.valid);++violations;continue;}
                if(!stress)assert(same(a.pointBounds,l.pointBounds)&&sameWork(a.boundsWork,l.boundsWork));
                else{if(a.pointBounds.valid&&l.pointBounds.valid)assert(same(a.pointBounds,l.pointBounds));else differentValid+=a.pointBounds.valid!=l.pointBounds.valid;}
                valid+=a.pointBounds.valid;unsupported+=a.boundsWork.kind==NorthlightReplayBounds::WorkKind::Unsupported;
                if(a.pointBounds.valid&&pick()%97==0)enclosedVertices+=enclosed(*scene.programs[a.originalShader],*a.shared,a.constants,async.context.inverseView,a.pointBounds);
            }
            budgetFrames+=legacy.replayBoundsCache.envelopeStats().timeDeferred>0;
            if(!stress)scene.churn(2); /* after both passes: identical expiry for both caches */
            async.logs=0;
        }
        std::printf("{\"mode\":\"%s\",\"frames\":%u,\"packets\":%zu,\"valid\":%zu,\"timeBudgetFrames\":%zu,\"immediateJoins\":%zu,\"cancelled\":%zu,\"protocolViolationsFailedOpen\":%zu,\"validSetDifferences\":%zu,\"unsupported\":%zu,\"enclosedVertices\":%zu}\n",
            mode.c_str(),frames,packets,valid,budgetFrames,lateJoins,cancelled,violations,differentValid,unsupported,enclosedVertices);
        assert(valid>0&&enclosedVertices>0&&(stress||budgetFrames>frames/4));
        return 0;
    }
#else
    if(mode=="bench"){
        // Real clock, production budgets. Sampled logging off, as on 119 of 120 frames.
        using Ms=std::chrono::duration<double,std::milli>;const auto now=[]{return std::chrono::steady_clock::now();};
        auto median=[](std::vector<double> v,double q){std::sort(v.begin(),v.end());return v[size_t(q*double(v.size()-1))];};
        std::printf("[");bool first=true;unsigned f=0;
        for(unsigned mix=0;mix<2;++mix)for(size_t count:{400,700,1000}){
            /* mix 1: half the meshes on the general interval evaluator with up to 12 bone tuples each (budget-bound, like the game logs) */
            Scene scene(skin,260,mix?12:3,mix?4:2);World legacy(scene.programs),async(scene.programs);warm(legacy,scene);warm(async,scene);
            std::vector<double> legacyMs,kickMs,immediateMs,overlapMs,workerMs,lagMs;size_t legacyValid=0,asyncValid=0;
            for(unsigned n=0;n<330;++n,++f){scene.frame(f,count,false,{&legacy,&async});NorthlightDiagnostics::configure(false);
                auto t=now();legacy.legacyPass();const double l=Ms(now()-t).count();
                t=now();async.replayBoundsKick();const double k=Ms(now()-t).count();
                const bool overlap=n&1;if(overlap)spin(1000);
                t=now();async.replayBoundsJoin();const double j=Ms(now()-t).count();
                const auto& items=async.replayBoundsWorker.items();(void)items;
                if(n<30)continue; /* warm envelopes */
                legacyMs.push_back(l);kickMs.push_back(k);(overlap?overlapMs:immediateMs).push_back(k+j);
                legacyValid+=legacy.pointBoundsValid;asyncValid+=async.pointBoundsValid;
                for(size_t i=0;i<count;++i){const auto& a=async.replays[i]->pointBounds;const auto& b=legacy.replays[i]->pointBounds;if(a.valid&&b.valid)assert(same(a,b));}
            }
            // Worker-side timing: one more job each, finished late.
            for(unsigned n=0;n<60;++n,++f){scene.frame(f,count,false,{&async});async.replayBoundsKick();spin(1500);
                const auto r=async.replayBoundsWorker.finish();workerMs.push_back(r.workerMs);lagMs.push_back(r.startLagMs);NorthlightReplayBoundsJob::apply(async.replayBoundsWorker.items(),async.replays,true);}
            std::printf("%s{\"mix\":\"%s\",\"draws\":%zu,\"envelopes\":%zu,\"legacyMainMs\":[%.3f,%.3f],\"asyncKickMs\":[%.3f,%.3f],\"asyncMainImmediateJoinMs\":[%.3f,%.3f],\"asyncMainAfter1msWorkMs\":[%.3f,%.3f],\"workerMs\":[%.3f,%.3f],\"startLagMs\":[%.3f,%.3f],\"legacyValidPerFrame\":%.1f,\"asyncValidPerFrame\":%.1f}",
                first?"":",",mix?"heavy":"light",count,async.replayBoundsCache.envelopeEntries(),median(legacyMs,.5),median(legacyMs,.9),median(kickMs,.5),median(kickMs,.9),median(immediateMs,.5),median(immediateMs,.9),
                median(overlapMs,.5),median(overlapMs,.9),median(workerMs,.5),median(workerMs,.9),median(lagMs,.5),median(lagMs,.9),double(legacyValid)/300,double(asyncValid)/300);first=false;
        }
        // 300 distinct shader/declaration pairs (> metadata EntryLimit 128): kick cost, cold and thrashing.
        {Scene scene(skin,260);std::vector<IDirect3DVertexShader9> many(300);World w(scene.programs);
            for(auto& sh:many)scene.programs[&sh]=scene.programs[&scene.shaders[0]];
            std::vector<double> cold,steady;size_t capped=0;
            for(unsigned n=0;n<120;++n){scene.frame(n,900,false,{&w});for(size_t i=0;i<w.replays.size();++i)w.replays[i]->originalShader=&many[(i*37)%many.size()];
                const auto t=now();w.replayBoundsKick();const double k=Ms(now()-t).count();w.replayBoundsJoin();capped+=w.replayBoundsCapped;
                (n<10?cold:steady).push_back(k);}
            std::printf(",{\"distinctPairs\":300,\"draws\":900,\"kickColdMs\":[%.3f,%.3f],\"kickThrashMs\":[%.3f,%.3f],\"cappedPerFrame\":%.1f}",median(cold,.5),median(cold,.9),median(steady,.5),median(steady,.9),double(capped)/120);}
        std::printf("]\n");return 0;
    }
#endif
    return 1;
}
"""

bench = r"""
#include "replay_bounds_job.h"
#include "replay_bounds_metadata.h"
"""

def build(root, name, source, flags, defines=()):
    (root / (name + '.cpp')).write_text(source)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *defines,
                    '-I', str(root), *fp.test_include_flags(), str(root / (name + '.cpp')), '-o', str(root / name)], check=True)
    return root / name

with tempfile.TemporaryDirectory(prefix='northlight-bounds-job-') as tmp:
    root = Path(tmp)
    (root / 'd3d9.h').write_text(stub)
    source = clock + harness
    shader = str(client_fixtures.four_bone_vs3())
    runs = [('o2', ['-O2'], [('equivalence', '600'), ('stress', '400'), ('priority', '80')]),
            ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'], [('equivalence', '200'), ('stress', '200'), ('priority', '80')]),
            ('tsan', ['-O1', '-g', '-fsanitize=thread'], [('equivalence', '150'), ('stress', '200'), ('priority', '80')])]
    for name, flags, modes in runs:
        binary = build(root, name, source, flags)
        for mode, frames, *extra in modes:
            out = subprocess.run([str(binary), shader, mode, frames, *extra], capture_output=True, text=True)
            if out.returncode or 'WARNING' in out.stderr or 'ERROR' in out.stderr:
                sys.exit(f'{name} {mode} failed ({out.returncode}):\n{out.stdout}{out.stderr[-6000:]}')
            print(name, out.stdout.strip())
    binary = build(root, 'bench', harness, ['-O2'])
    bench = json.loads(subprocess.run([str(binary), shader, 'bench'], check=True, capture_output=True, text=True).stdout)
    for row in bench:
        print('bench', json.dumps(row))
print('PASS replay bounds worker: async pass bit-identical to the 0.3.142 synchronous pass (bounds, work classes, visits, cache state and cursors) under a deterministic clock with early/late joins; stress: cancellation, mesh churn during the job, unknown programs and a protocol breach fail open; ASan/UBSan and TSan clean')
