#include "height_ownership_fixture.h"
#define main subdivision_reference_main
#include "displacement_subdivision.cpp"
#undef main

static unsigned liveTextures=0,liveSurfaces=0;
struct HeightSurface;
struct HeightTexture:NorthlightMirrorResources::ForwardIDirect3DTexture9 {
    static MirrorGate& testGate(){static MirrorGate gate;return gate;}
    UINT width,height;D3DFORMAT format;D3DPOOL pool;unsigned refs=1;int* fault;bool locked=false;
    std::vector<unsigned char> data;
    HeightTexture(UINT w,UINT h,D3DFORMAT f,D3DPOOL p,int* failure):ForwardIDirect3DTexture9(nullptr,testGate(),[](void*,const char*){},nullptr),width(w),height(h),format(f),pool(p),fault(failure),data(size_t(w)*h*4){++liveTextures;
        if(f==D3DFMT_A8R8G8B8)for(size_t i=0;i<data.size();++i)data[i]=static_cast<unsigned char>(i*37+i/13);}
    ~HeightTexture(){--liveTextures;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return D3DRTYPE_TEXTURE;}
    DWORD STDMETHODCALLTYPE GetLevelCount()override{return 1;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*)override{return D3DERR_NOTFOUND;}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level,D3DSURFACE_DESC* desc)override{assert(!level);*desc={};desc->Width=width;desc->Height=height;desc->Format=format;desc->Pool=pool;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT,IDirect3DSurface9**)override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT,D3DLOCKED_RECT* out,const RECT*,DWORD)override{
        const int fail=pool==D3DPOOL_SYSTEMMEM?*fault:0;if(fail==3)return E_FAIL;assert(!locked);locked=true;
        out->pBits=fail==4?nullptr:data.data();out->Pitch=INT(width*4)-(fail==5);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT)override{assert(locked);locked=false;return pool==D3DPOOL_SYSTEMMEM&&*fault==6?E_FAIL:D3D_OK;}
};
struct HeightSurface:NorthlightMirrorResources::ForwardIDirect3DSurface9 {
    static MirrorGate& testGate(){static MirrorGate gate;return gate;}
    HeightTexture* owner;unsigned refs=1;int* fault;bool locked=false;std::vector<unsigned char> storage;std::vector<unsigned char>* data;
    HeightSurface(HeightTexture* texture,unsigned width,unsigned height,int* failure):ForwardIDirect3DSurface9(nullptr,testGate(),[](void*,const char*){},nullptr),owner(texture),fault(failure){
        ++liveSurfaces;if(owner){owner->AddRef();data=&owner->data;}else{storage.resize(size_t(width)*height*4);data=&storage;}}
    ~HeightSurface(){assert(!locked);if(owner)owner->Release();--liveSurfaces;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* out,const RECT*,DWORD flags)override{
        assert(!owner&&flags==D3DLOCK_READONLY);if(*fault==12)return E_FAIL;locked=true;out->pBits=*fault==13?nullptr:data->data();out->Pitch=512-(*fault==14);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE UnlockRect()override{assert(locked);locked=false;return *fault==15?E_FAIL:D3D_OK;}
};
HRESULT HeightTexture::GetSurfaceLevel(UINT level,IDirect3DSurface9** out){assert(!level);*out=nullptr;if(*fault==9)return E_FAIL;*out=new HeightSurface(this,width,height,fault);return D3D_OK;}
struct HeightDevice:ForwardIDirect3DDevice9 {
    int fault=0;unsigned defaults=0,staging=0,updates=0;
    HeightDevice():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** result,HANDLE*)override{
        assert(levels==1&&usage==(pool==D3DPOOL_DEFAULT?D3DUSAGE_RENDERTARGET:0)&&format==D3DFMT_R32F);assert(pool==D3DPOOL_DEFAULT||pool==D3DPOOL_SYSTEMMEM);*result=nullptr;
        (pool==D3DPOOL_DEFAULT?defaults:staging)++;
        if((fault==1&&pool==D3DPOOL_DEFAULT)||(fault==2&&pool==D3DPOOL_SYSTEMMEM))return E_OUTOFMEMORY;
        *result=new HeightTexture(w,h,format,pool,&fault);return fault==8&&pool==D3DPOOL_DEFAULT?E_FAIL:D3D_OK;}
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* target)override{
        auto& a=*static_cast<HeightTexture*>(source);auto& b=*static_cast<HeightTexture*>(target);
        assert(a.pool==D3DPOOL_SYSTEMMEM&&b.pool==D3DPOOL_DEFAULT&&!a.locked&&!b.locked&&a.width==b.width&&a.height==b.height);
        ++updates;if(fault==7)return E_FAIL;b.data=a.data;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,IDirect3DSurface9** out,HANDLE*)override{
        assert(format==D3DFMT_R32F&&pool==D3DPOOL_SYSTEMMEM);*out=nullptr;if(fault==10)return E_OUTOFMEMORY;*out=new HeightSurface(nullptr,width,height,&fault);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* source,IDirect3DSurface9* target)override{
        if(fault==11)return E_FAIL;auto* a=static_cast<HeightSurface*>(source);auto* b=static_cast<HeightSurface*>(target);assert(a->owner&&!b->owner);*b->data=*a->data;return D3D_OK;}
};
struct DisplacementStateTestAccess {
    static const G::Height* get(NorthlightAutoDisplacement& g,IDirect3DTexture9* source,bool model,bool cpu){return g.height(source,model,cpu);}
    static IDirect3DTexture9* upload(NorthlightAutoDisplacement& g,IDirect3DTexture9* source,bool model){return g.heightTexture(source,model);}
    static void cpu(){
        for(bool model:{false,true})for(int failure=0;failure<=8;++failure){
            HeightDevice device;auto* source=new HeightTexture(128,128,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&device.fault);
            {NorthlightAutoDisplacement g(&device);const auto* h=get(g,source,model,false);assert(h&&h->values.size()==16384);
             const auto expected=h->values;const auto peak=h->peak();device.fault=failure;auto* map=upload(g,source,model);
             if(failure){assert(!map&&h->values==expected&&!h->gpuOnly()&&liveTextures==1);device.fault=0;map=upload(g,source,model);}
             assert(map&&liveTextures==2&&h->gpuOnly()&&h->values.capacity()==0&&h->peak()==peak);
             auto* gpu=static_cast<HeightTexture*>(map);assert(!std::memcmp(gpu->data.data(),expected.data(),expected.size()*4));
             const auto writes=device.updates;for(unsigned i=0;i<50;++i)assert(get(g,source,model,false)==h&&upload(g,source,model)==map);
             assert(device.updates==writes&&!h->values.capacity());
             // An evicted or overwritten albedo must not change the cached
             // height field when a CPU-only shader first needs its pixels.
             std::fill(source->data.begin(),source->data.end(),0);
             // CPU fallback must regenerate exact pixels, keep the valid GPU
             // allocation and stop pruning a source shared by both consumers.
             assert(get(g,source,model,true)==h&&h->values==expected&&!h->gpuOnly());
             assert(upload(g,source,model)==map&&h->values==expected&&g.textures[0].cpuNeeded);
             g.reset();assert(liveTextures==1&&g.textures.empty());}
            source->Release();assert(!liveTextures);
        }
        for(int failure=9;failure<=15;++failure){HeightDevice device;auto* source=new HeightTexture(128,128,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&device.fault);
            {NorthlightAutoDisplacement g(&device);auto* h=get(g,source,false,false);assert(h);const auto expected=h->values;auto* map=upload(g,source,false);assert(map);
             device.fault=failure;assert(!get(g,source,false,true)&&h->gpuOnly()&&!liveSurfaces&&liveTextures==2);
             assert(get(g,source,false,false)==h&&upload(g,source,false)==map);device.fault=0;g.endFrame();assert(get(g,source,false,true)==h&&h->values==expected&&!liveSurfaces);}
            source->Release();assert(!liveTextures);}
        // A read budget refusal cannot turn a GPU-only field into flat CPU relief.
        HeightDevice device;auto* source=new HeightTexture(128,128,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&device.fault);
        {NorthlightAutoDisplacement g(&device);auto* h=get(g,source,false,false);assert(h&&upload(g,source,false));g.readsThisFrame=2;
         assert(!get(g,source,false,true)&&h->gpuOnly());assert(get(g,source,false,false)==h);
         g.endFrame();assert(get(g,source,false,true)==h&&!h->gpuOnly());}
        source->Release();assert(!liveTextures);
        {HeightDevice d;auto* original=new HeightTexture(128,128,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&d.fault);
         {NorthlightAutoDisplacement g(&d);const auto* cpu=get(g,original,false,true);assert(cpu);const auto expected=cpu->values;
          assert(upload(g,original,false)&&cpu->values==expected&&!cpu->gpuOnly()&&!g.heightReadbacks);}
         original->Release();assert(!liveTextures);}
        {HeightDevice memoryDevice;NorthlightAutoDisplacement g(&memoryDevice);size_t before=0,after=0;
         for(unsigned i=0;i<32;++i){auto* original=new HeightTexture(128,128,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&memoryDevice.fault);
            NorthlightDisplacementStorage::Texture entry;entry.source=original;entry.height.width=entry.height.height=128;entry.height.values.resize(128*128,.6f);before+=entry.height.values.capacity()*4;g.textures.push_back(std::move(entry));
            assert(upload(g,original,false));}
         for(const auto& entry:g.textures)after+=entry.height.values.capacity()*4;
         assert(before==2u*1024*1024&&!after&&liveTextures==64);
         std::printf("HEIGHT_MEMORY_PASS maps=32 logicalGpuBytes=%zu cpuBeforeBytes=%zu cpuAfterBytes=%zu stagingRetainedBytes=0\n",before,before,after);
         g.reset();assert(!liveTextures);}
        Fixture fixture(2,false);G::Mesh before,after;assert(fixture.build(false,before,4,false,true,false,4));
        const size_t bytes=fixture.height.values.capacity()*4;fixture.height.discardCPU();assert(fixture.height.gpuOnly());
        assert(fixture.build(false,after,4,false,true,false,4));equal(before,after);
        assert(!fixture.build(false,after,4,false,false,false,4));
        std::printf("HEIGHT_OWNERSHIP_PASS faults=15 world_model=exact reset=empty fallback=exact sticky_cpu=1 geometryBytes=%zu freedCpuBytes=%zu\n",before.byteSize(),bytes);
    }
};

