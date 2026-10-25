// Isolate the production material composite's neutral-detail invariant.
// Synthetic adjacent facets; this is not an actual-MPQ or whole-scene replay.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include "world_compiled_shaders.h"
#include <cstdlib>
#include <cstdarg>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");va_end(args);}
#include "saved_state.h"
#include "auto_bump_compiled_shaders.h"
#include "auto_normal_cache.h"

static constexpr unsigned W=256,H=256;
static Image field(float r,float g,float b,float a){
    Image out{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H*16)};
    const float value[]={r,g,b,a};for(unsigned i=0;i<W*H;++i)std::memcpy(out.pixels.data()+i*16,value,16);return out;
}
int main(int argc,char** argv){
    assert(argc>=3&&argc<=6);const bool counterexample=std::strcmp(argv[2],"--counterexample")==0;
    assert(counterexample||std::strcmp(argv[2],"--fixed")==0);
    const unsigned split=argc>3?unsigned(std::atoi(argv[3])):W/2;
    const unsigned silhouette=argc>4?unsigned(std::atoi(argv[4])):W;
    assert(split>8&&split<W-8&&silhouette>split+8&&silhouette<=W);
    const float baseDistance=std::getenv("NORTHLIGHT_TEST_TERRAIN_DISTANCE")?std::atof(std::getenv("NORTHLIGHT_TEST_TERRAIN_DISTANCE")):1;
    assert(baseDistance>0&&baseDistance<=24); // Both folded sides stay inside the full-strength range.
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightTerrainNeutralFrame";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    std::vector<float> local(W*H*3);for(unsigned i=0;i<W*H;++i)local[i*3+2]=1;
    if(argc==6){
        // Actual MPQ bytes are supplied by validate_auto_bump.py's source.fgr.
        // Generation is production code; adjacent capture encoding below is analytic.
        auto* source=texture(d,image(argv[5]));NorthlightAutoNormalCache cache(d);
        ok(d->BeginScene());auto generated=cache.acquire(source);ok(d->EndScene());assert(generated);
        IDirect3DSurface9 *map=nullptr,*readback=nullptr;ok(generated.get()->GetSurfaceLevel(0,&map));D3DSURFACE_DESC desc{};ok(map->GetDesc(&desc));
        ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));ok(d->GetRenderTargetData(map,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        float energy=0;for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const auto* p=static_cast<const unsigned char*>(lock.pBits)+(y%desc.Height)*lock.Pitch+(x%desc.Width)*4;float nx=(float(p[2])-128)/127,ny=(float(p[1])-128)/127;unsigned i=(y*W+x)*3;local[i]=nx;local[i+1]=ny;local[i+2]=std::sqrt(std::max(0.f,1-nx*nx-ny*ny));energy=std::max(energy,std::fabs(nx)+std::fabs(ny));}
        ok(readback->UnlockRect());assert(energy>.01f);std::printf("TERRAIN_ACTUAL_SOURCE path=%s generatedDetail=%.9g syntheticCapture=true\n",argv[5],energy);drop(readback);drop(map);drop(source);
    }
    auto depth=field(0,0,0,0),material=field(0,0,0,1);
    std::vector<float> expected(W*H*3,.3f);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const float slope=x<split?.2f:-.2f,nx=(x+.5f)*2/W-1,boundary=float(split)*2/W-1;
        const float z=baseDistance*(1-slope*boundary)/(1-slope*nx),nd=x>=silhouette?1:100.f/99.9f*(1-.1f/z);
        // Transform tangent detail into each facet frame; neutral is its normal.
        const unsigned i=(y*W+x)*3;const float inv=1/std::sqrt(1+slope*slope);
        const float mx=(local[i]+slope*local[i+2])*inv,my=local[i+1],mz=(-slope*local[i]+local[i+2])*inv;
        const float sum=std::fabs(mx)+std::fabs(my)+std::fabs(mz);
        float ox=mx/sum,oy=my/sum;if(mz<0){const float prior=ox;ox=(1-std::fabs(oy))*(prior>=0?1:-1);oy=(1-std::fabs(prior))*(oy>=0?1:-1);}
        const float captured[]={ox*.5f+.5f,oy*.5f+.5f,z,x>=silhouette?0.f:1.f};
        const float delta=std::max(.8f*local[i]+.6f*local[i+2],0.f)-.6f;const float hue[]={1,.8f,.6f};
        if(x<silhouette)for(unsigned k=0;k<3;++k)expected[i+k]=std::max(.3f+.3f*delta*hue[k],0.f);
        std::memcpy(depth.pixels.data()+(y*W+x)*16,&nd,4);std::memcpy(material.pixels.data()+(y*W+x)*16,captured,16);
    }
    std::vector<IDirect3DTexture9*> textures;
    auto bind=[&](unsigned slot,const Image& image){auto* t=texture(d,image);textures.push_back(t);ok(d->SetTexture(slot,t));
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,D3DTEXF_POINT));
        ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));};
    bind(0,field(.3f,.3f,.3f,1));bind(1,depth);bind(4,field(1,1,1,1));bind(5,field(.3f,.3f,.3f,1));bind(8,field(1,1,1,1));bind(9,material);bind(10,field(1,1,1,1));bind(11,field(0,0,0,0));bind(12,field(1,1,1,1));bind(14,field(0,0,1,1));
    IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(kMaterialCompositeShader,&shader));ok(d->SetPixelShader(shader));
    float c[79][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=100;c[1][0]=c[1][1]=1;c[1][2]=-1;c[2][0]=1;
    for(unsigned i=0;i<4;++i)c[3+i][i]=1;c[20][2]=1;c[30][2]=1;c[33][0]=W;c[33][1]=H;c[33][2]=W/2;c[33][3]=H/2;c[52][0]=c[52][1]=1;c[73][0]=1;c[73][1]=48;c[74][0]=.8f;c[74][2]=.6f;c[76][0]=1;c[76][1]=.8f;c[76][2]=.6f;
    ok(d->SetPixelShaderConstantF(0,c[0],79));IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    float interior=0,join=0,edge=0;for(unsigned y=8;y<H-8;++y)for(unsigned x=8;x<W-8;++x){const float* p=reinterpret_cast<const float*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)+x*4;for(unsigned k=0;k<3;++k){assert(std::isfinite(p[k]));float e=std::fabs(p[k]-expected[(y*W+x)*3+k]);interior=std::max(interior,e);if(x>=split-2&&x<=split+2)join=std::max(join,e);if(x>=silhouette-2&&x<=silhouette+2)edge=std::max(edge,e);}}
    ok(staging->UnlockRect());std::printf("TERRAIN_NEUTRAL_FRAME interior=%.9g join=%.9g edge=%.9g split=%u silhouette=%u distance=%.6f counterexample=%u\n",interior,join,edge,split,silhouette,baseDistance,unsigned(counterexample));std::fflush(stdout);
    if(counterexample)assert(interior>.01f&&join>.01f);else assert(interior<.002f&&join<.002f&&edge<.002f);
    staging->Release();target->Release();shader->Release();for(auto* t:textures)t->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
