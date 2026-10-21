// Grazing-angle material demonstration using the production cache, capture and final shader.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "world_compiled_shaders.h"
static constexpr UINT W=1280,H=800;
static IDirect3DTexture9* solid(IDirect3DDevice9* d,float x,float y,float z,float a){Image i{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float data[]={x,y,z,a};std::memcpy(i.pixels.data(),data,16);return texture(d,i);}
static void save(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::string& path){
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));
    const UINT bytes=desc.Format==D3DFMT_A32B32G32R32F?16:4;std::ofstream f(path,std::ios::binary);UINT header[]={0x31524746,desc.Width,desc.Height,UINT(desc.Format),bytes};f.write((char*)header,20);D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(UINT y=0;y<desc.Height;++y)f.write((char*)lock.pBits+y*lock.Pitch,desc.Width*bytes);stage->UnlockRect();drop(stage);
}
int main(int argc,char** argv){
    assert(argc==3);std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NormalOblique";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *baseSurface=nullptr,*depthSurface=nullptr,*zbuffer=nullptr,*output=nullptr;
    IDirect3DTexture9 *base=nullptr,*depth=nullptr;
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&base,nullptr));base->GetSurfaceLevel(0,&baseSurface);
    ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth,nullptr));depth->GetSurfaceLevel(0,&depthSurface);
    ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&zbuffer,nullptr));
    auto first=image(folder+"source.fgr");IDirect3DTexture9* diffuse=nullptr;ok(d->CreateTexture(first.width,first.height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&diffuse,nullptr));
    for(unsigned level=0;level<diffuse->GetLevelCount();++level){auto im=image(folder+"source.fgr."+std::to_string(level));D3DLOCKED_RECT lock{};ok(diffuse->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+y*im.width*4,im.width*4);diffuse->UnlockRect(level);}
    auto vsBytes=read(folder+"terrain-vs.bin"),psBytes=read(folder+"terrain-ps.bin");std::vector<std::uint32_t> patched;assert(!NorthlightWaterShaderPatch::patch((std::uint32_t*)vsBytes.data(),vsBytes.size()/4,patched));
    IDirect3DVertexShader9 *vs=nullptr,*forwardVS=nullptr;IDirect3DPixelShader9 *ps=nullptr,*beauty=nullptr,*material=nullptr;
    ok(d->CreateVertexShader((DWORD*)vsBytes.data(),&vs));ok(d->CreateVertexShader((DWORD*)patched.data(),&forwardVS));ok(d->CreatePixelShader((DWORD*)psBytes.data(),&ps));
    auto beautyBytes=read(folder+"PreviewBase.bin");ok(d->CreatePixelShader((DWORD*)beautyBytes.data(),&beauty));ok(d->CreatePixelShader(kMaterialCompositeShader,&material));
    const float angle=.20943951f,s=std::sin(angle),c=std::cos(angle),xs=1.7320508f*H/W,ys=1.7320508f;
    float bank[79][4]={};bank[0][0]=1;bank[1][1]=s;bank[1][2]=-c;bank[2][1]=c;bank[2][2]=s;bank[3][1]=-.7f*c+4*s;bank[3][2]=-.7f*s-4*c;bank[3][3]=1;
    bank[4][0]=xs;bank[5][1]=ys;bank[6][2]=100.f/(.1f-100.f);bank[6][3]=-1;bank[7][2]=.1f*100/(.1f-100);
    bank[18][0]=bank[18][1]=.25f;bank[13][0]=bank[13][1]=.5f;
    const float light[]={-.4f,.3f,.8660254f};const float energy=.3f*c+.8660254f*s;
    bank[78][0]=.2f+energy;bank[78][1]=.22f+.95f*energy;bank[78][2]=.26f+.85f*energy;
    const float macro[]={bank[78][0],bank[78][1],bank[78][2]};auto* baseline=solid(d,macro[0],macro[1],macro[2],1);auto* normal=solid(d,0,c,s,1);auto* white=solid(d,1,1,1,1);
    struct V{float x,y,z,nx,ny,nz;};const V floor[]={{-24,-3.4f,0,0,0,1},{24,-3.4f,0,0,0,1},{-24,28,0,0,0,1},{24,28,0,0,0,1}};
    const D3DVERTEXELEMENT9 decls[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(decls,&decl));
    D3DVIEWPORT9 vp={0,0,W,H,0,1};d->SetRenderTarget(0,baseSurface);d->SetRenderTarget(1,depthSurface);d->SetDepthStencilSurface(zbuffer);d->SetViewport(&vp);
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})d->SetRenderState(key,TRUE);
    for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(key,FALSE);
    d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
    d->SetTexture(0,diffuse);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    d->SetVertexDeclaration(decl);d->SetVertexShader(forwardVS);d->SetPixelShader(beauty);d->SetVertexShaderConstantF(0,bank[0],34);d->SetPixelShaderConstantF(78,bank[78],1);
    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V)));ok(d->EndScene());d->SetRenderTarget(1,nullptr);
    NorthlightAutoBump bump(d);NorthlightQuality::Settings settings;settings.autoBump=1;settings.autoBumpStrength=100;bump.configure(settings);bump.setProjection(xs,ys,-1);
    bump.registerVertex(vs,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)vsBytes.data(),vsBytes.size()/4),(DWORD*)vsBytes.data(),vsBytes.size()/4);bump.registerPixel(ps,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)psBytes.data(),psBytes.size()/4));
    d->SetVertexShader(vs);d->SetPixelShader(ps);ok(d->BeginScene());bump.capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));},true);ok(d->EndScene());assert(bump.texture());
    d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,output);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(material);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    float f[79][4]={};f[0][0]=1.f/W;f[0][1]=1.f/H;f[0][2]=.1f;f[0][3]=100;f[1][0]=xs;f[1][1]=ys;f[1][2]=-1;f[2][0]=1;for(unsigned i=0;i<4;++i)f[3+i][i]=1;f[20][2]=1;f[30][2]=1;f[33][0]=W;f[33][1]=H;f[33][2]=W/2;f[33][3]=H/2;f[73][1]=48;std::memcpy(f[74],light,12);f[76][0]=1;f[76][1]=.95f;f[76][2]=.85f;
    d->SetTexture(0,base);d->SetTexture(1,depth);d->SetTexture(4,white);d->SetTexture(5,base);d->SetTexture(8,white);d->SetTexture(9,bump.texture());d->SetTexture(10,white);d->SetTexture(12,baseline);d->SetTexture(14,normal);
    for(unsigned sampler:{0u,1u,4u,5u,8u,9u,10u,12u,14u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    for(unsigned enabled=0;enabled<2;++enabled){f[73][0]=float(enabled)*.75f;d->SetPixelShaderConstantF(0,f[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());save(d,output,folder+(enabled?"normal-on.fgr":"normal-off.fgr"));}
    // Compare the same native diffuse draw used by the game, with and without POM.
    bank[12][1]=bank[12][2]=1;std::memcpy(bank[25],macro,12);
    for(unsigned mode=1;mode<=4;++mode){
        bump.endFrame();settings.autoBumpTerrain=mode==1?1:2;settings.autoBumpStrength=75;settings.autoParallaxShadows=mode>=3;settings.autoCavity=mode>=4;bump.configure(settings);
        const float shadowLight[4]={light[0],light[1],light[2],1};bump.setLight(shadowLight);
        d->SetRenderTarget(0,baseSurface);d->SetDepthStencilSurface(zbuffer);d->SetViewport(&vp);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(ps);d->SetVertexShaderConstantF(0,bank[0],34);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetTexture(0,diffuse);d->SetTexture(1,white);
        for(unsigned sampler:{0u,1u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);}
        float nativeConstants[3][4]={};d->SetPixelShaderConstantF(0,nativeConstants[0],3);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff18212d,1,0));ok(d->BeginScene());
        bump.capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));},true);
        ok(bump.nativeDraw([&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,floor,sizeof(V));}));ok(d->EndScene());
        d->SetRenderTarget(0,output);d->SetDepthStencilSurface(nullptr);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(material);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        d->SetTexture(0,base);d->SetTexture(1,depth);d->SetTexture(4,white);d->SetTexture(5,base);d->SetTexture(6,bump.occlusionTexture());d->SetTexture(8,white);d->SetTexture(9,bump.texture());d->SetTexture(10,white);d->SetTexture(12,baseline);d->SetTexture(14,normal);
        for(unsigned sampler:{0u,1u,4u,5u,6u,8u,9u,10u,12u,14u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
        f[73][0]=.75f;f[78][0]=bump.occlusionTexture()?1.f:0.f;f[18][0]=.2f;f[18][1]=.22f;f[18][2]=.26f;f[18][3]=1;
        d->SetPixelShaderConstantF(0,f[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());const char* names[]={"normal-on.fgr","pom-on.fgr","pom-shadow.fgr","pom-shadow-cavity.fgr"};save(d,output,folder+names[mode-1]);
    }
    std::puts("OBLIQUE_NATIVE_POM_PASS camera_height=.7 pitch_degrees=12 native_diffuse_normal_cache_capture_and_composite");std::puts("OBLIQUE_OCCLUSION_PASS");return 0;
}
