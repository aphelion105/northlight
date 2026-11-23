#include <windows.h>
#include <d3d9.h>
#include "forwarders.h"
#define NORTHLIGHT_MODEL_NORMALS_TEST
#include "model_normal_renderer.h"
#include "game_indexed_pair.h"
#include <cassert>
#include <cstdio>

using namespace NorthlightModelNormalPaletteShader;
using NorthlightModelNormalSlice::dst;
using NorthlightModelNormalSlice::src;

static void must(HRESULT hr){
    if(FAILED(hr))std::fprintf(stderr,"HRESULT=%08lx\n",static_cast<unsigned long>(hr));
    assert(SUCCEEDED(hr));
}

static const std::vector<std::uint32_t> vertexCode={
    0xfffe0300,0x05000051,dst(2,0),0x40400000,0,0,0,
    0x0200001f,0x80000000,dst(1,0),0x0200001f,0x80000002,dst(1,2),
    0x0200001f,0x80000000,dst(6,0),0x0200001f,0x8000000a,dst(6,1),
    0x03000005,dst(0,0,1),src(1,2,0),src(2,0,0),
    0x0200002e,dst(3,0,1),src(0,0,0),
    0x04000002,dst(0,1),src(1,0),src(2,31)|0x2000u,src(3,0,0),
    0x03000002,dst(6,0),src(0,1),src(2,2),
    0x03000001,dst(6,1),src(2,32)|0x2000u,src(3,0,0),65535
};

static void checkProof(){
    Program program;
    assert(build(vertexCode.data(),vertexCode.size(),program));
    assert(program.words.size()==vertexCode.size()+4&&program.semantic==0&&program.limit==256);
    assert(program.fixedReads.size()==1&&program.fixedReads[0].first==2&&program.fixedReads[0].count==1);
    assert(build(vertexCode.data(),vertexCode.size(),program,Bias::Separate));
    assert(program.words.size()==vertexCode.size()+7);
    const auto locate=[](auto word){return std::find(vertexCode.begin(),vertexCode.end(),word)-vertexCode.begin();};
    auto code=vertexCode;
    code[3]=0x40000000;
    assert(!build(code.data(),code.size(),program));
    code=vertexCode;
    code[locate(src(3,0,0))]=src(3,0,0x55);
    assert(!build(code.data(),code.size(),program));
    code=vertexCode;
    code[locate(src(2,31)|0x2000u)]=src(2,34)|0x2000u;
    assert(!build(code.data(),code.size(),program));
    code=vertexCode;
    code.insert(code.end()-1,{0x02000001,dst(6,1),src(0,0,0)});
    assert(!build(code.data(),code.size(),program));
    assert(!build(vertexCode.data(),vertexCode.size()-1,program));
}

