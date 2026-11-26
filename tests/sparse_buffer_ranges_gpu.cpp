#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include "stream_device.h"

static void ok(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"D3D failure %08lx\n",(unsigned long)hr);std::abort();}}
struct Vertex {float x,y,z,w;DWORD color;};
int main(int argc,char** argv){
    assert(argc==3);
    auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,128,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=128;pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;
    ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE|D3DCREATE_MULTITHREADED,&pp,&raw));
    const char* reason=nullptr;NorthlightStream::StreamDevice::Options options;
    auto* d=NorthlightStream::StreamDevice::make(raw,api,&pp,options,&reason);assert(d);
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
#ifdef _WIN64
    constexpr UINT length=53u<<20,spacing=5u<<20,vertexLockBytes=8192;
    using namespace NorthlightStream;
    static_assert(kMaxLargeShadow==std::size_t(64)<<20);
    static_assert(LargeShadowBudgetBytes==std::size_t(5126)<<20);
    auto& queue=d->streamCore().q;
    // Exercise admission above 4 GiB with accounting data only.
    constexpr std::int64_t used=LargeShadowBudgetBytes-length;
    queue.addLargeBytes(used);
    assert(queue.largeAdmit(length)&&!queue.largeAdmit(length+1));
    queue.addLargeBytes(-used);
    assert(!queue.largeAdmit(kMaxLargeShadow+1));
#else
    constexpr UINT length=40u<<20,spacing=5u<<20,vertexLockBytes=3*sizeof(Vertex);
#endif
    ok(d->CreateVertexBuffer(length,D3DUSAGE_DYNAMIC,0,D3DPOOL_DEFAULT,&vb,nullptr));
    ok(d->CreateIndexBuffer(length,D3DUSAGE_DYNAMIC,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
#ifdef _WIN64
    assert(queue.largeBytes()==2*std::size_t(length)&&queue.stats.largeShadowGrants.load()==2);
#endif
    for(unsigned region=0;region<8;++region){
        const float x=float(region%4)*32,y=float(region/4)*32;
        Vertex vertices[]={{x,y,.5f,1,0xff334455},{x+30,y,.5f,1,0xff334455},{x,y+30,.5f,1,0xff334455}};
        void* p=nullptr;ok(vb->Lock(region*spacing,sizeof vertices,&p,0));std::memcpy(p,vertices,sizeof vertices);ok(vb->Unlock());
        const WORD indices[]={0,1,2};ok(ib->Lock(region*spacing,sizeof indices,&p,0));std::memcpy(p,indices,sizeof indices);ok(ib->Unlock());
    }
    auto& stats=d->streamCore().q.stats;const auto before=stats.syncCalls.load();
    for(unsigned round=0;round<64;++round)for(unsigned region=0;region<8;++region){
        // Mutate only colors/indices; positions and all other bytes must survive.
        void* p=nullptr;ok(vb->Lock(region*spacing,vertexLockBytes,&p,D3DLOCK_NOOVERWRITE));
        auto* v=static_cast<Vertex*>(p);assert(v[0].x==float(region%4)*32&&v[2].y==float(region/4)*32+30);
        for(unsigned i=0;i<3;++i)v[i].color=0xff000000|((round+1)<<16)|((region+1)<<8)|0x80;
        ok(vb->Unlock());ok(ib->Lock(region*spacing,3*sizeof(WORD),&p,D3DLOCK_NOOVERWRITE));
        auto* indices=static_cast<WORD*>(p);assert(indices[2]==2);indices[0]=round%2;indices[1]=1-round%2;ok(ib->Unlock());
    }
    const auto syncs=stats.syncCalls.load()-before;
#ifdef _WIN64
    assert(queue.largeBytes()==2*std::size_t(length)&&stats.rangeShadowBytes.load()==0);
    std::printf("LARGE_SHADOW_GPU_PASS length=%u budget=%zu syncs=%llu\n",
                length,LargeShadowBudgetBytes,(unsigned long long)syncs);
#endif
#if NORTHLIGHT_EXPECT_SPARSE
    assert(syncs==0);
#else
    assert(syncs==2048);
#endif
    ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));
    ok(d->SetRenderState(D3DRS_LIGHTING,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    ok(d->SetStreamSource(0,vb,0,sizeof(Vertex)));ok(d->SetIndices(ib));
    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));ok(d->BeginScene());
    for(unsigned region=0;region<8;++region)ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,region*spacing/sizeof(Vertex),0,3,region*spacing/sizeof(WORD),1));
    ok(d->EndScene());
    IDirect3DSurface9 *rt=nullptr,*readback=nullptr;ok(d->GetRenderTarget(0,&rt));
    ok(d->CreateOffscreenPlainSurface(128,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(d->GetRenderTargetData(rt,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> image(128*64*4);
    for(unsigned y=0;y<64;++y)std::memcpy(image.data()+y*128*4,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,128*4);
    ok(readback->UnlockRect());
    for(unsigned region=0;region<8;++region){
        const auto pixel=(region/4*32+4)*128+region%4*32+4;
        assert(image[pixel*4]==0x80&&image[pixel*4+1]==region+1&&image[pixel*4+2]==64);
    }
    std::ofstream file(argv[2],std::ios::binary);file.write(reinterpret_cast<const char*>(image.data()),image.size());file.close();assert(file);
    readback->Release();rt->Release();ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));vb->Release();ib->Release();d->Release();
    api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("SPARSE_GPU_PASS syncs=%llu regions=8 updates=1024\n",(unsigned long long)syncs);
}
