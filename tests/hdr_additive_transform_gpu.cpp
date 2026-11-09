// The logged XYZ/NORMAL/DIFFUSE/TEX1 sky must retain native COUNT2 UVs.
#undef NDEBUG
#define main hdr_frame_unused
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_color.h"
static constexpr unsigned W=64,H=64;
static void fill(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DPixelShader9* shader,const float (&color)[4]){
    ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(shader));ok(d->SetPixelShaderConstantF(0,color,1));
    ok(d->BeginScene());ok(NorthlightHdr::drawQuad(d,W,H));ok(d->EndScene());
}
template<class Draw> static double timed(IDirect3DDevice9* d,Draw draw){
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    for(unsigned i=0;i<16;++i)draw();ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));
    for(unsigned i=0;i<32;++i)draw();ok(end->Issue(D3DISSUE_END));UINT64 a=0,b=0,hz=0;HRESULT hr;
    while((hr=end->GetData(&b,sizeof b,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);ok(start->GetData(&a,sizeof a,0));ok(frequency->GetData(&hz,sizeof hz,0));
    start->Release();end->Release();frequency->Release();assert(hz&&b>a);return double(b-a)*1000/double(hz)/32;
}



struct SkyVertex {float x,y,z,nx,ny,nz;DWORD color;float u,v;};
static D3DMATRIX identityMatrix(){D3DMATRIX m{};for(unsigned i=0;i<4;++i)m.m[i][i]=1;return m;}
static void skyState(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DTexture9* image,unsigned width,unsigned height,
                     const D3DMATRIX& projection,const D3DMATRIX& uv,D3DTEXTUREFILTERTYPE filter,D3DTEXTUREFILTERTYPE mip){
    ok(NorthlightHdr::quadState(d,target,width,height));ok(d->SetPixelShader(nullptr));
    ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1));
    const auto identity=identityMatrix();ok(d->SetTransform(D3DTS_WORLD,&identity));ok(d->SetTransform(D3DTS_VIEW,&identity));
    ok(d->SetTransform(D3DTS_PROJECTION,&projection));ok(d->SetTransform(D3DTS_TEXTURE0,&uv));
    ok(d->SetTexture(0,image));ok(d->SetTexture(1,nullptr));
    for(auto state:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})ok(d->SetTextureStageState(0,state,D3DTOP_MODULATE));
    for(auto state:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})ok(d->SetTextureStageState(0,state,D3DTA_TEXTURE));
    for(auto state:{D3DTSS_COLORARG2,D3DTSS_ALPHAARG2})ok(d->SetTextureStageState(0,state,D3DTA_DIFFUSE));
    ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));ok(d->SetTextureStageState(0,D3DTSS_RESULTARG,D3DTA_CURRENT));
    ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2));
    ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    ok(d->SetRenderState(D3DRS_SPECULARENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
    ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
    ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,state,filter));
    ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,mip));
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* constant=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    Image pattern{8,8,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(256)};
    for(unsigned i=0;i<64;++i){DWORD pixel=D3DCOLOR_ARGB(96+i*2,20+(i*37)%220,30+(i*53)%210,40+(i*17)%200);std::memcpy(pattern.pixels.data()+i*4,&pixel,4);}
    auto* image=texture(d,pattern);const auto identity=identityMatrix();
    unsigned cases=0;float legacyError=0,bakedError=0;
    for(bool lit:{false,true})for(bool tested:{false,true})for(bool transformed:{false,true})for(bool perspective:{false,true})for(bool affine:{false,true})for(auto filter:{D3DTEXF_POINT,D3DTEXF_LINEAR})
    for(auto mip:{D3DTEXF_NONE,D3DTEXF_LINEAR})for(float white:{80.f,160.f})for(unsigned opacity:{64u,160u,255u}){
        auto projection=identity,uv=identity;
        if(perspective){projection.m[2][2]=1.25f;projection.m[2][3]=1;projection.m[3][2]=-.625f;projection.m[3][3]=0;}
        if(affine){uv.m[0][0]=.5f;uv.m[1][0]=.25f;uv.m[2][0]=.125f;uv.m[0][1]=-.25f;uv.m[1][1]=.5f;uv.m[2][1]=.25f;}
        const DWORD color=D3DCOLOR_ARGB(opacity,200,180,240);
        SkyVertex vertices[]={{-1,1,0,0,0,1,color,-.125f,0},{1,1,0,0,0,1,color,1.25f,0},{-1,-1,0,0,0,1,color,-.125f,1},{1,-1,0,0,0,1,color,1.25f,1}};
        if(perspective)for(unsigned i=0;i<4;++i){const float z=i&1?2.f:1.f;vertices[i].x*=z;vertices[i].y*=z;vertices[i].z=z;}
        SkyVertex baked[4];std::copy(vertices,vertices+4,baked);
        if(transformed)for(auto& v:baked){const float u=v.u,w=v.v;v.u=u*uv.m[0][0]+w*uv.m[1][0]+uv.m[2][0];v.v=u*uv.m[0][1]+w*uv.m[1][1]+uv.m[2][1];}
        std::vector<std::uint16_t> reference(W*H*4),direct(W*H*4);NorthlightHdr::Color transfer;
        for(unsigned mode=0;mode<3;++mode){
            const float background[]={.3f,.7f,1.4f,.8f};fill(d,target,constant,background);
            skyState(d,target,image,W,H,projection,mode==2?identity:uv,filter,mip);
            if(!transformed)ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
            D3DMATERIAL9 material{};material.Diffuse={.8f,.6f,.4f,1};material.Ambient={.3f,.2f,.1f,1};
            ok(d->SetMaterial(&material));ok(d->SetRenderState(D3DRS_AMBIENT,0x00404040));ok(d->SetRenderState(D3DRS_COLORVERTEX,FALSE));
            ok(d->SetRenderState(D3DRS_LIGHTING,lit));ok(d->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_MATERIAL));
            D3DLIGHT9 light{};light.Type=D3DLIGHT_DIRECTIONAL;light.Diffuse={.6f,.4f,.2f,1};light.Direction={0,0,-1};ok(d->SetLight(0,&light));ok(d->LightEnable(0,lit));
            ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,tested));ok(d->SetRenderState(D3DRS_ALPHAREF,0));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));
            const float bank[16]={3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18};ok(d->SetPixelShaderConstantF(0,bank,4));
            DWORD alpha[4];const D3DRENDERSTATETYPE states[]={D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
            for(unsigned i=0;i<4;++i)ok(d->GetRenderState(states[i],alpha+i));
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mode==2?baked:vertices,sizeof(SkyVertex));};
            ok(d->BeginScene());if(mode)ok(transfer.draw(d,white,draw));else ok(transfer.legacyLayer(d,white,draw));ok(d->EndScene());
            float restored[16];ok(d->GetPixelShaderConstantF(0,restored,4));assert(!std::memcmp(bank,restored,sizeof bank));
            for(unsigned i=0;i<4;++i){DWORD value=0;ok(d->GetRenderState(states[i],&value));assert(value==alpha[i]);}
            DWORD value=0;ok(d->GetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,&value));assert(value==(transformed?D3DTTFF_COUNT2:D3DTTFF_DISABLE));
            D3DMATRIX matrix{};ok(d->GetTransform(D3DTS_TEXTURE0,&matrix));assert(!std::memcmp(&matrix,mode==2?&identity:&uv,sizeof matrix));
            IDirect3DPixelShader9* ps=nullptr;ok(d->GetPixelShader(&ps));assert(!ps);
            ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W*4;++x){const auto pixel=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x];const unsigned at=y*W*4+x;
                if(!mode)reference[at]=pixel;
                else if(mode==1){direct[at]=pixel;legacyError=std::max(legacyError,std::fabs(half(pixel)-half(reference[at])));}
                else bakedError=std::max(bakedError,std::fabs(half(pixel)-half(direct[at])));}
            ok(staging->UnlockRect());
        }
        assert(transfer.layers==1&&transfer.fixedAdditiveDirect==2&&transfer.payloadBytes()==W*H*4);++cases;
    }
    std::printf("HDR_ADDITIVE_ORACLE cases=%u rgbaLegacyError=%g rgbaBakedUvError=%g constantsAlphaUvRestored=true\n",cases,legacyError,bakedError);
    // The old UNORM8 premultiplied layer quantizes before sRGB decoding;
    // the direct transfer keeps float precision. Baked native UVs are exact.
    assert(legacyError<.02f&&bakedError==0);
    // Unsupported state must retain exactly the original compatibility path.
    for(unsigned reject=0;reject<8;++reject){
        NorthlightHdr::Color transfer;skyState(d,target,image,W,H,identity,identity,D3DTEXF_POINT,D3DTEXF_NONE);
        IDirect3DSurface9* msaa=nullptr;
        if(reject==0){ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&msaa,nullptr));ok(d->SetRenderTarget(0,msaa));}
        if(reject==1)ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_CAMERASPACENORMAL));
        if(reject==2)ok(d->SetRenderState(D3DRS_FOGENABLE,TRUE));
        if(reject==3)ok(d->SetRenderState(D3DRS_SPECULARENABLE,TRUE));
        if(reject==4)ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        if(reject==5)ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2|D3DTTFF_PROJECTED));
        if(reject==6)ok(d->SetTextureStageState(0,D3DTSS_RESULTARG,D3DTA_TEMP));
        if(reject==7)ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
        const SkyVertex q[]={{-1,1,0,0,0,1,0xff808080,0,0},{1,1,0,0,0,1,0xff808080,1,0},{-1,-1,0,0,0,1,0xff808080,0,1},{1,-1,0,0,0,1,0xff808080,1,1}};
        ok(d->BeginScene());ok(transfer.draw(d,80,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(SkyVertex));}));ok(d->EndScene());
        assert(transfer.layers==1&&transfer.fixedAdditiveDirect==0);if(msaa)msaa->Release();
    }
    std::printf("HDR_ADDITIVE_FALLBACK cases=8 msaaCoordinatesFogSpecularBlendProjectionResultOperation=retained\n");
    {
        IDirect3DSurface9* depth=nullptr;IDirect3DQuery9* query=nullptr;
        ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));ok(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query));
        SkyVertex q[]={{-1,1,.5f,0,0,1,0xff808080,0,0},{1,1,.5f,0,0,1,0xff808080,1,0},{-1,-1,.5f,0,0,1,0xff808080,0,1},{1,-1,.5f,0,0,1,0xff808080,1,1}};
        auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(SkyVertex));};
        DWORD nativeHits=0;std::vector<std::uint16_t> nativeProbe(W*H*4);
        unsigned sideCases=0;std::uint64_t visibleSamples=0;
        for(bool lit:{false,true})for(auto function:{D3DCMP_NEVER,D3DCMP_LESS,D3DCMP_EQUAL,D3DCMP_LESSEQUAL,D3DCMP_GREATER,D3DCMP_NOTEQUAL,D3DCMP_GREATEREQUAL,D3DCMP_ALWAYS})
        for(DWORD alphaRef:{0u,1u,64u,128u,192u,254u,255u})for(unsigned opacity:{1u,128u,255u}){
        for(auto& vertex:q)vertex.color=D3DCOLOR_ARGB(opacity,128,192,96);
        for(bool direct:{false,true}){
            NorthlightHdr::Color transfer;skyState(d,target,image,W,H,identity,identity,D3DTEXF_POINT,D3DTEXF_NONE);
            ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAREF,alphaRef));ok(d->SetRenderState(D3DRS_ALPHAFUNC,function));
            ok(d->SetRenderState(D3DRS_LIGHTING,lit));ok(d->SetRenderState(D3DRS_COLORVERTEX,TRUE));ok(d->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1));
            ok(d->SetDepthStencilSurface(depth));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
            ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_INCRSAT));
            ok(d->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,255));
            ok(d->BeginScene());ok(query->Issue(D3DISSUE_BEGIN));if(direct)ok(transfer.draw(d,80,draw));else ok(draw());ok(query->Issue(D3DISSUE_END));ok(d->EndScene());
            DWORD hits=0;HRESULT hr;do{hr=query->GetData(&hits,sizeof hits,D3DGETDATA_FLUSH);}while(hr==S_FALSE);ok(hr);
            if(!direct)nativeHits=hits;else assert(hits==nativeHits&&transfer.layers==0&&transfer.fixedAdditiveDirect==1&&transfer.payloadBytes()==0);
            assert(hits<=W*H);if(direct)visibleSamples+=hits;
            // Read both depth and stencil through an exact-match colour probe.
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
            ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_STENCILREF,1));ok(d->SetRenderState(D3DRS_STENCILMASK,255));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
            ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShader(constant));const float marker[]={.125f,.25f,.5f,.75f};ok(d->SetPixelShaderConstantF(0,marker,1));
            ok(d->BeginScene());ok(draw());ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W*4;++x){const auto pixel=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x];
                assert(half(pixel)==0||half(pixel)==marker[x%4]);if(!direct)nativeProbe[y*W*4+x]=pixel;else assert(pixel==nativeProbe[y*W*4+x]);}
            ok(staging->UnlockRect());
        }
        ++sideCases;}
        query->Release();ok(d->SetDepthStencilSurface(nullptr));depth->Release();
        std::printf("HDR_ADDITIVE_NATIVE_SIDE_EFFECTS depthStencil=exact cases=%u occlusionSamples=%llu layersAllocated=0\n",sideCases,(unsigned long long)visibleSamples);
    }
    if(argc==3){
        constexpr unsigned width=3840,height=2160;IDirect3DSurface9* large=nullptr;
        ok(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&large,nullptr));
        NorthlightHdr::Color transfer;
        const SkyVertex q[]={{-1,1,0,0,0,1,0xa0c8b4f0,0,0},{1,1,0,0,0,1,0xa0c8b4f0,1,0},{-1,-1,0,0,0,1,0xa0c8b4f0,0,1},{1,-1,0,0,0,1,0xa0c8b4f0,1,1}};
        for(unsigned draws:{2u,8u}){
            auto frame=[&](bool direct){ok(d->BeginScene());skyState(d,large,image,width,height,identity,identity,D3DTEXF_LINEAR,D3DTEXF_NONE);
                for(unsigned i=0;i<draws;++i){auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(SkyVertex));};if(direct)ok(transfer.draw(d,80,draw));else ok(transfer.legacyLayer(d,80,draw));}ok(d->EndScene());};
            double old[5],now[5];for(unsigned i=0;i<5;++i){if(i%2){now[i]=timed(d,[&]{frame(true);});old[i]=timed(d,[&]{frame(false);});}else{old[i]=timed(d,[&]{frame(false);});now[i]=timed(d,[&]{frame(true);});}}
            std::sort(old,old+5);std::sort(now,now+5);std::printf("HDR_ADDITIVE_BENCH width=%u height=%u msaa=0 draws=%u legacyMs=%.6f directMs=%.6f scope=isolated_pass_not_game_fps\n",width,height,draws,old[2],now[2]);
        }large->Release();
    }
    d->SetTexture(0,nullptr);image->Release();constant->Release();target->Release();staging->Release();d->Release();api->Release();DestroyWindow(window);
    std::printf("HDR_ADDITIVE_PASS\n");return 0;
}
