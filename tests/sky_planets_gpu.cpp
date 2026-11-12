#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include "sky_planet_draw.h"
#include "forever_celestial.h"

static void ok(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"D3D failure %08lx\n",(unsigned long)hr);std::abort();}}
struct Args {enum class Type{Indexed};Type type=Type::Indexed;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;unsigned primCount=2,first=3,minIndex=2,numVertices=8;int baseVertex=1;};
struct V {float p[3];DWORD color;float uv[2];};
int main(int argc,char** argv){
    assert(argc==2);constexpr unsigned N=96;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightPlanetSizeTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=N;pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&d));
    unsigned cases=0;
    const DWORD emissionCode[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
    IDirect3DPixelShader9* emission=nullptr;ok(d->CreatePixelShader(emissionCode,&emission));
    for(bool hdr:{false,true}){
    const auto targetFormat=hdr?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8;
    const unsigned bytes=hdr?8:4;
    IDirect3DSurface9 *target=nullptr,*readback=nullptr;ok(d->CreateRenderTarget(N,N,targetFormat,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,targetFormat,D3DPOOL_SYSTEMMEM,&readback,nullptr));ok(d->SetRenderTarget(0,target));
    ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(hdr?emission:nullptr));const float radiance[]={6,3,1,.625f};ok(d->SetPixelShaderConstantF(0,radiance,1));
    ok(d->SetRenderState(D3DRS_LIGHTING,FALSE));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
    D3DMATRIX identity{};for(unsigned i=0;i<4;++i)identity.m[i][i]=1;ok(d->SetTransform(D3DTS_VIEW,&identity));ok(d->SetTransform(D3DTS_PROJECTION,&identity));
    auto pixels=[&]{ok(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> result(N*N*bytes);for(unsigned y=0;y<N;++y)std::memcpy(result.data()+y*N*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,N*bytes);ok(readback->UnlockRect());return result;};
    const float factor=NorthlightForeverSky::planetScale();
    for(const auto& mesh:NorthlightSkyComposition::PlanetMeshes)for(auto format:{D3DFMT_INDEX16,D3DFMT_INDEX32}){
        Args args;args.primCount=mesh.count/2;args.numVertices=mesh.count;
        V source[12]{};std::uint16_t relative[12]={0,1,2,2,1,3};
        if(mesh.count==8){const std::uint16_t ring[]={0,2,4,4,2,6,1,3,5,5,3,7};std::memcpy(relative,ring,sizeof ring);}
        float extent=0;for(unsigned i=0;i<mesh.count;++i){auto& v=source[i+3];std::copy_n(mesh.positions[i],3,v.p);v.color=0xa04080c0+i*0x010101;v.uv[0]=float(i%2);v.uv[1]=float(i/2%2);for(unsigned c=0;c<3;++c)extent=std::max(extent,std::fabs(v.p[c]-mesh.anchor[c]));}
        D3DMATRIX transform{};transform.m[0][0]=transform.m[2][1]=.8f/extent;transform.m[3][0]=-mesh.anchor[0]*.8f/extent;transform.m[3][1]=-mesh.anchor[2]*.8f/extent;transform.m[3][2]=.5f;transform.m[3][3]=1;ok(d->SetTransform(D3DTS_WORLD,&transform));
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;ok(d->CreateVertexBuffer(sizeof source,0,0,D3DPOOL_MANAGED,&vb,nullptr));
        const unsigned indexSize=format==D3DFMT_INDEX16?2:4;ok(d->CreateIndexBuffer(16*indexSize,0,format,D3DPOOL_MANAGED,&ib,nullptr));
        void* data=nullptr;ok(vb->Lock(0,sizeof source,&data,0));std::memcpy(data,source,sizeof source);ok(vb->Unlock());
        ok(ib->Lock(0,16*indexSize,&data,0));std::memset(data,0,16*indexSize);for(unsigned i=0;i<args.primCount*3;++i){const unsigned index=relative[i]+2;std::memcpy(static_cast<char*>(data)+(i+args.first)*indexSize,&index,indexSize);}ok(ib->Unlock());
        ok(d->SetStreamSource(0,vb,0,sizeof(V)));ok(d->SetIndices(ib));
        NorthlightSkyPlanet::Draw draw;assert(draw.prepare(d,args,factor));
        auto native=[&]{return d->DrawIndexedPrimitive(args.prim,args.baseVertex,args.minIndex,args.numVertices,args.first,args.primCount);};
        V scaled[8]{};for(unsigned i=0;i<mesh.count;++i){scaled[i]=source[i+3];for(unsigned c=0;c<3;++c)scaled[i].p[c]=mesh.anchor[c]+(scaled[i].p[c]-mesh.anchor[c])*factor;}
        for(unsigned blend:{0u,1u,2u}){
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend!=0));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,blend==2?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x20304050,1,0));ok(d->BeginScene());ok(draw.submit(d,native));ok(d->EndScene());auto actual=pixels();
            IDirect3DVertexBuffer9* restoredV=nullptr;IDirect3DIndexBuffer9* restoredI=nullptr;UINT offset=0,stride=0;ok(d->GetStreamSource(0,&restoredV,&offset,&stride));ok(d->GetIndices(&restoredI));assert(restoredV==vb&&restoredI==ib&&offset==0&&stride==sizeof(V));restoredV->Release();restoredI->Release();
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x20304050,1,0));ok(d->BeginScene());ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,mesh.count,args.primCount,relative,D3DFMT_INDEX16,scaled,sizeof(V)));ok(d->EndScene());auto expected=pixels();assert(actual==expected);
            unsigned covered=0;for(unsigned i=0;i<N*N;++i){
                if(hdr){std::uint16_t red=0;std::memcpy(&red,expected.data()+i*bytes,2);covered+=red>0x3c00;} // emission remains above 1
                else covered+=expected[i*bytes]!=0x50;
            }assert(covered>20);
            ok(d->SetStreamSource(0,vb,0,sizeof(V)));ok(d->SetIndices(ib));++cases;
        }
        ok(vb->Lock(0,sizeof source,&data,D3DLOCK_READONLY));assert(!std::memcmp(data,source,sizeof source));ok(vb->Unlock());
        draw.clear();assert(!draw.ready());assert(!draw.prepare(d,args,1));assert(!draw.ready());
        auto invalid=args;invalid.first=UINT_MAX;assert(!draw.prepare(d,invalid,factor));
        ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));vb->Release();ib->Release();
    }
    readback->Release();target->Release();
    }
    std::printf("PASS %u pixel-exact planet/ring draws, 16/32-bit indices, base vertex, SDR/HDR emission, alpha/additive blend and native state/source preservation\n",cases);
    emission->Release();d->Release();api->Release();DestroyWindow(window);FreeLibrary(module);
}
