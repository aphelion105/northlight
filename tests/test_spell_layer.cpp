// Task 34: behavioural tests of the production spell_layer.h against the FakeDevice of test_spell_layer_api.h.
#include "test_spell_layer_api.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    {DrawFacts f=cand();f.fixedFunction=true;expect(f,Kind::Hazard,0,Reason::Order);}
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
    // accepted reorders
    {DrawFacts f=cand();f.water=true;f.family=Family::None;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Water);}
    {DrawFacts f=cand();f.rain=true;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Rain);}
    {DrawFacts f=cand();f.uiTag=true;f.family=Family::None;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Name);}
    {DrawFacts f=cand();f.skinned=true;f.fadeRun=true;expect(f,Kind::InPlace,0,Reason::Count,InPlaceKind::Fade);}
    {DrawFacts f=cand();f.skinned=true;expect(f,Kind::Defer,SubAdditive);} // skinned outside a fade run defers
    {DrawFacts f=cand();f.fadeRun=true;expect(f,Kind::Defer,SubAdditive);} // a run only changes skinned draws
    // Z-writing draws never defer
    {DrawFacts f=cand();f.zWrite=true;expect(f,Kind::TranslucentZ);}
    {DrawFacts f=cand();f.zWrite=true;f.srcBlend=kBlendOne;f.destBlend=kBlendZero;expect(f,Kind::Opaque);}
    {DrawFacts f=cand();f.zWrite=true;f.blend=false;expect(f,Kind::Opaque);}
    {DrawFacts f=cand();f.zWrite=true;f.colorWrite=0;expect(f,Kind::Opaque);}
    {DrawFacts f=cand();f.zWrite=true;f.water=true;expect(f,Kind::TranslucentZ);}
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
struct Opts{bool on=true;unsigned maxEntries=512,maxBlocks=512;bool conflictCopy=true;unsigned mask=15;size_t arena=1u<<20,upVb=1u<<16,upIb=1u<<14;};
struct Rig{
    Fake::FakeDevice dev;Layer layer;Opts o;
    Fake::Surface *T,*D,*Other;
    Fake::VB *ring,*stat,*extra,*ring2;Fake::IB *ringIb,*stIb;
    Fake::Decl *declP,*declTwo;Fake::VShader *vs,*vsEff;Fake::PShader *ps;Fake::Texture *texA,*texB;
    std::map<void*,Fake::BufState*> bufs;std::map<void*,long long> ids;std::map<void*,int> marks;
    std::vector<std::pair<void*,bool>> markLog;std::vector<size_t> drawsAtWrite;
    std::vector<int> deferredTags;std::vector<std::vector<long long>> steps;
    UINT ringCur=0,ringIbCur=0;int nextTag=1;Result last;
    static constexpr int kMarker=9000;

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
        ringIb=newIB(1u<<14,D3DUSAGE_DYNAMIC);stIb=newIB(1u<<12,0);
        for(UINT i=0;i<stat->b.size;++i)stat->b.cur()[i]=uint8_t(i*13+5);
        for(UINT q=0;q<64;++q){const uint16_t ix[6]={0,1,2,2,1,3};std::memcpy(stIb->b.cur()+q*12,ix,12);}
        declP=reg(new Fake::Decl({{0,0,2,0,0,0},{0,12,4,0,10,0}}));
        declTwo=reg(new Fake::Decl({{0,0,2,0,0,0},{0,12,4,0,10,0},{1,0,1,0,5,0}}));
        vs=reg(new Fake::VShader);vsEff=reg(new Fake::VShader);ps=reg(new Fake::PShader);texA=reg(new Fake::Texture);texB=reg(new Fake::Texture);
        dev.SetRenderTarget(0,T);dev.SetDepthStencilSurface(D);
        dev.SetVertexShader(vs);dev.SetPixelShader(ps);dev.SetVertexDeclaration(declP);dev.SetTexture(0,texA);dev.SetSamplerState(0,1,3);
        dev.SetStreamSource(0,stat,0,16);dev.SetStreamSource(3,extra,0,8);dev.SetIndices(stIb);
        const float c[4]={1,2,3,4};dev.SetVertexShaderConstantF(5,c,1);
        opaqueZ();translucent(kSrcAlpha,kBlendOne);
        Config c2;c2.on=o.on;c2.classMask=o.mask;c2.maxEntries=o.maxEntries;c2.maxBlocks=o.maxBlocks;c2.arenaBytes=o.arena;c2.uploadVbBytes=o.upVb;c2.uploadIbBytes=o.upIb;
        c2.conflictCopy=o.conflictCopy;c2.markGuard=&Rig::markCb;c2.markCtx=this;
        layer.attach(&dev);layer.configure(c2);layer.beginFrame(false);
    }
    ~Rig(){
        layer.reset();
        for(auto& m:marks)CHECK(m.second==0);
        for(auto* p:std::initializer_list<IUnknown*>{T,D,Other,ring,ring2,stat,extra,ringIb,stIb,declP,declTwo,vs,vsEff,ps,texA,texB})p->Release();
    }
    // ---- game-side helpers ----
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
    std::vector<long long> norm(const Fake::State& s,bool geom){
        std::vector<long long> v;
        for(DWORD x:s.rs)v.push_back(x);
        for(auto& row:s.ss)for(DWORD x:row)v.push_back(x);
        for(auto* t:s.tex)v.push_back(id(t));
        v.push_back(id(s.vs));v.push_back(id(s.ps));v.push_back(id(s.decl));
        for(auto& row:s.vsc)for(float f:row){uint32_t u;std::memcpy(&u,&f,4);v.push_back(u);}
        v.insert(v.end(),{s.vp.X,s.vp.Y,s.vp.Width,s.vp.Height,s.scissor.left,s.scissor.top,s.scissor.right,s.scissor.bottom});
        for(auto& x:s.st){v.push_back(x.freq);if(geom){v.push_back(id(x.vb));v.push_back(x.off);v.push_back(x.stride);}}
        if(geom)v.push_back(id(s.ib));
        return v;
    }
    long long id(const void* p){if(!p)return 0;auto it=ids.find(const_cast<void*>(p));return it==ids.end()?-1:it->second;}
    std::vector<long long> snapNorm(){
        auto v=norm(dev.s,true);for(auto* r:dev.rt)v.push_back(id(r));v.push_back(id(dev.ds));return v;
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
    template<class F> int issueWith(const DrawArgs& a,Family fam,F&& tweak){
        const int tag=nextTag++;const float v[4]={float(tag),0,0,0};dev.SetVertexShaderConstantF(31,v,1);
        DrawFacts f=facts(fam);tweak(f);
        last=classify(f,o.mask);bool deferred=false;
        if(o.on){
            if(last.kind==Kind::Defer)deferred=layer.defer(a,dev.rt[0],dev.ds,last.sub);
            else if(last.kind==Kind::Hazard)layer.hazard(last.reason);
            else if(last.kind==Kind::InPlace)layer.noteInPlace(last.inPlace);
        }
        if(deferred)deferredTags.push_back(tag);else drawInPlace(a);
        record();return tag;
    }
    int issue(const DrawArgs& a,Family fam=Family::Particle){return issueWith(a,fam,[](DrawFacts&){});}
    // B: the effects (other target, own state, a marker draw), then the replay; the device state must be identical across the replay.
    void boundary(){
        dev.SetDepthStencilSurface(nullptr);dev.SetRenderTarget(0,Other);
        D3DVIEWPORT9 vp;vp.Width=100;vp.Height=50;dev.SetViewport(&vp);const RECT sc{1,2,3,4};dev.SetScissorRect(&sc);
        dev.SetVertexShader(vsEff);dev.SetVertexDeclaration(declP);dev.SetTexture(0,texB);dev.SetTexture(2,texA);dev.SetStreamSource(0,extra,8,8);dev.SetStreamSource(1,nullptr,0,0);dev.SetIndices(nullptr);dev.SetRenderTarget(1,Other);
        rs(D3DRS_ZENABLE,0);rs(D3DRS_ALPHABLENDENABLE,0);rs(D3DRS_ZWRITEENABLE,0);
        const float v[4]={float(kMarker),0,0,0};dev.SetVertexShaderConstantF(31,v,1);
        dev.DrawPrimitive(D3DPT_TRIANGLESTRIP,0,2);
        const auto before=snapNorm();
        layer.replay();
        CHECK(snapNorm()==before);CHECK(layer.pending()==0);
        // the game goes on in the world pass
        dev.SetRenderTarget(1,nullptr);
        dev.SetRenderTarget(0,T);dev.SetDepthStencilSurface(D);dev.SetVertexShader(vs);dev.SetTexture(0,texA);dev.SetTexture(2,nullptr);dev.SetStreamSource(0,ring,0,16);
        translucent(kSrcAlpha,kBlendOne);record();
    }
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
    return a.kind==b.kind&&a.prim==b.prim&&a.tag==b.tag&&a.vtx==b.vtx&&a.idx==b.idx&&!a.oob&&!b.oob&&A.id(a.ds)==B.id(b.ds)&&A.norm(a.st,geom)==B.norm(b.st,geom);
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
                int(a.vtx==d.vtx),int(a.idx==d.idx),int(p.ref->norm(a.st,false)==p.lay->norm(d.st,false)),int(a.oob),int(d.oob));
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
    r.issueWith(Rig::dp(e/16),Family::Model,[](DrawFacts& f){f.water=true;}); // water: in place, reorder accepted
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
    CHECK(c.totalDeferred()==7&&c.deferred[0]==5&&c.deferred[1]==1&&c.deferred[2]==1&&c.inPlace[0]==1);
    CHECK(c.totalHazards()==0&&c.conflicts==0&&c.materialized==0&&c.flushes==0&&c.replays==1&&c.replayedDraws==7&&c.replaySkipped==0&&c.blocksCaptured==7);
    CHECK(p.lay->layer.poolSize()==7);
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
    CHECK(Fake::live==base+2);                                                // the buffer (kept by the layer) + the state block
    r.boundary();
    CHECK(Fake::live==base+2);                                                // replayed; the pooled block still holds its last captured references
    r.layer.trim(0);CHECK(Fake::live==base);                                  // trimmed: the buffer dies with the block
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
static void sTexWrite(Rig& r){
    deferQuad(r,1);
    if(r.o.on){
        CHECK(r.layer.pendingSamples(r.texA)&&!r.layer.pendingSamples(r.texB));
        r.layer.onTextureWrite(r.texB);CHECK(r.layer.pending()==1);          // not sampled by a pending draw: no effect
        r.layer.onTextureWrite(r.texA);CHECK(r.layer.pending()==0);
    }
    r.record();deferQuad(r,2);r.boundary();
}
static void testTexWrite(){
    Pair p=runBoth(sTexWrite);const Counters& c=p.lay->layer.totalCounters();
    CHECK(c.hazards[unsigned(Reason::TexWrite)]==1&&p.lay->deferredTags.size()==1);checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
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
    Opts b;b.maxBlocks=3;Pair q=runBoth(sCapacity,b);
    CHECK(q.lay->deferredTags==std::vector<int>({1,2,3})&&q.lay->layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1);checkEquivalent(q);CHECK(q.lay->order()==q.ref->order());
    Opts a;a.arena=128;                                                       // UP bytes: 64 per quad
    Pair u=runBoth([](Rig& r){for(int i=0;i<3;++i){uint8_t v[64];const Bytes b2=Rig::quad(i+1);std::memcpy(v,b2.data(),64);r.issue(DrawArgs::primitiveUP(D3DPT_TRIANGLESTRIP,2,v,16));}r.boundary();},a);
    CHECK(u.lay->deferredTags.size()==2&&u.lay->layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1);checkEquivalent(u);CHECK(u.lay->order()==u.ref->order());
    Opts v;v.upVb=64;                                                         // the upload buffer cannot hold the conflict copy of two quads
    Pair w=runBoth(sDiscard,v);
    CHECK(w.lay->layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1&&w.lay->layer.totalCounters().conflicts==0&&w.lay->drawsAtWrite.back()==2);checkEquivalent(w);CHECK(w.lay->order()==w.ref->order());
}
static void sCaptureFail(Rig& r){
    deferQuad(r,1);deferQuad(r,2);
    r.dev.failNthCreate=r.dev.createCalls+1;                                   // the next CreateStateBlock call fails once: the third entry's
    deferQuad(r,3);deferQuad(r,4);r.boundary();
}
static void testCaptureFailure(){
    Pair p=runBoth(sCaptureFail);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags==std::vector<int>({1,2})&&c.hazards[unsigned(Reason::Capture)]==1&&c.flushes==1&&c.replayedDraws==2);
    checkEquivalent(p);CHECK(p.lay->order()==p.ref->order());
    // a pooled block whose Capture fails (second frame), and nothing pending (the first entry): also falls back
    Rig r(Opts{});deferQuad(r,1);deferQuad(r,2);r.boundary();r.layer.beginFrame(false);
    r.dev.failCaptureAfter=1;const int t1=deferQuad(r,3),t2=deferQuad(r,4);(void)t1;(void)t2;
    CHECK(r.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&r.layer.pending()==0&&r.layer.mode()==Mode::Fallback&&r.layer.poolSize()==1);
    r.layer.beginFrame(false);r.dev.failCaptureAfter=-1;CHECK(r.layer.mode()==Mode::Cooldown);
    Rig z(Opts{});z.dev.failCreateBlock=true;                                 // the very first capture fails: nothing pending, still a fallback with cooldown
    deferQuad(z,1);CHECK(z.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&z.layer.mode()==Mode::Fallback&&z.dev.draws.size()==1);
    z.dev.failCreateBlock=false;
    Rig s(Opts{});s.dev.SetStreamSourceFreq(0,(1u<<30)|4);                    // an instanced stream is not captured
    CHECK(!s.layer.defer(Rig::dp(0),s.T,s.D,SubAdditive)&&s.layer.pending()==0&&s.layer.totalCounters().hazards[unsigned(Reason::Capture)]==1&&s.layer.mode()==Mode::Fallback);
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
    // memory pressure: no new deferral, pool released, counters untouched
    deferQuad(r,6);r.boundary();CHECK(l.poolSize()>0);
    l.beginFrame(true);CHECK(l.mode()==Mode::Cooldown&&l.poolSize()==0);
    const unsigned total=l.totalCounters().totalDeferred();deferQuad(r,7);CHECK(l.pending()==0&&l.totalCounters().totalDeferred()==total);
    l.beginFrame(false);CHECK(l.mode()==Mode::Armed);
    // a frame's counters roll over
    CHECK(l.lastFrameCounters().totalDeferred()==0&&l.frameCounters().totalDeferred()==0);
}
static void testTrim(){
    Rig r(Opts{});deferQuad(r,1);deferQuad(r,2);deferQuad(r,3);
    r.layer.trim(0);CHECK(r.layer.poolSize()==3);                              // never below what is pending
    r.boundary();CHECK(r.layer.poolSize()==3);r.layer.trim(1);CHECK(r.layer.poolSize()==1);r.layer.trim(0);CHECK(r.layer.poolSize()==0);
}
static void testPendingTargets(){
    Rig r(Opts{});CHECK(r.layer.target()==nullptr);deferQuad(r,1);CHECK(r.layer.target()==r.T&&r.layer.depth()==r.D);
    const int live=Fake::live;r.boundary();CHECK(r.layer.target()==nullptr&&Fake::live==live); // T and DS references were held once and let go (the block stays pooled)
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
        CHECK(r.layer.pending()==4&&Fake::live>afterRig);
        const size_t draws=r.dev.draws.size();
        r.layer.reset();
        CHECK(r.dev.draws.size()==draws&&r.layer.pending()==0&&Fake::live==afterRig&&r.layer.poolSize()==0);   // dropped, not drawn; blocks and refs released
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
static void testStress512(){
    const auto t0=std::chrono::steady_clock::now();
    Pair p=runBoth(sStress);const Counters& c=p.lay->layer.totalCounters();
    CHECK(p.lay->deferredTags.size()==512&&c.conflicts==1&&c.materialized==512&&c.conflictBytes==512*64&&c.totalHazards()==0&&c.replayedDraws==512&&c.blocksCaptured==512);
    std::set<int> all(p.lay->deferredTags.begin(),p.lay->deferredTags.end());
    checkEquivalent(p,all);CHECK(p.lay->order()==expectedDeferredOrder(p.ref->order(),p.lay->deferredTags));
    CHECK(c.arenaHigh==512*64);
    // the 513th candidate of a frame is a capacity hazard
    Rig r(Opts{});for(int i=0;i<513;++i)deferQuad(r,i);
    CHECK(r.layer.totalCounters().hazards[unsigned(Reason::Capacity)]==1&&r.layer.pending()==0&&r.dev.draws.size()==513);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count()<20.0);
}

