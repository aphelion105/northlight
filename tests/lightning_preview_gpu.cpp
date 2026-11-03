#define main composite_fixture_main
#include "composite_distance_gpu.cpp"
#undef main
#include "local_light_selection.h"
struct SkyScreen {float x,y,z,w;DWORD colour;float u,v,u1,v1;};
struct CasterVertex {float x,y,z,nx,ny,nz,u,v;};
static void saveSurface(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* staging,unsigned w,unsigned h,const std::string& file){
    ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::ofstream out(file,std::ios::binary);const unsigned header[]={0x31524746,w,h,D3DFMT_A32B32G32R32F,16};out.write(reinterpret_cast<const char*>(header),20);
    for(unsigned y=0;y<h;++y)out.write(static_cast<const char*>(lock.pBits)+y*lock.Pitch,w*16);
    ok(staging->UnlockRect());assert(out);
}
int main(int argc,char** argv){
    assert(argc==4);const std::string prefix=argv[1];const auto depth=image(prefix+"depth.fgr"),normals=image(prefix+"normals.fgr");const unsigned W=depth.width,H=depth.height;
    auto bytes=read(prefix+"constants.f32");assert(bytes.size()==84*16);float c[84][4];std::memcpy(c,bytes.data(),bytes.size());
    bytes=read(prefix+"source.bin");assert(bytes.size()==48);NorthlightLocalLights::Light light;std::memcpy(&light,bytes.data(),48);assert(NorthlightLocalLights::valid(light));
    const auto sprite=image(prefix+"bolt0.fgr");const bool mask=std::ifstream(prefix+"bolt1.fgr").good();
    bytes=read(prefix+"bolt-vertices.bin");assert(bytes.size()%sizeof(SkyScreen)==0);std::vector<SkyScreen> sky(bytes.size()/sizeof(SkyScreen));std::memcpy(sky.data(),bytes.data(),bytes.size());
    auto info=read(prefix+"bolt-info.bin");assert(info.size()==8);unsigned mode,tint;std::memcpy(&mode,info.data(),4);std::memcpy(&tint,info.data()+4,4);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLightningPreview";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[3]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    auto dt=texture(d,depth),nt=texture(d,normals),zero=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{0,0,0,0}}),bolt=texture(d,sprite);
    auto mt=mask?texture(d,image(prefix+"bolt1.fgr")):nullptr;
    const std::string shaderDirectory=std::string(argv[2]).substr(0,std::string(argv[2]).find_last_of("/\\")+1);
    auto code=read(argv[2]);IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));
    IDirect3DPixelShader9 *correction=nullptr,*casterPS=nullptr;IDirect3DVertexShader9* casterVS=nullptr;
    code=read(shaderDirectory+"FlashShadow.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&correction));
    code=read(shaderDirectory+"ShadowPS.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&casterPS));
    code=read(shaderDirectory+"ShadowVS.bin");ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&casterVS));
    bytes=read(prefix+"casters.bin");assert(bytes.size()%sizeof(CasterVertex)==0);std::vector<CasterVertex> casters(bytes.size()/sizeof(CasterVertex));std::memcpy(casters.data(),bytes.data(),bytes.size());
    IDirect3DTexture9* shadow[2]{};IDirect3DSurface9 *shadowSurface[2]{},*shadowDepth=nullptr;
    ok(d->CreateDepthStencilSurface(1024,1024,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&shadowDepth,nullptr));
    for(unsigned s=0;s<16;++s){ok(d->SetTexture(s,zero));for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetVertexShader(nullptr));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_LIGHTING})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    for(unsigned cascade=0;cascade<2;++cascade){
        ok(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,shadow+cascade,nullptr));ok(shadow[cascade]->GetSurfaceLevel(0,shadowSurface+cascade));
        ok(d->SetRenderTarget(0,shadowSurface[cascade]));ok(d->SetDepthStencilSurface(shadowDepth));D3DVIEWPORT9 sv{0,0,1024,1024,0,1};ok(d->SetViewport(&sv));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1));ok(d->SetVertexShader(casterVS));ok(d->SetVertexShaderConstantF(0,c[7+cascade*4],4));ok(d->SetPixelShader(casterPS));const float opaque[]={0,0,0,-1};ok(d->SetPixelShaderConstantF(0,opaque,1));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(casters.size()/3),casters.data(),sizeof(CasterVertex)));ok(d->EndScene());
    }
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetVertexShader(nullptr));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    for(unsigned phase=0;phase<3;++phase){
        ok(d->SetTexture(1,dt));ok(d->SetTexture(14,nt));ok(d->SetTexture(11,zero));ok(d->SetTexture(9,zero));ok(d->SetTexture(12,zero));
        for(unsigned k=0;k<3;++k){c[36][k]=light.position[k];c[44][k]=phase==1?light.diffuse[k]:0;}
        c[36][3]=light.attenuationEnd;c[44][3]=NorthlightLocalLightSelection::transport(light);
        ok(d->SetPixelShaderConstantF(0,c[0],84));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetPixelShader(shader));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        saveSurface(d,target,staging,W,H,prefix+"unshadowed-"+std::to_string(phase)+".fgr");
        ok(d->SetTexture(2,shadow[0]));ok(d->SetTexture(3,shadow[1]));ok(d->SetPixelShader(correction));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        saveSurface(d,target,staging,W,H,prefix+"light-"+std::to_string(phase)+".fgr");
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX2));
        ok(d->SetTexture(0,bolt));ok(d->SetTexture(1,mt));
        for(unsigned slot=0;slot<2;++slot){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,k,D3DTEXF_LINEAR));
            ok(d->SetTextureStageState(slot,D3DTSS_TEXCOORDINDEX,slot));ok(d->SetTextureStageState(slot,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
            ok(d->SetTextureStageState(slot,D3DTSS_COLOROP,slot==1&&!mask?D3DTOP_DISABLE:D3DTOP_MODULATE));
            ok(d->SetTextureStageState(slot,D3DTSS_COLORARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(slot,D3DTSS_COLORARG2,slot?D3DTA_CURRENT:D3DTA_DIFFUSE));
            ok(d->SetTextureStageState(slot,D3DTSS_ALPHAOP,slot||!mode?D3DTOP_MODULATE:D3DTOP_SELECTARG2));ok(d->SetTextureStageState(slot,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(slot,D3DTSS_ALPHAARG2,slot?D3DTA_CURRENT:D3DTA_DIFFUSE));
        }
        ok(d->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
        for(auto& v:sky)v.colour=phase==1?tint:tint&0xffffff;
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(sky.size()/3),sky.data(),sizeof(SkyScreen)));ok(d->EndScene());
        saveSurface(d,target,staging,W,H,prefix+"bolt-"+std::to_string(phase)+".fgr");
    }
    for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetPixelShader(nullptr));shader->Release();correction->Release();casterPS->Release();casterVS->Release();for(auto p:shadowSurface)p->Release();for(auto p:shadow)p->Release();shadowDepth->Release();if(mt)mt->Release();bolt->Release();zero->Release();nt->Release();dt->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("PASS offline GPU lightning frames: before/peak/after");
}
