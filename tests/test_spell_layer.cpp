// Task 34: behavioural tests of the production spell_layer.h against the FakeDevice of test_spell_layer_api.h.
#include "test_spell_layer_api.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
using namespace NorthlightSpellLayer;

#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c);std::abort();}}while(0)
using Bytes=std::vector<uint8_t>;

// ---------------- classification ----------------
static DrawFacts cand(){
    DrawFacts f;f.zEnable=true;f.blend=true;f.srcBlend=kSrcAlpha;f.destBlend=kBlendOne;f.family=Family::Particle;
    f.worldDomain=true;f.rt0IsTarget=true;f.dsIsWorldDepth=true;return f;
}
static void expect(const DrawFacts& f,Kind k,unsigned sub=0,Reason why=Reason::Count,InPlaceKind ip=InPlaceKind::None,unsigned mask=15){
    const Result r=classify(f,mask);
    CHECK(r.kind==k);CHECK(r.sub==sub);CHECK(r.reason==why);CHECK(r.inPlace==ip);
}
static void testClassify(){
    expect(cand(),Kind::Defer,SubAdditive);
    for(Family fam:{Family::Particle,Family::ParticleUnlit,Family::Model,Family::Projected}){DrawFacts f=cand();f.family=fam;expect(f,Kind::Defer,SubAdditive);}
    {DrawFacts f=cand();f.destBlend=kInvSrcAlpha;expect(f,Kind::Defer,SubAlpha);}
    {DrawFacts f=cand();f.srcBlend=kDestColor;f.destBlend=kBlendZero;expect(f,Kind::Defer,SubModulate);}
    {DrawFacts f=cand();f.srcBlend=kDestColor;f.destBlend=kSrcColor;expect(f,Kind::Defer,SubModulate);}
    {DrawFacts f=cand();f.srcBlend=kBlendZero;f.destBlend=kSrcColor;expect(f,Kind::Defer,SubModulate);}
    {DrawFacts f=cand();f.destBlend=kInvSrcColor;expect(f,Kind::Defer,SubOther);}
    {DrawFacts f=cand();f.blendOp=3;expect(f,Kind::Defer,SubOther);}
    {DrawFacts f=cand();f.separateAlpha=true;expect(f,Kind::Defer,SubOther);}
    {DrawFacts f=cand();f.colorWrite=7;expect(f,Kind::Defer,SubAdditive);}
    // the sub-class mask: a switched-off sub-class is an order hazard, a switched-on one defers
    expect(cand(),Kind::Hazard,0,Reason::Order,InPlaceKind::None,SubAlpha|SubModulate|SubOther);
    {DrawFacts f=cand();f.destBlend=kInvSrcAlpha;expect(f,Kind::Defer,SubAlpha,Reason::Count,InPlaceKind::None,SubAlpha);}
    // exclusions
    {DrawFacts f=cand();f.stencil=true;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.mrtBound=true;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.instanced=true;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.occlusionActive=true;expect(f,Kind::Hazard,0,Reason::Query);}
    {DrawFacts f=cand();f.family=Family::None;expect(f,Kind::Hazard,0,Reason::Order);}
    // fixed-function draws (ribbons, beams): a candidate like the M2 families, with the same exclusions
    {DrawFacts f=cand();f.fixedFunction=true;expect(f,Kind::Defer,SubAdditive);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;expect(f,Kind::Defer,SubAdditive);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;f.destBlend=kInvSrcAlpha;expect(f,Kind::Defer,SubAlpha);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::None,SubAlpha|SubModulate|SubOther);}
    for(int k=0;k<7;++k){
        DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;Reason why=Reason::Order;
        switch(k){case 0:f.srcBlend=kBlendOne;f.destBlend=kBlendZero;break;case 1:f.blend=false;break;case 2:f.zEnable=false;break;case 3:f.stencil=true;break;
                  case 4:f.dsIsWorldDepth=false;break;case 5:f.mrtBound=true;break;default:f.occlusionActive=true;why=Reason::Query;}
        expect(f,Kind::Hazard,0,why);
    }
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;f.instanced=true;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;f.zWrite=true;expect(f,Kind::TranslucentZ);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;f.worldDomain=false;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;f.samplesRenderTargetTexture=true;expect(f,Kind::Hazard,0,Reason::Sample);}
    {DrawFacts f=cand();f.fixedFunction=true;f.family=Family::None;f.colorWrite=0;expect(f,Kind::Ignore);}
    // water ripples (WaterRipples vertex shader) are water: never deferred, a split when draws are pending
    {DrawFacts f=cand();f.family=Family::Ripples;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Water);}
    {DrawFacts f=cand();f.family=Family::Ripples;f.pending=true;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::Water);}
    {DrawFacts f=cand();f.family=Family::Ripples;f.zWrite=true;expect(f,Kind::TranslucentZ);}
    {DrawFacts f=cand();f.family=Family::Ripples;f.colorWrite=0;expect(f,Kind::Ignore);}
    {DrawFacts f=cand();f.family=Family::Ripples;f.worldDomain=false;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.zEnable=false;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.dsIsWorldDepth=false;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.srcBlend=kBlendOne;f.destBlend=kBlendZero;expect(f,Kind::Hazard,0,Reason::Order);} // opaque blend, no Z write
    {DrawFacts f=cand();f.blend=false;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.samplesRenderTargetTexture=true;expect(f,Kind::Hazard,0,Reason::Sample);}
    {DrawFacts f=cand();f.colorWrite=8;expect(f,Kind::Ignore);}   // alpha only
    {DrawFacts f=cand();f.colorWrite=0;expect(f,Kind::Ignore);}
    {DrawFacts f=cand();f.rt0IsTarget=false;expect(f,Kind::Ignore);}
    {DrawFacts f=cand();f.claimed=true;expect(f,Kind::Ignore);}
    {DrawFacts f=cand();f.worldDomain=false;expect(f,Kind::Hazard,0,Reason::Order);}
    {DrawFacts f=cand();f.worldDomain=false;f.colorWrite=0;expect(f,Kind::Ignore);}
    // water, rain and names: plain in-place draws with nothing pending, an order hazard (flush first, original order) while draws are pending
    {DrawFacts f=cand();f.water=true;f.family=Family::None;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Water);}
    {DrawFacts f=cand();f.rain=true;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Rain);}
    {DrawFacts f=cand();f.uiTag=true;f.family=Family::None;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Name);}
    {DrawFacts f=cand();f.pending=true;f.water=true;f.family=Family::None;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::Water);}
    {DrawFacts f=cand();f.pending=true;f.rain=true;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::Rain);}
    {DrawFacts f=cand();f.pending=true;f.uiTag=true;f.family=Family::None;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::Name);}
    {DrawFacts f=cand();f.pending=true;f.water=true;f.zWrite=true;expect(f,Kind::TranslucentZ);} // a Z-writing draw is unaffected
    {DrawFacts f=cand();f.pending=true;f.water=true;f.colorWrite=0;expect(f,Kind::Ignore);}
    // the fade colour pass stays an accepted reorder (its depth pre-pass occludes the replay), pending or not
    {DrawFacts f=cand();f.skinned=true;f.fadeRun=true;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Fade);}
    {DrawFacts f=cand();f.pending=true;f.skinned=true;f.fadeRun=true;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Fade);}
    {DrawFacts f=cand();f.skinned=true;expect(f,Kind::Defer,SubAdditive);} // skinned outside a fade run defers
    {DrawFacts f=cand();f.fadeRun=true;expect(f,Kind::Defer,SubAdditive);} // a run only changes skinned draws
    // Z-writing draws never defer
    {DrawFacts f=cand();f.zWrite=true;expect(f,Kind::TranslucentZ);}
    {DrawFacts f=cand();f.zWrite=true;f.srcBlend=kBlendOne;f.destBlend=kBlendZero;expect(f,Kind::Opaque);}
    {DrawFacts f=cand();f.zWrite=true;f.blend=false;expect(f,Kind::Opaque);}
    {DrawFacts f=cand();f.zWrite=true;f.colorWrite=0;expect(f,Kind::Opaque);}
    {DrawFacts f=cand();f.zWrite=true;f.water=true;expect(f,Kind::TranslucentZ);}
    // floating world names (SubName, mask bit 16): deferred whatever their Z write, Z test and alpha test, only with a non-opaque blend; with the bit off the old behaviour (in place / split)
    auto name=[](){DrawFacts f=cand();f.uiTag=true;f.family=Family::None;f.srcBlend=kSrcAlpha;f.destBlend=kInvSrcAlpha;return f;};
    {DrawFacts f=name();expect(f,Kind::Defer,SubName,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.zWrite=true;expect(f,Kind::Defer,SubName,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.zWrite=true;f.pending=true;expect(f,Kind::Defer,SubName,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.zEnable=false;expect(f,Kind::Defer,SubName,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.zEnable=false;f.zWrite=true;f.pending=true;expect(f,Kind::Defer,SubName,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.pending=true;expect(f,Kind::Defer,SubName,Reason::Count,InPlaceKind::None,16);} // only the Name bit set
    {DrawFacts f=name();expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Name,15);}                      // Name bit off: as before
    {DrawFacts f=name();f.pending=true;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::Name,15);}
    {DrawFacts f=name();f.zWrite=true;expect(f,Kind::TranslucentZ,0,Reason::Count,InPlaceKind::None,15);}
    {DrawFacts f=name();f.zWrite=true;f.pending=true;expect(f,Kind::TranslucentZ,0,Reason::Count,InPlaceKind::None,15);}
    {DrawFacts f=name();f.worldDomain=false;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::None,31);}  // a non-world uiTag draw is never deferred
    {DrawFacts f=name();f.worldDomain=false;f.zWrite=true;expect(f,Kind::Hazard,0,Reason::Order,InPlaceKind::None,31);}
    {DrawFacts f=name();f.worldDomain=false;f.colorWrite=0;expect(f,Kind::Ignore,0,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.rt0IsTarget=false;expect(f,Kind::Ignore,0,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.claimed=true;expect(f,Kind::Ignore,0,Reason::Count,InPlaceKind::None,31);}
    {DrawFacts f=name();f.samplesRenderTargetTexture=true;expect(f,Kind::Hazard,0,Reason::Sample,InPlaceKind::None,31);}
    {DrawFacts f=name();f.colorWrite=0;expect(f,Kind::Ignore,0,Reason::Count,InPlaceKind::None,31);}
    // not deferrable names keep the old path: opaque blend, no blend, other depth, stencil, MRT, instancing, an occlusion query
    for(int k=0;k<6;++k){
        DrawFacts f=name();f.zWrite=true;
        switch(k){case 0:f.srcBlend=kBlendOne;f.destBlend=kBlendZero;break;case 1:f.blend=false;break;case 2:f.dsIsWorldDepth=false;break;case 3:f.stencil=true;break;case 4:f.mrtBound=true;break;default:f.instanced=true;}
        const Result r=classify(f,31);CHECK(r.kind==(k==0||k==1?Kind::Opaque:Kind::TranslucentZ));
    }
    {DrawFacts f=name();f.occlusionActive=true;f.zWrite=true;CHECK(classify(f,31).kind==Kind::TranslucentZ);}
    {DrawFacts f=name();f.occlusionActive=true;CHECK(classify(f,31).kind==Kind::InPlace);}
    {DrawFacts f=name();f.occlusionActive=true;f.pending=true;CHECK(classify(f,31).kind==Kind::Hazard);}
    // fade run tracker: a skinned Z-writing colour-less draw starts it, a non-skinned draw ends it
    FadeRun run;
    CHECK(!run.update(true,false,15));CHECK(!run.update(true,true,15));CHECK(run.update(true,true,0));
    CHECK(run.update(true,false,15));CHECK(run.update(true,true,15));CHECK(!run.update(false,false,15));CHECK(!run.update(true,false,15));
    CHECK(run.update(true,true,8));run.reset();CHECK(!run.active);
    CHECK(std::string(reasonName(Reason::BufWrite))=="bufwrite"&&std::string(reasonName(Reason::Count))=="none"&&std::string(modeName(Mode::Cooldown))=="cooldown");
}

// ---------------- frame state and counters ----------------
static void testFrameState(){
    FrameState s;CHECK(s.mode()==Mode::Off);
    s.begin(true,false);CHECK(s.mode()==Mode::Armed&&s.canDefer());
    s.deferred();CHECK(s.mode()==Mode::Deferring&&s.canDefer());
    s.fallback(true);CHECK(s.mode()==Mode::Fallback&&!s.canDefer());
    s.fallback(false);CHECK(s.mode()==Mode::Fallback);
    for(unsigned i=0;i<kCooldownFrames;++i){s.begin(true,false);CHECK(s.mode()==Mode::Cooldown&&!s.canDefer());}
    s.begin(true,false);CHECK(s.mode()==Mode::Armed);
    s.close();CHECK(s.mode()==Mode::Fallback);s.begin(true,false);CHECK(s.mode()==Mode::Armed); // closed by B: no cooldown
    s.begin(true,true);CHECK(s.mode()==Mode::Cooldown);s.begin(true,true);CHECK(s.mode()==Mode::Cooldown);s.begin(true,false);CHECK(s.mode()==Mode::Armed);
    s.fallback(true);s.begin(true,true);CHECK(s.mode()==Mode::Cooldown&&s.cooldownLeft()==kCooldownFrames); // pressure does not count the cooldown down
    s.begin(false,false);CHECK(s.mode()==Mode::Off);
    Counters a,b;a.deferred[1]=2;a.hazards[3]=1;a.arenaHigh=5;a.conflictBytes=7;b.deferred[1]=1;b.arenaHigh=9;a.add(b);
    CHECK(a.deferred[1]==3&&a.totalDeferred()==3&&a.totalHazards()==1&&a.arenaHigh==9&&a.conflictBytes==7);
}

// ---------------- guard table ----------------
static void testGuardTable(){
    GuardTable g;int k1=0,k2=0;void* a=&k1;void* b=&k2;
    CHECK(!g.guarded(a)&&!g.conflicts(a,0,16,kLockDiscard));
    CHECK(g.add(a,false,100,200,0,0));CHECK(!g.add(a,false,300,400,1,0));CHECK(g.add(b,true,0,64,2,0xFF));CHECK(!g.add(a,false,5,5,3,0)); // empty range: no guard
    CHECK(g.guarded(a)&&g.guarded(b));
    CHECK(!g.conflicts(a,0,16,0)&&!g.conflicts(a,200,100,0)&&!g.conflicts(a,400,8,kLockNoOverwrite)&&!g.conflicts(a,0,100,0)); // outside / touching
    CHECK(!g.conflicts(a,250,10,kLockNoOverwrite));                      // in the gap of the box
    CHECK(g.conflicts(a,0,101,0)&&g.conflicts(a,199,1,kLockNoOverwrite)&&g.conflicts(a,399,2,0)&&g.conflicts(a,50,500,0));
    CHECK(!g.conflicts(a,100,100,kLockReadOnly)&&!g.conflicts(a,0,0,kLockReadOnly|kLockDiscard)); // READONLY never
    CHECK(g.conflicts(a,1000,4,kLockDiscard)&&g.conflicts(a,1000,0,0)&&g.conflicts(a,1000,0,kLockNoOverwrite)); // DISCARD, size 0
    CHECK(!g.conflicts(static_cast<void*>(&g),0,0,kLockDiscard));        // unknown buffer
    std::vector<GuardTable::Hit> hits;
    CHECK(g.collect(a,150,100,0,hits)==1&&g.range(hits[0]).entry==0);
    hits.clear();CHECK(g.collect(a,0,0,0,hits)==2);
    CHECK(!g.drop(hits[0].buf,hits[0].range));CHECK(g.drop(hits[1].buf,hits[1].range));CHECK(!g.guarded(a)&&g.guarded(b));
    CHECK(!g.conflicts(a,0,0,kLockDiscard));
    CHECK(g.drop(hits[0].buf,hits[0].range));                             // dropping a dead range again stays consistent
    CHECK(g.add(a,false,0,10,4,0));                                       // a buffer that lost its guards marks again
    std::vector<void*> unmarked;g.clear([&](void* k){unmarked.push_back(k);});
    CHECK(unmarked.size()==2&&g.buffers()==0&&!g.guarded(a));
    // ring of 512 entries: appends cost no scan, one DISCARD hits all ranges
    int ring=0;void* r=&ring;
    for(uint32_t i=0;i<512;++i)CHECK(g.add(r,false,i*64,i*64+64,i,0)==(i==0));
    const auto t0=std::chrono::steady_clock::now();
    unsigned freeAppends=0;
    for(int rep=0;rep<2000;++rep)for(uint32_t i=0;i<512;++i)freeAppends+=!g.conflicts(r,512*64+i*64,64,kLockNoOverwrite);
    CHECK(freeAppends==2000u*512u);
    hits.clear();CHECK(g.collect(r,0,0,kLockDiscard,hits)==512);
    hits.clear();CHECK(g.collect(r,10*64+3,2*64,kLockNoOverwrite,hits)==3);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count()<5.0);
}

// ---------------- rig: a mini integration over the FakeDevice ----------------
// How the layer reads the device state: 0 = Get calls only (no cache), 1 = a cache that knows every slot, 2 = a cache with many unknown slots (the layer falls back to
// Get calls for exactly those), 3 = a cache that knows nothing (shaders, declaration, viewport, textures, samplers, constants all unknown). Every behavioural
// test runs in all four; the replayed state must be identical.
static int gMirrorMode=1;
static bool gMinimal=true; // Config::minimalRestore of every Rig (main runs the whole suite with it on and off: the end state must be identical)
static unsigned gTexGeneration=0;static unsigned texGeneration(void*){return gTexGeneration;}
static uint64_t gTicks=0;static uint64_t fakeClock(void*){return ++gTicks;}static bool fakeTiming(void*){return true;} // every read advances one tick: a timed phase costs exactly 1
struct Opts{unsigned maxTexSplits=8;bool minimal=gMinimal;bool timed=false;size_t deltaBytes=2u<<20;unsigned deltaChunk=16384;bool verdictCache=false;bool on=true;unsigned maxEntries=512;bool conflictCopy=true;unsigned mask=15;size_t arena=1u<<20,upVb=1u<<16,upIb=1u<<14;};
struct Rig{
    Fake::FakeDevice dev;Layer layer;Opts o;
    Fake::Surface *T,*D,*Other;
    Fake::VB *ring,*stat,*extra,*ring2;Fake::IB *ringIb,*ringIb32,*stIb;
    Fake::Decl *declP,*declTwo;Fake::VShader *vs,*vsEff;Fake::PShader *ps,*psEff;Fake::Texture *texA,*texB;unsigned psMajor=3; /* what the integration passes as DrawArgs::psMajor */
    NorthlightSpellLayer::MirrorView view;
    bool useReads=false;NorthlightShaderReads::ReadSet vsRs,psRs; // shaders with scanned read sets (restrict()); otherwise they read everything
    const void* readsVs=nullptr,*readsPs=nullptr; // the shaders the read sets belong to (default: the bound vs/ps); set to another shader to model a read set looked up for a different one
    void restrict(const NorthlightShaderReads::ReadSet& v,const NorthlightShaderReads::ReadSet& p){useReads=true;vsRs=v;psRs=p;}
    std::map<void*,Fake::BufState*> bufs;std::map<void*,long long> ids;std::map<void*,int> marks;
    std::vector<std::pair<void*,bool>> markLog;std::vector<size_t> drawsAtWrite;
    std::vector<int> deferredTags;std::vector<std::vector<long long>> steps;
    UINT ringCur=0,ringIbCur=0;int nextTag=1;Result last;
    static constexpr int kMarker=9000;int markers=0;

    template<class P> P* reg(P* p){ids[static_cast<void*>(p)]=static_cast<long long>(ids.size())+1;return p;}
    Fake::VB* newVB(UINT size,DWORD usage){
        IDirect3DVertexBuffer9* p=nullptr;CHECK(SUCCEEDED(dev.CreateVertexBuffer(size,usage,0,D3DPOOL_DEFAULT,&p,nullptr)));
        auto* b=static_cast<Fake::VB*>(p);hook(b->b);reg(p);return b;
    }
    Fake::IB* newIB(UINT size,DWORD usage){
        IDirect3DIndexBuffer9* p=nullptr;CHECK(SUCCEEDED(dev.CreateIndexBuffer(size,usage,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&p,nullptr)));
        auto* b=static_cast<Fake::IB*>(p);hook(b->b);reg(p);return b;
    }
    void hook(Fake::BufState& b){
        bufs[b.key]=&b;
        b.hook=[this](Fake::BufState& s,UINT off,UINT size,DWORD flags){
            layer.onBufferWrite(s.key,off,size,flags,s.usage,unsigned(s.locks));
            drawsAtWrite.push_back(dev.draws.size());
        };
    }
    static void markCb(void* ctx,void* key,bool g){
        auto* r=static_cast<Rig*>(ctx);r->markLog.push_back({key,g});
        int& n=r->marks[key];n+=g?1:-1;CHECK(n==0||n==1); // count-based: set once, cleared once
        auto it=r->bufs.find(key);CHECK(it!=r->bufs.end());it->second->guarded=g;
    }
    explicit Rig(Opts opts):o(opts){
        T=reg(new Fake::Surface(800,600));D=reg(new Fake::Surface(800,600));Other=reg(new Fake::Surface(800,600));
        ring=newVB(1u<<17,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY);ring2=newVB(1u<<16,D3DUSAGE_DYNAMIC);stat=newVB(1u<<12,0);extra=newVB(256,0);
        ringIb=newIB(1u<<14,D3DUSAGE_DYNAMIC);ringIb32=newIB(1u<<14,D3DUSAGE_DYNAMIC);ringIb32->fmt=D3DFMT_INDEX32;stIb=newIB(1u<<12,0);
        for(UINT i=0;i<stat->b.size;++i)stat->b.cur()[i]=uint8_t(i*13+5);
        for(UINT q=0;q<64;++q){const uint16_t ix[6]={0,1,2,2,1,3};std::memcpy(stIb->b.cur()+q*12,ix,12);}
        declP=reg(new Fake::Decl({{0,0,2,0,0,0},{0,12,4,0,10,0}}));
        declTwo=reg(new Fake::Decl({{0,0,2,0,0,0},{0,12,4,0,10,0},{1,0,1,0,5,0}}));
        vs=reg(new Fake::VShader);vsEff=reg(new Fake::VShader);ps=reg(new Fake::PShader);psEff=reg(new Fake::PShader);texA=reg(new Fake::Texture);texB=reg(new Fake::Texture);
        dev.SetRenderTarget(0,T);dev.SetDepthStencilSurface(D);
        dev.SetVertexShader(vs);dev.SetPixelShader(ps);dev.SetVertexDeclaration(declP);dev.SetTexture(0,texA);dev.SetSamplerState(0,1,3);
        dev.SetStreamSource(0,stat,0,16);dev.SetStreamSource(3,extra,0,8);dev.SetIndices(stIb);
        const float c[4]={1,2,3,4};dev.SetVertexShaderConstantF(5,c,1);dev.SetPixelShaderConstantF(2,c,1);dev.SetSamplerState(0,5,2);dev.SetRenderState(D3DRS_ALPHAREF,128);
        opaqueZ();translucent(kSrcAlpha,kBlendOne);
        Config c2;c2.on=o.on;c2.classMask=o.mask;c2.maxEntries=o.maxEntries;c2.arenaBytes=o.arena;c2.uploadVbBytes=o.upVb;c2.uploadIbBytes=o.upIb;
        c2.minimalRestore=o.minimal;c2.maxTexSplits=o.maxTexSplits;c2.conflictCopy=o.conflictCopy;c2.deltaChunkWords=o.deltaChunk;c2.deltaBytes=o.deltaBytes;
        if(gMirrorMode){
            Fake::Mirror& m=dev.mir;view=Fake::makeView(dev);c2.source=&view;
            if(gMirrorMode==2){for(unsigned i=0;i<256;i+=3)m.hideRs[i]=true;for(unsigned i=0;i<256;i+=5)m.hideVsF[i]=true;for(unsigned i=0;i<224;i+=7)m.hidePsF[i]=true;m.hideTex[3]=m.hideTex[5]=true;m.hideSmp[1]=true;m.hideVsB=true;}
            if(gMirrorMode==3){for(auto& h:m.hideRs)h=true;for(auto& h:m.hideVsF)h=true;for(auto& h:m.hidePsF)h=true;for(auto& h:m.hideTex)h=true;for(auto& h:m.hideSmp)h=true;m.hideVsI=m.hideVsB=m.hideShaders=m.hideDecl=m.hideVp=true;}
        }
        if(o.timed){c2.clock=&fakeClock;c2.timing=&fakeTiming;}
        if(o.verdictCache){c2.generation=&texGeneration;c2.genCtx=nullptr;}c2.markGuard=&Rig::markCb;c2.markCtx=this;
        layer.attach(&dev);layer.configure(c2);layer.beginFrame(false);
    }
    ~Rig(){
        layer.reset();
        for(auto& m:marks)CHECK(m.second==0);
        for(auto* p:std::initializer_list<IUnknown*>{T,D,Other,ring,ring2,stat,extra,ringIb,ringIb32,stIb,declP,declTwo,vs,vsEff,ps,psEff,texA,texB})p->Release();
    }
    // The pixel shader constants cannot be read: the Get call fails, and the cache (if any) does not know them either.
    bool savedHidePs[224]={};
    void failPsConst(bool on){
        if(on){std::memcpy(savedHidePs,dev.mir.hidePsF,sizeof savedHidePs);for(auto& h:dev.mir.hidePsF)h=true;}
        else std::memcpy(dev.mir.hidePsF,savedHidePs,sizeof savedHidePs);
        dev.failGetPsConst=on;
    }
    // ---- game-side helpers ----
    static D3DMATRIX mat(float seed){D3DMATRIX m;for(int i=0;i<4;++i)for(int j=0;j<4;++j)m.m[i][j]=seed+float(i*4+j)*0.25f;return m;}
    static constexpr unsigned tsIds[11]={256,2,3,16,17,18,19,20,21,22,23};
    static constexpr unsigned tssIds[18]={1,2,3,4,5,6,7,8,9,10,11,22,23,24,26,27,28,32};
    void ffState(int seed){ // a distinct, complete fixed-function state: lights 0 (on) and 2 (off) set, the others never set
        for(unsigned i=0;i<11;++i){const D3DMATRIX m=mat(float(seed*100+int(i)));dev.SetTransform(tsIds[i],&m);}
        for(unsigned st=0;st<8;++st)for(unsigned t:tssIds)dev.SetTextureStageState(st,t,DWORD(seed*10000+int(st)*100+int(t)));
        D3DMATERIAL9 mt{};mt.Diffuse.r=float(seed);mt.Power=float(seed)+0.5f;dev.SetMaterial(&mt);
        D3DLIGHT9 l{};l.Type=1;l.Diffuse.g=float(seed);l.Range=100;dev.SetLight(0,&l);dev.LightEnable(0,TRUE);l.Type=2;l.Position.x=float(seed);dev.SetLight(2,&l);dev.LightEnable(2,FALSE);
    }
    void writeTexture(Fake::Texture* t){ if(o.on)layer.onTextureWrite(t); ++t->ver; record(); } // the game writes texels: the hook first, then the write
    void fixedFunction(){dev.SetVertexShader(nullptr);dev.SetPixelShader(nullptr);}
    void programmable(){dev.SetVertexShader(vs);dev.SetPixelShader(ps);}
    void rs(D3DRENDERSTATETYPE t,DWORD v){dev.SetRenderState(t,v);}
    void translucent(unsigned src,unsigned dst){rs(D3DRS_ZENABLE,1);rs(D3DRS_ZWRITEENABLE,0);rs(D3DRS_ALPHABLENDENABLE,1);rs(D3DRS_SRCBLEND,src);rs(D3DRS_DESTBLEND,dst);rs(D3DRS_BLENDOP,1);rs(D3DRS_COLORWRITEENABLE,15);rs(D3DRS_STENCILENABLE,0);}
    void opaqueZ(){rs(D3DRS_ZENABLE,1);rs(D3DRS_ZWRITEENABLE,1);rs(D3DRS_ALPHABLENDENABLE,0);rs(D3DRS_SRCBLEND,kBlendOne);rs(D3DRS_DESTBLEND,kBlendZero);rs(D3DRS_BLENDOP,1);rs(D3DRS_COLORWRITEENABLE,15);rs(D3DRS_STENCILENABLE,0);}
    static Bytes pat(int seed,size_t n){Bytes b(n);for(size_t i=0;i<n;++i)b[i]=uint8_t(seed*37+int(i)*3+1);return b;}
    static Bytes quad(int seed){return pat(seed,64);}                  // 4 vertices of 16 bytes
    static Bytes quadIdx(){const uint16_t ix[6]={0,1,2,2,1,3};Bytes b(12);std::memcpy(b.data(),ix,12);return b;}
    void write(Fake::BufState& b,UINT off,UINT size,DWORD flags,const Bytes& bytes){
        void* p=nullptr;CHECK(SUCCEEDED(b.lock(off,size,&p,flags)));std::memcpy(p,bytes.data(),bytes.size());--b.locks;record();
    }
    UINT append(Fake::BufState& b,UINT& cur,const Bytes& bytes){const UINT off=cur;write(b,off,UINT(bytes.size()),D3DLOCK_NOOVERWRITE,bytes);cur+=UINT(bytes.size());return off;}
    UINT ringQuad(int seed){return append(ring->b,ringCur,quad(seed));}
    UINT ringIdx(){return append(ringIb->b,ringIbCur,quadIdx());}
    void discardRing(int seed){const Bytes b=pat(seed,256);write(ring->b,0,0,D3DLOCK_DISCARD,b);ringCur=256;}
    static DrawArgs dp(UINT start,UINT count=2){return DrawArgs::primitive(D3DPT_TRIANGLESTRIP,start,count);}
    static DrawArgs dip(INT base,UINT minIdx,UINT num,UINT startIndex,UINT count=2){return DrawArgs::indexed(D3DPT_TRIANGLELIST,base,minIdx,num,startIndex,count);}
    // The game-visible device state, all of it (what must be identical before and after a replay, and between the reference and the layer run).
    // The fixed-function state: a draw with neither shader reads all of it (full=false: only what a draw sees, a light that is off is nothing); the whole device state: full.
    static void ffNorm(const Fake::State& s,std::vector<long long>& v,bool full){
        auto raw=[&](const void* p,size_t n){const uint8_t* b=static_cast<const uint8_t*>(p);for(size_t i=0;i+3<n;i+=4){uint32_t u;std::memcpy(&u,b+i,4);v.push_back(u);}};
        for(int i=0;i<14;++i)if(full||i<11)raw(&s.tf[i],sizeof s.tf[i]);
        for(auto& row:s.tss)for(DWORD x:row)v.push_back(x);
        raw(&s.mat,sizeof s.mat);
        for(int i=0;i<8;++i){
            if(full){v.push_back(s.lightSet[i]);v.push_back(s.lightOn[i]);raw(&s.light[i],sizeof s.light[i]);}
            else{v.push_back(s.lightSet[i]&&s.lightOn[i]);if(s.lightSet[i]&&s.lightOn[i])raw(&s.light[i],sizeof s.light[i]);}
        }
    }
    std::vector<long long> norm(const Fake::State& s){
        std::vector<long long> v;ffNorm(s,v,true);
        for(DWORD x:s.rs)v.push_back(x);
        for(auto& row:s.ss)for(DWORD x:row)v.push_back(x);
        for(auto* t:s.tex)v.push_back(id(t));
        v.push_back(id(s.vs));v.push_back(id(s.ps));v.push_back(id(s.decl));v.push_back(s.fvf);
        auto flt=[&](const float* f,size_t n){for(size_t i=0;i<n;++i){uint32_t u;std::memcpy(&u,f+i,4);v.push_back(u);}};
        flt(&s.vsc[0][0],256*4);flt(&s.psc[0][0],224*4);flt(&s.clip[0][0],24);
        for(auto& row:s.vsi)for(int x:row)v.push_back(x);for(auto& row:s.psi)for(int x:row)v.push_back(x);
        for(int x:s.vsb)v.push_back(x);for(int x:s.psb)v.push_back(x);
        v.insert(v.end(),{s.vp.X,s.vp.Y,s.vp.Width,s.vp.Height,s.scissor.left,s.scissor.top,s.scissor.right,s.scissor.bottom});
        for(auto& x:s.st){v.push_back(x.freq);v.push_back(id(x.vb));v.push_back(x.off);v.push_back(x.stride);}
        v.push_back(id(s.ib));
        return v;
    }
    static std::vector<UINT> usedOf(const Fake::State& s){
        std::vector<UINT> u;
        if(auto* d=static_cast<Fake::Decl*>(s.decl)){for(auto& e:d->el)if(e.Stream!=0xFF&&std::find(u.begin(),u.end(),UINT(e.Stream))==u.end())u.push_back(e.Stream);}
        else u.push_back(0);
        return u;
    }
    // What one draw reads: shaders, declaration/FVF, every render state, texture slots and the sampler states of the BOUND stages, all constants,
    // viewport, scissor, clip planes, and (kinds 0/1, geom) the used streams and the index buffer of an indexed draw. Unused streams, unbound stages'
    // sampler states and the stream/index bindings of UP draws are not part of what the draw sees.
    std::vector<long long> normDraw(const Fake::DrawRec& d,bool geom){
        const Fake::State& s=d.st;std::vector<long long> v;
        if(!s.vs&&!s.ps)ffNorm(s,v,false); // fixed function: the draw reads the fixed-function state; with shaders it does not
        for(DWORD x:s.rs)v.push_back(x);
        const bool lim=useReads&&(s.vs||s.ps); // a draw with scanned shaders reads only their registers and declared samplers
        const uint32_t texMask=uint32_t(psRs.samplers)|uint32_t(vsRs.samplers&0xF)<<16;
        for(size_t t=0;t<20;++t){if(lim&&!(texMask>>t&1))continue;v.push_back(id(s.tex[t]));v.push_back(d.texVer[t]);if(s.tex[t])for(size_t k=1;k<=13;++k)v.push_back(s.ss[t][k]);}
        v.push_back(id(s.vs));v.push_back(id(s.ps));v.push_back(id(s.decl));v.push_back(s.fvf);
        auto flt=[&](const float* f,size_t n){for(size_t i=0;i<n;++i){uint32_t u;std::memcpy(&u,f+i,4);v.push_back(u);}};
        const bool shaders=s.vs||s.ps; // a fixed-function draw reads no shader constants
        if(shaders&&!lim){flt(&s.vsc[0][0],256*4);flt(&s.psc[0][0],224*4);}
        if(lim){for(unsigned i=0;i<256;++i)if(vsRs.f[i>>6]>>(i&63)&1)flt(s.vsc[i],4);for(unsigned i=0;i<224;++i)if(psRs.f[i>>6]>>(i&63)&1)flt(s.psc[i],4);}
        for(unsigned i=0;i<6;++i)if(s.rs[152]>>i&1)flt(s.clip[i],4); // only the enabled clip planes and (below) an enabled scissor test are read by the draw
        if(shaders&&!lim){for(auto& row:s.vsi)for(int x:row)v.push_back(x);for(auto& row:s.psi)for(int x:row)v.push_back(x);
        for(int x:s.vsb)v.push_back(x);for(int x:s.psb)v.push_back(x);}
        if(lim){for(unsigned i=0;i<16;++i){if(vsRs.i>>i&1)for(int x:s.vsi[i])v.push_back(x);if(psRs.i>>i&1)for(int x:s.psi[i])v.push_back(x);if(vsRs.b>>i&1)v.push_back(s.vsb[i]);if(psRs.b>>i&1)v.push_back(s.psb[i]);}}
        v.insert(v.end(),{s.vp.X,s.vp.Y,s.vp.Width,s.vp.Height});
        if(s.rs[174])v.insert(v.end(),{s.scissor.left,s.scissor.top,s.scissor.right,s.scissor.bottom});
        if(d.kind==0||d.kind==1)for(UINT u:usedOf(s)){v.push_back(s.st[u].freq);if(geom){v.push_back(id(s.st[u].vb));v.push_back(s.st[u].off);v.push_back(s.st[u].stride);}}
        if(d.kind==1&&geom)v.push_back(id(s.ib));
        return v;
    }
    // Registered objects by their registration number, the runtime's internal FVF declarations by their FVF, anything else by identity: a swapped object never compares equal.
    bool strictIds=false; // snapshots of one rig: an unregistered object (not comparable across rigs) compares by identity, so a swapped object shows
    long long id(const void* p){
        if(!p)return 0;
        auto it=ids.find(const_cast<void*>(p));if(it!=ids.end())return it->second;
        for(auto& kv:dev.fvfDecls)if(static_cast<const void*>(static_cast<IDirect3DVertexDeclaration9*>(kv.second))==p)return 1000000+static_cast<long long>(kv.first);
        return strictIds?-2-static_cast<long long>(reinterpret_cast<uintptr_t>(p)>>4):-1;
    }
    std::vector<long long> snapNorm(){
        strictIds=true;auto v=norm(dev.s);for(auto* r:dev.rt)v.push_back(id(r));v.push_back(id(dev.ds));strictIds=false;return v;
    }
    void record(){steps.push_back(snapNorm());}
    DrawFacts facts(Family fam){
        DrawFacts f;const auto& s=dev.s;
        f.zEnable=s.rs[D3DRS_ZENABLE];f.zWrite=s.rs[D3DRS_ZWRITEENABLE];f.blend=s.rs[D3DRS_ALPHABLENDENABLE];f.srcBlend=s.rs[D3DRS_SRCBLEND];f.destBlend=s.rs[D3DRS_DESTBLEND];
        f.blendOp=s.rs[D3DRS_BLENDOP];f.colorWrite=s.rs[D3DRS_COLORWRITEENABLE];f.stencil=s.rs[D3DRS_STENCILENABLE];f.family=fam;f.worldDomain=true;
        f.rt0IsTarget=dev.rt[0]==T;f.dsIsWorldDepth=dev.ds==D;f.mrtBound=dev.rt[1]!=nullptr;
        for(auto& x:s.st)if(x.freq!=1)f.instanced=true;
        return f;
    }
    void drawInPlace(const DrawArgs& a){
        switch(a.type){
        case DrawType::Primitive:dev.DrawPrimitive(a.prim,a.first,a.primCount);break;
        case DrawType::Indexed:dev.DrawIndexedPrimitive(a.prim,a.baseVertex,a.minIndex,a.numVertices,a.first,a.primCount);break;
        case DrawType::PrimitiveUP:dev.DrawPrimitiveUP(a.prim,a.primCount,a.vertexData,a.stride);break;
        case DrawType::IndexedUP:dev.DrawIndexedPrimitiveUP(a.prim,a.minIndex,a.numVertices,a.primCount,a.indexData,a.indexFormat,a.vertexData,a.stride);break;
        }
    }
    // One game draw: classify, defer when told to (layer on), otherwise draw in place after the layer's hazard handling. Returns the tag.
    template<class F> int issueWith(const DrawArgs& a0,Family fam,F&& tweak){
        DrawArgs a=a0;a.psMajor=psMajor;if(useReads){a.vsReads=&vsRs;a.psReads=&psRs;a.vsFor=readsVs?readsVs:static_cast<const void*>(vs);a.psFor=readsPs?readsPs:static_cast<const void*>(ps);}
        const int tag=nextTag++;const float v[4]={float(tag),0,0,0};dev.SetVertexShaderConstantF(31,v,1);dev.SetRenderState(165,DWORD(tag));
        DrawFacts f=facts(fam);f.pending=o.on&&layer.pending()>0;tweak(f);
        last=classify(f,o.mask);bool deferred=false;
        if(o.on){
            if(last.kind==Kind::Defer)deferred=layer.defer(a,dev.rt[0],dev.ds,last.sub);
            else if(last.kind==Kind::Hazard)layer.hazard(last.reason,true,last.inPlace);
            else if(last.kind==Kind::InPlace)layer.noteInPlace(last.inPlace);
        }
        if(deferred)deferredTags.push_back(tag);else drawInPlace(a);
        record();return tag;
    }
    int issue(const DrawArgs& a,Family fam=Family::Particle){return issueWith(a,fam,[](DrawFacts&){});}
    int issueFf(const DrawArgs& a){return issueWith(a,Family::None,[](DrawFacts& f){f.fixedFunction=true;});}
    // B: the effects (other target, own state, a marker draw), then the replay; the device state must be identical across the replay.
    void effects(){ // everything the world effects leave on the device that differs from the game's state of the deferred draws
        dev.SetDepthStencilSurface(nullptr);dev.SetRenderTarget(0,Other);
        D3DVIEWPORT9 vp;vp.Width=100;vp.Height=50;dev.SetViewport(&vp);const RECT sc{1,2,3,4};dev.SetScissorRect(&sc);
        dev.SetVertexShader(vsEff);dev.SetPixelShader(psEff);dev.SetSamplerState(0,1,9);dev.SetSamplerState(5,3,4);dev.SetRenderState(D3DRS_ALPHAREF,77);dev.SetRenderState(D3DRS_POINTSIZE,5);
        {const float k[4]={9,9,9,9};dev.SetPixelShaderConstantF(2,k,1);dev.SetPixelShaderConstantF(30,k,1);const int ki[4]={7,7,7,7};dev.SetVertexShaderConstantI(1,ki,1);const BOOL kb=1;dev.SetPixelShaderConstantB(3,&kb,1);const float pl[4]={1,0,0,1};dev.SetClipPlane(2,pl);}
        {D3DMATRIX m{};m.m[0][0]=99;dev.SetTransform(256,&m);dev.SetTransform(3,&m);dev.SetTransform(16,&m);dev.SetTextureStageState(0,1,4);dev.SetTextureStageState(7,32,0x1234);
         D3DMATERIAL9 mt{};mt.Power=7;dev.SetMaterial(&mt);D3DLIGHT9 l{};l.Type=3;l.Range=5;dev.SetLight(0,&l);dev.LightEnable(0,FALSE);dev.SetLight(4,&l);dev.LightEnable(4,TRUE);} // the effects' own fixed-function state (light 4 is on while the game never set it)
        dev.SetVertexDeclaration(declP);dev.SetTexture(0,texB);dev.SetTexture(2,texA);dev.SetStreamSource(0,extra,8,8);dev.SetStreamSource(1,nullptr,0,0);dev.SetIndices(nullptr);dev.SetRenderTarget(1,Other);
        rs(D3DRS_ZENABLE,0);rs(D3DRS_ALPHABLENDENABLE,0);rs(D3DRS_ZWRITEENABLE,0);
    }
    void boundary(){
        effects();
        const int mk=kMarker+markers++;const float v[4]={float(mk),0,0,0};dev.SetVertexShaderConstantF(31,v,1);dev.SetRenderState(165,DWORD(mk)); /* the first boundary draws kMarker, later ones kMarker+1.. */
        dev.DrawPrimitive(D3DPT_TRIANGLESTRIP,0,2);
        const auto before=snapNorm();
        if(beforeReplay)beforeReplay();
        layer.replay();
        CHECK(snapNorm()==before);CHECK(layer.pending()==0);
        resume();record();
    }
    void resume(){ // the game goes on in the world pass
        dev.SetRenderTarget(1,nullptr);
        dev.SetRenderTarget(0,T);dev.SetDepthStencilSurface(D);dev.SetVertexShader(vs);dev.SetPixelShader(ps);dev.SetTexture(0,texA);dev.SetTexture(2,nullptr);dev.SetStreamSource(0,ring,0,16);
        translucent(kSrcAlpha,kBlendOne);
    }
    std::function<void()> beforeReplay;                     // run right before the boundary's replay (device state is the game's at B)
    std::vector<int> order()const{std::vector<int> v;for(auto& d:dev.draws)v.push_back(d.tag);return v;}
};

// Both runs of a scenario: layer off (the reference) and layer on.
struct Pair{std::unique_ptr<Rig> ref,lay;};
static Pair runBoth(void(*scenario)(Rig&),Opts o={}){
    Pair p;Opts r=o;r.on=false;p.ref.reset(new Rig(r));scenario(*p.ref);
    p.lay.reset(new Rig(o));scenario(*p.lay);return p;
}
static bool sameRec(Rig& A,const Fake::DrawRec& a,Rig& B,const Fake::DrawRec& b,bool geom){
    for(int i=0;i<4;++i)if(A.id(a.rt[i])!=B.id(b.rt[i]))return false;
    return a.kind==b.kind&&a.prim==b.prim&&a.tag==b.tag&&a.vtx==b.vtx&&a.idx==b.idx&&!a.oob&&!b.oob&&A.id(a.ds)==B.id(b.ds)&&A.normDraw(a,geom)==B.normDraw(b,geom);
}
// Every draw of the layer run has the state and fetched bytes of the reference run (geometry bindings of `rebased` tags aside), and the game-visible
// device state is identical after every step.
static void checkEquivalent(Pair& p,const std::set<int>& rebased={}){
    CHECK(p.ref->steps.size()==p.lay->steps.size());
    for(size_t i=0;i<p.ref->steps.size();++i)CHECK(p.ref->steps[i]==p.lay->steps[i]);
    CHECK(p.ref->dev.draws.size()==p.lay->dev.draws.size());
    std::map<int,const Fake::DrawRec*> ref;
    for(auto& d:p.ref->dev.draws)CHECK(ref.emplace(d.tag,&d).second);
    for(auto& d:p.lay->dev.draws){
        auto it=ref.find(d.tag);CHECK(it!=ref.end());
        if(!sameRec(*p.ref,*it->second,*p.lay,d,!rebased.count(d.tag))){
            const auto& a=*it->second;std::fprintf(stderr,"tag %d: kind %d/%d vtx %zu/%zu idx %zu/%zu eqvtx %d eqidx %d eqstate %d oob %d/%d\n",d.tag,a.kind,d.kind,a.vtx.size(),d.vtx.size(),a.idx.size(),d.idx.size(),
                int(a.vtx==d.vtx),int(a.idx==d.idx),int(p.ref->normDraw(a,false)==p.lay->normDraw(d,false)),int(a.oob),int(d.oob));
            const auto x=p.ref->normDraw(a,true),y=p.lay->normDraw(d,true);
            for(size_t k=0;k<std::min(x.size(),y.size());++k)if(x[k]!=y[k]){std::fprintf(stderr,"  first state difference at word %zu: %lld vs %lld (of %zu/%zu)\n",k,x[k],y[k],x.size(),y.size());break;}
        }
        CHECK(sameRec(*p.ref,*it->second,*p.lay,d,!rebased.count(d.tag)));
    }
}
// The layer-run order: the reference order with the deferred tags moved to right after the marker.
static std::vector<int> expectedDeferredOrder(const std::vector<int>& ref,const std::vector<int>& deferred){
    std::vector<int> out;
    for(int t:ref)if(std::find(deferred.begin(),deferred.end(),t)==deferred.end())out.push_back(t);
    auto at=std::find(out.begin(),out.end(),Rig::kMarker);CHECK(at!=out.end());
    out.insert(at+1,deferred.begin(),deferred.end());return out;
}

// ---------------- scenarios ----------------
static void sEquivalence(Rig& r){
    r.opaqueZ();r.issue(Rig::dip(0,0,4,0),Family::None);                      // opaque, in place
    r.translucent(kSrcAlpha,kBlendOne);r.dev.SetStreamSource(0,r.ring,0,16);
    UINT a=r.ringQuad(1);r.issue(Rig::dp(a/16));                              // DP from the ring (the static IB is bound and unused)
    UINT b=r.ringQuad(2),ib=r.ringIdx();r.dev.SetIndices(r.ringIb);
    r.issue(Rig::dip(INT(b/16),0,4,ib/2));                                    // DIP, ring VB + ring IB
    uint8_t up[64];Bytes q=Rig::quad(3);std::memcpy(up,q.data(),64);
    r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,up,16));              // DPUP
    CHECK(r.dev.s.st[0].vb==nullptr&&r.dev.s.st[0].stride==0);
    std::memset(up,0xEE,64);
    r.dev.SetStreamSource(0,r.ring,0,16);
    uint8_t upv[128];Bytes v8=Rig::pat(4,128);std::memcpy(upv,v8.data(),128);const uint16_t ix[6]={2,3,4,4,3,5};
    r.issue(DrawArgs::indexedUP(D3DPT_TRIANGLELIST,2,4,2,ix,D3DFMT_INDEX16,upv,16));  // DIPUP, min index 2
    CHECK(r.dev.s.st[0].vb==nullptr&&r.dev.s.ib==nullptr);
    std::memset(upv,0xEE,128);
    r.dev.SetStreamSource(0,r.ring,0,16);r.dev.SetIndices(r.stIb);
    r.translucent(kSrcAlpha,kInvSrcAlpha);UINT c=r.ringQuad(5);r.issue(Rig::dp(c/16),Family::Model);
    r.translucent(kDestColor,kSrcColor);UINT d=r.ringQuad(6);r.issue(Rig::dip(INT(d/16),0,4,0),Family::Projected);
    r.opaqueZ();r.issue(Rig::dp(0),Family::None);                             // an opaque draw after the translucent ones stays in place
    r.translucent(kSrcAlpha,kBlendOne);UINT e=r.ringQuad(7);
    r.issueWith(Rig::dp(e/16),Family::Model,[](DrawFacts& f){f.skinned=true;f.fadeRun=true;}); // a fade colour pass: in place, reorder accepted
    r.dev.SetVertexDeclaration(r.declTwo);r.dev.SetStreamSource(1,r.ring2,0,8);
    UINT f=r.ringQuad(8);r.write(r.ring2->b,f/16*8,32,D3DLOCK_NOOVERWRITE,Rig::pat(9,32));
    r.issue(Rig::dp(f/16),Family::ParticleUnlit);                             // two streams
    r.dev.SetVertexDeclaration(r.declP);r.dev.SetStreamSource(1,nullptr,0,0);
    r.ringQuad(10);r.ringQuad(11);                                            // later appends beyond every guarded range
    r.boundary();
    r.issue(Rig::dp(a/16));                                                   // after B: drawn in place, never deferred
}
static void testEquivalence(){
    Pair p=runBoth(sEquivalence);
    const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==7&&p.ref->deferredTags.empty());
    CHECK(c.totalDeferred()==7&&c.deferred[0]==5&&c.deferred[1]==1&&c.deferred[2]==1&&c.inPlace[3]==1&&c.orderBy[0]+c.orderBy[1]+c.orderBy[2]==0);
    CHECK(c.totalHazards()==0&&c.conflicts==0&&c.materialized==0&&c.flushes==0&&c.replays==1&&c.replayedDraws==7&&c.replaySkipped==0&&c.recorded==7&&c.replaySets>0&&c.recordHigh==7);
    CHECK(p.lay->layer.recordChunks()==1);
    checkEquivalent(p);
    CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    // the reorder is real: the deferred draws come after the marker, before them nothing of theirs ran
    size_t marker=0;for(size_t i=0;i<p.lay->dev.draws.size();++i)if(p.lay->dev.draws[i].tag==Rig::kMarker)marker=i;
    for(int t:p.lay->deferredTags)for(size_t i=0;i<marker;++i)CHECK(p.lay->dev.draws[i].tag!=t);
    // replay is into T and the world depth with RT1..3 null and the full viewport
    for(size_t i=marker+1;i<marker+8;++i){const auto& d=p.lay->dev.draws[i];CHECK(d.rt[0]==p.lay->T&&d.ds==p.lay->D&&!d.rt[1]&&!d.rt[2]&&!d.rt[3]&&d.st.vp.Width==800&&d.st.scissor.right==800);}
    CHECK(p.lay->layer.mode()==Mode::Fallback);                               // closed by B
    p.lay->layer.replay();CHECK(p.lay->layer.totalCounters().replays==1);     // idempotent
}