static void benchmark(){
    Fixture retained(2,false),pruned(2,false);
    retained.height.width=retained.height.height=128;retained.height.values.resize(128*128);
    for(size_t i=0;i<retained.height.values.size();++i)retained.height.values[i]=float((i*37+i/11)%256)/255;
    pruned.height=retained.height;pruned.height.discardCPU();
    G::Mesh reference,proof;assert(retained.build(false,reference,4,false,true,false,4)&&pruned.build(false,proof,4,false,true,false,4));equal(reference,proof);
    for(unsigned pair=0;pair<8;++pair)for(unsigned turn=0;turn<2;++turn){const bool compact=(turn+pair)%2;auto& fixture=compact?pruned:retained;uint64_t digest=0;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned draw=0;draw<128;++draw){G::Mesh output;assert(fixture.build(false,output,4,false,true,false,4));digest=digest*33+output.vertexCount+output.primitiveCount;}
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("HEIGHT_BUILD_TIMING mode=%s pair=%u builds=128 cpuMs=%.6f digest=%llu cpuCapacityBytes=%zu\n",compact?"gpu-only":"retained",pair,ms,(unsigned long long)digest,fixture.height.values.capacity()*4);
    }
}

static void gpu(const char* backend,const char* shaderPath){
    auto module=LoadLibraryA(backend);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);auto window=CreateWindowA("STATIC","Height ownership",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&d)));
    std::ifstream file(shaderPath,std::ios::binary|std::ios::ate);assert(file);std::vector<DWORD> code(size_t(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),code.size()*4);
    IDirect3DPixelShader9* ps=nullptr;assert(SUCCEEDED(d->CreatePixelShader(code.data(),&ps)));
    IDirect3DTexture9* source=nullptr;assert(SUCCEEDED(d->CreateTexture(128,128,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source,nullptr)));
    D3DLOCKED_RECT lock{};assert(SUCCEEDED(source->LockRect(0,&lock,nullptr,0)));for(unsigned y=0;y<128;++y)for(unsigned x=0;x<512;++x)static_cast<unsigned char*>(lock.pBits)[y*lock.Pitch+x]=static_cast<unsigned char>(x*37+y*11);assert(SUCCEEDED(source->UnlockRect(0)));
    unsigned compared=0;
    {NorthlightAutoDisplacement g(d);
     for(unsigned reset=0;reset<2;++reset){
      for(bool model:{false,true}){
       g.endFrame();const auto* h=DisplacementStateTestAccess::get(g,source,model,false);assert(h);const auto expected=h->values;
       assert(SUCCEEDED(d->BeginScene()));auto* map=DisplacementStateTestAccess::upload(g,source,model);assert(SUCCEEDED(d->EndScene()));assert(map&&h->gpuOnly()&&!h->values.capacity());D3DSURFACE_DESC desc{};assert(SUCCEEDED(map->GetLevelDesc(0,&desc))&&desc.Pool==D3DPOOL_DEFAULT);
       NorthlightDrawSnapshot::Ref<IDirect3DTexture9> output;NorthlightDrawSnapshot::Ref<IDirect3DSurface9> surface,copy,back;
       assert(SUCCEEDED(d->CreateTexture(128,128,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,output.out(),nullptr)));
       assert(SUCCEEDED(output->GetSurfaceLevel(0,surface.out()))&&SUCCEEDED(d->CreateOffscreenPlainSurface(128,128,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,copy.out(),nullptr))&&SUCCEEDED(d->GetRenderTarget(0,back.out())));
       assert(SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&SUCCEEDED(d->SetRenderTarget(0,surface.p)));D3DVIEWPORT9 viewport={0,0,128,128,0,1};assert(SUCCEEDED(d->SetViewport(&viewport)));
       for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(state,FALSE);
       d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(ps);assert(SUCCEEDED(d->SetTexture(0,map)));
       d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
       const float vertices[]={-.5f,-.5f,0,1,0,0,127.5f,-.5f,0,1,1,0,-.5f,127.5f,0,1,0,1,127.5f,127.5f,0,1,1,1};
       assert(SUCCEEDED(d->BeginScene())&&SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,24))&&SUCCEEDED(d->EndScene()));assert(SUCCEEDED(d->GetRenderTargetData(surface.p,copy.p)));
       assert(SUCCEEDED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)));for(unsigned y=0;y<128;++y)assert(!std::memcmp(static_cast<char*>(lock.pBits)+y*lock.Pitch,expected.data()+y*128,512));assert(SUCCEEDED(copy->UnlockRect()));compared+=16384;
       assert(SUCCEEDED(d->SetTexture(0,nullptr))&&SUCCEEDED(d->SetRenderTarget(0,back.p)));
       assert(SUCCEEDED(d->BeginScene()));const auto* restored=DisplacementStateTestAccess::get(g,source,model,true);assert(SUCCEEDED(d->EndScene()));assert(restored&&restored->values==expected);
      }
      g.reset();assert(SUCCEEDED(d->Reset(&pp)));
     }
    }
    source->Release();ps->Release();d->Release();api->Release();DestroyWindow(window);FreeLibrary(module);
    std::printf("HEIGHT_GPU_PIXELS_PASS samples=%u bitexact=1 reset=2 fallback=exact cpuCapacityAfterUpload=0 pool=DEFAULT\n",compared);
}
int main(int argc,char** argv){if(argc==1)DisplacementStateTestAccess::cpu();else if(argc==2&&std::string(argv[1])=="--benchmark")benchmark();else{assert(argc==3);gpu(argv[1],argv[2]);}}
