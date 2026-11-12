#include "packet_batch_fixture.h"
struct PacketTexture:LimitTexture {
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT,D3DSURFACE_DESC* d)override{*d={};d->Width=d->Height=4;d->Format=D3DFMT_A8R8G8B8;d->Pool=D3DPOOL_MANAGED;return D3D_OK;}
};
struct ReuseDevice:PacketDevice {
    bool samplerFailure=false,pixelFailure=false;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD s,IDirect3DBaseTexture9** v)override{*v=state.textures[s];if(*v)(*v)->AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD* v)override{if(samplerFailure){samplerFailure=false;return E_FAIL;}*v=state.samplers[s*14+k];return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT f,float* v,UINT n)override{if(pixelFailure){pixelFailure=false;return E_FAIL;}assert(f+n<=224);std::memcpy(v,state.pf.data()+f*4,n*16);return D3D_OK;}
};
void MaterialCaptureStateTestAccess::captureAllocations(){
    using B=NorthlightAutoBump;ReuseDevice d;PacketTexture texture;for(auto& t:d.state.textures)t=&texture;
    B b(&d);b.profileTag=19;b.setDeferredCapture(true);b.setTextureWriteGuard(true);
    B::Vertex vertex;vertex.kind=4;vertex.patched=&d.vs;vertex.floatOnly=true;vertex.constantFirst=0;vertex.constantCount=4;
    B::NativeCapture model;model.shader=&d.ps;model.first=model.constant=20;model.last=24;model.pixelFloatOnly=true;model.samplers=1;
    float vertices[]={1,1,1,0,2,1,1,0,1,2,1,0},constants[20]={};NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};
    NorthlightAutoNormalCache::Lease normal;NorthlightAutoDisplacement::CaptureSource source;NorthlightDrawSnapshot::Mesh mesh;assert(b.deferredSnapshots.readUP(&d.decl,args.draw,nullptr,args.format,vertices,16,mesh));source.mesh=b.deferredSnapshots.retain(std::move(mesh));source.declaration=&d.decl;d.decl.AddRef();const auto original=source.mesh;
    auto check=[&]{
        for(auto& p:b.sparePackets){assert(!p->mesh&&!p->normal&&!p->vertex&&!p->pixel&&!p->declaration&&!p->gpuIndex&&p->textures.empty());for(auto v:p->gpuVertex)assert(!v);}
        if(b.memoryPressure)assert(b.sparePackets.empty());
    };
    auto record=[&](bool shared=true,bool single=false){B::ReplayState saved(&d,&d.vs,d.state.viewport);const bool result=b.recordCapture(args,normal,false,saved,model,vertex,constants,D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,single,shared?&source:nullptr);check();return result;};
    auto clear=[&]{const auto alloc=allocationCount;b.clearDeferred();assert(alloc==allocationCount);check();assert(texture.refs==1);};
    auto configure=[&](unsigned phase){b.reset();b.setMemoryPressurePolicy(false,false);if(phase==1)b.setMemoryPressurePolicy(true,true);if(phase==2)b.setMemoryPressurePolicy(true,false);if(phase==3){b.setMemoryPressurePolicy(true,true);b.setMemoryPressurePolicy(false,false);}model.first=model.constant=20;model.last=24;model.samplers=1;source.mesh=original;};
    for(unsigned phase=0;phase<4;++phase)for(unsigned draws:{8u,64u,128u,256u}){
        configure(phase);unsigned accepted=0,barriers=0;uint64_t hash=0;const auto startAlloc=allocationCount;
        for(unsigned i=0;i<draws;++i){for(unsigned j=0;j<20;++j)constants[j]=float(i+1)*.001f+float(j)*.01f;if(!record()){assert(!b.deferred.empty());clear();++barriers;assert(record());}++accepted;const auto& packet=*b.deferred.back();for(float v:packet.pixelConstants){unsigned bits;std::memcpy(&bits,&v,4);hash=hash*131+bits;}assert(packet.mesh==source.mesh);}
        clear();b.endFrame();check();if(phase==1||phase==2)assert(b.sparePackets.empty());const auto allocations=allocationCount-startAlloc;
        std::printf("PACKET_CASE phase=%u draws=%u accepted=%u barriers=%u allocations=%zu hash=%llu endEmpty=%u\n",phase,draws,accepted,barriers,allocations,(unsigned long long)hash,unsigned(b.sparePackets.empty()));
    }
    configure(1);
    // Changing constant spans and sampler aliases preserves capture layout.
    model.first=0;model.last=223;model.samplers=65535;assert(record());clear();model.first=model.constant=20;model.last=24;model.samplers=1;assert(record());
    assert(b.deferred.back()->pixelConstants.capacity()==20&&b.deferred.back()->textures.capacity()==1);clear();check();
    // Large immutable payloads retain the existing admission budget.
    for(unsigned i=0;i<64;++i)assert(record());clear();
    auto giant=std::make_shared<NorthlightDrawSnapshot::Mesh>(*original);giant->streams[0].bytes.resize(b.deferredByteLimit-sizeof(B::Deferred)-1024);source.mesh=giant;
    assert(record());check();clear();source.mesh=original;
    // Early failures release the local packet and all borrowed resources.
    model.first=0;model.last=223;model.samplers=65535;assert(record());clear();model.first=model.constant=20;model.last=24;model.samplers=1;
    failAllocationAfter=0;assert(!record());assert(failAllocationAfter==-1);check();assert(texture.refs==1);
    for(unsigned failure=0;failure<4;++failure){assert(record());clear();if(failure==0)d.samplerFailure=true;if(failure==1){d.pixelFailure=true;model.first=0;}if(failure==2)b.singleDepthRetry=1;if(failure==3)assert(b.deferredSnapshots.configureBudget(1,0));
        assert(!record(failure!=3,failure==2));check();assert(texture.refs==1);model.first=20;b.singleDepthRetry=0;if(failure==3)assert(b.deferredSnapshots.configureBudget(2u<<20,0));
    }
    configure(0);b.deferred.reserve(64);b.deferredTextures.reserve(64);
    // Warm exact buffers before checking map allocations alone.
    model.samplers=15;for(unsigned i=0;i<8;++i)assert(record());clear();model.samplers=1;assert(record());
    const auto duplicateStart=allocationCount;failAllocationAfter=0;const bool duplicate=record();const auto remaining=failAllocationAfter;failAllocationAfter=-1;
