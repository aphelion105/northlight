#undef NDEBUG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_hdr_frame_unused
#include "composite_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
#include "hdr_frame.h"
#include "hdr_color.h"
#include "world_compiled_shaders.h"

static constexpr unsigned W=64,H=64;
static void fill(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DPixelShader9* shader,const float (&color)[4]){
    ok(NorthlightHdr::quadState(device,target,W,H));ok(device->SetPixelShader(shader));ok(device->SetPixelShaderConstantF(0,color,1));
    ok(device->BeginScene());ok(NorthlightHdr::drawQuad(device,W,H));ok(device->EndScene());
}
static void sample(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DSurface9* staging,float (&color)[4],unsigned x=0,unsigned y=0){
    ok(device->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::uint16_t values[4];std::memcpy(values,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,8);ok(staging->UnlockRect());
    for(unsigned i=0;i<4;++i)color[i]=half(values[i]);
}
static IDirect3DTexture9* legacy(IDirect3DDevice9* device,bool spot){
    Image image{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H*16)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const float value=spot?(x==W/2&&y==H/2?1.f:.1f):1.f;
        const float pixel[]={value,value,value,1};std::memcpy(image.pixels.data()+(y*W+x)*16,pixel,16);
    }
    return texture(device,image);
}
static IDirect3DTexture9* solid(IDirect3DDevice9* device,float r,float g,float b,float a){
    Image image{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float rgba[]={r,g,b,a};std::memcpy(image.pixels.data(),rgba,16);return texture(device,image);
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    IDirect3DSurface9 *source=nullptr,*destination=nullptr,*readback=nullptr,*scalar=nullptr;IDirect3DPixelShader9* constant=nullptr;
    ok(device->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&source,nullptr));
    ok(device->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&destination,nullptr));
    ok(device->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(device->CreateOffscreenPlainSurface(1,1,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&scalar,nullptr));
    ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    auto* white=legacy(device,false);auto* spot=legacy(device,true);
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.autoExposure=false;settings.bloom=0;
    auto prepare=[&](IDirect3DTexture9* input){ok(device->BeginScene());ok(frame.prepareLegacy(device,input,nullptr,nullptr,W,H,0,1,settings));ok(device->EndScene());};
    auto finish=[&](float dt=1.f/60,bool cut=false){ok(device->BeginScene());ok(frame.finish(device,source,destination,settings,dt,true,cut));ok(device->EndScene());};
    auto exposure=[&]{float value[4];sample(device,frame.exposureSurface(),scalar,value);return value[0];};
    prepare(white);unsigned cases=0;
    float actual[4],reference[4];
    const float ordinary[]={1,1,1,1};fill(device,source,constant,ordinary);finish();sample(device,destination,readback,actual,W/2,H/2);
    assert(std::fabs(actual[0]*80-203)<.406);++cases;
    const float grey[]={.18f,.18f,.18f,1};fill(device,source,constant,grey);finish();sample(device,destination,readback,reference,W/2,H/2);
    settings.maxNits=2000;finish();sample(device,destination,readback,actual,W/2,H/2);assert(actual[0]==reference[0]);++cases;
    settings.maxNits=1000;
    const float hdr[]={4,2,.5f,1};fill(device,source,constant,hdr);finish();sample(device,destination,readback,actual,W/2,H/2);
    assert(actual[0]*80>203&&actual[0]*80<1000&&std::fabs(actual[0]/actual[1]-2)<.004&&std::fabs(actual[0]/actual[2]-8)<.02);++cases;
    float lowerPeak=actual[0];settings.maxNits=2000;finish();sample(device,destination,readback,actual,W/2,H/2);assert(actual[0]>lowerPeak);++cases;settings.maxNits=1000;
    settings.legacyExpansion=2;prepare(white);sample(device,frame.surface(),readback,actual,W/2,H/2);assert(std::fabs(actual[0]-1)<.001);++cases;
    prepare(spot);sample(device,frame.surface(),readback,actual,W/2,H/2);assert(actual[0]>3.9f&&actual[0]<=4.01f);++cases;
    settings.maxNits=4000;prepare(spot);sample(device,frame.surface(),readback,reference,W/2,H/2);assert(reference[0]==actual[0]);++cases;
    settings.legacyExpansion=0;prepare(spot);sample(device,frame.surface(),readback,actual,W/2,H/2);assert(std::fabs(actual[0]-1)<.001);++cases;
    settings.maxNits=1000;settings.autoExposure=true;
    for(unsigned fps:{2u,30u,60u,144u}){
        frame.invalidateExposure();fill(device,source,constant,grey);finish(1.f/fps,true);assert(std::fabs(exposure())<.01);
        const float dark[]={.09f,.09f,.09f,1};fill(device,source,constant,dark);
        for(unsigned i=0;i<2*fps;++i)finish(1.f/fps);
        assert(std::fabs(exposure()-.9f)<.03);++cases;
        fill(device,source,constant,grey);finish(1.f/fps,true);
        const float bright[]={.72f,.72f,.72f,1};fill(device,source,constant,bright);
        for(unsigned i=0;i<fps/2;++i)finish(1.f/fps);
        assert(std::fabs(exposure()+1.8f)<.03);++cases;
    }
    fill(device,source,constant,grey);finish(1.f/60,true);float neutral=exposure();
    settings.bloom=1;settings.maxNits=2000;settings.uiWhite=320;finish(1.f/60,true);assert(exposure()==neutral);++cases;
    const float black[]={0,0,0,1};fill(device,source,constant,black);finish(1.f/60,true);assert(exposure()==1);++cases;
    const float extreme[]={64,64,64,1};fill(device,source,constant,extreme);finish(1.f/60,true);assert(exposure()==-2);++cases;
    settings.autoExposure=false;settings.bloom=0;settings.exposure=1;fill(device,source,constant,grey);finish();sample(device,destination,readback,actual,W/2,H/2);
    assert(std::fabs(actual[0]*80-.36f*203)<.2);++cases;
    // Production world/material shaders must retain native Northlight energy above SDR white.
    auto* greySource=solid(device,.5f,.5f,.5f,1);auto* depth=solid(device,.5f,0,0,1);auto* light=solid(device,4,2,1,1);
    auto* baseline=solid(device,.5f,.5f,.5f,1);auto* neutralTexture=solid(device,0,0,0,1);auto* distance=solid(device,.1f*100/(100-.5f*(100-.1f)),0,0,1);
    settings.legacyExpansion=0;prepare(greySource);
    float bank[68][4]{};bank[0][0]=bank[0][1]=1.f/W;bank[0][2]=.1f;bank[0][3]=100;bank[1][0]=bank[1][1]=bank[1][2]=1;bank[2][0]=1;
    for(unsigned i=0;i<4;++i)bank[3+i][i]=1;bank[20][2]=bank[30][2]=1;bank[33][0]=W;bank[33][1]=H;bank[33][2]=W/2;bank[33][3]=H/2;
    IDirect3DTexture9* worldSource=greySource;
    auto worldPass=[&](const DWORD* code,IDirect3DTexture9* fog){
        IDirect3DPixelShader9* shader=nullptr;ok(device->CreatePixelShader(code,&shader));ok(device->BeginScene());ok(NorthlightHdr::quadState(device,source,W,H,16));
        for(unsigned i=0;i<16;++i)ok(device->SetTexture(i,neutralTexture));
        const unsigned slots[]={0,1,2,8,9,10,12,15};IDirect3DTexture9* values[]={worldSource,depth,frame.texture(),light,fog,neutralTexture,baseline,distance};
        for(unsigned i=0;i<8;++i)ok(device->SetTexture(slots[i],values[i]));
        ok(device->SetPixelShaderConstantF(0,bank[0],68));ok(device->SetPixelShader(shader));ok(NorthlightHdr::drawQuad(device,W,H));ok(device->EndScene());shader->Release();sample(device,source,readback,actual,W/2,H/2);
    };
    worldPass(kWorldCompositeHdrShader,neutralTexture);assert(actual[0]>1.8f&&actual[1]>.9f);++cases;
    worldPass(kWorldUnlitCompositeHdrShader,neutralTexture);assert(std::fabs(actual[0]-.214041f)<.001f);++cases;
    auto* brightFog=solid(device,4,2,1,1);worldPass(kWorldUnlitCompositeHdrShader,brightFog);assert(actual[0]>4&&actual[1]>2);++cases;
    baseline->Release();baseline=solid(device,2,2,2,1);worldSource=spot;settings.legacyExpansion=0;prepare(spot);worldPass(kWorldCompositeHdrShader,neutralTexture);const float unexpandedLit=actual[0];
    settings.legacyExpansion=2;prepare(spot);worldPass(kWorldCompositeHdrShader,neutralTexture);assert(std::fabs(actual[0]-unexpandedLit-3)<.02f);++cases;settings.legacyExpansion=0;worldSource=greySource;
    // Material detail disabled: an already composed FP16 signal is unchanged.
    IDirect3DPixelShader9* material=nullptr;ok(device->CreatePixelShader(kMaterialCompositeHdrShader,&material));
    IDirect3DTexture9* materialInput=solid(device,4,2,.5f,1);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,source,W,H,16));
    ok(device->SetTexture(0,materialInput));ok(device->SetTexture(1,depth));ok(device->SetPixelShaderConstantF(0,bank[0],68));ok(device->SetPixelShader(material));ok(NorthlightHdr::drawQuad(device,W,H));ok(device->EndScene());sample(device,source,readback,actual,W/2,H/2);assert(actual[0]==4&&actual[1]==2);++cases;material->Release();materialInput->Release();
    // Common fixed-function UI shades its original geometry once, without a full-screen layer.
    NorthlightHdr::Color color;
    auto* worldVariant=color.replacement(device,constant,80,0);auto* uiVariant=color.replacement(device,constant,160,0);assert(worldVariant&&uiVariant);
    for(unsigned i=0;i<20;++i){assert(color.replacement(device,constant,80,0)==worldVariant);assert(color.replacement(device,constant,160,0)==uiVariant);}assert(color.patched==2);++cases;
    fill(device,destination,constant,grey);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));
    device->SetPixelShader(nullptr);device->SetTexture(0,white);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    unsigned fixedSubmits=0;ok(color.draw(device,160,[&]{++fixedSubmits;return NorthlightHdr::drawQuad(device,W,H);}));
    assert(fixedSubmits==1&&color.payloadBytes()==0);IDirect3DPixelShader9* restored=nullptr;ok(device->GetPixelShader(&restored));assert(!restored);
    ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);assert(std::fabs(actual[0]-2)<.005f);++cases;
    fill(device,destination,constant,hdr);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));device->SetPixelShader(constant);device->SetPixelShaderConstantF(0,ordinary,1);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_INVDESTCOLOR);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
    ok(color.draw(device,80,[&]{return NorthlightHdr::drawQuad(device,W,H);}));ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);assert(std::fabs(actual[0]-5)<.01f&&actual[1]>2.9f&&color.payloadBytes()==0&&color.layers==0&&color.screenDirect==1);
    DWORD restoredBlend=0;ok(device->GetRenderState(D3DRS_SRCBLEND,&restoredBlend));assert(restoredBlend==D3DBLEND_INVDESTCOLOR);++cases;color.reset();
    // A legacy shader's result still has the original UNORM render-target
    // range, even when replayed directly into a floating-point destination.
    fill(device,destination,constant,black);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));
    device->SetPixelShader(constant);device->SetPixelShaderConstantF(0,hdr,1);
    ok(color.draw(device,80,[&]{return NorthlightHdr::drawQuad(device,W,H);}));ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);
    assert(std::fabs(actual[0]-1)<.005f&&std::fabs(actual[1]-1)<.005f&&std::fabs(actual[2]-.214041f)<.005f);++cases;color.reset();
    for(auto* value:{greySource,depth,light,baseline,neutralTexture,distance,brightFog})value->Release();
    // A real game Reset's resource loss preserves the last adapted exposure.
    settings.autoExposure=true;fill(device,source,constant,grey);finish(1.f/60,true);const float beforeReset=exposure();
    frame.saveAndReset(device);prepare(white);finish(0);assert(std::fabs(exposure()-beforeReset)<.002f);++cases;
    // Presentation writes a compatible SDR image as a second MRT, including under MSAA.
    IDirect3DSurface9 *legacyTarget=nullptr,*legacyRead=nullptr,*multisampled=nullptr;
    ok(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&legacyTarget,nullptr));ok(device->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&legacyRead,nullptr));
    settings.autoExposure=false;settings.exposure=0;settings.bloom=0;fill(device,source,constant,ordinary);
    ok(device->BeginScene());ok(frame.finish(device,source,destination,settings,0,true,false,.18f,nullptr,nullptr,nullptr,nullptr,nullptr,legacyTarget));ok(device->EndScene());
    ok(device->GetRenderTargetData(legacyTarget,legacyRead));D3DLOCKED_RECT bytes{};ok(legacyRead->LockRect(&bytes,nullptr,D3DLOCK_READONLY));assert(*static_cast<DWORD*>(bytes.pBits)==0xffffffff);ok(legacyRead->UnlockRect());++cases;
    legacyTarget->Release();
    ok(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,&legacyTarget,nullptr));ok(device->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,&multisampled,nullptr));
    ok(device->BeginScene());ok(frame.finish(device,source,multisampled,settings,0,true,false,.18f,nullptr,nullptr,nullptr,nullptr,nullptr,legacyTarget));ok(device->EndScene());
    ok(device->StretchRect(multisampled,nullptr,destination,nullptr,D3DTEXF_NONE));sample(device,destination,readback,actual,W/2,H/2);assert(std::fabs(actual[0]*80-203)<.406f);++cases;
    // Match the production sky bootstrap: resolve the current, nonblack
    // legacy framebuffer before replaying an alpha-blended sky layer.
    IDirect3DTexture9* skyCopy=nullptr;IDirect3DSurface9* skyCopySurface=nullptr;
    ok(device->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&skyCopy,nullptr));ok(skyCopy->GetSurfaceLevel(0,&skyCopySurface));
    ok(device->ColorFill(legacyTarget,nullptr,0xff336699));NorthlightHdr::Color skyColor;
    ok(device->BeginScene());ok(skyColor.convertWorld(device,legacyTarget,multisampled,skyCopy,skyCopySurface,W,H,80,80));ok(device->EndScene());
    const float skyLayer[]={4,2,.5f,.25f};
    auto skyDraw=[&](IDirect3DPixelShader9* shader,bool patch){
        ok(device->BeginScene());ok(NorthlightHdr::quadState(device,multisampled,W,H));device->SetPixelShader(shader);device->SetPixelShaderConstantF(0,skyLayer,1);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        if(patch)ok(skyColor.draw(device,80,[&]{return NorthlightHdr::drawQuad(device,W,H);}));else ok(NorthlightHdr::drawQuad(device,W,H));
        ok(device->EndScene());ok(device->StretchRect(multisampled,nullptr,destination,nullptr,D3DTEXF_NONE));sample(device,destination,readback,actual,W/2,H/2);
    };
    if(argc==3){const auto previous=read(argv[2]);std::vector<std::uint32_t> patched;
        assert(NorthlightHdrUi::patch(kHdrCalibration,sizeof kHdrCalibration/4,reinterpret_cast<const std::uint32_t*>(previous.data()),previous.size()/4,80,false,patched));
        IDirect3DPixelShader9* before=nullptr;ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&before));skyDraw(before,false);assert(actual[0]>6);
        std::printf("HDR_LEGACY_COUNTERFACTUAL red=%g UNORM_white=1 overshootReproduced=1\n",actual[0]);before->Release();
        ok(device->BeginScene());ok(skyColor.convertWorld(device,legacyTarget,multisampled,skyCopy,skyCopySurface,W,H,80,80));ok(device->EndScene());
    }
    skyDraw(constant,true);assert(std::fabs(actual[0]-(.25f+.75f*.0331048f))<.002f&&std::fabs(actual[1]-(.25f+.75f*.132868f))<.002f);++cases;
    Image splitDepth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float value=y<H/3?1.f:y<2*H/3?.9985f:.5f;std::memcpy(splitDepth.pixels.data()+(y*W+x)*4,&value,4);}
    auto* split=texture(device,splitDepth);const float skyRed=actual[0];
    ok(device->BeginScene());ok(frame.prepareLegacy(device,white,split,multisampled,W,H,0,.94f,settings));ok(device->EndScene());
    sample(device,frame.surface(),readback,actual,W/2,H/6);assert(std::fabs(actual[0]-skyRed)<.002f);
    // Actual captured horizon depth: beyond .94, but still opaque geometry.
    sample(device,frame.surface(),readback,actual,W/2,H/2);assert(std::fabs(actual[0]-1)<.002f);++cases;
    sample(device,frame.surface(),readback,actual,W/2,3*H/4);assert(std::fabs(actual[0]-1)<.002f);++cases;
    split->Release();skyCopySurface->Release();skyCopy->Release();skyColor.reset();
    // On-demand SDR copies match the former MRT mirror even with MSAA 4.
    fill(device,source,constant,grey);settings.exposure=2;settings.contrast=1.4f;settings.bloom=1;
    ok(device->BeginScene());ok(frame.finish(device,source,multisampled,settings,0,true,false));
    ok(frame.mirrorLegacy(device,legacyTarget));ok(device->EndScene());
    IDirect3DSurface9* legacyResolved=nullptr;ok(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&legacyResolved,nullptr));
    ok(device->StretchRect(legacyTarget,nullptr,legacyResolved,nullptr,D3DTEXF_NONE));ok(device->GetRenderTargetData(legacyResolved,legacyRead));ok(legacyRead->LockRect(&bytes,nullptr,D3DLOCK_READONLY));
    const DWORD mirrored=*static_cast<DWORD*>(bytes.pBits);ok(legacyRead->UnlockRect());assert((mirrored&0xffffff)==0x767676);++cases;
    legacyResolved->Release();
    legacyTarget->Release();legacyRead->Release();multisampled->Release();
    frame.reset();white->Release();spot->Release();constant->Release();source->Release();destination->Release();readback->Release();scalar->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("HDR_FRAME_GPU_PASS cases=%u headroom=1 hue=1 diffuseStable=1 legacyExpansion=1 exposureT90=2/30/60/144fps exposureBounds=1 peakBloomUiIndependent=1 worldFogMaterialHeadroom=1 expansionDoesNotBoostLighting=1 fixedFunction=1\n",cases);
}
