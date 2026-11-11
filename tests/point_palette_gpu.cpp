// Production point-cube batching, source proof, fault fallback and paired timings.
#undef NDEBUG
#include "renderer.cpp"
#include "actor_client_programs.h"
#include <cassert>
#include <cstdio>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PaletteFaults:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned fault=0,hits=0;bool after=false;
    IDirect3DVertexBuffer9 *wrapped=nullptr,*wrappedRaw=nullptr;
    struct Buffer:ForwardIDirect3DVertexBuffer9 {
        PaletteFaults& owner;ULONG refs=1;
        Buffer(IDirect3DVertexBuffer9* value,PaletteFaults& o):ForwardIDirect3DVertexBuffer9(value),owner(o){}
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
        ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){if(owner.wrapped==this)owner.wrapped=owner.wrappedRaw=nullptr;real->Release();delete this;}return n;}
        HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** out,DWORD flags)override{
            if(owner.fault!=4)return real->Lock(offset,size,out,flags);owner.fault=0;++owner.hits;
            if(owner.after)must(real->Lock(offset,size,out,flags));else *out=nullptr;return E_FAIL;
        }
        HRESULT STDMETHODCALLTYPE Unlock()override{
            if(owner.fault!=5)return real->Unlock();owner.fault=0;++owner.hits;if(owner.after)must(real->Unlock());return E_FAIL;
        }
    };
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{
        if(fault!=1||code!=kLocalPaletteShadowVSShader)return real->CreateVertexShader(code,out);fault=0;++hits;
        if(after)must(real->CreateVertexShader(code,out));else *out=nullptr;return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* code,IDirect3DVertexDeclaration9** out)override{
        if(fault!=2||code[2].Stream!=1)return real->CreateVertexDeclaration(code,out);fault=0;++hits;
        if(after)must(real->CreateVertexDeclaration(code,out));else *out=nullptr;return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT size,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* handle)override{
        const bool target=size==84*sizeof(float)&&usage==D3DUSAGE_WRITEONLY&&pool==D3DPOOL_DEFAULT;
        if(target&&fault==3){fault=0;++hits;if(after)must(real->CreateVertexBuffer(size,usage,fvf,pool,out,handle));else *out=nullptr;return E_FAIL;}
        const auto hr=real->CreateVertexBuffer(size,usage,fvf,pool,out,handle);if(target&&SUCCEEDED(hr)){wrappedRaw=*out;wrapped=new Buffer(*out,*this);*out=wrapped;}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream,IDirect3DVertexBuffer9* value,UINT offset,UINT stride)override{return real->SetStreamSource(stream,value==wrapped?wrappedRaw:value,offset,stride);}
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT stream,UINT value)override{
        if(fault!=6||!(value&D3DSTREAMSOURCE_INSTANCEDATA))return real->SetStreamSourceFreq(stream,value);fault=0;++hits;
        if(after)must(real->SetStreamSourceFreq(stream,value));return E_FAIL;
    }
};
struct PointTerrainCoverageTest {
    struct Vertex {float x,y,z;BYTE weights[4],bones[4];float nx,ny,nz,u,v;DWORD color;};
    static void run(IDirect3DDevice9* device,bool timing){
        PaletteFaults calls(device);auto* d=&calls;WorldRenderer w(d);w.valid=true;
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.uploadedMap="point-palette";w.meshGeneration=1;
        const auto& words=ClientShaders::OneBoneVs3;auto program=std::make_shared<NorthlightActorDeformation::Program>();
        assert(NorthlightActorDeformation::compile(words,std::size(words),*program)&&program->kernel.singleSkin());
        IDirect3DVertexShader9 *original=nullptr,*shader=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(words),&original));
        std::vector<uint32_t> patched,sliced;assert(NorthlightShadowShader::patch(words,std::size(words),patched));
        if(NorthlightModelNormalSlice::shadowVertex(patched.data(),patched.size(),sliced))patched.swap(sliced);
        must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader));w.actorPrograms[original]=program;
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},
            {0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
            {0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        IDirect3DVertexDeclaration9* decl=nullptr;must(d->CreateVertexDeclaration(elements,&decl));
        const D3DVERTEXELEMENT9 world[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(world,&w.shadowDecl));
        for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
        std::vector<Vertex> vertices;constexpr unsigned grid=16;
        for(unsigned y=0;y<=grid;++y)for(unsigned x=0;x<=grid;++x){Vertex v{};v.x=(float(x)/grid-.5f)*1.2f;v.y=(float(y)/grid-.5f)*1.2f;v.z=.08f*std::sin(float(x+y));v.weights[0]=255;v.bones[0]=BYTE((x+y*3)%21);v.nz=1;v.color=0xffffffff;vertices.push_back(v);}
        auto source=std::make_shared<NorthlightDrawSnapshot::Mesh>();source->vertexCount=UINT(vertices.size());source->indexed=true;
        for(unsigned y=0;y<grid;++y)for(unsigned x=0;x<grid;++x){const unsigned a=y*(grid+1)+x,b=a+1,c=a+grid+1,e=c+1;source->indices.insert(source->indices.end(),{a,b,c,b,e,c});}
        source->primitiveCount=UINT(source->indices.size()/3);source->streams[0].stride=sizeof(Vertex);source->streams[0].bytes.resize(vertices.size()*sizeof(Vertex));std::memcpy(source->streams[0].bytes.data(),vertices.data(),source->streams[0].bytes.size());
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* data=nullptr;
        must(d->CreateVertexBuffer(UINT(source->streams[0].bytes.size()+8*sizeof(Vertex)),0,0,D3DPOOL_MANAGED,&vb,nullptr));must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,vertices.data(),source->streams[0].bytes.size());must(vb->Unlock());
        must(d->CreateIndexBuffer(UINT((source->indices.size()+7)*4),0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));must(ib->Lock(7*sizeof(uint32_t),UINT(source->indices.size()*4),&data,0));std::memcpy(data,source->indices.data(),source->indices.size()*4);must(ib->Unlock());
        w.meshBuffers.resize(1);w.meshBuffers[0].vb=vb;w.meshBuffers[0].ib=ib;w.vertices=vb;w.indices=ib;
        for(unsigned a=0;a<100;++a){auto p=std::make_unique<WorldRenderer::Replay>();p->shared=source;p->program=program.get();
            p->originalShader=original;original->AddRef();p->shader=shader;shader->AddRef();p->decl=decl;decl->AddRef();p->stream[0]=vb;vb->AddRef();p->stride[0]=sizeof(Vertex);p->index=ib;ib->AddRef();
            p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=5;p->offset[0]=3*sizeof(Vertex);p->min=0;p->vertices=source->vertexCount;p->start=7;p->count=source->primitiveCount;p->projectionKind=2;p->constantUsage=NorthlightShaderConstants::analyze(words,std::size(words),2);p->constantGroup=a;
            const float angle=float(a)*6.2831853f/100,tx=5*std::cos(angle),ty=5*std::sin(angle),tz=4+(float(a%7)-3)*.6f;
            for(unsigned b=0;b<75;++b){float* c=p->constantStorage+4*(31+3*b);c[0]=c[5]=c[10]=1;c[3]=tx+float(b%3)*.005f;c[7]=ty;c[11]=tz;}w.replays.push_back(std::move(p));}
        auto bounds=[&](unsigned expected){for(auto& p:w.replays){p->pointBounds={};p->boundsWork={};}
            for(unsigned pass=0;pass<128;++pass){w.replayBoundsBegin();w.pointCalculateReplayBounds();bool complete=true;for(const auto& p:w.replays)complete&=p->pointBounds.valid&&p->boundsWork.paletteSpan==expected;if(complete){w.replayBoundsResolved=true;return;}if(pass==127)std::fprintf(stderr,"BOUNDS expected=%u valid=%u firstSpan=%u firstKind=%u\n",expected,w.pointBoundsValid,w.replays[0]->boundsWork.paletteSpan,unsigned(w.replays[0]->boundsWork.kind));assert(pass<127);}};
        bounds(21);
        for(auto state:{D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})must(d->SetRenderState(state,FALSE));
        must(d->SetRenderState(D3DRS_ZENABLE,TRUE));must(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));must(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        IDirect3DSurface9* read=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr));IDirect3DQuery9* event=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));
        auto drain=[&]{must(event->Issue(D3DISSUE_END));const auto start=GetTickCount();HRESULT hr;while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<30000);SwitchToThread();}must(hr);};
        auto image=[&]{std::vector<float> out(6*256*256);for(unsigned f=0;f<6;++f){must(d->GetRenderTargetData(w.pointSlot().faces[f],read));D3DLOCKED_RECT lock{};must(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y)std::memcpy(out.data()+f*256*256+y*256,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(read->UnlockRect());}return out;};
        auto compare=[&](const std::vector<float>& a,const std::vector<float>& b){assert(a.size()==b.size());size_t covered=0;for(size_t i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i])&&std::fabs(a[i]-b[i])<=2e-6f);covered+=a[i]<.999f;}assert(covered>1000);};
        NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};
        auto render=[&](bool optimized){w.pointPaletteUnavailable=!optimized;w.pointSlot().schedule.invalidate();w.pointSlot().retryAt=0;w.pointWork={};must(d->BeginScene());const auto result=w.renderPointShadow(light,true,true);must(d->EndScene());drain();for(unsigned stream=0;stream<2;++stream){UINT freq=0;must(d->GetStreamSourceFreq(stream,&freq));assert(freq==1);}return result;};
        unsigned comparisons=0;
        for(unsigned pose=0;pose<3;++pose){if(pose){const float angle=float(pose)*.3f;w.context.inverseView[0]=w.context.inverseView[5]=std::cos(angle);w.context.inverseView[1]=std::sin(angle);w.context.inverseView[4]=-std::sin(angle);
                for(auto& p:w.replays)for(unsigned bone=0;bone<21;++bone)p->constantStorage[4*(31+3*bone)+3]+=.05f;bounds(21);}
            assert(render(false));const auto reference=image();const auto oldDraws=w.pointDraws;unsigned weights[6];std::copy_n(w.pointSlot().faceWeights,6,weights);
            assert(render(true));compare(reference,image());assert(std::equal(weights,weights+6,w.pointSlot().faceWeights));assert(w.pointWork.palettePackets>0&&w.pointDraws<oldDraws/2);++comparisons;}
        for(unsigned emitter:{0u,1u}){WorldRenderer::PointReplayRuns runs{{0,1}};std::map<size_t,WorldRenderer::PointReplayRuns> splits{{emitter,runs}};
            const std::vector<size_t> pair={0,1};size_t at=0;bool drawn=false;float rows[16]={};const auto draws=w.pointDraws;
            assert(w.pointPaletteDraw(pair,at,splits,rows,drawn)&&!drawn&&!at&&w.pointDraws==draws);}
        // Sparse bone IDs must use their span, not the number of groups.
        auto sparse=std::make_shared<NorthlightDrawSnapshot::Mesh>(*source);auto sparseVertices=vertices;for(auto& v:sparseVertices)v.bones[0]=BYTE(v.bones[0]+50);std::memcpy(sparse->streams[0].bytes.data(),sparseVertices.data(),sparse->streams[0].bytes.size());
        must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,sparseVertices.data(),sparse->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=sparse;bounds(71);
        assert(render(false));const auto sparseImage=image();assert(render(true));compare(sparseImage,image());assert(!w.pointWork.paletteDraws);++comparisons;
        must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,vertices.data(),source->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=source;bounds(21);
        std::vector<std::unique_ptr<WorldRenderer::Replay>> tail;while(w.replays.size()>12){tail.push_back(std::move(w.replays.back()));w.replays.pop_back();}
        for(unsigned span:{1u,16u,41u,42u}){auto changed=std::make_shared<NorthlightDrawSnapshot::Mesh>(*source);auto v=vertices;
            for(unsigned i=0;i<v.size();++i)v[i].bones[0]=BYTE(i%span);std::memcpy(changed->streams[0].bytes.data(),v.data(),changed->streams[0].bytes.size());
            must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,v.data(),changed->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=changed;bounds(span);
            assert(render(false));const auto expected=image();assert(render(true));compare(expected,image());assert(bool(w.pointWork.paletteDraws)==(span<=41));++comparisons;}
        must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,vertices.data(),source->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=source;bounds(21);
        while(!tail.empty()){w.replays.push_back(std::move(tail.back()));tail.pop_back();}bounds(21);
        IDirect3DTexture9* white=nullptr;must(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT lock{};must(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0xffffffff;must(white->UnlockRect(0));
        for(auto& p:w.replays){p->cutoff=.5f;p->texture=white;white->AddRef();}assert(render(false));const auto alpha=image();assert(render(true));compare(alpha,image());assert(!w.pointWork.paletteDraws);++comparisons;
        must(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0x00ffffff;must(white->UnlockRect(0));
        for(unsigned i=0;i<w.replays.size();++i)w.replays[i]->cutoff=i==10||i==50||i==90?.5f:-1;
        assert(render(false));const auto mixed=image();assert(render(true));compare(mixed,image());assert(w.pointWork.paletteDraws&&w.pointWork.palettePackets);++comparisons;
        for(auto& p:w.replays){p->cutoff=-1;p->texture->Release();p->texture=nullptr;}white->Release();
        assert(render(false));const auto reference=image();
        for(unsigned fault=1;fault<=6;++fault)for(bool after:{false,true}){w.releasePointGPU();calls.fault=fault;calls.after=after;const auto hits=calls.hits;
            const bool result=render(true);assert(calls.hits==hits+1&&!calls.fault);if(fault<=5){assert(result&&!w.pointWork.paletteDraws);compare(reference,image());}
            else assert(!result&&!w.pointSlot().ready);w.frames+=121;assert(render(true));compare(reference,image());assert(w.pointWork.paletteDraws);++comparisons;}
        w.releasePointGPU(true);assert(render(true));compare(reference,image());++comparisons;
        std::printf("POINT_PALETTE_GPU_PASS comparisons=%u actors=100 faces=6 spans=1,16,21,41,42,71 coldResume=1 alphaFallback=1 faults=12 recoveryWithoutRestart=1 reset=1 depthTolerance=2e-6\n",comparisons);
        if(timing){IDirect3DQuery9 *begin=nullptr,*end=nullptr,*hz=nullptr,*disjoint=nullptr;must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
            for(unsigned unique=0;unique<2;++unique){for(unsigned a=0;a<w.replays.size();++a)w.replays[a]->count=source->primitiveCount-(unique?a:0);
            for(unsigned block=0;block<8;++block)for(unsigned mode=0;mode<2;++mode){const bool optimized=(block&1)?!mode:mode;double cpu=0,gpu=0,complete=0;unsigned draws=0,packets=0;
                for(unsigned round=0;round<8;++round){const auto start=std::chrono::steady_clock::now();w.pointPaletteUnavailable=!optimized;w.pointWork={};must(d->BeginScene());must(disjoint->Issue(D3DISSUE_BEGIN));must(hz->Issue(D3DISSUE_END));must(begin->Issue(D3DISSUE_END));
                    for(unsigned l=0;l<10;++l){light.sourceId=100+l;light.position[0]=float(l)*.07f;w.pointSlot().schedule.invalidate();assert(w.renderPointShadow(light,true,true));draws+=w.pointDraws;}
                    must(end->Issue(D3DISSUE_END));must(disjoint->Issue(D3DISSUE_END));must(d->EndScene());const auto submitted=std::chrono::steady_clock::now();drain();const auto done=std::chrono::steady_clock::now();UINT64 a=0,b=0,h=0;BOOL invalid=TRUE;must(begin->GetData(&a,sizeof a,0));must(end->GetData(&b,sizeof b,0));must(hz->GetData(&h,sizeof h,0));must(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&h&&b>=a);
                    if(round){cpu+=std::chrono::duration<double,std::milli>(submitted-start).count();complete+=std::chrono::duration<double,std::milli>(done-start).count();gpu+=double(b-a)*1000/h;packets+=w.pointWork.palettePackets;}must(d->Present(nullptr,nullptr,nullptr,nullptr));}
                std::printf("POINT_PALETTE_TIMING unique=%u block=%u optimized=%u cpuMs=%.6f gpuMs=%.6f completeMs=%.6f draws=%u packets=%u\n",unique,block,unsigned(optimized),cpu/7,gpu/7,complete/7,draws/8,packets/7);
            }}begin->Release();end->Release();hz->Release();disjoint->Release();}
        read->Release();event->Release();original->Release();shader->Release();decl->Release();
    }
};
int main(int argc,char** argv){assert(argc==3||argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);const auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
    const auto hwnd=CreateWindowW(L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));PointTerrainCoverageTest::run(d,argc==4);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