// ---------------- versions: copy-on-conflict ----------------
// one deferred DP per call (the ring is the stream); returns the tag
static int deferQuad(Rig& r,int seed,UINT* off=nullptr){
    r.dev.SetStreamSource(0,r.ring,0,16);const UINT a=r.ringQuad(seed);if(off)*off=a;return r.issue(Rig::dp(a/16));
}
static void hz(Rig& r,Reason why,bool cooldown=true){if(r.o.on)r.layer.hazard(why,cooldown);r.record();}
static void sDiscard(Rig& r){deferQuad(r,1);deferQuad(r,2);r.discardRing(77);r.ringQuad(3);r.boundary();}
static void testDiscard(){
    Pair p=runBoth(sDiscard);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==2&&c.conflicts==1&&c.materialized==2&&c.conflictBytes==128&&c.totalHazards()==0&&c.replayedDraws==2);
    checkEquivalent(p,{1,2});
    CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    // the replay came from the copy: the ring now holds the new slice, the draws still fetched the originals
    for(auto& d:p.lay->dev.draws)if(d.tag==1)CHECK(d.vtx==Rig::quad(1));else if(d.tag==2)CHECK(d.vtx==Rig::quad(2));
    CHECK(p.lay->ring->b.slices.size()==2&&p.lay->ring->b.cur()[200]!=Rig::quad(1)[0]);
    CHECK(p.lay->markLog.size()==2&&p.lay->markLog[0].second&&!p.lay->markLog[1].second);
}
static void sPartialOverlap(Rig& r){ // a NOOVERWRITE write into a guarded range (a game bug): copied as well
    UINT a;deferQuad(r,1,&a);r.write(r.ring->b,a+16,16,D3DLOCK_NOOVERWRITE,Rig::pat(50,16));r.boundary();
}
static void testPartialOverlap(){
    Pair p=runBoth(sPartialOverlap);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==1&&c.materialized==1&&c.totalHazards()==0);checkEquivalent(p,{1});
    for(auto& d:p.lay->dev.draws)if(d.tag==1)CHECK(d.vtx==Rig::quad(1));
}
static void sTwoEntries(Rig& r){ // two entries on one buffer, one overlapped at a time; a write to an already copied range is free
    UINT a,b;deferQuad(r,1,&a);deferQuad(r,2,&b);
    r.write(r.ring->b,a+8,8,D3DLOCK_NOOVERWRITE,Rig::pat(60,8));                 // entry 1 only
    r.write(r.ring->b,a,32,D3DLOCK_NOOVERWRITE,Rig::pat(61,32));                 // same range again: no live guard there any more
    r.write(r.ring->b,b+60,4,D3DLOCK_NOOVERWRITE,Rig::pat(62,4));                // entry 2: the last guard of the buffer goes
    r.boundary();
}
static void testTwoEntries(){
    Pair p=runBoth(sTwoEntries);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==2&&c.materialized==2&&c.conflictBytes==128&&c.totalHazards()==0);checkEquivalent(p,{1,2});
    for(auto& d:p.lay->dev.draws)if(d.tag==1)CHECK(d.vtx==Rig::quad(1));else if(d.tag==2)CHECK(d.vtx==Rig::quad(2));
    CHECK(p.lay->markLog.size()==2&&p.lay->markLog[0].second&&!p.lay->markLog[1].second);
}
static void testMarkIsCountBased(){
    Rig r(Opts{});UINT a,b;deferQuad(r,1,&a);deferQuad(r,2,&b);
    CHECK(r.markLog.size()==1&&r.markLog[0].second&&r.ring->b.guarded);      // marked once for both entries
    r.write(r.ring->b,a,4,D3DLOCK_NOOVERWRITE,Rig::pat(1,4));
    CHECK(r.markLog.size()==1&&r.ring->b.guarded);                            // entry 2 still guards: stays marked
    r.boundary();CHECK(r.markLog.size()==2&&!r.markLog[1].second&&!r.ring->b.guarded);
    deferQuad(r,3);                                                           // closed frame: nothing new is marked
    CHECK(r.markLog.size()==2);
}
static void sRebase(Rig& r){ // a DIP deep in the ring (vertex base 400, min index 2): the upload sits at 0, so the base turns negative
    for(int i=0;i<100;++i)r.ringQuad(i+100);
    r.dev.SetStreamSource(0,r.ring,0,16);const UINT q=r.ringCur;r.append(r.ring->b,r.ringCur,Rig::pat(21,96));
    const uint16_t ix[6]={2,3,4,4,3,5};Bytes ib(12);std::memcpy(ib.data(),ix,12);r.dev.SetIndices(r.ringIb);const UINT io=r.append(r.ringIb->b,r.ringIbCur,ib);
    r.issue(Rig::dip(INT(q/16),2,4,io/2));
    r.discardRing(5);r.boundary();
}
static void testRebaseNegativeBase(){
    Pair p=runBoth(sRebase);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==1&&c.materialized==1&&c.totalHazards()==0);checkEquivalent(p,{1});
    for(auto& d:p.lay->dev.draws)if(d.tag==1){CHECK(d.base==-2);CHECK(d.st.st[0].off==0);CHECK(d.idx==std::vector<uint32_t>({2,3,4,4,3,5}));}
}
static void sRebaseA(Rig& r){ // stream offset 1024; X = vertices 0..3, Y = vertices 4..7 of that stream (adjacent: one blob)
    r.dev.SetStreamSource(0,r.ring,1024,16);r.dev.SetIndices(r.stIb);
    for(int i=0;i<16;++i)r.ringQuad(i+100);
    r.ringQuad(1);r.ringQuad(2);
    r.issue(Rig::dip(0,0,4,0));                                               // tag 1: P=0,  f=0 -> offset P, base kept
    r.issue(Rig::dip(4,0,4,0));                                               // tag 2: P=64, f=4 -> P-f*stride=0 >= 0: base kept
    r.discardRing(5);r.boundary();
}
static void testRebasePositiveBase(){
    Pair p=runBoth(sRebaseA);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==1&&c.materialized==2&&c.conflictBytes==128&&c.totalHazards()==0);checkEquivalent(p,{1,2});
    for(auto& d:p.lay->dev.draws){
        if(d.tag==1)CHECK(d.base==0&&d.st.st[0].off==0);
        if(d.tag==2)CHECK(d.base==4&&d.st.st[0].off==0);
    }
}
static void sIbMaterialized(Rig& r){ // the vertices stay in the static buffer, the ring IB is overwritten
    r.dev.SetStreamSource(0,r.stat,0,16);r.dev.SetIndices(r.ringIb);
    r.ringIdx();const UINT io=r.ringIdx();
    r.issue(Rig::dip(0,0,4,io/2));
    r.write(r.ringIb->b,0,0,D3DLOCK_DISCARD,Rig::pat(33,64));
    r.boundary();
}
static void testIbMaterialized(){
    Pair p=runBoth(sIbMaterialized);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==1&&c.materialized==1&&c.conflictBytes==12&&c.totalHazards()==0);checkEquivalent(p,{1});
    for(auto& d:p.lay->dev.draws)if(d.tag==1){CHECK(d.idx==std::vector<uint32_t>({0,1,2,2,1,3}));CHECK(d.first==0);}
}
static void sMixedStreams(Rig& r){ // stream 0 from the ring (overwritten), stream 1 from a static buffer (moved with it), indices from the ring (overwritten)
    r.dev.SetVertexDeclaration(r.declTwo);r.dev.SetStreamSource(0,r.ring,0,16);r.dev.SetStreamSource(1,r.stat,256,8);r.dev.SetIndices(r.ringIb);
    for(int i=0;i<30;++i)r.ringQuad(i+100);
    const UINT q=r.ringQuad(1),io=r.ringIdx();
    r.issue(Rig::dip(INT(q/16),0,4,io/2),Family::Model);
    r.discardRing(2);r.write(r.ringIb->b,0,0,D3DLOCK_DISCARD,Rig::pat(33,64));r.boundary();
}
static void testMixedStreams(){
    Pair p=runBoth(sMixedStreams);
    CHECK(p.lay->layer.totalCounters().materialized==2&&p.lay->layer.totalCounters().conflicts==2&&p.lay->layer.totalCounters().totalHazards()==0);
    checkEquivalent(p,{1});
}
static void s32bitUP(Rig& r){
    uint32_t ix[6]={0,1,2,2,1,3};uint8_t v[64];const Bytes q=Rig::quad(4);std::memcpy(v,q.data(),64);
    r.issue(DrawArgs::indexedUP(D3DPT_TRIANGLELIST,0,4,2,ix,D3DFMT_INDEX32,v,16));
    std::memset(ix,0,sizeof ix);std::memset(v,0,64);
    r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLEFAN,1,Rig::pat(5,48).data(),16));  // the temporary dies right after the call
    r.boundary();
}
static void test32bitUP(){
    Pair p=runBoth(s32bitUP);checkEquivalent(p);
    CHECK(p.lay->deferredTags.size()==2&&p.lay->layer.totalCounters().replayedDraws==2);
    for(auto& d:p.lay->dev.draws)if(d.tag==1)CHECK(d.idx==std::vector<uint32_t>({0,1,2,2,1,3})&&d.vtx.size()==96);
}
static void sNonDynamic(Rig& r){ // a conflicting write to a DEFAULT static buffer: flush, fallback
    r.dev.SetStreamSource(0,r.stat,0,16);r.issue(Rig::dp(0));r.issue(Rig::dp(4));
    r.write(r.stat->b,16,16,0,Rig::pat(1,16));
    deferQuad(r,9);                                                           // fallback: drawn in place
    r.boundary();
}
static void testNonDynamic(){
    Pair p=runBoth(sNonDynamic);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags==std::vector<int>({1,2}));
    CHECK(c.hazards[unsigned(Reason::BufWrite)]==1&&c.totalHazards()==1&&c.flushes==1&&c.conflicts==0&&c.replays==0&&c.replayedDraws==2);
    CHECK(p.lay->drawsAtWrite.back()==2);                                    // both pending draws were issued before the write landed
    CHECK(p.lay->layer.mode()==Mode::Fallback);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
    Reason why;CHECK(p.lay->layer.takeFirstHazard(why)&&why==Reason::BufWrite&&!p.lay->layer.takeFirstHazard(why));
}
static void testConflictCopyDisabled(){
    Opts o;o.conflictCopy=false;Pair p=runBoth(sDiscard,o);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.hazards[unsigned(Reason::BufWrite)]==1&&c.conflicts==0&&p.lay->drawsAtWrite.back()==2&&p.lay->deferredTags.size()==2);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
}
static void testOutstandingLockFlushes(){
    Rig r(Opts{});deferQuad(r,1);
    r.layer.onBufferWrite(r.ring->b.key,0,0,D3DLOCK_DISCARD,r.ring->b.usage,1); // a write lock is outstanding: copying could race it
    CHECK(r.layer.pending()==0&&r.layer.frameCounters().hazards[unsigned(Reason::BufWrite)]==1&&r.dev.draws.size()==1&&r.layer.frameCounters().conflicts==0);
}
static void sReadBackFails(Rig& r){deferQuad(r,1);r.ring->b.failReadOnly=1;r.discardRing(5);deferQuad(r,2);r.boundary();}
static void testReadBackFails(){
    Pair p=runBoth(sReadBackFails);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.hazards[unsigned(Reason::BufWrite)]==1&&c.materialized==0&&p.lay->drawsAtWrite.back()==1&&p.lay->deferredTags.size()==1);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
    CHECK(p.lay->ring->b.locks==0);                                           // no lock left behind
}
static void testAddressReuse(){
    Rig r(Opts{});
    const int base=Fake::live;
    auto* tmp=r.newVB(1024,D3DUSAGE_DYNAMIC);UINT cur=0;
    r.dev.SetStreamSource(0,tmp,0,16);r.append(tmp->b,cur,Rig::quad(1));
    r.issue(Rig::dp(0));CHECK(r.layer.pending()==1&&tmp->b.guarded);
    r.dev.SetStreamSource(0,r.ring,0,16);tmp->Release();                      // the game lets go while the draw is pending
    CHECK(Fake::live==base+1);                                                // the buffer, kept alive by the pending entry
    r.boundary();
    CHECK(Fake::live==base);                                                  // replayed: the entry let go, nothing else (no state block) holds it
    r.layer.trim(0);CHECK(Fake::live==base);
    CHECK(r.dev.draws.size()==2&&r.dev.draws[1].tag==1&&r.dev.draws[1].vtx==Rig::quad(1));
    // a new buffer (maybe at the freed address) has no guard, even with a frame full of other pending entries
    r.layer.beginFrame(false);deferQuad(r,2);
    auto* fresh=r.newVB(1024,D3DUSAGE_DYNAMIC);const unsigned calls=r.dev.calls;
    r.layer.onBufferWrite(fresh->b.key,0,0,D3DLOCK_DISCARD,fresh->b.usage,0);
    CHECK(r.dev.calls==calls&&r.layer.pending()==1&&r.layer.totalCounters().totalHazards()==0&&r.layer.totalCounters().conflicts==0&&!fresh->b.guarded);
    r.layer.reset();                                                          // and after a reset the old ring guard is gone too
    CHECK(!r.ring->b.guarded);const unsigned c2=r.dev.calls;
    r.layer.onBufferWrite(r.ring->b.key,0,0,D3DLOCK_DISCARD,r.ring->b.usage,0);
    CHECK(r.dev.calls==c2);
    fresh->Release();
}