struct PaletteDevice:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    enum class Fault {None, Allocate, Bind, FixedRegister, SecondPalette, IndexBind, LiftedBias};
    Fault fault=Fault::None;
    unsigned hits=0, calls=0, fixedUploads=0, indexedRateBinds=0;
    unsigned pixelBinds=0;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{
        ++pixelBinds;
        return real->SetPixelShader(shader);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* data,UINT count)override{
        if(fault==Fault::LiftedBias&&first==Base-1&&count==1){
            fault=Fault::None;++hits;return E_FAIL;
        }
        if(first==2&&count==1){
            ++fixedUploads;
            if(fault==Fault::FixedRegister){fault=Fault::None;++hits;return E_FAIL;}
        }
        if(fault==Fault::SecondPalette&&calls==1&&first==Base&&count==2*RowsPerBone){
            fault=Fault::None;++hits;return E_FAIL;
        }
        return real->SetVertexShaderConstantF(first,data,count);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{
        const auto hr=real->CreateVertexShader(code,out);
        if(FAILED(hr))std::printf("CreateVertexShader HRESULT=%08lx\n",static_cast<unsigned long>(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* code,IDirect3DVertexDeclaration9** out)override{
        const auto hr=real->CreateVertexDeclaration(code,out);
        if(FAILED(hr))std::printf("CreateVertexDeclaration HRESULT=%08lx\n",static_cast<unsigned long>(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT size,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared)override{
        if(fault==Fault::Allocate&&size==(2*MaxBones-1)*sizeof(float)){
            fault=Fault::None;++hits;*out=nullptr;return E_OUTOFMEMORY;
        }
        return real->CreateVertexBuffer(size,usage,fvf,pool,out,shared);
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT slot,UINT frequency)override{
        const HRESULT hr=real->SetStreamSourceFreq(slot,frequency);
        if(slot==0&&(frequency&D3DSTREAMSOURCE_INDEXEDDATA))++indexedRateBinds;
        if(fault==Fault::Bind&&slot==0&&(frequency&D3DSTREAMSOURCE_INDEXEDDATA)){
            must(hr);fault=Fault::None;++hits;return E_FAIL;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* buffer)override{
        const HRESULT hr=real->SetIndices(buffer);
        if(fault==Fault::IndexBind){must(hr);fault=Fault::None;++hits;return E_FAIL;}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT count)override{
        ++calls;
        return real->DrawIndexedPrimitive(type,base,min,vertices,start,count);
    }
};

struct ModelNormalPaletteTest {
    static std::vector<std::uint16_t> run(PaletteDevice& d,NorthlightModelNormals& normals,bool enabled,PaletteDevice::Fault fault,
                                         bool alphaMask=false,bool splitMaterials=false,bool variedRows=false){
        using NorthlightDrawSnapshot::Ref;
        using Normals=NorthlightModelNormals;
        const D3DVERTEXELEMENT9 layout[]={
            {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_UBYTE4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
        struct Vertex {float position[4]; std::uint32_t bone;};
        const Vertex vertices[]={{{-.7f,-.7f,.5f,1},5},{{.7f,-.7f,.5f,1},5},{{0,.7f,.5f,1},5}};
        const std::uint16_t indices[]={0,1,2,0,1,2,0,1,2};
        Ref<IDirect3DVertexDeclaration9> decl;
        Ref<IDirect3DVertexBuffer9> vb;
        Ref<IDirect3DIndexBuffer9> ib;
        Ref<IDirect3DVertexShader9> vs;
        Ref<IDirect3DPixelShader9> ps;
        Ref<IDirect3DTexture9> masks[2];
        must(d.CreateVertexDeclaration(layout,decl.out()));
        must(d.CreateVertexBuffer(sizeof vertices,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,vb.out(),nullptr));
        must(d.CreateIndexBuffer(sizeof indices,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.out(),nullptr));
        auto receiver=vertexCode;
        if(variedRows)std::replace(receiver.begin(),receiver.end(),src(2,2),src(2,17));
        if(alphaMask){
            receiver.insert(receiver.begin()+19,{0x0200001fu,0x80000005u,dst(6,2,3),0x0200001fu,0x8001000au,dst(6,3)});
            receiver.insert(receiver.end()-1,{0x02000001u,dst(6,2,3),src(1,0),0x02000001u,dst(6,3),src(2,239)});
        }
        must(d.CreateVertexShader(reinterpret_cast<const DWORD*>(receiver.data()),vs.out()));
        const DWORD pixelCode[]={0xffff0300,0x0200001f,0x8000000a,dst(1,0),0x02000001,dst(8,0),src(1,0),65535};
        const DWORD maskedCode[]={0xffff0300,0x05000051,dst(2,7),0x3f000000,0x3f000000,0x3f000000,0x3f000000,
            0x0200001f,0x8000000a,dst(1,0),0x0200001f,0x80000005,dst(1,1,3),0x0200001f,0x90000000,dst(10,0),
            0x03000042,dst(0,0),src(1,1),src(10,0),0x03000002,dst(0,0),src(0,0),src(2,7)|0x01000000u,
            0x01000041,dst(0,0),0x02000001,dst(8,0),src(1,0),65535};
        must(d.CreatePixelShader(alphaMask?maskedCode:pixelCode,ps.out()));
        if(alphaMask)for(unsigned mask=0;mask<2;++mask){
            must(d.CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,masks[mask].out(),nullptr));
            D3DLOCKED_RECT locked{};must(masks[mask]->LockRect(0,&locked,nullptr,0));
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
                reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]=x==mask?0xffffffff:0;
            must(masks[mask]->UnlockRect(0));
        }
        void* bytes=nullptr;
        must(vb->Lock(0,0,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);must(vb->Unlock());
        must(ib->Lock(0,0,&bytes,0));std::memcpy(bytes,indices,sizeof indices);must(ib->Unlock());
        assert(normals.targets(64,64));
        normals.palette.unavailable=!enabled;
        normals.instanceRowsEnabled=variedRows;
        Normals::Vertex vertex;
        vertex.shader=vs.p;vertex.compact=!alphaMask;vertex.reads=NorthlightShaderReads::scan(receiver.data(),receiver.size());
        assert(vertex.reads.exact);
        Normals::Pixel pixel;
        pixel.shader=ps.p;pixel.count=1;
        if(alphaMask)pixel.reads.samplers=pixel.reads.sampledSamplers=1;
        constexpr unsigned packets=5;
        float bank[packets][256*4]={};
        for(unsigned i=0;i<packets;++i){
            auto packet=std::make_unique<Normals::Pending>();
            packet->vertex=&vertex;packet->pixel=&pixel;packet->constants=bank[i];
            packet->ps.resize(4);
            packet->declaration=decl.p;decl->AddRef();
            packet->gpuVertex[0]=vb.p;vb->AddRef();packet->gpuIndex=ib.p;ib->AddRef();
            packet->resident=true;packet->mesh.indexed=true;
            packet->mesh.vertexCount=3;packet->mesh.primitiveCount=1;packet->mesh.streams[0].stride=sizeof(Vertex);
            packet->paletteSpan={5,1};
            packet->placement.indexStart=(i/2)*3;
            bank[i][4*2]=i==4?.1f:-.1f;
            if(variedRows)bank[i][4*17]=float(int(i)-2)*.07f;
            if(alphaMask){
                bank[i][4*239]=float(i);
                auto& sampled=packet->sampled.emplace_back();
                sampled.texture=masks[splitMaterials?i%2:0].p;sampled.texture->AddRef();
                for(unsigned state=0;state<13;++state)must(d.GetSamplerState(0,D3DSAMPLERSTATETYPE(state+1),&sampled.states[state]));
            }
            bank[i][4*(Base+RowsPerBone*5)]=.2f*float(int(i)-2);
            bank[i][4*(Base+RowsPerBone*5+1)+i%3]=1;
            bank[i][4*(Base+RowsPerBone*5+1)+3]=1;
            packet->viewport={0,0,64,64,0,1};
            for(unsigned j=0;j<std::size(Normals::rasterKeys);++j)must(d.GetRenderState(Normals::rasterKeys[j],&packet->raster[j]));
            normals.pending.push_back(std::move(packet));
        }
        d.hits=d.calls=d.fixedUploads=d.indexedRateBinds=0;d.fault=fault;
        must(d.BeginScene());
        assert(normals.finishCaptures(true));
        must(d.EndScene());
        const bool instanced=enabled&&fault==PaletteDevice::Fault::None&&!splitMaterials;
        const bool partial=fault==PaletteDevice::Fault::SecondPalette;
        std::printf("palette enabled=%u fault=%u calls=%u injected=%u planned=%u bindFailures=%u unavailable=%u\n",
            unsigned(enabled),unsigned(fault),d.calls,d.hits,normals.paletteWork.planned,normals.paletteWork.failures,unsigned(normals.palette.unavailable));
        assert(d.calls==(instanced?3:partial?4:packets)&&d.hits==unsigned(fault!=PaletteDevice::Fault::None));
        assert(normals.paletteWork.draws==(instanced?2:partial?1:0)&&normals.paletteWork.packets==(instanced?4:partial?2:0));
        assert(normals.paletteWork.linkedPackets==(instanced&&alphaMask?4:0));
        if(instanced){
            assert(d.fixedUploads==unsigned(!variedRows)&&d.indexedRateBinds==1);
            assert(normals.paletteWork.liftedDraws==(variedRows?2:0)&&
                   normals.paletteWork.liftedPackets==(variedRows?4:0));
            assert(normals.paletteWork.inheritedDraws==1&&normals.paletteWork.skippedBinds==5);
        }
        if(partial)assert(normals.paletteWork.failures==1&&d.indexedRateBinds==1);
        for(unsigned slot=0;slot<2;++slot){UINT rate=0;must(d.GetStreamSourceFreq(slot,&rate));assert(rate==1);}
        Ref<IDirect3DSurface9> readback;
        must(d.CreateOffscreenPlainSurface(64,64,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,readback.out(),nullptr));
        must(d.GetRenderTargetData(normals.surface,readback.p));
        D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<std::uint16_t> image(64*64*4);
        for(unsigned y=0;y<64;++y)std::memcpy(image.data()+y*64*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,64*8);
        must(readback->UnlockRect());
        return image;
    }
};

#include "native_indexed_pair_gpu.inc"
#include "ordered_particle_pair_gpu.inc"

int main(int argc,char** argv){
    assert(argc==2);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    checkProof();
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(window);
    auto backend=LoadLibraryA(argv[1]);assert(backend);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(backend,"Direct3DCreate9"));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* raw=nullptr;
    must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&raw));
    D3DCAPS9 caps{};must(raw->GetDeviceCaps(&caps));
    std::printf("caps vs=%08lx streams=%lu stride=%lu constants=%lu\n",static_cast<unsigned long>(caps.VertexShaderVersion),
        static_cast<unsigned long>(caps.MaxStreams),static_cast<unsigned long>(caps.MaxStreamStride),static_cast<unsigned long>(caps.MaxVertexShaderConst));
    must(raw->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    {
        PaletteDevice device(raw);
        NorthlightModelNormals normals(&device);
        using Fault=PaletteDevice::Fault;
        const auto control=ModelNormalPaletteTest::run(device,normals,false,Fault::None);
        assert(std::count(control.begin(),control.end(),std::uint16_t(0x3c00))>256);
        for(auto fault:{Fault::Allocate,Fault::Bind,Fault::FixedRegister,Fault::SecondPalette,Fault::None}){
            normals.reset();
            assert(ModelNormalPaletteTest::run(device,normals,true,fault)==control);
        }
        normals.reset();
        must(raw->Reset(&pp));
        must(raw->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        assert(ModelNormalPaletteTest::run(device,normals,true,Fault::None)==control);
        for(bool splitMaterials:{false,true}){
            normals.reset();
            const auto masked=ModelNormalPaletteTest::run(device,normals,false,Fault::None,true,splitMaterials);
            assert(std::count(masked.begin(),masked.end(),std::uint16_t(0x3c00))>64);
            normals.reset();
            assert(ModelNormalPaletteTest::run(device,normals,true,Fault::None,true,splitMaterials)==masked);
        }
        normals.reset();
        const auto varied=ModelNormalPaletteTest::run(device,normals,false,Fault::None,false,false,true);
        assert(varied!=control);
        for(const auto fault:{Fault::None,Fault::LiftedBias}){
            normals.reset();
            assert(ModelNormalPaletteTest::run(device,normals,true,fault,false,false,true)==varied);
        }
        normals.reset();
        must(raw->Reset(&pp));
        must(raw->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        assert(ModelNormalPaletteTest::run(device,normals,true,Fault::None,false,false,true)==varied);
        std::puts("MODEL_NORMAL_INSTANCE_ROWS_PASS same pixels, 5->3 draws, prefix bind fallback and Reset");
        nativeIndexedPairs(device);
        orderedParticlePair(device);
    }
    raw->Release();api->Release();FreeLibrary(backend);DestroyWindow(window);
    std::puts("MODEL_NORMAL_PALETTE_PASS same pixels, 5->3 draws with matched alpha masks, material changes split, allocation and setup fallback, used resources Reset");
}