int main(){
    struct T{const char* name;void(*fn)();};
    const T tests[]={{"classify",testClassify},{"frame state",testFrameState},{"guard table",testGuardTable},{"equivalence",testEquivalence},
        {"discard copy",testDiscard},{"partial overlap",testPartialOverlap},{"two entries",testTwoEntries},{"count-based marks",testMarkIsCountBased},
        {"rebase negative base",testRebaseNegativeBase},{"rebase kept base",testRebasePositiveBase},{"index buffer copy",testIbMaterialized},
        {"mixed streams",testMixedStreams},{"32-bit UP",test32bitUP},{"non-dynamic buffer",testNonDynamic},{"copy disabled",testConflictCopyDisabled},
        {"outstanding lock",testOutstandingLockFlushes},{"read-back fails",testReadBackFails},{"address reuse",testAddressReuse},
        {"clear hazard",testClearHazard},{"hazard without pending",testHazardWithoutPending},{"copy hazard",testCopyHazard},{"texture write",testTexWrite},
        {"gpu write",testGpuWrite},{"query hazard",testQueryHazard},{"order hazard",testOrderHazard},{"sample hazard",testSampleHazardAndOtherTargets},
        {"rt mismatch",testRtMismatch},{"capacity",testCapacity},{"capture failure",testCaptureFailure},{"frame end",testFrameEnd},
        {"cooldown and pressure",testCooldownAndPressure},{"trim",testTrim},{"pending targets",testPendingTargets},{"layer off",testLayerOff},
        {"reset and teardown",testResetAndTeardown},{"512 entries",testStress512}};
    for(const T& t:tests){t.fn();CHECK(Fake::live==0);std::printf("ok %s\n",t.name);std::fflush(stdout);}
    std::printf("spell layer: %zu tests passed\n",sizeof tests/sizeof tests[0]);
    return 0;
}
