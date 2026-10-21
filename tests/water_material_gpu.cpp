// Real tiled mesh, including an unlisted liquid VS drawn BEFORE the listed one.
#define main unused_distance_fixture
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <cstdarg>
static void logf(const char* f,...){va_list a;va_start(a,f);std::vfprintf(stderr,f,a);std::fputc('\n',stderr);va_end(a);}
template<class T>static void drop(T*& p){if(p){p->Release();p=nullptr;}}
#include "saved_state.h"
#include "water_renderer.h"
struct NameMaskCompositeBind {NameMaskCompositeBind(IDirect3DDevice9*,IDirect3DSurface9* p){assert(!p);}};
#include "surface_renderer.h"
#include "water_material.h"
#include "material_fixture_compiled.h"
constexpr unsigned W=192,H=128;
struct MeshVertex {float x,y,z;};
static float projected(float z){return 1000.f/(1000.f-.1f)*(1-.1f/z);}
int main(int argc,char** argv){
    assert(argc>=2);bool baseline=argc>2;WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"LiquidMaterialOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DVertexShader9 *known=nullptr,*other=nullptr;IDirect3DPixelShader9* nativePS=nullptr;
    ok(d->CreateVertexShader(kNativeWater,&known));ok(d->CreateVertexShader(kOtherWater,&other));ok(d->CreatePixelShader(kNativeColor,&nativePS));
    auto identity=[&](const DWORD* code,unsigned count,bool vs){std::vector<NorthlightWaterShaderPatch::Word> patched;assert(!NorthlightWaterShaderPatch::patch((const std::uint32_t*)code,count,patched));return NorthlightWaterShaderIdentity{NorthlightWaterShaderPatch::fnv1a64((const std::uint32_t*)code,count),NorthlightWaterShaderPatch::fnv1a64(patched.data(),patched.size()),unsigned(patched.size()),vs};};
    const NorthlightWaterShaderIdentity ids[]={identity(kNativeWater,std::size(kNativeWater),true),identity(kNativeColor,std::size(kNativeColor),false)};
    IDirect3DSurface9 *target=nullptr,*readback=nullptr,*sceneDepth=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&sceneDepth,nullptr));
    Image z{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float depth=y>H/2?projected(5/((2.f*y/H-1)/1.7f)):1;std::memcpy(z.pixels.data()+(y*W+x)*4,&depth,4);}auto* bed=texture(d,z);
    float vc[9][4]={};vc[0][0]=1.3f;vc[1][1]=1.7f;vc[2][2]=-1000.f/999.9f;vc[2][3]=-1;vc[3][2]=-.1f*1000/999.9f;vc[5][0]=vc[6][1]=vc[7][2]=vc[8][3]=1;
    NorthlightSurfaceFrame context;context.c[0][0]=1.f/W;context.c[0][1]=1.f/H;context.c[0][2]=.1f;context.c[0][3]=1000;context.c[1][0]=1.3f;context.c[1][1]=1.7f;context.c[1][2]=-1;context.c[2][1]=1;context.c[4][1]=1;context.c[5][2]=1;context.c[6][0]=-1;context.c[7][2]=3;context.c[7][3]=1;context.c[8][0]=context.c[8][1]=context.c[8][2]=.1f;context.c[10][2]=1;
    NorthlightQuality::Settings settings;settings.waterSSR=0;settings.waterGlints=0;settings.waterPointLights=0;
    auto pixels=[&]{ok(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<DWORD> result(W*H);for(unsigned y=0;y<H;++y)std::memcpy(result.data()+y*W,(char*)lock.pBits+y*lock.Pitch,W*4);readback->UnlockRect();return result;};
    auto render=[&](bool legacy,bool split,bool reverse,bool lateSnapshot=false){
        NorthlightWaterRenderer mask(d,ids,std::size(ids),false);mask.registerVertex(known,ids[0].hash,kNativeWater,std::size(kNativeWater));mask.registerPixel(nativePS,ids[1].hash,kNativeColor,std::size(kNativeColor));NorthlightSurfaceRenderer material(d);
        NorthlightWaterMaterial::Detector detector;int lookup=0;detector.noteCreate(&lookup,8,64,0);
        d->SetDepthStencilSurface(sceneDepth);d->SetRenderTarget(0,target);D3DVIEWPORT9 vp={0,0,W,H,0,1};d->SetViewport(&vp);d->SetFVF(D3DFVF_XYZ);d->SetVertexShaderConstantF(0,vc[0],9);
        for(auto s:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(s,0);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff14242e,1,0));ok(d->BeginScene());
        unsigned tiles=split?4:1;
        for(unsigned t=0;t<tiles;++t){unsigned tile=reverse?tiles-t-1:t;float left=split?-40+20.f*tile:-40,right=split?left+20:40;MeshVertex mesh[]={{left,-3,-4},{right,-3,-4},{left,-3,-100},{right,-3,-4},{right,-3,-100},{left,-3,-100}};
            auto* vs=split&&tile%2==0?other:known;d->SetVertexShader(vs);d->SetPixelShader(nativePS);
            const bool accepted=legacy?mask.recognizesVertex(vs):detector.matches(&lookup,512,512,0);
            if(accepted&&(!lateSnapshot||vs==known)&&material.canCapture())assert(material.capture(target,bed,W,H,settings));
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,mesh,sizeof(MeshVertex));};
            if(accepted){
                if(!legacy){d->SetRenderState(D3DRS_WRAP7,D3DWRAP_U);d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_LESS);d->SetRenderState(D3DRS_ALPHAREF,255);}
                if(legacy)mask.capture(vs,nativePS,W,H,sceneDepth,vp,NorthlightWaterRenderer::UserVertices,0,draw);else mask.captureMaterial(vs,nativePS,W,H,sceneDepth,vp,NorthlightWaterRenderer::UserVertices,0,draw);
                IDirect3DVertexShader9* restoredVS=nullptr;IDirect3DPixelShader9* restoredPS=nullptr;IDirect3DSurface9 *restoredRT=nullptr,*restoredZ=nullptr;
                ok(d->GetVertexShader(&restoredVS));ok(d->GetPixelShader(&restoredPS));ok(d->GetRenderTarget(0,&restoredRT));ok(d->GetDepthStencilSurface(&restoredZ));assert(restoredVS==vs&&restoredPS==nativePS&&restoredRT==target&&restoredZ==sceneDepth);drop(restoredVS);drop(restoredPS);drop(restoredRT);drop(restoredZ);
                if(!legacy){DWORD wrap=0,alpha=0;ok(d->GetRenderState(D3DRS_WRAP7,&wrap));ok(d->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha));assert(wrap==D3DWRAP_U&&alpha==TRUE);d->SetRenderState(D3DRS_WRAP7,0);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);}
            }
            ok(draw());
        }
        auto* liquid=mask.maskTextureForDepth(0,1);material.water(target,liquid,bed,W,H,context,settings,{},nullptr,bed);ok(d->EndScene());auto result=pixels();
        d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);d->SetTexture(2,nullptr);d->SetTexture(3,nullptr);d->SetTexture(4,nullptr);return result;
    };
    auto reference=render(false,false,false);unsigned failures=0,cases=0;
    auto contaminated=render(false,true,false,true);unsigned contamination=0;for(unsigned at=0;at<W*H;++at)for(unsigned k=0;k<3;++k)contamination+=std::abs(int((contaminated[at]>>(k*8))&255)-int((reference[at]>>(k*8))&255))>2;
    assert(contamination>0);std::printf("negative control: late first-water snapshot contaminates %u colour channels despite complete mask\n",contamination);
    for(bool reverse:{false,true}){auto actual=render(baseline,true,reverse);unsigned tested=0,bad=0;
        for(unsigned y=H/2+8;y<H-8;++y)for(unsigned x=8;x<W-8;++x){unsigned at=y*W+x;bool differs=false;for(unsigned k=0;k<3;++k)differs|=std::abs(int((actual[at]>>(k*8))&255)-int((reference[at]>>(k*8))&255))>2;bad+=differs;++tested;}
        bool pass=bad<tested/100;failures+=!pass;++cases;std::printf("material tiled order=%s samples=%u seamPixels=%u %s\n",reverse?"reverse":"forward",tested,bad,pass?"PASS":"FAIL");}
    std::printf("water material continuity: %u cases, %u failures\n",cases,failures);return failures?1:0;
}
