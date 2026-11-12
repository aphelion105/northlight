// Opaque stock WMO geometry, authored lights, production cube shader/culling.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <fstream>
struct PeerClock {using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<PeerClock,duration>;static time_point now(){return time_point(duration(1000000));}};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK PeerClock
#include "replay_bounds.h"
#include "point_light_shadow.h"
#include "local_light_compiled_shaders.h"
using namespace NorthlightReplayBounds;
static void ok(HRESULT h){assert(SUCCEEDED(h));}
struct Vertex {float x,y,z,u,v;};
int main(int argc,char** argv){
    assert(argc==3);std::ifstream input(argv[2],std::ios::binary);assert(input);unsigned count=0;input.read((char*)&count,4);assert(count&&count%3==0);
    std::vector<Vertex> vertices(count);input.read((char*)vertices.data(),count*sizeof(Vertex));assert(input);
    auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=count;mesh->primitiveCount=count/3;
    mesh->streams[0].stride=sizeof(Vertex);mesh->streams[0].bytes.resize(count*sizeof(Vertex));std::memcpy(mesh->streams[0].bytes.data(),vertices.data(),count*sizeof(Vertex));
    const D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    NorthlightActorDeformation::Program p;p.major=3;p.positionRegister=0;p.inputs.push_back({0,0,0});
    NorthlightActorDeformation::Operation op;op.code=1;op.destination=0x800f0000;op.source[0].token=0x90e40000;p.operations.push_back(op);
    float bank[1024]={},inverse[16]={};for(unsigned j=0;j<4;++j)inverse[j*5]=1;
    EnvelopeCache cache;cache.beginFrame(false);Budget budget;budget.maxVertices=count;Bounds box;
    assert(cache.calculate(p,*mesh,mesh,layout,2,bank,inverse,budget,box)==Status::Valid);
    p.operations[0].code=2;p.operations[0].source[1].token=0xa0e40000;
    cache.beginFrame(false);budget=Budget{};budget.maxVertices=1;
    assert(cache.calculate(p,*mesh,mesh,layout,2,bank,inverse,budget,box,EnvelopeCache::Stage::Build)==Status::Budget);
    assert(!budget.vertices&&cache.stats().completed==1);
    cache.beginFrame(false);budget=Budget{};budget.maxVertices=0;
    assert(cache.calculate(p,*mesh,mesh,layout,2,bank,inverse,budget,box,EnvelopeCache::Stage::Cheap)==Status::Valid&&box.valid);

    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"RigidPeerPoint";assert(RegisterClassW(&wc));
    auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    ok(device->CreateVertexShader(kLocalShadowVSShader,&vs));ok(device->CreatePixelShader(kLocalShadowPSShader,&ps));
    const D3DVERTEXELEMENT9 gpuLayout[]={layout[0],{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;ok(device->CreateVertexDeclaration(gpuLayout,&declaration));
    ok(device->SetVertexDeclaration(declaration));ok(device->SetVertexShader(vs));ok(device->SetPixelShader(ps));
    constexpr unsigned resolution=128;IDirect3DSurface9 *target=nullptr,*depth=nullptr,*readback=nullptr;
    ok(device->CreateRenderTarget(resolution,resolution,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(device->CreateDepthStencilSurface(resolution,resolution,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    ok(device->CreateOffscreenPlainSurface(resolution,resolution,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(device->SetRenderTarget(0,target));ok(device->SetRenderTarget(1,nullptr));ok(device->SetDepthStencilSurface(depth));
    const D3DVIEWPORT9 viewport{0,0,resolution,resolution,0,1};ok(device->SetViewport(&viewport));
    for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(device->SetRenderState(state,FALSE));
    ok(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(device->SetRenderState(D3DRS_ZENABLE,TRUE));ok(device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(device->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    const float material[]={0,0,0,-1};ok(device->SetPixelShaderConstantF(0,material,1));
    float center[3],extent=1;for(unsigned k=0;k<3;++k){center[k]=(box.low[k]+box.high[k])*.5f;extent=std::max(extent,box.high[k]-box.low[k]);}
    const float directions[9][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,1,0},{1,0,1},{0,1,1}};
    size_t beforeDraws=0,afterDraws=0,occupied=0;unsigned checked=0;
    for(const auto& direction:directions){float light[3];for(unsigned k=0;k<3;++k)light[k]=center[k]+direction[k]*extent*2;
        const float range=extent*8;float matrices[6][16];for(unsigned face=0;face<6;++face)assert(NorthlightPointShadow::faceMatrix(light,.1f,range,face,matrices[face],resolution));
        NorthlightPointShadow::FaceClip clip(matrices);const unsigned mask=clip.mask(box.low,box.high,box.valid);assert(mask&&mask!=63);
        float sphere[]={range*range,1.f/resolution,0,0};ok(device->SetPixelShaderConstantF(1,sphere,1));
        for(unsigned face=0;face<6;++face){std::vector<unsigned> reference;
            for(unsigned optimized=0;optimized<2;++optimized){ok(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));
                ok(device->SetVertexShaderConstantF(0,matrices[face],4));ok(device->BeginScene());
                if(!optimized||(mask&(1u<<face))){ok(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,vertices.data(),sizeof(Vertex)));if(optimized)++afterDraws;else ++beforeDraws;}
                ok(device->EndScene());ok(device->GetRenderTargetData(target,readback));D3DLOCKED_RECT locked;ok(readback->LockRect(&locked,nullptr,D3DLOCK_READONLY));
                std::vector<unsigned> pixels(resolution*resolution);for(unsigned y=0;y<resolution;++y)std::memcpy(pixels.data()+y*resolution,(char*)locked.pBits+y*locked.Pitch,resolution*4);ok(readback->UnlockRect());
                if(!optimized){reference=std::move(pixels);for(auto word:reference){float z;std::memcpy(&z,&word,4);if(z<1)++occupied;}}
                else assert(pixels==reference);
            }++checked;
        }
    }
    assert(occupied>0&&afterDraws<beforeDraws);
    std::printf("RIGID_PEER_POINT_GPU_PASS vertices=%u triangles=%u lights=9 faces=%u draws=%zu/%zu triangleExposure=%zu/%zu occupiedPixels=%zu equality=bitexact timing_valid=false\n",count,count/3,checked,beforeDraws,afterDraws,beforeDraws*(count/3),afterDraws*(count/3),occupied);
    device->SetPixelShader(nullptr);device->SetVertexShader(nullptr);device->SetVertexDeclaration(nullptr);readback->Release();depth->Release();target->Release();declaration->Release();ps->Release();vs->Release();device->Release();api->Release();DestroyWindow(hwnd);FreeLibrary(module);
}