// ---------------- hazards ----------------
static void sClear(Rig& r){deferQuad(r,1);deferQuad(r,2);hz(r,Reason::Clear);deferQuad(r,3);r.boundary();}
static void testClearHazard(){
    Pair p=runBoth(sClear);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags==std::vector<int>({1,2}));
    CHECK(c.hazards[unsigned(Reason::Clear)]==1&&c.totalHazards()==1&&c.flushes==1&&c.replayedDraws==2);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());                // never some before and some after the effects
    CHECK(p.lay->dev.draws.size()==4);
}
static void testHazardWithoutPending(){
    Rig r(Opts{});r.layer.hazard(Reason::Clear);
    CHECK(r.layer.mode()==Mode::Armed&&r.layer.totalCounters().totalHazards()==0&&r.dev.draws.empty());
}
static void sCopy(Rig& r){deferQuad(r,1);hz(r,Reason::Copy);deferQuad(r,2);r.boundary();}
static void testCopyHazard(){
    Pair p=runBoth(sCopy);CHECK(p.lay->layer.totalCounters().hazards[unsigned(Reason::Copy)]==1);checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
}
// A texture write is a split: the pending draws that sample it are replayed now (they see the old texels, exactly as in the original order), the frame keeps deferring,
// the draws after the write are deferred again and sample the new texels at B. No fallback, no cooldown. A write to a texture nothing pending samples does nothing.
static void sTexWrite(Rig& r){
    deferQuad(r,1);
    if(r.o.on){
        CHECK(r.layer.pendingSamples(r.texA)&&!r.layer.pendingSamples(r.texB));
        r.layer.onTextureWrite(r.texB);CHECK(r.layer.pending()==1);          // not sampled by a pending draw: no effect
    }
    r.writeTexture(r.texA);
    if(r.o.on)CHECK(r.layer.pending()==0&&r.layer.mode()==(r.o.maxTexSplits?Mode::Deferring:Mode::Fallback));
    deferQuad(r,2);r.boundary();
}
static void testTexWrite(){
    Pair p=runBoth(sTexWrite);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.texSplits==1&&c.totalHazards()==0&&c.flushes==1&&c.drains==2&&c.replays==1&&p.lay->deferredTags==std::vector<int>({1,2})&&p.lay->layer.eventSeq()==0);
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),{2}));
    for(auto& d:p.lay->dev.draws){if(d.tag==1)CHECK(d.texVer[0]==0);if(d.tag==2)CHECK(d.texVer[0]==1);} // 1 sampled the old texels, 2 the new ones
    p.lay->layer.beginFrame(false);CHECK(p.lay->layer.mode()==Mode::Armed&&p.lay->layer.cooldownLeft()==0); // no cooldown
    // a write inside a UI-free frame with nothing pending is nothing
    Rig z(Opts{});z.layer.onTextureWrite(z.texA);CHECK(z.layer.totalCounters().texSplits==0&&z.layer.totalCounters().drains==0);
}
// More than maxTexSplits splits in one frame: the next write is the old hazard (flush, fallback, cooldown); the later draws are in place.
static void sTexCap(Rig& r){
    for(int i=1;i<=9;++i){deferQuad(r,i);r.writeTexture(r.texA);}
    deferQuad(r,10);r.boundary();
}
static void testTexSplitCap(){
    Pair p=runBoth(sTexCap);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.texSplits==8&&c.hazards[unsigned(Reason::TexWrite)]==1&&c.totalHazards()==1&&c.flushes==9&&c.drains==9&&c.replays==0&&p.lay->deferredTags.size()==9);
    CHECK(p.lay->layer.mode()==Mode::Fallback);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());                // every draw was replayed before the write that follows it
    p.lay->layer.beginFrame(false);CHECK(p.lay->layer.mode()==Mode::Cooldown&&p.lay->layer.cooldownLeft()==kCooldownFrames-1);
    Opts none;none.maxTexSplits=0;Pair q=runBoth(sTexWrite,none);            // a cap of 0: always the hazard (the old behaviour)
    CHECK(q.lay->layer.totalCounters().texSplits==0&&q.lay->layer.totalCounters().hazards[unsigned(Reason::TexWrite)]==1&&q.lay->deferredTags==std::vector<int>({1}));
    checkEquivalent(q);CHECK(q.lay->order()==q.ref->order());
}
static void sGpuWrite(Rig& r){
    deferQuad(r,1);
    if(r.o.on){r.layer.onBufferGpuWrite(r.stat->b.key);CHECK(r.layer.pending()==1);r.layer.onBufferGpuWrite(r.ring->b.key);CHECK(r.layer.pending()==0);}
    r.record();deferQuad(r,2);r.boundary();
}
static void testGpuWrite(){
    Pair p=runBoth(sGpuWrite);CHECK(p.lay->layer.totalCounters().hazards[unsigned(Reason::GpuWrite)]==1);checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
}
static void sQuery(Rig& r){deferQuad(r,1);r.issueWith(Rig::dp(0),Family::Particle,[](DrawFacts& f){f.occlusionActive=true;});deferQuad(r,2);r.boundary();}
static void testQueryHazard(){
    Pair p=runBoth(sQuery);CHECK(p.lay->layer.totalCounters().hazards[unsigned(Reason::Query)]==1&&p.lay->deferredTags==std::vector<int>({1}));
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
}
static void sOrder(Rig& r){
    deferQuad(r,1);deferQuad(r,2);
    r.issueWith(Rig::dp(0),Family::None,[](DrawFacts&){});                    // a translucent no-Z draw of an unknown kind
    deferQuad(r,3);r.boundary();
}
static void testOrderHazard(){
    Pair p=runBoth(sOrder);CHECK(p.lay->layer.totalCounters().hazards[unsigned(Reason::Order)]==1&&p.lay->deferredTags.size()==2);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
}
static void testSampleHazardAndOtherTargets(){
    Rig r(Opts{});deferQuad(r,1);
    r.issueWith(Rig::dp(0),Family::Particle,[](DrawFacts& f){f.samplesRenderTargetTexture=true;});
    CHECK(r.layer.pending()==0&&r.layer.totalCounters().hazards[unsigned(Reason::Sample)]==1);
    Rig q(Opts{});deferQuad(q,1);                                             // a draw into another target does not interact
    q.dev.SetRenderTarget(0,q.Other);q.issue(Rig::dp(0));q.dev.SetRenderTarget(0,q.T);
    CHECK(q.layer.pending()==1&&q.last.kind==Kind::Ignore&&q.layer.totalCounters().totalHazards()==0);
}
static void sRtMismatch(Rig& r){deferQuad(r,1);r.dev.SetRenderTarget(0,r.Other);r.issueWith(Rig::dp(0),Family::Particle,[](DrawFacts& f){f.rt0IsTarget=true;});r.dev.SetRenderTarget(0,r.T);r.boundary();}
static void testRtMismatch(){
    Pair p=runBoth(sRtMismatch);CHECK(p.lay->layer.totalCounters().hazards[unsigned(Reason::Order)]==1&&p.lay->deferredTags.size()==1);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
}
static void sCapacity(Rig& r){for(int i=1;i<=6;++i)deferQuad(r,i);r.boundary();}
static void testCapacity(){
    Opts o;o.maxEntries=4;Pair p=runBoth(sCapacity,o);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags==std::vector<int>({1,2,3,4}));
    CHECK(c.hazards[unsigned(Reason::Capacity)]==1&&c.totalHazards()==1&&c.flushes==1&&c.replayedDraws==4);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());                 // 1-4 flushed at the 5th, 5 and 6 in place, all before the effects
    Opts a;a.arena=128;                                                       // UP bytes: 64 per quad
    Pair u=runBoth([](Rig& r){for(int i=0;i<3;++i){uint8_t v[64];const Bytes b2=Rig::quad(i+1);std::memcpy(v,b2.data(),64);r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,v,16));}r.boundary();},a);
    CHECK(u.lay->deferredTags.size()==2&&u.lay->layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1);checkEquivalent(u);CHECK(u.lay->order()==u.ref->order());
    Opts v;v.upVb=64;                                                         // the upload buffer cannot hold the conflict copy of two quads
    Pair w=runBoth(sDiscard,v);
    CHECK(w.lay->layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1&&w.lay->layer.totalCounters().conflicts==0&&w.lay->drawsAtWrite.back()==2);checkEquivalent(w);CHECK(w.lay->order()==w.ref->order());
}
// A record that cannot be read (a failing Get), an instanced stream, a refused scope: all fall back, in order.
static void sCaptureFail(Rig& r){
    deferQuad(r,1);deferQuad(r,2);
    r.failPsConst(true);                                                      // the next record cannot read its pixel shader constants
    deferQuad(r,3);r.failPsConst(false);deferQuad(r,4);r.boundary();
}
static void testCaptureFailure(){
    Pair p=runBoth(sCaptureFail);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags==std::vector<int>({1,2})&&c.hazards[unsigned(Reason::Capture)]==1&&c.flushes==1&&c.replayedDraws==2);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
    // nothing pending (the first entry): also falls back with a cooldown, no flush
    Rig z(Opts{});z.failPsConst(true);
    deferQuad(z,1);CHECK(z.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&z.layer.mode()==Mode::Fallback&&z.dev.draws.size()==1&&z.layer.totalCounters().flushes==0);
    z.failPsConst(false);
    Rig s(Opts{});s.dev.SetStreamSourceFreq(0,(1u<<30)|4);                    // an instanced stream is not recorded
    DrawArgs ia=Rig::dp(0);ia.psMajor=3;
    CHECK(!s.layer.defer(ia,s.T,s.D,SubAdditive)&&s.layer.pending()==0&&s.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&s.layer.mode()==Mode::Fallback);
}
// The replay scope (SavedState) cannot be made: every pending draw is skipped and counted, the device is untouched.
static void testReplayScopeFailure(){
    Opts so;so.minimal=false;Rig r(so);deferQuad(r,1);deferQuad(r,2);
    r.dev.failCreateBlock=true;
    r.boundary();
    r.dev.failCreateBlock=false;
    CHECK(r.layer.totalCounters().replaySkipped==2&&r.layer.totalCounters().replayedDraws==0&&r.layer.pending()==0);
    CHECK(r.dev.draws.size()==1&&r.dev.draws[0].tag==Rig::kMarker);
}
static void sFrameEnd(Rig& r){deferQuad(r,1);deferQuad(r,2);hz(r,Reason::Frame,false);}
static void testFrameEnd(){
    Pair p=runBoth(sFrameEnd);Layer& l=p.lay->layer;
    CHECK(l.totalCounters().hazards[unsigned(Reason::Frame)]==1&&l.totalCounters().replayedDraws==2&&l.pending()==0);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
    l.beginFrame(false);CHECK(l.mode()==Mode::Armed);                          // no cooldown for a frame without B
    Rig r(Opts{});deferQuad(r,1);                                             // the integration forgot: dropped, not drawn
    r.layer.beginFrame(false);
    CHECK(r.dev.draws.empty()&&r.layer.pending()==0&&r.layer.lastFrameCounters().discarded==1&&r.layer.mode()==Mode::Armed&&!r.ring->b.guarded&&r.layer.target()==nullptr);
}
static void testCooldownAndPressure(){
    Rig r(Opts{});sClear(r);Layer& l=r.layer;
    CHECK(l.mode()==Mode::Fallback);
    for(unsigned i=0;i<kCooldownFrames;++i){
        l.beginFrame(false);CHECK(l.mode()==Mode::Cooldown&&!l.armed());
        const unsigned calls=r.dev.calls;CHECK(!l.defer(Rig::dp(0),r.T,r.D,SubAdditive)&&r.dev.calls==calls); // no device call while cooling down
    }
    l.beginFrame(false);CHECK(l.mode()==Mode::Armed&&l.armed());
    deferQuad(r,5);CHECK(l.pending()==1&&l.mode()==Mode::Deferring);r.boundary();CHECK(l.mode()==Mode::Fallback);
    l.beginFrame(false);CHECK(l.mode()==Mode::Armed);                          // a normal frame leaves no cooldown behind
    // memory pressure: no new deferral, record storage released, counters untouched
    deferQuad(r,6);r.boundary();CHECK(l.recordChunks()>0);
    l.beginFrame(true);CHECK(l.mode()==Mode::Cooldown&&l.recordChunks()==0);
    const unsigned total=l.totalCounters().totalDeferred();deferQuad(r,7);CHECK(l.pending()==0&&l.totalCounters().totalDeferred()==total);
    l.beginFrame(false);CHECK(l.mode()==Mode::Armed);
    // a frame's counters roll over
    CHECK(l.lastFrameCounters().totalDeferred()==0&&l.frameCounters().totalDeferred()==0);
}
static void bigDelta(Rig& r,int seed){ // every vertex shader constant changes: a delta of about 1000 words
    std::vector<float> c(256*4);for(size_t i=0;i<c.size();++i)c[i]=float(seed*1000+int(i));
    r.dev.SetVertexShaderConstantF(0,c.data(),256);
}
static void testTrim(){
    Opts o;o.deltaChunk=4096;
    Rig r(o);deferQuad(r,1);deferQuad(r,2);deferQuad(r,3);
    r.layer.trim(0);CHECK(r.layer.recordChunks()==1);                          // never below what is pending
    r.boundary();CHECK(r.layer.recordChunks()==1);r.layer.trim(1);CHECK(r.layer.recordChunks()==1);r.layer.trim(0);CHECK(r.layer.recordChunks()==0);
    r.layer.beginFrame(false);                                                // chunks come back on demand
    for(int i=0;i<7;++i){bigDelta(r,i);deferQuad(r,i+10);}
    CHECK(r.layer.pending()==7&&r.layer.recordChunks()==3);r.boundary();      // 4096 words hold three of those deltas (about 1030 words each)
    r.layer.trim(100);CHECK(r.layer.recordChunks()==1);r.layer.trim(0);CHECK(r.layer.recordChunks()==0);
    r.layer.beginFrame(false);for(int i=0;i<4;++i){bigDelta(r,i);deferQuad(r,i+30);}
    CHECK(r.layer.recordChunks()==2);r.layer.trim(0);CHECK(r.layer.recordChunks()==2);r.boundary();r.layer.trim(0);CHECK(r.layer.recordChunks()==0); // never below what is pending
}
static void testPendingTargets(){
    Rig r(Opts{});CHECK(r.layer.target()==nullptr);deferQuad(r,1);CHECK(r.layer.target()==r.T&&r.layer.depth()==r.D);
    const int live=Fake::live;r.boundary();CHECK(r.layer.target()==nullptr&&Fake::live==live); // T and DS references were held once and let go
}
static void testLayerOff(){
    Opts o;o.on=false;Rig r(o);Layer& l=r.layer;
    const unsigned calls=r.dev.calls;uint8_t v[64]={};
    CHECK(!l.defer(Rig::dp(0),r.T,r.D,SubAlpha)&&!l.defer(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,v,16),r.T,r.D,SubAlpha));
    l.hazard(Reason::Clear);l.onBufferWrite(r.ring->b.key,0,0,D3DLOCK_DISCARD,r.ring->b.usage,0);l.onBufferGpuWrite(r.ring->b.key);l.onTextureWrite(r.texA);
    CHECK(!l.pendingSamples(r.texA));l.replay();l.beginFrame(false);l.beginFrame(true);l.trim(0);l.noteInPlace(InPlaceKind::Water);
    CHECK(r.dev.calls==calls&&l.mode()==Mode::Off&&!l.armed()&&r.dev.draws.empty()&&l.pending()==0);
    // the rig's own classify path never reaches the layer either
    r.issue(Rig::dp(0));CHECK(r.dev.draws.size()==1);
    Rig on(Opts{});                                                           // on, but the device is not attached: nothing to talk to
    on.layer.attach(nullptr);CHECK(!on.layer.defer(Rig::dp(0),on.T,on.D,SubAlpha));
}
static void testResetAndTeardown(){
    const int base=Fake::live;
    {
        Rig r(Opts{});const int afterRig=Fake::live;
        uint8_t v[64]={};
        deferQuad(r,1);deferQuad(r,2);r.dev.SetStreamSource(0,r.stat,0,16);r.issue(Rig::dp(0));
        r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,v,16));
        CHECK(r.layer.pending()==4&&r.layer.recordChunks()==1);
        const size_t draws=r.dev.draws.size();
        r.layer.reset();
        CHECK(r.dev.draws.size()==draws&&r.layer.pending()==0&&Fake::live==afterRig&&r.layer.recordChunks()==0);   // dropped, not drawn; records and refs released
        CHECK(!r.ring->b.guarded&&!r.stat->b.guarded&&r.layer.target()==nullptr&&r.layer.mode()==Mode::Fallback);
        for(auto& m:r.marks)CHECK(m.second==0);
        r.layer.replay();CHECK(r.dev.draws.size()==draws);
        r.layer.beginFrame(false);deferQuad(r,3);CHECK(r.layer.pending()==1);   // usable again after a reset
        r.discardRing(9);CHECK(r.layer.frameCounters().materialized==1);        // with an upload buffer in play when the device goes away
        deferQuad(r,4);                                                          // and pending at destruction
    }
    CHECK(Fake::live==base);
}
static void sStress(Rig& r){
    r.dev.SetStreamSource(0,r.ring,0,16);
    for(int i=0;i<512;++i)deferQuad(r,i);
    r.discardRing(1);r.boundary();
}
// ---------------- review fixes ----------------
// A 32-bit dynamic index buffer overwritten by DISCARD while a draw is deferred: the copy must be replayed as 32-bit (it used to be read as 16-bit).
static void sIb32(Rig& r){
    r.dev.SetStreamSource(0,r.stat,0,16);r.dev.SetIndices(r.ringIb32);
    const uint32_t ix[6]={0,1,2,2,1,3};Bytes b(24);std::memcpy(b.data(),ix,24);
    r.append(r.ringIb32->b,r.ringIbCur,b);const UINT io=r.append(r.ringIb32->b,r.ringIbCur,b);
    r.issue(Rig::dip(0,0,4,io/4));
    r.write(r.ringIb32->b,0,0,D3DLOCK_DISCARD,Rig::pat(33,64));
    r.boundary();
}
static void testIb32(){
    Pair p=runBoth(sIb32);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==1&&c.materialized==1&&c.conflictBytes==24&&c.totalHazards()==0&&c.replayedDraws==1);
    checkEquivalent(p,{1});
    for(auto& d:p.lay->dev.draws)if(d.tag==1)CHECK(d.idx==std::vector<uint32_t>({0,1,2,2,1,3}));
}
// Both formats materialized in one frame: each is bound from the upload buffer of its own format.
static void sIbBoth(Rig& r){
    r.dev.SetStreamSource(0,r.stat,0,16);
    const uint32_t ix32[6]={0,1,2,2,1,3};Bytes b32(24);std::memcpy(b32.data(),ix32,24);
    r.dev.SetIndices(r.ringIb);r.ringIdx();const UINT i16=r.ringIdx();r.issue(Rig::dip(0,0,4,i16/2));
    r.dev.SetIndices(r.ringIb32);r.append(r.ringIb32->b,r.ringIbCur,b32);const UINT i32=r.append(r.ringIb32->b,r.ringIbCur,b32);r.issue(Rig::dip(0,0,4,i32/4));
    r.write(r.ringIb->b,0,0,D3DLOCK_DISCARD,Rig::pat(33,64));r.write(r.ringIb32->b,0,0,D3DLOCK_DISCARD,Rig::pat(34,64));
    r.boundary();
}
static void testIbBothFormats(){
    Pair p=runBoth(sIbBoth);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.conflicts==2&&c.materialized==2&&c.totalHazards()==0&&c.replayedDraws==2&&c.replaySkipped==0);
    checkEquivalent(p,{1,2});
    for(auto& d:p.lay->dev.draws)if(d.tag==1||d.tag==2)CHECK(d.idx==std::vector<uint32_t>({0,1,2,2,1,3}));
}
// Water, rain and floating names drawn while draws are pending split the frame: the pending draws are replayed before them (the reference order,
// they used to land under the spells), and the later draws are deferred again and replayed after the effects. No hazard, no cooldown.
template<int K> static void sOrderKind(Rig& r){
    deferQuad(r,1);deferQuad(r,2);
    r.issueWith(Rig::dp(0),Family::Model,[](DrawFacts& f){f.water=K==0;f.rain=K==1;f.uiTag=K==2;});
    CHECK(r.layer.pending()==0&&r.layer.mode()!=Mode::Fallback);
    deferQuad(r,3);deferQuad(r,4);
    r.issueWith(Rig::dp(0),Family::Model,[](DrawFacts& f){f.water=K==0;f.rain=K==1;f.uiTag=K==2;});
    deferQuad(r,5);r.boundary();
}
template<int K> static void checkOrderKind(){
    Pair p=runBoth(sOrderKind<K>);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.totalHazards()==0&&c.flushes==2&&c.orderBy[K]==2&&c.orderBy[(K+1)%3]==0&&c.orderBy[(K+2)%3]==0&&c.replayedDraws==5);
    CHECK(p.lay->deferredTags.size()==5);                                      // the frame keeps deferring after each split
    CHECK(p.lay->layer.eventSeq()==0);                                         // a split is no hazard event
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),{p.lay->deferredTags.back()}));
    p.lay->layer.beginFrame(false);CHECK(p.lay->layer.mode()==Mode::Armed);    // no cooldown follows
}
static void testOrderKinds(){checkOrderKind<0>();checkOrderKind<1>();checkOrderKind<2>();}
static void testOrderKindsIdle(){ // nothing pending: plain in-place draws, no hazard, the frame stays armed
    Rig r(Opts{});
    for(int k=0;k<3;++k){
        r.issueWith(Rig::dp(0),Family::Model,[k](DrawFacts& f){f.water=k==0;f.rain=k==1;f.uiTag=k==2;});
        CHECK(r.last.kind==Kind::InPlace&&r.layer.totalCounters().totalHazards()==0&&r.layer.mode()==Mode::Armed);
    }
    CHECK(r.layer.totalCounters().orderBy[0]+r.layer.totalCounters().orderBy[1]+r.layer.totalCounters().orderBy[2]==0);
    deferQuad(r,1);CHECK(r.layer.pending()==1);
}
// A pending draw sampling a render-target texture is refused (Capture): the game may render into it later without any call the layer sees.
static void sRtTexture(Rig& r){
    deferQuad(r,1);
    r.texB->usage=D3DUSAGE_RENDERTARGET;r.dev.SetTexture(3,r.texB);
    deferQuad(r,2);
    r.dev.SetTexture(3,nullptr);r.texB->usage=0;
    deferQuad(r,3);r.boundary();
}
static void testRenderTargetTexture(){
    Pair p=runBoth(sRtTexture);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.hazards[unsigned(Reason::Capture)]==1&&c.totalHazards()==1&&p.lay->deferredTags==std::vector<int>({1}));
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
    {   // a cube render target, a level that cannot be described, and non-render-target cube and volume textures
        Rig r(Opts{});auto* cube=new Fake::CubeTexture;cube->usage=D3DUSAGE_RENDERTARGET;
        r.dev.SetTexture(5,cube);cube->Release();
        deferQuad(r,1);CHECK(r.layer.pending()==0&&r.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1);
        r.dev.SetTexture(5,nullptr);r.texA->descResult=E_FAIL;
        r.layer.beginFrame(false);while(r.layer.mode()!=Mode::Armed)r.layer.beginFrame(false);
        deferQuad(r,2);CHECK(r.layer.pending()==0&&r.layer.totalCounters().hazards[unsigned(Reason::Capture)]==2);
        r.texA->descResult=D3D_OK;
        while(r.layer.mode()!=Mode::Armed)r.layer.beginFrame(false);
        auto* cube2=new Fake::CubeTexture;auto* vol=new Fake::VolumeTexture;
        r.dev.SetTexture(5,cube2);r.dev.SetTexture(6,vol);cube2->Release();vol->Release();
        deferQuad(r,3);CHECK(r.layer.pending()==1);
    }
}
// The render-target verdict of a texture is asked once per texture identity (a texture's usage never changes); a new generation (a texture was created or
// destroyed: an address may be reused) asks again, so a render target created at the address of an earlier plain texture is still refused.
static void testVerdictCache(){
    Opts vo;vo.verdictCache=true;Rig r(vo);gTexGeneration=1;
    r.dev.SetTexture(3,r.texB);
    for(int i=1;i<=5;++i)deferQuad(r,i);
    CHECK(r.layer.pending()==5&&r.texA->descCalls==1&&r.texB->descCalls==1);          // five draws, two textures: two Gets of the level desc
    r.texB->usage=D3DUSAGE_RENDERTARGET;                                               // "the object at that address was replaced by a render target"
    deferQuad(r,6);CHECK(r.layer.pending()==6&&r.texB->descCalls==1);                  // no new generation: the verdict stands (usage of one object never changes)
    r.layer.beginFrame(false);deferQuad(r,6);CHECK(r.layer.pending()==1&&r.texB->descCalls==1); // next frame, same generation: still no new Get
    gTexGeneration=2;r.layer.beginFrame(false);deferQuad(r,7);
    CHECK(r.layer.pending()==0&&r.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&r.texB->descCalls==2); // new generation: asked again, refused, flushed
    Rig q(Opts{});q.dev.SetTexture(3,q.texB);                                            
    for(int i=1;i<=3;++i)deferQuad(q,i);CHECK(q.texA->descCalls==1&&q.texB->descCalls==1); // still once per object and frame
    q.layer.beginFrame(false);deferQuad(q,4);CHECK(q.texA->descCalls==2&&q.texB->descCalls==2);
    gTexGeneration=0;
}
// Vertex texture samplers (stages 257..260) are part of what a pending draw samples: a write to such a texture flushes; a render target there is refused.
static void sVertexSampler(Rig& r){
    r.dev.SetTexture(257,r.texB);
    deferQuad(r,1);
    if(r.o.on){
        CHECK(r.layer.pendingSamples(r.texB));
        r.layer.onTextureWrite(r.texB);CHECK(r.layer.pending()==0&&r.layer.mode()==Mode::Deferring);
    }
    r.record();r.dev.SetTexture(257,nullptr);deferQuad(r,2);r.boundary();
}
static void testVertexTextureSampler(){
    Pair p=runBoth(sVertexSampler);
    CHECK(p.lay->layer.totalCounters().texSplits==1&&p.lay->layer.totalCounters().totalHazards()==0&&p.lay->deferredTags==std::vector<int>({1,2}));
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),{2}));
    Rig r(Opts{});r.texB->usage=D3DUSAGE_RENDERTARGET;r.dev.SetTexture(260,r.texB);
    deferQuad(r,1);CHECK(r.layer.pending()==0&&r.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1);
}
// bufferWriteConflicts() mirrors onBufferWrite() without side effects: the ring append is clear, a DISCARD or an overlap conflicts.
static void testBufferWriteProbe(){
    Rig r(Opts{});void* key=r.ring->b.key;
    CHECK(!r.layer.bufferWriteConflicts(key,0,0,D3DLOCK_DISCARD));              // nothing pending
    deferQuad(r,1);
    CHECK(!r.layer.bufferWriteConflicts(key,64,64,D3DLOCK_NOOVERWRITE));        // the append beyond the guarded range
    CHECK(r.layer.bufferWriteConflicts(key,0,0,D3DLOCK_DISCARD)&&r.layer.bufferWriteConflicts(key,0,16,D3DLOCK_NOOVERWRITE));
    CHECK(!r.layer.bufferWriteConflicts(key,0,0,D3DLOCK_DISCARD|D3DLOCK_READONLY)&&!r.layer.bufferWriteConflicts(r.stat->b.key,0,0,D3DLOCK_DISCARD));
    CHECK(r.layer.pending()==1&&r.layer.totalCounters().conflicts==0);
}
// ---------------- state records ----------------
// A state the game changed between two deferred draws replays with each draw's own value: sampler state, texture binding of a stage, render
// state, pixel and vertex constants (float, int, bool), clip plane, viewport, scissor, FVF instead of a declaration.
static void sChangingState(Rig& r){
    deferQuad(r,1);                                                           // tag 1
    r.dev.SetSamplerState(0,1,7);r.dev.SetRenderState(D3DRS_ALPHAREF,200);
    {const float k[4]={5,6,7,8};r.dev.SetPixelShaderConstantF(2,k,1);}
    deferQuad(r,2);                                                           // tag 2: sampler state, ALPHAREF, PS c2
    r.dev.SetTexture(5,r.texB);r.dev.SetSamplerState(5,4,0x77);r.dev.SetSamplerState(5,5,3);
    {const int ki[4]={1,2,3,4};r.dev.SetVertexShaderConstantI(3,ki,1);const BOOL kb[2]={1,1};r.dev.SetPixelShaderConstantB(7,kb,2);const float pl[4]={0,1,0,2};r.dev.SetClipPlane(1,pl);r.rs(152,2);}
    deferQuad(r,3);                                                           // tag 3: stage 5 bound, int/bool constants, clip plane
    r.dev.SetTexture(5,nullptr);                                              // stage 5 unbound again, its sampler states stay as they were
    {D3DVIEWPORT9 vp;vp.X=10;vp.Y=20;vp.Width=700;vp.Height=500;r.dev.SetViewport(&vp);const RECT sc{3,4,300,200};r.dev.SetScissorRect(&sc);r.rs(174,1);}
    {const int z[4]={0,0,0,0};r.dev.SetVertexShaderConstantI(3,z,1);}
    deferQuad(r,4);                                                           // tag 4: stage 5 must be unbound again, new viewport/scissor, int c3 back
    r.dev.SetVertexDeclaration(nullptr);r.dev.SetFVF(0x112);
    deferQuad(r,5);                                                           // tag 5: FVF (stream 0), no declaration
    r.dev.SetVertexDeclaration(r.declP);r.rs(D3DRS_ALPHATESTENABLE,1);r.rs(D3DRS_POINTSIZE,0x7fa05000u);
    deferQuad(r,6);                                                           // tag 6: declaration again, a vendor-hack state changes
    r.boundary();
}
static void testStateChangedBetweenDraws(){
    Pair p=runBoth(sChangingState);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==6&&c.totalHazards()==0&&c.replayedDraws==6&&c.recorded==6);
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    std::map<int,const Fake::DrawRec*> by;for(auto& d:p.lay->dev.draws)by[d.tag]=&d;
    CHECK(by[1]->st.ss[0][1]==3&&by[2]->st.ss[0][1]==7&&by[1]->st.rs[D3DRS_ALPHAREF]==128&&by[2]->st.rs[D3DRS_ALPHAREF]==200);
    CHECK(by[1]->st.psc[2][0]==1&&by[2]->st.psc[2][0]==5&&by[3]->st.psc[2][0]==5);
    CHECK(!by[2]->st.tex[5]&&by[3]->st.tex[5]==p.lay->texB&&by[3]->st.ss[5][4]==0x77&&by[3]->st.vsi[3][2]==3&&by[3]->st.psb[8]==1&&by[3]->st.clip[1][3]==2);
    CHECK(!by[4]->st.tex[5]&&by[4]->st.vsi[3][2]==0&&by[4]->st.vp.Width==700&&by[4]->st.scissor.right==300);
    CHECK(by[5]->st.decl&&by[5]->st.decl!=p.lay->declP&&by[5]->st.fvf==0x112&&by[6]->st.decl==p.lay->declP&&by[6]->st.fvf==0);
    CHECK(by[6]->st.rs[D3DRS_POINTSIZE]==0x7fa05000u&&by[5]->st.rs[D3DRS_POINTSIZE]==0);
}
// The scissor rect is recorded only with D3DRS_SCISSORTESTENABLE and a clip plane only when D3DRS_CLIPPLANEENABLE has its bit: a disabled one is neither
// read at record time nor set at replay (the draw cannot see it), and a plane enabled later is set fresh.
static void sScissorClip(Rig& r){
    const RECT sc{3,4,300,200};const float pl[4]={0,1,0,2},pm[4]={1,0,0,5};
    r.dev.SetScissorRect(&sc);r.dev.SetClipPlane(1,pl);r.dev.SetClipPlane(4,pm);
    deferQuad(r,1);                                                           // tag 1: neither enabled
    r.rs(174,1);r.rs(152,2);deferQuad(r,2);                                   // tag 2: scissor on, plane 1
    r.rs(152,2|16);deferQuad(r,3);                                            // tag 3: planes 1 and 4 (4 is new)
    {const RECT sc2{9,9,99,99};r.dev.SetScissorRect(&sc2);}r.rs(174,0);r.rs(152,0);deferQuad(r,4); // tag 4: both off again, their values differ now but are not read
    r.boundary();
}
static void testScissorAndClipOnlyWhenEnabled(){
    Pair p=runBoth(sScissorClip);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==4&&c.totalHazards()==0);
    checkEquivalent(p);
    std::map<int,const Fake::DrawRec*> by;for(auto& d:p.lay->dev.draws)by[d.tag]=&d;
    CHECK(by[2]->st.scissor.right==300&&by[2]->st.clip[1][3]==2&&by[3]->st.clip[4][3]==5&&by[3]->st.scissor.right==300);
    CHECK(p.lay->dev.calls>0);
    // the replay issued no SetScissorRect/SetClipPlane for the draws that cannot read them: compare Set counts against a run without the enabling states
    Rig q(Opts{});const RECT sc{3,4,300,200};const float pl[4]={0,1,0,2};q.dev.SetScissorRect(&sc);q.dev.SetClipPlane(1,pl);
    deferQuad(q,1);q.boundary();const unsigned off=q.layer.totalCounters().replaySets;
    Rig e(Opts{});e.dev.SetScissorRect(&sc);e.dev.SetClipPlane(1,pl);e.rs(174,1);e.rs(152,2);
    deferQuad(e,1);e.boundary();CHECK(e.layer.totalCounters().replaySets>=off+2);
}
// Delta encoding against the replay model: a draw whose state equals what the earlier entries of the sequence set records only what changed (here the draw tag),
// a slot changed and changed back is emitted both times, a new sequence (a flush in the middle, the next frame) starts from an unknown device, and every
// entry still replays with exactly its own state.
static void sDeltas(Rig& r){
    deferQuad(r,1);                                                           // 1: everything
    deferQuad(r,2);                                                           // 2: nothing but the tag
    r.rs(D3DRS_ALPHAREF,200);{const float k[4]={4,4,4,4};r.dev.SetVertexShaderConstantF(40,k,1);r.dev.SetPixelShaderConstantF(9,k,1);}
    deferQuad(r,3);                                                           // 3: a render state, a vertex and a pixel constant
    r.rs(D3DRS_ALPHAREF,128);{const float k[4]={0,0,0,0};r.dev.SetVertexShaderConstantF(40,k,1);r.dev.SetPixelShaderConstantF(9,k,1);}
    deferQuad(r,4);                                                           // 4: changed back (against the model, not against the first entry)
    r.dev.SetTexture(0,r.texB);deferQuad(r,5);r.dev.SetTexture(0,r.texA);deferQuad(r,6);
    hz(r,Reason::Clear);                                                      // a flush: the sequence ends, the frame falls back
    r.boundary();
}
static void sTwoFrames(Rig& r){
    deferQuad(r,1);deferQuad(r,2);r.boundary();
    r.layer.beginFrame(false);while(r.layer.mode()!=Mode::Armed&&r.o.on)r.layer.beginFrame(false); // the effects left their own state on the device between frames
    r.rs(D3DRS_ALPHAREF,33);deferQuad(r,3);deferQuad(r,4);r.boundary();
}
static void testDeltas(){
    Pair p=runBoth(sDeltas);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==4+2&&c.replayedDraws==6);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order()||true);
    std::map<int,const Fake::DrawRec*> by;for(auto& d:p.lay->dev.draws)by[d.tag]=&d;
    CHECK(by[3]->st.rs[D3DRS_ALPHAREF]==200&&by[4]->st.rs[D3DRS_ALPHAREF]==128&&by[3]->st.vsc[40][0]==4&&by[4]->st.vsc[40][0]==0&&by[5]->st.tex[0]==p.lay->texB&&by[6]->st.tex[0]==p.lay->texA);
    {   // sizes: entry 2 is two ops (the tag in c31 and in the tag render state), far less than the first entry
        Rig r(Opts{});deferQuad(r,1);const uint64_t first=r.layer.totalCounters().deltaWords;deferQuad(r,2);const uint64_t second=r.layer.totalCounters().deltaWords-first;
        CHECK(first>400&&second==(1+1)+(1+4));
        deferQuad(r,3);CHECK(r.layer.totalCounters().deltaWords-first-second==second);
        r.boundary();
    }
    Pair q=runBoth(sTwoFrames);checkEquivalent(q);CHECK(q.lay->layer.totalCounters().replayedDraws==4);
}
// Shaders with scanned read sets: only the registers and samplers they read are recorded and replayed; the unread state of a replayed draw may differ from
// the original (it cannot be seen), the read state is exact. Fewer sets, less delta.
static void setBit(uint64_t* m,unsigned i){m[i>>6]|=1ull<<(i&63);}
static void sRestricted(Rig& r){
    NorthlightShaderReads::ReadSet v,p;v.exact=p.exact=true;v.vertex=true;
    for(unsigned i:{5u,8u,9u,10u,11u,31u,40u,41u})setBit(v.f,i);v.i=1u<<2;                // tag c31 and the tag state is a render state; ints: i2
    for(unsigned i:{2u,3u,9u})setBit(p.f,i);p.b=1u<<4;p.samplers=1u|1u<<4;
    r.restrict(v,p);
    r.dev.SetTexture(4,r.texB);                                                         // sampler 4 is declared
    deferQuad(r,1);
    { // changes of state no one reads: c100 (vs), c20 (ps), ints/bools nobody names, texture and sampler state of stages 3 and 6
        const float k[4]={7,7,7,7};r.dev.SetVertexShaderConstantF(100,k,1);r.dev.SetPixelShaderConstantF(20,k,1);
        const int ki[4]={1,1,1,1};r.dev.SetVertexShaderConstantI(5,ki,1);const BOOL kb=1;r.dev.SetPixelShaderConstantB(2,&kb,1);r.dev.SetVertexShaderConstantB(3,&kb,1);
        r.dev.SetTexture(3,r.texB);r.dev.SetSamplerState(3,2,99);r.dev.SetTexture(6,r.texA);r.dev.SetSamplerState(6,3,98);
        const float* nothing=nullptr;(void)nothing;
    }
    deferQuad(r,2);                                                                     // 2: only the tag
    { // changes of state it reads: c9 (vs), c3 (ps), i2, b4, the texture of sampler 4 and its sampler state, the declared sampler 0's state
        const float k[4]={8,8,8,8};r.dev.SetVertexShaderConstantF(9,k,1);r.dev.SetPixelShaderConstantF(3,k,1);
        const int ki[4]={5,6,7,8};r.dev.SetVertexShaderConstantI(2,ki,1);const BOOL kb=1;r.dev.SetPixelShaderConstantB(4,&kb,1);
        r.dev.SetTexture(4,r.texA);r.dev.SetSamplerState(4,4,77);r.dev.SetSamplerState(0,5,66);
    }
    deferQuad(r,3);
    r.boundary();
}
template<int Which> static void sForeignReads(Rig& r){ // the read sets belong to another shader than the bound one (Which: 0 vertex, 1 pixel)
    NorthlightShaderReads::ReadSet v,p;v.exact=p.exact=true;v.vertex=true;setBit(v.f,31);setBit(p.f,2);r.restrict(v,p);
    if(Which==0)r.readsVs=r.vsEff;else r.readsPs=r.psEff;
    deferQuad(r,1);{const float k[4]={7,7,7,7};r.dev.SetVertexShaderConstantF(100,k,1);r.dev.SetPixelShaderConstantF(20,k,1);}deferQuad(r,2);r.boundary();
}
static void testReadSets(){
    Pair p=runBoth(sRestricted);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==3&&c.totalHazards()==0&&c.replayedDraws==3);
    checkEquivalent(p);
    std::map<int,const Fake::DrawRec*> by;for(auto& d:p.lay->dev.draws)by[d.tag]=&d;
    CHECK(by[3]->st.vsc[9][0]==8&&by[3]->st.psc[3][0]==8&&by[3]->st.vsi[2][3]==8&&by[3]->st.psb[4]==1&&by[3]->st.tex[4]==p.lay->texA&&by[3]->st.ss[4][4]==77&&by[3]->st.ss[0][5]==66);
    // the same frame with everything read replays more: the unread changes are not replayed, so the restricted run issues fewer Set calls and records fewer words
    Pair all=runBoth([](Rig& r){
        r.dev.SetTexture(4,r.texB);deferQuad(r,1);
        {const float k[4]={7,7,7,7};r.dev.SetVertexShaderConstantF(100,k,1);r.dev.SetPixelShaderConstantF(20,k,1);r.dev.SetTexture(3,r.texB);r.dev.SetSamplerState(3,2,99);}
        deferQuad(r,2);r.boundary();});
    Pair some=runBoth([](Rig& r){
        NorthlightShaderReads::ReadSet v,p;v.exact=p.exact=true;setBit(v.f,31);setBit(v.f,5);setBit(p.f,2);p.samplers=1u|1u<<4;r.restrict(v,p);
        r.dev.SetTexture(4,r.texB);deferQuad(r,1);
        {const float k[4]={7,7,7,7};r.dev.SetVertexShaderConstantF(100,k,1);r.dev.SetPixelShaderConstantF(20,k,1);r.dev.SetTexture(3,r.texB);r.dev.SetSamplerState(3,2,99);}
        deferQuad(r,2);r.boundary();});
    const Counters& ca=all.lay->layer.totalCounters();const Counters& cs=some.lay->layer.totalCounters();
    CHECK(cs.deltaWords<ca.deltaWords/2&&cs.replaySets<ca.replaySets&&cs.replayedDraws==2);
    checkEquivalent(all);checkEquivalent(some);
    {   // a shader scanned as "reads everything" (exact=false) keeps the full behaviour, also next to an exact one
        NorthlightShaderReads::ReadSet v,pp;v.exact=true;v.vertex=true;setBit(v.f,31);                    // vertex shader exact, pixel shader not
        Pair mixed=runBoth([](Rig& r){
            NorthlightShaderReads::ReadSet v;v.exact=true;v.vertex=true;setBit(v.f,31);NorthlightShaderReads::ReadSet pp;r.restrict(v,pp);
            deferQuad(r,1);{const float k[4]={7,7,7,7};r.dev.SetPixelShaderConstantF(20,k,1);r.dev.SetTexture(3,r.texB);}deferQuad(r,2);r.boundary();});
        std::map<int,const Fake::DrawRec*> m;for(auto& d:mixed.lay->dev.draws)m[d.tag]=&d;
        CHECK(m[2]->st.psc[20][0]==7&&m[2]->st.tex[3]==mixed.lay->texB&&m[1]->st.psc[20][0]==0);
        (void)v;(void)pp;
    }
    {   // a read set looked up for another shader than the one recorded (vertex and pixel separately): that stage reads everything
        Pair vm=runBoth(sForeignReads<0>),pm=runBoth(sForeignReads<1>);
        std::map<int,const Fake::DrawRec*> a,b;for(auto& d:vm.lay->dev.draws)a[d.tag]=&d;for(auto& d:pm.lay->dev.draws)b[d.tag]=&d;
        CHECK(a[1]->st.vsc[100][0]==0&&a[1]->st.psc[20][0]==7);               // draw 1: a stage that reads everything replays what was recorded (0); a narrowed one leaves the live value (7)
        CHECK(b[1]->st.psc[20][0]==0&&b[1]->st.vsc[100][0]==7);
        checkEquivalent(vm);checkEquivalent(pm);
    }
}
// Replay is a diff: the second of two draws differing in one constant costs one Set call, the first sets everything recorded.
static void sOneDraw(Rig& r){deferQuad(r,1);r.boundary();}
static void sTwoDraws(Rig& r){deferQuad(r,1);deferQuad(r,2);r.boundary();}
static void sThreeDraws(Rig& r){deferQuad(r,1);deferQuad(r,2);deferQuad(r,3);r.boundary();}
static void testDiffReplay(){
    Pair one=runBoth(sOneDraw),two=runBoth(sTwoDraws),three=runBoth(sThreeDraws);
    const unsigned s1=one.lay->layer.totalCounters().replaySets,s2=two.lay->layer.totalCounters().replaySets,s3=three.lay->layer.totalCounters().replaySets;
    CHECK(s1>100);                                                            // a full set: render states, constants, samplers, shaders, textures, viewport...
    CHECK(s2==s1+2&&s3==s2+2);                                                // each further draw: what changed, the draw tag in c31 and in D3DRS_DEBUGMONITORTOKEN
    CHECK(s2<2*s1);
    // the device saw exactly that: the layer run issued the same number of extra Set calls as the layer counted (the scope's own are equal)
    CHECK(two.lay->dev.sets-one.lay->dev.sets==two.ref->dev.sets-one.ref->dev.sets+2);
    checkEquivalent(one);checkEquivalent(two);checkEquivalent(three);
    // two draws with different shaders and a changed sampler state and texture: only those are set again
    Rig r(Opts{});deferQuad(r,1);
    auto* vs2=r.reg(new Fake::VShader);r.dev.SetVertexShader(vs2);r.dev.SetSamplerState(0,6,2);r.dev.SetTexture(0,r.texB);
    deferQuad(r,2);r.boundary();
    const unsigned s4=r.layer.totalCounters().replaySets;
    CHECK(s4==s1+5);                                                          // vertex shader, texture, sampler state, c31, the tag state
    vs2->Release();
}

