#define main displacement_state_reference_main
#include "displacement_state_reuse.cpp"
#undef main
#define main subdivision_reference_main
#include "displacement_subdivision.cpp"
#undef main
#include "draw_world_state.h"

template<class Base,class Description,D3DRESOURCETYPE Type> struct CaptureBuffer:Base {
    unsigned refs=1;std::vector<unsigned char> data;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID)override{return E_FAIL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD)override{return 0;}
    DWORD STDMETHODCALLTYPE GetPriority()override{return 0;}
    void STDMETHODCALLTYPE PreLoad()override{}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return Type;}
    HRESULT STDMETHODCALLTYPE Lock(UINT,UINT,void**,DWORD)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE Unlock()override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDesc(Description*)override{return E_FAIL;}
};
using CaptureVB=CaptureBuffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC,D3DRTYPE_VERTEXBUFFER>;
using CaptureIB=CaptureBuffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC,D3DRTYPE_INDEXBUFFER>;
static volatile uint64_t accelerationSink=0;

static void heldDrawViewport(){
    struct Reader {
        unsigned viewReads=0,depthReads=0;
        bool viewportOk=true;
        HRESULT GetViewport(D3DVIEWPORT9* out){++viewReads;*out={0,0,128,128,0,1};return viewportOk?D3D_OK:E_FAIL;}
        HRESULT GetRenderState(D3DRENDERSTATETYPE key,DWORD* out){assert(key==D3DRS_ZENABLE);++depthReads;*out=FALSE;return D3D_OK;}
    } reader;
    DeviceMirror mirror;MirrorGuard held(mirror.gate);
    mirror.viewport={0,0,128,128,0,.94f};mirror.viewportKnown=true;
    mirror.renderStateKnown[D3DRS_ZENABLE]=true;mirror.renderStates[D3DRS_ZENABLE]=TRUE;
    auto query=[&]{return NorthlightDrawPreparation::readHeldWorldState(&reader,mirror,true,0,.94f);};
    const auto known=query();assert(known.read&&known.inWorld&&known.viewport.MaxZ==.94f&&!reader.viewReads&&!reader.depthReads);
    ++mirror.targetWrites;
    const auto duringTarget=query();assert(duringTarget.read&&!duringTarget.inWorld&&duringTarget.viewport.MaxZ==1&&reader.viewReads==1&&reader.depthReads==1);
    --mirror.targetWrites;mirror.renderStateKnown[D3DRS_ZENABLE]=false;
    reader.viewportOk=false;
    assert(!query().read&&reader.viewReads==2&&reader.depthReads==1);

    using NorthlightDrawPreparation::TerrainAdmission;
    TerrainAdmission terrain;
    assert(!terrain.matches(mirror));
    terrain.target=mirror.targets[0]=reinterpret_cast<IDirect3DSurface9*>(1);
    terrain.depth=mirror.depth=reinterpret_cast<IDirect3DSurface9*>(2);
    mirror.targetKnown[0]=mirror.depthKnown=true;
    terrain.viewport=mirror.viewport;
    std::fill_n(mirror.vsFloatKnown+4,4,true);
    assert(terrain.matches(mirror));
    mirror.vsFloat[8][0]=2;
    assert(terrain.matches(mirror));
    mirror.vsFloat[4][0]=1;
    assert(!terrain.matches(mirror));
    mirror.vsFloat[4][0]=0;
    mirror.vsFloatKnown[5]=false;
    assert(!terrain.matches(mirror));
    mirror.vsFloatKnown[5]=true;
    mirror.viewport.MaxZ=1;
    assert(!terrain.matches(mirror));
    mirror.viewport=terrain.viewport;
    ++mirror.targetWrites;
    assert(!terrain.matches(mirror));
    --mirror.targetWrites;
    mirror.depthKnown=false;
    assert(!terrain.matches(mirror));
    mirror.depthKnown=true;
    mirror.targets[0]=mirror.depth;
    assert(!terrain.matches(mirror));
    mirror.targets[0]=terrain.target;
    assert(terrain.matches(mirror));
}