#ifdef NORTHLIGHT_TEXTURE_INTERN_TEST
    assert(duplicate&&remaining==0&&allocationCount==duplicateStart&&"DUPLICATE_TEXTURE_MUST_NOT_ALLOCATE");
#else
    assert(!duplicate&&remaining==-1&&allocationCount==duplicateStart+1);
#endif
    assert(b.deferredTextures.size()==1&&b.deferredTextureBytes==64);clear();
    configure(0);b.deferred.reserve(64);b.deferredTextures.reserve(64);
    for(unsigned i=0;i<64;++i)assert(record());clear();
    {PacketTexture unique[64];model.samplers=1;
     const auto before=allocationCount;for(unsigned i=0;i<64;++i){d.state.textures[0]=unique+i;assert(record());}const auto allocations=allocationCount-before;
     assert(b.deferredTextures.size()==64&&b.deferredTextureBytes==64*64);clear();for(auto& t:unique)assert(t.refs==1);
     std::printf("UNIQUE_TEXTURE_CASE allocations=%zu\n",allocations);
    }
    for(auto& t:d.state.textures)t=&texture;
    model.samplers=15;for(unsigned i=0;i<64;++i)assert(record());clear();
    {PacketTexture other[2];
     auto setupA=[&]{clear();for(auto& t:d.state.textures)t=&texture;model.samplers=1;assert(record());};
     auto stages=[&]{d.state.textures[1]=&texture;d.state.textures[2]=&other[0];d.state.textures[3]=&other[1];};
     auto rolledBack=[&]{assert(b.deferred.size()==1&&b.deferredTextures.size()==1&&b.deferredTextureBytes==64&&b.deferredTextures.count(&texture)&&!b.deferredTextures.count(other)&&!b.deferredTextures.count(other+1)&&"DEFERRED_TEXTURE_ROLLBACK_MISMATCH");assert(texture.refs==2&&other[0].refs==1&&other[1].refs==1);check();};
     // Calibrate the prefix of this exact operation, rather than hard-coding
     // allocation ordinals that intentionally differ between STL insertions.
     setupA();stages();model.samplers=7;auto before=allocationCount;assert(record());const auto prefix=allocationCount-before;
     setupA();stages();model.samplers=15;failAllocationAfter=int(prefix);assert(!record());assert(failAllocationAfter==-1);rolledBack();
     setupA();stages();model.samplers=15;before=allocationCount;assert(record());const auto inserts=allocationCount-before;
     setupA();stages();model.samplers=15;
     {std::vector<std::unique_ptr<B::Deferred>> tight;tight.reserve(1);tight.push_back(std::move(b.deferred.front()));b.deferred.swap(tight);}
     failAllocationAfter=int(inserts);assert(!record());assert(failAllocationAfter==-1);rolledBack();clear();
     std::printf("TEXTURE_INTERN_PASS duplicateNoAllocation=%u aliasStages=1 uniqueControl=1 insertFailureRollback=1 pushFailureRollback=1\n",unsigned(duplicate));
    }
    for(auto& t:d.state.textures)t=&texture;model.samplers=1;
    configure(1);
    for(unsigned release=0;release<6;++release){assert(record());clear();assert(record());if(release==0)b.endFrame();if(release==1)b.trimMemory();if(release==2)b.reset();if(release==3){b.setMemoryPressurePolicy(false,false);b.setMemoryPressurePolicy(true,true);}if(release==4){clear();b.setDeferredCapture(false);}if(release==5){auto q=NorthlightQuality::Settings{};q.autoBump=0;b.configure(q);}assert(b.sparePackets.empty());check();if(release==4)b.setDeferredCapture(true);}
    b.reset();check();assert(texture.refs==1&&d.vs.refs==1&&d.ps.refs==1);
    std::puts("MATERIAL_CAPTURE_ALLOCATIONS_PASS sameAdmission=1 aliases=1 uniqueControl=1 rollback=1 frameAndTrimRelease=1 leasesDropped=1");
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);MaterialCaptureStateTestAccess::captureAllocations();}