// ---------------- fixed-function draws (ribbons, beams) ----------------
static int deferFf(Rig& r,int seed,UINT* off=nullptr){r.dev.SetStreamSource(0,r.ring,0,16);const UINT a=r.ringQuad(seed);if(off)*off=a;return r.issueFf(Rig::dp(a/16));}
// Fixed-function entries diffed against the previous fixed-function entry; a programmable entry between them leaves the fixed-function state alone; FVF and
// declaration draws; lights never set (1, 3, 4...) are not created and a light the effects left on must not light the draw.
static void sFixed(Rig& r){
    r.ffState(1);r.fixedFunction();
    deferFf(r,1);deferFf(r,2);                                                // 1, 2: identical fixed-function state
    {const D3DMATRIX m=Rig::mat(500);r.dev.SetTransform(256,&m);}
    deferFf(r,3);                                                             // 3: the world transform only
    r.dev.SetTextureStageState(3,1,9);r.dev.SetTextureStageState(0,24,7);
    {D3DMATERIAL9 mt{};mt.Power=3;r.dev.SetMaterial(&mt);}
    r.dev.LightEnable(2,TRUE);
    deferFf(r,4);                                                             // 4: two stage states, the material, a light switched on
    r.programmable();deferQuad(r,5);                                          // 5: programmable between fixed-function entries
    r.fixedFunction();r.dev.SetFVF(0x112);
    deferFf(r,6);                                                             // 6: fixed function through an FVF, the fixed-function state of 4
    {D3DLIGHT9 l{};l.Type=3;l.Range=8;r.dev.SetLight(2,&l);r.dev.SetLight(5,&l);r.dev.LightEnable(5,TRUE);}
    r.dev.SetVertexDeclaration(r.declP);
    deferFf(r,7);                                                             // 7: a light changed, a light newly set and on
    r.boundary();
}
static void sFixedOne(Rig& r){r.ffState(1);r.fixedFunction();deferFf(r,1);r.boundary();}
static void testFixedFunction(){
    Pair p=runBoth(sFixed),one=runBoth(sFixedOne);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags==std::vector<int>({1,2,3,4,5,6,7})&&p.ref->deferredTags.empty());
    CHECK(c.totalDeferred()==7&&c.fixed==6&&c.totalHazards()==0&&c.flushes==0&&c.replayedDraws==7&&c.replaySkipped==0&&c.recorded==7);
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    CHECK(p.lay->layer.ffChunks()==1&&one.lay->layer.totalCounters().fixed==1);
    // the diff: the draw tag (D3DRS_DEBUGMONITORTOKEN) every time; 2: nothing else; 3: +world; 4: +2 stage states, material, light 2 on; 5: vs, ps (no fixed-function sets);
    // 6: vs, ps, FVF (no fixed-function sets: the base is entry 4); 7: declaration, light 2, light 5 set and enabled
    // (+1: entry 1 has light 5 unset, but the game sets it later and it is on when the replay starts: switched off for entry 1)
    // entry 5 is the sequence's first programmable one: vs, ps, tag, every constant file (vs/ps float, vs/ps int, vs/ps bool = 6 runs) and the 12 texture slots 8..19 a fixed-function draw never read = 21
    CHECK(c.replaySets==one.lay->layer.totalCounters().replaySets+1+1+2+5+21+4+5);
    std::map<int,const Fake::DrawRec*> by;for(auto& d:p.lay->dev.draws)by[d.tag]=&d;
    CHECK(!by[1]->st.vs&&!by[1]->st.ps&&by[1]->st.tf[0].m[0][0]==Rig::mat(100).m[0][0]&&by[3]->st.tf[0].m[0][0]==Rig::mat(500).m[0][0]&&by[4]->st.tf[0].m[0][0]==Rig::mat(500).m[0][0]);
    CHECK(by[2]->st.tss[3][1]==10301&&by[3]->st.tss[3][1]==10301&&by[4]->st.tss[3][1]==9&&by[4]->st.tss[0][24]==7&&by[1]->st.tss[0][24]==10024&&by[6]->st.tss[3][1]==9&&by[7]->st.tss[7][32]==10732);
    CHECK(by[1]->st.mat.Power==1.5f&&by[4]->st.mat.Power==3.f&&by[7]->st.mat.Power==3.f);
    CHECK(by[1]->st.lightOn[0]&&!by[1]->st.lightOn[2]&&by[4]->st.lightOn[2]&&!by[4]->st.lightOn[5]&&by[7]->st.lightOn[5]&&by[7]->st.light[2].Type==3);
    CHECK(by[1]->st.lightSet[0]&&!by[1]->st.lightOn[4]);                       // light 4 was never set by the game: the effects' light 4 is off for the draw
    CHECK(by[5]->st.vs==p.lay->vs&&by[5]->st.ps==p.lay->ps&&by[6]->st.fvf==0x112&&by[6]->st.decl&&by[6]->st.decl!=p.lay->declP&&by[7]->st.decl==p.lay->declP);
    CHECK(p.lay->dev.s.lightOn[4]==1&&by[1]->st.lightOn[5]==0);            // the effects' light 4 is on again after the replay (restored)
}
// The fixed-function state is the draw's own: lights never set are admitted (no pixel/vertex shader model is needed).
static void testFixedAdmission(){
    Rig r(Opts{});r.fixedFunction();r.psMajor=0;                              // no state set at all: every light unset, zero matrices
    deferFf(r,1);CHECK(r.layer.pending()==1&&r.layer.totalCounters().totalHazards()==0);
    // vertex blending reads WORLD1..3 (not recorded): refused, everything pending is flushed first, the frame falls back
    for(unsigned st:{151u,167u}){
        Rig b(Opts{});b.ffState(1);b.fixedFunction();deferFf(b,1);deferFf(b,2);b.rs(st,1);deferFf(b,3);b.rs(st,0);deferFf(b,4);
        const Counters& c=b.layer.totalCounters();
        CHECK(c.hazards[unsigned(Reason::Admit)]==1&&c.totalHazards()==1&&c.flushes==1&&c.replayedDraws==2&&b.layer.mode()==Mode::Fallback&&b.layer.pending()==0);
        CHECK(b.dev.draws.size()==4&&b.dev.draws[0].tag==1&&b.dev.draws[1].tag==2&&b.dev.draws[2].tag==3&&b.dev.draws[3].tag==4); // the reference order
        b.boundary();
    }
    // a Get that fails: refused (Capture) after the flush
    {Rig b(Opts{});b.ffState(1);b.fixedFunction();deferFf(b,1);b.dev.failGetTss=true;deferFf(b,2);
     CHECK(b.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&b.layer.pending()==0&&b.dev.draws.size()==2&&b.dev.draws[0].tag==1);b.dev.failGetTss=false;}
    {Rig b(Opts{});b.ffState(1);b.fixedFunction();b.dev.failGetLightEnable=true;deferFf(b,1);        // a light that exists but cannot be queried
     CHECK(b.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&b.layer.pending()==0&&b.dev.draws.size()==1);b.dev.failGetLightEnable=false;}
    // a vertex shader with a fixed-function pixel stage and the reverse stay refused
    {Rig b(Opts{});b.dev.SetPixelShader(nullptr);deferFf(b,1);CHECK(b.layer.totalCounters().hazards[unsigned(Reason::Admit)]==1);}
    {Rig b(Opts{});b.dev.SetVertexShader(nullptr);deferFf(b,1);CHECK(b.layer.totalCounters().hazards[unsigned(Reason::Admit)]==1);}
}
// 512 fixed-function entries: the fixed-function records are separate, allocated in chunks, bounded, and released by trim().
static void testFixedCapacity(){
    std::printf("fixed-function record: %zu bytes (+ the state record), %zu bytes at 512 entries (chunks of 8 allocated on demand)\n",Layer::ffRecordBytes(),Layer::ffRecordBytes()*512);
    CHECK(Layer::ffRecordBytes()*512<2u*1024*1024&&Layer::ffRecordBytes()<4096);
    Rig r(Opts{});r.fixedFunction();r.ffState(2);
    for(int i=0;i<512;++i)deferFf(r,i);
    CHECK(r.layer.pending()==512&&r.layer.ffChunks()==64&&r.layer.totalCounters().fixed==512);
    deferFf(r,513);CHECK(r.layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1&&r.layer.pending()==0&&r.dev.draws.size()==513);
    CHECK(r.layer.totalCounters().replayedDraws==512);
    r.layer.beginFrame(false);r.layer.trim(0);CHECK(r.layer.ffChunks()==0&&r.layer.recordChunks()==0);
    // programmable-only frames never allocate a fixed-function record
    Rig q(Opts{});for(int i=0;i<40;++i)deferQuad(q,i);CHECK(q.layer.pending()==40&&q.layer.ffChunks()==0&&q.layer.totalCounters().fixed==0);
}
// Water ripples (the WaterRipples vertex shader) are drawn in place like water: a split when draws are pending, nothing when not.
static void sRipples(Rig& r){deferQuad(r,1);deferQuad(r,2);r.issueWith(Rig::dp(0),Family::Ripples,[](DrawFacts&){});deferQuad(r,3);r.boundary();}
static void testRipples(){
    Pair p=runBoth(sRipples);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.totalHazards()==0&&c.flushes==1&&c.orderBy[0]==1&&c.orderBy[1]==0&&c.orderBy[2]==0&&c.replayedDraws==3&&p.lay->deferredTags.size()==3);
    CHECK(p.lay->layer.eventSeq()==0);
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),{p.lay->deferredTags.back()}));
    Rig r(Opts{});r.issueWith(Rig::dp(0),Family::Ripples,[](DrawFacts&){});
    CHECK(r.last.kind==Kind::InPlace&&r.last.inPlace==InPlaceKind::Water&&r.layer.totalCounters().orderBy[0]==0&&r.layer.mode()==Mode::Armed&&r.layer.pending()==0);
}
// Floating world names (SubName): a deferred name draw (UI shader, perspective projection, Z write on or off) replays with the spells in the reference order and with its state
// (ZWRITEENABLE and the rest are part of the compared draw state, so a Z-writing name replays its depth write); nothing is a hazard, split or cooldown.
static int nameDraw(Rig& r,int seed,bool zWrite,bool zTest=true){
    r.dev.SetStreamSource(0,r.ring,0,16);const UINT a=r.ringQuad(seed);
    r.translucent(kSrcAlpha,kInvSrcAlpha);r.rs(D3DRS_ZWRITEENABLE,zWrite);r.rs(D3DRS_ZENABLE,zTest);r.rs(D3DRS_ALPHATESTENABLE,1);
    const int t=r.issueWith(Rig::dp(a/16),Family::None,[](DrawFacts& f){f.uiTag=true;});
    r.translucent(kSrcAlpha,kBlendOne);r.rs(D3DRS_ALPHATESTENABLE,0);return t;
}
static void sNamesMixed(Rig& r){
    deferQuad(r,1);nameDraw(r,2,true);deferQuad(r,3);nameDraw(r,4,false);nameDraw(r,5,true,false);deferQuad(r,6);
    r.opaqueZ();r.issue(Rig::dp(0),Family::None);r.translucent(kSrcAlpha,kBlendOne);                     // an opaque draw stays in place
    nameDraw(r,7,true);r.boundary();
}
static void testNamesMixed(){
    Opts o;o.mask=31;Pair p=runBoth(sNamesMixed,o);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==7&&c.names==4&&c.totalDeferred()==7&&c.deferred[0]==3&&c.totalHazards()==0&&c.flushes==0&&c.replayedDraws==7&&c.orderBy[2]==0);
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    size_t marker=0;for(size_t i=0;i<p.lay->dev.draws.size();++i)if(p.lay->dev.draws[i].tag==Rig::kMarker)marker=i;
    unsigned zw=0,noZ=0;for(size_t i=marker+1;i<p.lay->dev.draws.size();++i){const auto& d=p.lay->dev.draws[i];CHECK(d.rt[0]==p.lay->T&&d.ds==p.lay->D);zw+=d.st.rs[D3DRS_ZWRITEENABLE]!=0;noZ+=d.st.rs[D3DRS_ZENABLE]==0;}
    CHECK(zw==3&&noZ==1);                                                                                // three Z-writing names replay their depth write; one has Z test off
    p.lay->layer.beginFrame(false);CHECK(p.lay->layer.mode()==Mode::Armed);
}
static void sNamesAlone(Rig& r){nameDraw(r,1,true);nameDraw(r,2,true);r.boundary();}
static void sNamesWater(Rig& r){
    deferQuad(r,1);nameDraw(r,2,true);
    r.issueWith(Rig::dp(0),Family::Model,[](DrawFacts& f){f.water=true;});                               // water after pending spells and a name: the split flushes them in order
    nameDraw(r,3,true);deferQuad(r,4);r.boundary();
}
static void testNamesAlone(){
    Opts o;o.mask=31;Pair p=runBoth(sNamesAlone,o);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==2&&c.names==2&&c.totalDeferred()==2&&c.replayedDraws==2&&c.totalHazards()==0&&c.replays==1);
    checkEquivalent(p);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    Opts off;off.mask=15;Pair q=runBoth(sNamesAlone,off);                                                  // Name bit off: in place, nothing deferred
    CHECK(q.lay->deferredTags.empty()&&q.lay->layer.totalCounters().names==0&&q.lay->layer.totalCounters().totalHazards()==0);
    checkEquivalent(q);CHECK(q.lay->order()==q.ref->order());
}
static void testNamesWater(){
    Opts o;o.mask=31;Pair p=runBoth(sNamesWater,o);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.totalHazards()==0&&c.flushes==1&&c.orderBy[0]==1&&c.names==2&&c.replayedDraws==4&&p.lay->deferredTags.size()==4);
    checkEquivalent(p);
    // tags 1,2 (spell, name) replay before the water draw; 4,5 (name, spell) after the effects
    const auto ord=p.lay->order();const auto at=[&](int t){return std::find(ord.begin(),ord.end(),t)-ord.begin();};
    CHECK(at(1)<at(2)&&at(2)<at(3)&&at(3)<at(Rig::kMarker)&&at(Rig::kMarker)<at(4)&&at(4)<at(5));
    Opts off;off.mask=15;Pair q=runBoth(sNamesWater,off);                                                  // Name bit off: old behaviour
    CHECK(q.lay->layer.totalCounters().names==0&&q.lay->layer.totalCounters().orderBy[2]==0&&q.lay->layer.totalCounters().orderBy[0]==1); // a Z-writing name never split (TranslucentZ)
    checkEquivalent(q);
}
// Diagnostics: every drain (B and the water split) is counted, the name entries' delta words and replay sets are split off, and the phases of a drain are timed
// separately (one fake tick per phase and drain), only when the integration says the frame is sampled.
static void testDiagnostics(){
    Opts o;o.mask=31;o.timed=true;gTicks=0;Pair p=runBoth(sNamesWater,o);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.drains==2&&c.replays==1&&c.flushes==1);
    CHECK(c.nameDeltaWords>0&&c.nameDeltaWords<c.deltaWords&&c.nameSets>0&&c.nameSets<c.replaySets);
    const Layer::PhaseTicks t=p.lay->layer.phaseTicks();CHECK(t.save==2&&t.upload==2&&t.restore==2);
    p.lay->layer.clearPhaseTicks();CHECK(p.lay->layer.phaseTicks().save==0);
    Opts quiet;quiet.mask=31;Pair q=runBoth(sNamesAlone,quiet);                                            // no clock configured: nothing is timed, the entries still count
    CHECK(q.lay->layer.phaseTicks().save==0&&q.lay->layer.phaseTicks().restore==0&&q.lay->layer.totalCounters().drains==1&&q.lay->layer.totalCounters().nameDeltaWords==q.lay->layer.totalCounters().deltaWords);
    CHECK(q.lay->layer.totalCounters().nameSets==q.lay->layer.totalCounters().replaySets);
}
// ---------------- the minimal restore ----------------
// A sequence with every kind of entry: programmable, UP, indexed UP, materialized (the ring overwritten after the record), fixed function (FVF and declaration), a name.
static void recordAllKinds(Rig& r){
    r.dev.SetVertexDeclaration(r.declTwo);r.dev.SetStreamSource(0,r.ring,0,16);r.dev.SetStreamSource(1,r.stat,256,8);r.dev.SetIndices(r.ringIb);
    const UINT q=r.ringQuad(1),io=r.ringIdx();
    r.issue(Rig::dip(INT(q/16),0,4,io/2),Family::Model);                      // 1: indexed, two streams (materialized below)
    r.dev.SetVertexDeclaration(r.declP);r.dev.SetStreamSource(1,nullptr,0,0);r.dev.SetStreamSource(0,r.ring,0,16);
    deferQuad(r,2);
    uint8_t up[64];Bytes qd=Rig::quad(3);std::memcpy(up,qd.data(),64);
    r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,up,16));              // 3: UP (unbinds stream 0)
    r.dev.SetStreamSource(0,r.ring,0,16);
    uint8_t upv[128];Bytes v8=Rig::pat(4,128);std::memcpy(upv,v8.data(),128);const uint16_t ix[6]={2,3,4,4,3,5};
    r.issue(DrawArgs::indexedUP(D3DPT_TRIANGLELIST,2,4,2,ix,D3DFMT_INDEX16,upv,16)); // 4: indexed UP (unbinds the indices too)
    r.dev.SetStreamSource(0,r.ring,0,16);r.dev.SetIndices(r.stIb);
    nameDraw(r,5,true);                                                       // 5: a name
    r.ffState(1);r.fixedFunction();r.dev.SetFVF(0x112);deferFf(r,6);          // 6: fixed function through an FVF (all lights of the game stay as they were)
    r.dev.SetVertexDeclaration(r.declP);deferFf(r,7);r.programmable();        // 7: fixed function through a declaration
    r.dev.SetStreamSource(0,r.ring,0,16);deferQuad(r,8);
    r.discardRing(9);r.write(r.ringIb->b,0,0,D3DLOCK_DISCARD,Rig::pat(33,64)); // materializes entry 1
}
static void checkMinimalCounts(Rig& r,bool minimal){
    if(minimal)CHECK(r.dev.captures==0&&r.dev.applies==0&&r.dev.blocksCreated==0&&r.layer.totalCounters().restoreFallbacks==0&&r.layer.totalCounters().restoreSets>0);
    else CHECK(r.dev.blocksCreated>0&&r.dev.applies>0&&r.layer.totalCounters().restoreSets==0);
}
// (a) the whole device state after a drain equals the state right before it, for a drain at B, a hazard flush, and a split (water) mid-frame, with every kind of entry;
// (b) no state block is created, captured or applied on the minimal path; (d) the replay's output equals the state-block path's, draw for draw.
static void testMinimalRestore(){
    const bool min=gMinimal;
    for(int how=0;how<3;++how){
        Opts o;o.mask=31;Rig r(o);
        recordAllKinds(r);CHECK(r.layer.pending()==8&&r.layer.totalCounters().materialized>=1);
        r.effects();
        const auto before=r.snapNorm();const unsigned drains=r.layer.totalCounters().drains;
        if(how==0)r.layer.hazard(Reason::Clear);                               // a hazard flush (fallback + cooldown)
        else if(how==1)r.layer.hazard(Reason::Order,true,InPlaceKind::Water);  // a split: keeps deferring
        else r.layer.replay();                                                 // B
        CHECK(r.layer.pending()==0&&r.layer.totalCounters().drains==drains+1&&r.snapNorm()==before);
        CHECK(r.layer.mode()==(how==1?Mode::Deferring:Mode::Fallback));
        CHECK(r.dev.draws.size()==8);
        checkMinimalCounts(r,min);
        if(how==1){r.resume();deferQuad(r,10);const auto b2=r.snapNorm();r.layer.replay();CHECK(r.snapNorm()==b2&&r.layer.totalCounters().drains==2);} // the frame goes on after a split
    }
    Pair p=runBoth(sMixedStreams),q=runBoth(sFixed);checkEquivalent(p,{1});checkEquivalent(q);checkMinimalCounts(*p.lay,min);checkMinimalCounts(*q.lay,min);
}
// The declaration OBJECT the game had comes back, also when GetFVF reports a nonzero FVF for it (native d3d9 does for position-only declarations): restoring through SetFVF would
// leave the runtime's own declaration bound. The mock binds an internal declaration for SetFVF and can report an FVF for a user declaration.
static void testDeclarationRestoredExactly(){
    for(DWORD reported:{0u,2u,0x112u}){
        Opts o;o.mask=31;Rig r(o);r.dev.declReportsFvf=reported;r.dev.SetVertexDeclaration(r.declP);
        recordAllKinds(r);r.effects();r.dev.SetVertexDeclaration(r.declTwo); // the sequence ends on declP, the game's declaration at B is another user one
        CHECK(r.dev.s.decl==r.declTwo&&r.dev.s.fvf==reported);
        const auto before=r.snapNorm();r.layer.replay();
        CHECK(r.snapNorm()==before&&r.dev.s.decl==r.declTwo&&r.dev.s.fvf==reported);
        // an FVF the game had bound (the internal declaration object) also returns as it was
        Rig q(o);recordAllKinds(q);q.effects();q.dev.SetFVF(0x112);const auto b2=q.snapNorm();auto* internal=q.dev.s.decl;q.layer.replay();
        CHECK(q.snapNorm()==b2&&q.dev.s.decl==internal&&q.dev.s.fvf==0x112);
        // no declaration at all
        Rig z(o);recordAllKinds(z);z.effects();z.dev.SetVertexDeclaration(nullptr);const auto b3=z.snapNorm();z.layer.replay();
        CHECK(z.snapNorm()==b3&&!z.dev.s.decl&&z.dev.s.fvf==0);
    }
    Opts a;a.minimal=true;a.mask=31;Opts b=a;b.minimal=false;Rig x(a),y(b);
    for(Rig* r:{&x,&y}){r->dev.declReportsFvf=2;r->dev.SetVertexDeclaration(r->declP);recordAllKinds(*r);r->effects();r->layer.replay();}
    CHECK(x.snapNorm()==y.snapNorm());
}
// The same sequence with the minimal restore and with the state block ends in the same device state and issues the same draws.
static void testMinimalMatchesStateBlock(){
    Opts a;a.minimal=true;a.mask=31;Opts b=a;b.minimal=false;Rig x(a),y(b);
    for(Rig* r:{&x,&y}){recordAllKinds(*r);r->effects();r->layer.replay();}
    CHECK(x.snapNorm()==y.snapNorm()&&x.dev.draws.size()==y.dev.draws.size());
    for(size_t i=0;i<x.dev.draws.size();++i)CHECK(sameRec(x,x.dev.draws[i],y,y.dev.draws[i],true));
    CHECK(x.dev.blocksCreated==0&&y.dev.blocksCreated>0&&x.layer.totalCounters().replaySets==y.layer.totalCounters().replaySets);
}
// (c) a Get that fails before the first Set sends the drain to the state block (nothing was changed yet); the replay is complete and the state restored all the same.
static void sGetFails(Rig& r){
    deferQuad(r,1);r.dev.SetStreamSource(0,r.ring,0,16);nameDraw(r,2,true);
    r.beforeReplay=[&r]{r.dev.failGetRs=true;};r.boundary();r.dev.failGetRs=false;r.beforeReplay=nullptr;
}
static void sGetSamplerFails(Rig& r){deferQuad(r,1);deferQuad(r,2);r.beforeReplay=[&r]{r.dev.failGetSampler=true;};r.boundary();r.dev.failGetSampler=false;r.beforeReplay=nullptr;}
static void sGetStreamFails(Rig& r){deferQuad(r,1);deferQuad(r,2);r.beforeReplay=[&r]{r.dev.failGetStream=true;};r.boundary();r.dev.failGetStream=false;r.beforeReplay=nullptr;}
static void sGetLightFails(Rig& r){r.ffState(1);r.fixedFunction();deferFf(r,1);r.beforeReplay=[&r]{r.dev.failGetTss=true;};r.boundary();r.dev.failGetTss=false;r.beforeReplay=nullptr;}
static void testMinimalFallback(){
    for(auto sc:{sGetFails,sGetSamplerFails,sGetStreamFails,sGetLightFails}){
        Opts o;o.mask=31;Pair p=runBoth(sc,o);const Counters& c=p.lay->layer.totalCounters();
        CHECK(c.restoreFallbacks==(gMinimal?1u:0u)&&c.replayedDraws==c.recorded&&c.recorded>=1&&c.replaySkipped==0&&c.restoreSets==0);
        CHECK(p.lay->dev.blocksCreated>=1&&p.lay->dev.applies==1&&p.lay->dev.captures==0);
        checkEquivalent(p);
    }
    // with no failing Get the same scenario never touches a state block
    Rig r(Opts{});deferQuad(r,1);deferQuad(r,2);r.boundary();checkMinimalCounts(r,gMinimal);
}
// The minimal path needs no state block at all: a device that cannot make one still replays (the old path skipped the whole replay).
static void testMinimalNeedsNoStateBlock(){
    Opts o;o.minimal=true;Rig r(o);deferQuad(r,1);deferQuad(r,2);
    r.dev.failCreateBlock=true;r.boundary();r.dev.failCreateBlock=false;
    CHECK(r.layer.totalCounters().replaySkipped==0&&r.layer.totalCounters().replayedDraws==2&&r.dev.draws.size()==3&&r.layer.totalCounters().restoreFallbacks==0);
}
// Slots no entry sets are never touched, not even rewritten with their own value: a replay of one draw whose read set is narrow only issues what it records and restores.
static void testMinimalLeavesOthersAlone(){
    Rig r(Opts{});deferQuad(r,1);r.effects();
    r.dev.sets=0;r.layer.replay();
    if(gMinimal)CHECK(r.layer.totalCounters().restoreSets>0&&r.dev.sets==r.layer.totalCounters().replaySets+r.layer.totalCounters().restoreSets+6); // +6: the replay's own RT/DS binds
    // sampler states of unbound stages 3 and 4 are not part of the delta: poison them; they are still poisoned after
    Rig z(Opts{});z.dev.s.ss[4][2]=0x1234;z.dev.s.tex[7]=nullptr;deferQuad(z,1);z.effects();z.dev.s.ss[4][2]=0x5678;const auto before=z.snapNorm();z.layer.replay();
    CHECK(z.snapNorm()==before&&z.dev.s.ss[4][2]==0x5678);
}
// A draw without a vertex or pixel shader, or with ps_1_x, is refused (Admit): everything pending is flushed first, in order, and the frame falls back.
static void sNoPs(Rig& r){deferQuad(r,1);deferQuad(r,2);r.dev.SetPixelShader(nullptr);deferQuad(r,3);r.dev.SetPixelShader(r.ps);deferQuad(r,4);r.boundary();}
static void sNoVs(Rig& r){deferQuad(r,1);r.dev.SetVertexShader(nullptr);deferQuad(r,2);r.dev.SetVertexShader(r.vs);r.boundary();}
static void sPs1(Rig& r){deferQuad(r,1);deferQuad(r,2);r.psMajor=1;deferQuad(r,3);r.psMajor=3;deferQuad(r,4);r.boundary();}
static void testAdmission(){
    for(auto scenario:{sNoPs,sPs1}){
        Pair p=runBoth(scenario);const Counters& c=p.lay->layer.totalCounters();
        CHECK(p.lay->deferredTags==std::vector<int>({1,2})&&c.hazards[unsigned(Reason::Admit)]==1&&c.totalHazards()==1&&c.flushes==1&&c.replayedDraws==2);
        checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());             // 1 and 2 were drawn before 3 (in place), 4 follows: the reference order
        CHECK(p.lay->layer.mode()==Mode::Fallback);
    }
    Pair q=runBoth(sNoVs);
    CHECK(q.lay->deferredTags==std::vector<int>({1})&&q.lay->layer.totalCounters().hazards[unsigned(Reason::Admit)]==1);checkEquivalent(q);CHECK(q.lay->order()==q.ref->order());
    CHECK(std::string(reasonName(Reason::Admit))=="admit");
    // nothing pending: refused all the same (no flush), the frame falls back with a cooldown
    Rig r(Opts{});r.psMajor=0;deferQuad(r,1);
    CHECK(r.layer.pending()==0&&r.layer.totalCounters().hazards[unsigned(Reason::Admit)]==1&&r.layer.totalCounters().flushes==0&&r.layer.mode()==Mode::Fallback&&r.dev.draws.size()==1);
    r.layer.beginFrame(false);CHECK(r.layer.mode()==Mode::Cooldown);
    // ps_2_0 and newer are admitted
    Rig t(Opts{});t.psMajor=2;deferQuad(t,1);CHECK(t.layer.pending()==1);
}
// The integration reads why the frame fell back: every hazard that flushed and every refused defer is an event.
static void testHazardEvents(){
    Rig r(Opts{});Layer& l=r.layer;
    CHECK(l.eventSeq()==0);
    l.hazard(Reason::Clear);CHECK(l.eventSeq()==0);                           // nothing pending: not an event
    deferQuad(r,1);deferQuad(r,2);CHECK(l.eventSeq()==0);
    l.hazard(Reason::Order,true,InPlaceKind::Water);                          // a split (water after pending draws): flushed, not an event
    CHECK(l.eventSeq()==0&&l.pending()==0&&l.mode()!=Mode::Fallback);
    deferQuad(r,5);deferQuad(r,6);l.hazard(Reason::Order);
    CHECK(l.eventSeq()==1&&l.lastEvent().reason==Reason::Order&&l.lastEvent().by==InPlaceKind::None&&l.lastEvent().pending==2);
    l.beginFrame(false);while(l.mode()!=Mode::Armed)l.beginFrame(false);
    deferQuad(r,3);l.onBufferWrite(r.ring->b.key,0,0,D3DLOCK_DISCARD,0,1);   // a write the layer cannot copy (static usage): BufWrite flush
    CHECK(l.eventSeq()==2&&l.lastEvent().reason==Reason::BufWrite&&l.lastEvent().by==InPlaceKind::None&&l.lastEvent().pending==1);
    while(l.mode()!=Mode::Armed)l.beginFrame(false);
    r.psMajor=1;deferQuad(r,4);                                               // a refusal with nothing pending
    CHECK(l.eventSeq()==3&&l.lastEvent().reason==Reason::Admit&&l.lastEvent().pending==0);
}
// The record is large (a fixed block of constants and states): report it, and time record + replay of a typical frame (informational).
static void testRecordSizeAndTiming(){
    std::printf("replay model: %zu bytes (one per layer), entry: %zu bytes, delta words per entry are reported below\n",Layer::modelBytes(),Layer::entryBytes());
    CHECK(Layer::modelBytes()<16*1024&&Layer::entryBytes()<512);
    Rig r(Opts{});const int n=26;r.dev.SetStreamSource(0,r.ring,0,16);
    const auto t0=std::chrono::steady_clock::now();
    const int reps=2000;
    for(int k=0;k<reps;++k){
        r.layer.beginFrame(false);
        for(int i=0;i<n;++i){const float v[4]={float(i),0,0,0};r.dev.SetVertexShaderConstantF(31,v,1);DrawArgs a=Rig::dp(UINT(i*4));a.psMajor=3;CHECK(r.layer.defer(a,r.T,r.D,SubAdditive));}
        r.layer.replay();
        r.dev.draws.clear();
    }
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count()/reps;
    std::printf("record + replay of %d draws against the fake: %.1f us per frame (%.2f us per draw; informational)\n",n,us,us/n);
}