void DisplacementStateTestAccess::acceleration(bool benchmark){
    heldDrawViewport();
    Fixture fixture(16,false);G::Mesh reference;assert(fixture.build(false,reference,4,false,true,false,4));
    auto changed=fixture;for(float& value:changed.height.values)value=1-value;
    G::Mesh independent;assert(changed.build(false,independent,4,false,true,false,4));equal(reference,independent);
    // Equal height peaks permit exact GPU-height geometry reuse, with each
    // material retaining its own height texture. Different peaks cannot reuse.
    NorthlightAutoDisplacement g(nullptr);auto source=std::make_shared<const Mesh>(fixture.mesh);
    Declaration sourceDecl,gpuDecl;LimitTexture height;
    auto upload=[&](const Mesh& mesh){
        auto e=std::make_unique<NorthlightAutoDisplacement::Entry>();
        auto* vb=new CaptureVB;vb->data=mesh.streams[0].bytes;e->vb=vb;
        auto* ib=new CaptureIB;ib->data.resize(mesh.indices.size()*G::indexElementBytes(mesh));G::copyIndices(mesh,ib->data.data());e->ib=ib;
        e->decl=&gpuDecl;gpuDecl.AddRef();e->sourceDecl=&sourceDecl;sourceDecl.AddRef();e->source=source;e->height=&height;height.AddRef();
        e->vertices=mesh.vertexCount;e->primitives=mesh.primitiveCount;e->stride=mesh.streams[0].stride;e->program=7;e->domain=4;e->detail=2;e->address=3;e->uvConstants={1,2,3};
        e->bytes=vb->data.size()+ib->data.size()+4096;
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
        e->geometryBytes=vb->data.size()+ib->data.size();e->subdivisionFactor=4;e->heightPeak=fixture.height.peak();
#endif
        return e;
    };
    g.retain(upload(reference));auto* original=g.entries.front().get();
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
    auto find=[&](const std::shared_ptr<const Mesh>& mesh=std::shared_ptr<const Mesh>{},unsigned factor=4,float peak=-1){return g.subdivisionCandidate(mesh?mesh:source,0,false,&sourceDecl,7,4,2,3,{1,2,3},factor,peak<0?fixture.height.peak():peak);};
    assert(find()==original);assert(!find({},8));assert(!find({},4,.125f));
    assert(!g.subdivisionCandidate(source,0,false,&gpuDecl,7,4,2,3,{1,2,3},4,fixture.height.peak()));
    assert(!g.subdivisionCandidate(source,0,false,&sourceDecl,8,4,2,3,{1,2,3},4,fixture.height.peak()));
    assert(!g.subdivisionCandidate(source,0,false,&sourceDecl,7,2,2,3,{1,2,3},4,fixture.height.peak()));
    assert(!g.subdivisionCandidate(source,0,false,&sourceDecl,7,4,3,3,{1,2,3},4,fixture.height.peak()));
    assert(!g.subdivisionCandidate(source,0,false,&sourceDecl,7,4,2,4,{1,2,3},4,fixture.height.peak()));
    assert(!g.subdivisionCandidate(source,0,false,&sourceDecl,7,4,2,3,{1,2,4},4,fixture.height.peak()));
    auto revised=std::make_shared<Mesh>(*source);revised->streams[0].bytes[0]^=1;assert(!find(revised));
    const auto initial=g.resident;auto shared=std::make_unique<NorthlightAutoDisplacement::Entry>();
    g.shareSubdivision(*shared,*original);shared->source=source;shared->geometryBytes=original->geometryBytes;shared->bytes=original->bytes;
    g.retain(std::move(shared));assert(g.resident==initial+4096);
    g.evictOldest();assert(g.entries.size()==1&&g.resident==initial);
    g.evictOldest();assert(g.entries.empty()&&!g.resident);g.retain(upload(reference));original=g.entries.front().get();
#endif
    double ms=0;uint64_t digest=0;const unsigned draws=benchmark?64:4;
    for(unsigned i=0;i<draws;++i){
        const auto start=std::chrono::steady_clock::now();std::unique_ptr<NorthlightAutoDisplacement::Entry> next;
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
        auto* candidate=find();assert(candidate);next=std::make_unique<NorthlightAutoDisplacement::Entry>();g.shareSubdivision(*next,*candidate);
#else
        G::Mesh mesh;assert(changed.build(false,mesh,4,false,true,false,4));next=upload(mesh);
#endif
        ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        assert(next->vertices==reference.vertexCount&&next->primitives==reference.primitiveCount);
        assert(static_cast<CaptureVB*>(next->vb)->data==reference.streams[0].bytes);
        assert(static_cast<CaptureIB*>(next->ib)->data==static_cast<CaptureIB*>(original->ib)->data);
        digest=digest*33+next->vertices+next->primitives;
    }
    std::printf("CAPTURE_ACCEL stage=subdivision cpuMs=%.6f operations=%u digest=%llu\n",ms,draws,(unsigned long long)digest);
    g.reset();assert(sourceDecl.refs==1&&gpuDecl.refs==1&&height.refs==1);

    CaptureBackend backend;DeviceMirror mirror;ExtensionDevice extension(&backend,&mirror);
    NorthlightAutoDisplacement capture(&extension);capture.setTrustedConstantEpochSource(extension.trustedConstantSource());
    float bank[1024];for(unsigned i=0;i<1024;++i)bank[i]=float(i)*.03125f;
    const unsigned reads=benchmark?2048:64;double elapsed=0;
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
    NorthlightAutoDisplacement::CaptureSource facts;
#endif
    {MirrorGuard held(mirror.gate);assert(SUCCEEDED(extension.SetVertexShaderConstantF(0,bank,256)));
        for(unsigned i=0;i<reads;++i){
            bank[124+i%900]=float(i);assert(SUCCEEDED(extension.SetVertexShaderConstantF(31,bank+124,225)));
            assert(capture.readConstantBank(0,256,true,capture.rangeConstants,capture.rangeConstantStamp,capture.rangeConstantFirst,capture.rangeConstantCount));
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
            capture.publishConstants(facts,capture.rangeConstants,capture.rangeConstantStamp);
            const auto* previousRange=capture.rangeConstants;
#endif
            const auto start=std::chrono::steady_clock::now();
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
            assert(capture.readConstants(0,256,true,&facts.bank));
#else
            assert(capture.readConstants(0,256,true));
#endif
            elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            assert(!std::memcmp(capture.constants,bank,sizeof bank));
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
            assert(capture.constants==previousRange&&capture.constantBankTransfers==i+1);
#endif
            accelerationSink+=unsigned(capture.constants[i%1024]);
        }
        std::printf("CAPTURE_ACCEL stage=constants cpuMs=%.6f operations=%u digest=%llu\n",elapsed,reads,(unsigned long long)accelerationSink);
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
        const auto reader=capture.constantReader;
        capture.constantReader=+[](void*,NorthlightConstantEpoch::Stamp&)->bool{assert(false);return false;};
        assert(capture.readConstants(0,256,true)&&!std::memcmp(capture.constants,bank,sizeof bank));
        capture.constantReader=reader;
        // A changed bank between admission and prepare must fetch the new pose.
        const auto transfers=capture.constantBankTransfers;
        assert(capture.readConstantBank(0,256,true,capture.rangeConstants,capture.rangeConstantStamp,capture.rangeConstantFirst,capture.rangeConstantCount));
        capture.publishConstants(facts,capture.rangeConstants,capture.rangeConstantStamp);
        bank[124]=1234;assert(SUCCEEDED(extension.SetVertexShaderConstantF(31,bank+124,1)));
        assert(capture.readConstants(0,256,true,&facts.bank)&&capture.constantBankTransfers==transfers&&!std::memcmp(capture.constants,bank,sizeof bank));
        for(unsigned change=0;change<3;++change){
            assert(capture.readConstantBank(0,256,true,capture.rangeConstants,capture.rangeConstantStamp,capture.rangeConstantFirst,capture.rangeConstantCount));
            capture.publishConstants(facts,capture.rangeConstants,capture.rangeConstantStamp);
            if(change==0)mirror.constantEpoch.writeBool(1);
            if(change==1)mirror.constantEpoch.writeInt(1);
            if(change==2)mirror.constantEpoch.invalidate();
            assert(capture.readConstants(0,256,true,&facts.bank)&&capture.constantBankTransfers==transfers);
            assert(!std::memcmp(capture.constants,bank,sizeof bank));
        }
        assert(capture.readConstantBank(0,256,true,capture.rangeConstants,capture.rangeConstantStamp,capture.rangeConstantFirst,capture.rangeConstantCount));
        capture.publishConstants(facts,capture.rangeConstants,capture.rangeConstantStamp);
#endif
    }
#ifdef NORTHLIGHT_CAPTURE_ACCELERATION
    const auto transfers=capture.constantBankTransfers;
    assert(capture.readConstants(0,256,true,&facts.bank)&&capture.constantBankTransfers==transfers);
    capture.reset();assert(capture.readConstants(0,256,true,&facts.bank)&&capture.constantBankTransfers==transfers);
#endif

    std::puts("CAPTURE_ACCELERATION_PASS exact_geometry shared_lifetime byte_ledger complete_keys range_bank_transfer changed_pose gate reset");
}
int main(int argc,char** argv){DisplacementStateTestAccess::acceleration(argc==2&&std::string(argv[1])=="--benchmark");}
