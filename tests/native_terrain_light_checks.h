#pragma once
#include "patch_terrain_shadow_coverage.h"
// Actual terrain VS/PS programs exercise the production scoped submit, including
// the interleaved position/RGB/falloff registers between selected sources.
static int nativeTerrainLightChecks(char** argv){
    constexpr unsigned Size=128;auto vertex=words(argv[2]),pixel=words(argv[3]);bool rgbOnly=false;
    if(std::strcmp(argv[5],"terrain-neutral")==0){std::vector<uint32_t> changed;assert(NorthlightTerrainShadow::patch(reinterpret_cast<const uint32_t*>(pixel.data()),pixel.size(),changed));pixel.assign(changed.begin(),changed.end());}
    if(std::strcmp(argv[5],"terrain-coverage")==0){std::vector<uint32_t> changed;assert(NorthlightTerrainCoverage::vertex(reinterpret_cast<const uint32_t*>(vertex.data()),vertex.size(),changed));vertex.assign(changed.begin(),changed.end());NorthlightTerrainCoverage::PixelInfo info;assert(NorthlightTerrainCoverage::pixel(reinterpret_cast<const uint32_t*>(pixel.data()),pixel.size(),changed,info,1));pixel.assign(changed.begin(),changed.end());}
    assert(!NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(vertex.data()),vertex.size()));
    assert(NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(vertex.data()),vertex.size(),&rgbOnly,true)==3&&rgbOnly);
    assert(NorthlightNativeLocalPatch::pointOwnerPixel(reinterpret_cast<const uint32_t*>(pixel.data()),pixel.size()));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightTerrainOwnerTest";assert(RegisterClassW(&wc));
    auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,Size,Size,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=Size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* real=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&real));FaultDevice fault(real);IDirect3DDevice9* d=&fault;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;must(d->CreateVertexShader(vertex.data(),&vs));must(d->CreatePixelShader(pixel.data(),&ps));
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));
    IDirect3DTexture9* texture=nullptr;must(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));D3DLOCKED_RECT lock;must(texture->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=0xffb39980;must(texture->UnlockRect(0));
    for(unsigned i=0;i<2;++i){must(d->SetTexture(i,texture));must(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));must(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));must(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
    must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));must(d->SetVertexDeclaration(declaration));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));must(d->SetRenderState(D3DRS_FOGENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));must(d->SetRenderState(D3DRS_ZENABLE,FALSE));
    const float camera[]={1545,-4321,21};unsigned cases=0;float worst=0,nativeDifference=0;
    for(unsigned samples:{0u,2u}){
        DWORD quality=0;must(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A16B16G16R16F,TRUE,D3DMULTISAMPLE_TYPE(samples),&quality));
        IDirect3DSurface9 *target=nullptr,*resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        must(d->SetDepthStencilSurface(nullptr));must(d->SetRenderTarget(0,target));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));
        for(float angle:{0.f,.4f,-1.1f})for(unsigned blend:{0u,1u,2u}){
            const float co=std::cos(angle),si=std::sin(angle);float c[37][4]{};
            c[0][0]=co;c[0][1]=si;c[1][0]=-si;c[1][1]=co;c[2][2]=1;c[3][0]=-co*camera[0]+si*camera[1];c[3][1]=-si*camera[0]-co*camera[1];c[3][2]=-camera[2];c[3][3]=1;
            c[4][0]=c[5][1]=1;c[6][2]=1.001f;c[6][3]=1;c[7][2]=-.1001f;c[12][1]=c[12][2]=1;c[18][0]=c[18][1]=c[19][0]=c[19][1]=.2f;
            c[23][0]=camera[0];c[23][1]=camera[1];for(unsigned k=0;k<3;++k)c[25][k]=.08f;
            for(unsigned s=0;s<3;++s){c[28+3*s][0]=float(s)-1;c[28+3*s][2]=2;c[29+3*s][0]=s==1?.03f:1.5f;c[29+3*s][1]=s==1?.08f:.6f;c[29+3*s][2]=s==1?.15f:.2f;c[29+3*s][3]=.37f+float(s)*.1f;c[30+3*s][0]=.1f;c[30+3*s][1]=.7f;c[30+3*s][2]=.03f;}
            float inverse[16]={co,-si,0,0,si,co,0,0,0,0,1,0,camera[0],camera[1],camera[2],1},bank[44];assert(NorthlightNativeDirect::terrainBank(c[28],c[0],bank));
            NorthlightNativeDirect::BankMatch match;NorthlightEquipmentLights::decodeBank(bank,3,inverse,camera,match.values,match.valid);
            struct Binding {float ownerGain;};Binding bindings[]={{1},{0},{1}};for(unsigned s=0;s<3;++s){assert(match.valid[s]);match.bindings[s]=int(s);assert(std::fabs(match.values[s].position[0]-(camera[0]+float(s)-1))<.002f);assert(std::fabs(match.values[s].position[1]-camera[1])<.002f);assert(std::fabs(match.values[s].position[2]-(camera[2]+2))<.002f);}
            float rgb[16],changed[36];const unsigned mask=NorthlightNativeDirect::ownedBank(bank,3,match,bindings,rgb);assert(mask==5);std::copy_n(c[28],36,changed);for(unsigned s:{0u,2u})std::copy_n(rgb+4*s,3,changed+12*s+4);
            must(d->SetVertexShaderConstantF(0,c[0],37));const float pc[3][4]={{.2f,.2f,.2f,0},{0,0,0,0},{.1f,.08f,.06f,0}};must(d->SetPixelShaderConstantF(0,pc[0],3));
            must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend!=0));must(d->SetRenderState(D3DRS_SRCBLEND,blend==1?D3DBLEND_SRCALPHA:D3DBLEND_ONE));must(d->SetRenderState(D3DRS_DESTBLEND,blend==1?D3DBLEND_INVSRCALPHA:D3DBLEND_ONE));must(d->SetRenderState(D3DRS_ZWRITEENABLE,blend==0));must(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
            struct Vertex {float p[3],n[3];};const Vertex mesh[]={{{camera[0]-4,camera[1]+4,camera[2]+4},{0,0,-1}},{{camera[0]+4,camera[1]+4,camera[2]+4},{0,0,-1}},{{camera[0]-4,camera[1]-4,camera[2]+4},{0,0,-1}},{{camera[0]+4,camera[1]-4,camera[2]+4},{0,0,-1}}};
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(Vertex));};
            auto render=[&](bool owned,bool absent,unsigned faultMode=0){
                must(d->SetVertexShaderConstantF(28,absent?changed:c[28],9));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff203040,1,0));must(d->BeginScene());
                NativeOwnerSubmitTest owner(d);HRESULT hr=S_OK;
                if(faultMode==1)fault.failAt=fault.sets+1;
                if(owned)hr=owner.draw(mask,rgb,bank,draw,true,c[28],changed);else hr=draw();must(hr);assert(!owner.failed);fault.failAt=0;
                float after[36];must(d->GetVertexShaderConstantF(28,after,9));assert(!std::memcmp(after,absent?changed:c[28],sizeof after));
                must(d->EndScene());must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));return pixels(d,resolved,Size);
            };
            const auto absent=render(false,true),native=render(false,false),owned=render(true,false),fallback=render(true,false,1);
            float delta=0;for(size_t i=0;i<owned.size();++i){worst=std::max(worst,std::fabs(owned[i]-absent[i]));nativeDifference=std::max(nativeDifference,std::fabs(native[i]-absent[i]));delta=std::max(delta,std::fabs(fallback[i]-native[i]));}assert(worst<.00001f&&delta<.00001f);++cases;
        }
        target->Release();resolved->Release();
    }
    assert(nativeDifference>.02f);std::printf("NATIVE_TERRAIN_OWNER_PASS cases=%u cameras=3 blends=3 msaa=2 rawNativeDifference=%.6g ownedError=%.6g alphaAndNeighboursPreserved=1 exactRestore=1 setFailureFallback=1\n",cases,nativeDifference,worst);
    declaration->Release();vs->Release();ps->Release();texture->Release();real->Release();api->Release();FreeLibrary(library);DestroyWindow(hwnd);return 0;
}