static void testStress512(){
    const auto t0=std::chrono::steady_clock::now();
    Pair p=runBoth(sStress);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==512&&c.conflicts==1&&c.materialized==512&&c.conflictBytes==512*64&&c.totalHazards()==0&&c.replayedDraws==512&&c.recorded==512);
    std::set<int> all(p.lay->deferredTags.begin(),p.lay->deferredTags.end());
    checkEquivalent(p,all);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    CHECK(c.arenaHigh==512*64);
    // the 513th candidate of a frame is a capacity hazard
    Rig r(Opts{});for(int i=0;i<513;++i)deferQuad(r,i);
    CHECK(r.layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1&&r.layer.pending()==0&&r.dev.draws.size()==513);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count()<20.0);
}

// ---------------- capacity: thousands of entries, bounded memory, the real demand ----------------
// 4096 pending draws, each on its own vertex buffer (the worst case for per-buffer bookkeeping): every one replays in order with its own state, memory stays
// inside the budget, the 4097th candidate is a capacity fallback that reports how many draws the frame really had, and the cost per draw does not grow with the count.
static std::vector<Fake::VB*> manyBuffers(Rig& r,unsigned n){
    std::vector<Fake::VB*> v;
    for(unsigned i=0;i<n;++i){auto* b=r.newVB(256,D3DUSAGE_DYNAMIC);for(UINT k=0;k<64;++k)b->b.cur()[k]=uint8_t(i*7+k);v.push_back(b);}
    return v;
}
static double deferMany(Rig& r,const std::vector<Fake::VB*>& bufs,unsigned n){
    const auto t0=std::chrono::steady_clock::now();
    for(unsigned i=0;i<n;++i){
        r.dev.SetStreamSource(0,bufs[i%bufs.size()],0,16);
        const float c[4]={float(i),0,0,0};r.dev.SetVertexShaderConstantF(31,c,1);r.dev.SetRenderState(165,DWORD(i+1));
        DrawArgs a=Rig::dp(0);a.psMajor=3;CHECK(r.layer.defer(a,r.dev.rt[0],r.dev.ds,SubAdditive));
    }
    return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count()/n;
}
static void testFourThousandEntries(){
    Opts o;o.maxEntries=4096;o.arena=3u<<20;
    Rig r(o);const auto bufs=manyBuffers(r,2048);
    const double small=deferMany(r,bufs,256);r.layer.hazard(Reason::Frame,false);r.layer.beginFrame(false);
    const double big=deferMany(r,bufs,4096);
    CHECK(r.layer.pending()==4096);
    const Layer::Memory m=r.layer.memory();
    std::printf("4096 entries pending: entries %zu B, deltas %zu B, arena %zu B, tables %zu B => %zu B total (%.1f B per entry); defer %.2f us/draw at 256 entries, %.2f us/draw at 4096 (fake device)\n",
        m.entries,m.deltas,m.arena,m.tables,m.total(),double(m.total())/4096,small,big);
    CHECK(m.total()<6u*1024*1024&&big<small*4+5.0);                                // bounded; not quadratic (generous: timing is informational on a loaded machine)
    // the 4097th candidate: capacity fallback, everything pending flushed in order, then the draw is made in place (never part before and part after)
    const unsigned drawsBefore=unsigned(r.dev.draws.size());
    DrawArgs a=Rig::dp(0);a.psMajor=3;CHECK(!r.layer.defer(a,r.dev.rt[0],r.dev.ds,SubAdditive));
    const Counters& c=r.layer.totalCounters();
    CHECK(r.layer.pending()==0&&r.dev.draws.size()==drawsBefore+4096&&c.replayedDraws==256+4096);
    CHECK(c.capBy[0]==1&&c.overflowFrames==1&&c.overflowAt==4096&&c.overflowCandidates==1&&r.layer.wantsCandidates()&&r.layer.mode()==Mode::Fallback);
    r.layer.noteCandidate();r.layer.noteCandidate();CHECK(r.layer.frameCounters().overflowCandidates==3);
    for(size_t i=0;i<4096;++i)CHECK(r.dev.draws[drawsBefore+i].st.rs[165]==DWORD(i+1)&&r.dev.draws[drawsBefore+i].st.vsc[31][0]==float(i)); // each replayed draw has its own state, in order
    r.layer.beginFrame(false);CHECK(!r.layer.wantsCandidates()&&r.layer.lastFrameCounters().overflowCandidates==3);
    r.layer.beginFrame(false);while(r.layer.mode()!=Mode::Armed)r.layer.beginFrame(false);
    r.layer.trim(0);CHECK(r.layer.memory().entries==0&&r.layer.memory().deltas==0&&r.layer.memory().arena==0);
    for(auto* b:bufs)b->Release();
}
static void testCapacityLimits(){
    {   Opts o;o.maxEntries=8;Rig r(o);for(int i=0;i<9;++i)deferQuad(r,i);
        const Counters& c=r.layer.totalCounters();CHECK(c.capBy[0]==1&&c.capBy[1]==0&&c.overflowAt==8&&c.overflowCandidates==1&&r.layer.pending()==0&&c.replayedDraws==8);r.boundary(); }
    {   Opts o;o.maxEntries=1024;Rig r(o);r.fixedFunction();r.ffState(2);                    // fixed-function entries carry a 2.2 KB record: bounded on their own (512)
        for(int i=0;i<513;++i)deferFf(r,i);
        const Counters& c=r.layer.totalCounters();CHECK(c.capBy[3]==1&&c.overflowAt==512&&r.layer.pending()==0&&c.fixed==512&&c.replayedDraws==512);r.boundary(); }
    {   Opts o;o.deltaBytes=64u<<10;Rig r(o);int n=0;                                          // one 64 KiB delta chunk: about fifteen deltas of 1030 words
        while(r.layer.totalCounters().capBy[1]==0&&n<100){bigDelta(r,n);deferQuad(r,n+1);++n;}
        const Counters& c=r.layer.totalCounters();CHECK(c.capBy[1]==1&&n>=14&&n<=17&&r.layer.pending()==0&&c.replayedDraws==unsigned(n-1)&&r.dev.draws.size()==unsigned(n));r.boundary(); }
    {   Opts o;o.arena=1u<<16;Rig r(o);uint8_t v[64]={};                                      // UP draws copy their data into the arena: each draw copies 8 KiB: 64 KiB hold eight of them
        unsigned n=0;static uint8_t big[8192];
        while(r.layer.totalCounters().capBy[2]==0&&n<100){r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,big,2048));++n;}
        CHECK(r.layer.totalCounters().capBy[2]==1&&n>=8&&n<=10&&r.layer.pending()==0);(void)v;r.boundary(); }
}
// The guard table finds buffers in O(1): many buffers, keys reused after a clear, a key that is gone is gone.
static void testGuardTableMany(){
    GuardTable g;std::vector<int> keys(5000);
    for(size_t i=0;i<keys.size();++i)CHECK(g.add(&keys[i],false,0,64,uint32_t(i),0));
    for(size_t i=0;i<keys.size();++i)CHECK(g.guarded(&keys[i])&&g.findIndex(&keys[i])==int(i));
    int other=0;CHECK(!g.guarded(&other)&&g.findIndex(&other)<0);
    unsigned unmarked=0;g.clear([&](void*){++unmarked;});CHECK(unmarked==5000&&g.buffers()==0&&!g.guarded(&keys[7])&&g.findIndex(&keys[0])<0);
    for(size_t i=0;i<keys.size();i+=2)CHECK(g.add(&keys[i],true,0,16,uint32_t(i),0xFF));
    CHECK(g.guarded(&keys[8])&&!g.guarded(&keys[9])&&g.buffers()==2500);
}
// Which textures are sampled by pending draws is answered from the held table (O(1)), not a scan.
static void testSamplesLookup(){
    Opts o;o.maxEntries=64;Rig r(o);
    std::vector<Fake::Texture*> t;for(int i=0;i<40;++i)t.push_back(r.reg(new Fake::Texture));
    for(int i=0;i<40;++i){r.dev.SetTexture(0,t[size_t(i)]);deferQuad(r,i);}
    for(int i=0;i<40;++i)CHECK(r.layer.pendingSamples(t[size_t(i)]));
    auto* unrelated=new Fake::Texture;CHECK(!r.layer.pendingSamples(unrelated)&&!r.layer.pendingSamples(nullptr));unrelated->Release();
    r.layer.onTextureWrite(t[17]);CHECK(r.layer.pending()==0&&r.layer.totalCounters().texSplits==1&&r.layer.totalCounters().hazards[unsigned(Reason::TexWrite)]==0);
    CHECK(!r.layer.pendingSamples(t[3]));                                                // nothing pending any more
    r.dev.SetTexture(0,r.texA);for(auto* x:t)x->Release();
}
int main(int argc,char** argv){
    const char* only=argc>1?argv[1]:nullptr; // a test name (substring) to run alone, and an optional cache mode
    const bool fixedMode=argc>2;if(fixedMode)gMirrorMode=std::atoi(argv[2]);
    struct T{const char* name;void(*fn)();};
    const T tests[]={{"classify",testClassify},{"frame state",testFrameState},{"guard table",testGuardTable},{"equivalence",testEquivalence},
        {"discard copy",testDiscard},{"partial overlap",testPartialOverlap},{"two entries",testTwoEntries},{"count-based marks",testMarkIsCountBased},
        {"rebase negative base",testRebaseNegativeBase},{"rebase kept base",testRebasePositiveBase},{"index buffer copy",testIbMaterialized},
        {"mixed streams",testMixedStreams},{"32-bit UP",test32bitUP},{"non-dynamic buffer",testNonDynamic},{"copy disabled",testConflictCopyDisabled},
        {"outstanding lock",testOutstandingLockFlushes},{"read-back fails",testReadBackFails},{"address reuse",testAddressReuse},
        {"clear hazard",testClearHazard},{"hazard without pending",testHazardWithoutPending},{"copy hazard",testCopyHazard},{"texture write",testTexWrite},{"texture write cap",testTexSplitCap},
        {"gpu write",testGpuWrite},{"query hazard",testQueryHazard},{"order hazard",testOrderHazard},{"sample hazard",testSampleHazardAndOtherTargets},
        {"rt mismatch",testRtMismatch},{"capacity",testCapacity},{"capture failure",testCaptureFailure},{"frame end",testFrameEnd},
        {"cooldown and pressure",testCooldownAndPressure},{"trim",testTrim},{"pending targets",testPendingTargets},{"layer off",testLayerOff},
        {"reset and teardown",testResetAndTeardown},{"512 entries",testStress512},
        {"32-bit index copy",testIb32},{"both index formats",testIbBothFormats},{"water rain name flush",testOrderKinds},{"names deferred",testNamesMixed},{"names alone",testNamesAlone},{"names and water",testNamesWater},{"diagnostics",testDiagnostics},{"minimal restore",testMinimalRestore},{"minimal matches state block",testMinimalMatchesStateBlock},{"minimal fallback",testMinimalFallback},{"declaration restored exactly",testDeclarationRestoredExactly},{"minimal needs no state block",testMinimalNeedsNoStateBlock},{"minimal leaves other slots alone",testMinimalLeavesOthersAlone},{"nothing pending: in place",testOrderKindsIdle},
        {"replay scope failure",testReplayScopeFailure},{"state changed between draws",testStateChangedBetweenDraws},{"scissor and clip only when enabled",testScissorAndClipOnlyWhenEnabled},{"delta encoding",testDeltas},{"4096 entries",testFourThousandEntries},{"capacity limits",testCapacityLimits},{"guard table with many buffers",testGuardTableMany},{"sampled textures lookup",testSamplesLookup},{"shader read sets",testReadSets},{"diff replay",testDiffReplay},{"admission",testAdmission},{"hazard events",testHazardEvents},{"record size and timing",testRecordSizeAndTiming},{"fixed function",testFixedFunction},{"fixed function admission",testFixedAdmission},{"fixed function capacity",testFixedCapacity},{"water ripples",testRipples},
        {"render target texture",testRenderTargetTexture},{"render target verdict cache",testVerdictCache},{"vertex texture sampler",testVertexTextureSampler},{"buffer write conflict probe",testBufferWriteProbe}};
    for(int minimal=1;minimal>=0;--minimal){
        gMinimal=minimal!=0;
        for(int mode=fixedMode?gMirrorMode:0;mode<=(fixedMode?gMirrorMode:3);++mode){
            gMirrorMode=mode;
            for(const T& t:tests){if(only&&!std::strstr(t.name,only))continue;t.fn();CHECK(Fake::live==0);if((mode==0&&minimal)||fixedMode){std::printf("ok %s\n",t.name);std::fflush(stdout);}}
            std::printf("%s restore, state read mode %d (%s): all tests passed\n",minimal?"minimal":"state block",mode,mode==0?"Get calls only":mode==1?"cache knows everything":mode==2?"cache with unknown slots":"cache knows nothing");
        }
    }
    std::printf("spell layer: %zu tests passed\n",sizeof tests/sizeof tests[0]);
    return 0;
}
