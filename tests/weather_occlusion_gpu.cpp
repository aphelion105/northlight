// Real production passes against a foreground mushroom silhouette and open sky.
#define NORTHLIGHT_SURFACE_HELPERS_ONLY
#define NORTHLIGHT_TEST_REAL_NAME_MASK
#include "surface_effects_gpu.cpp"
#undef NORTHLIGHT_SURFACE_HELPERS_ONLY
#ifdef NORTHLIGHT_EXTERNAL_ASSETS
#include "resource_shader_checks.h"
#endif

static void cryptCapture(IDirect3DDevice9* d,char** paths){
    NorthlightSurfaceFrame c;const auto constants=read(paths[0]);assert(constants.size()==sizeof c);std::memcpy(&c,constants.data(),sizeof c);
    const auto depth=image(paths[1]),scene=image(paths[2]);assert(depth.width==scene.width&&depth.height==scene.height);
    const auto data=read(paths[3]);auto roof=std::make_shared<NorthlightWeatherShelter::Roof>();assert(data.size()==12+roof->height.size()*4);
    float origin[3];std::memcpy(origin,data.data(),12);roof->x=origin[0];roof->y=origin[1];roof->z=origin[2];std::memcpy(roof->height.data(),data.data()+12,data.size()-12);
    auto* depthTexture=texture(d,depth);
    IDirect3DSurface9 *source=nullptr,*target=nullptr,*stage=nullptr;
    ok(d->CreateOffscreenPlainSurface(scene.width,scene.height,D3DFORMAT(scene.format),D3DPOOL_SYSTEMMEM,&source,nullptr));
    D3DLOCKED_RECT upload{};ok(source->LockRect(&upload,nullptr,0));
    for(unsigned y=0;y<scene.height;++y)std::memcpy(static_cast<char*>(upload.pBits)+y*upload.Pitch,scene.pixels.data()+y*scene.width*scene.bytes,scene.width*scene.bytes);ok(source->UnlockRect());
    ok(d->CreateRenderTarget(scene.width,scene.height,D3DFORMAT(scene.format),D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(scene.width,scene.height,D3DFORMAT(scene.format),D3DPOOL_SYSTEMMEM,&stage,nullptr));
    unsigned changed[2]={};
    {NorthlightSurfaceRenderer surface(d);
    for(unsigned mode=0;mode<2;++mode){
        ok(d->UpdateSurface(source,nullptr,target,nullptr));ok(d->BeginScene());
        surface.precipitation(target,depthTexture,scene.width,scene.height,c,NorthlightQuality::Preset::Quality,nullptr,nullptr,mode?roof:nullptr);
        ok(d->EndScene());ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        const uint32_t header[]={0x31524746,scene.width,scene.height,scene.format,scene.bytes};std::ofstream output(mode?"crypt-after.fgr":"crypt-before.fgr",std::ios::binary);output.write((const char*)header,sizeof header);
        for(unsigned y=0;y<scene.height;++y){const auto* row=static_cast<const char*>(lock.pBits)+y*lock.Pitch;output.write(row,scene.width*scene.bytes);
            for(unsigned x=0;x<scene.width;++x)changed[mode]+=std::memcmp(row+x*scene.bytes,scene.pixels.data()+(y*scene.width+x)*scene.bytes,scene.bytes)!=0;}
        ok(stage->UnlockRect());surface.endFrame();
    }}
    std::printf("CRYPT captured depth/projection/HDR scene: changed pixels before=%u after=%u\n",changed[0],changed[1]);
    assert(changed[0]>100&&changed[1]==0);
    drop(stage);drop(target);drop(source);drop(depthTexture);
}
int main(int argc,char** argv){
    assert(argc>=2);bool allowGame=false;char** capture=nullptr;
    for(int i=2;i<argc;++i){if(std::strcmp(argv[i],"--allow-running-game")==0)allowGame=true;
        else if(std::strcmp(argv[i],"--capture")==0&&i+4<argc){capture=argv+i+1;i+=4;}else assert(false);}
    if(!allowGame)requireNoGame();
#ifdef NORTHLIGHT_EXTERNAL_ASSETS
    std::string resourceError;
    assert(NorthlightAssets::initialize("assets/northlight-assets.bin",resourceError));
#endif
    HWND window=CreateWindowExW(0,L"STATIC",L"Northlight weather occlusion",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
#ifdef NORTHLIGHT_EXTERNAL_ASSETS
    checkResourceShaders(d);
#endif
    const unsigned W=320,H=180;
    IDirect3DSurface9 *target=nullptr,*stage=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));
    IDirect3DSurface9* nameDepth=nullptr;ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&nameDepth,nullptr));
    ok(d->SetDepthStencilSurface(nameDepth));
    // Names reserve 0x80; the unrelated actor/unlit bit 0x40 must not clip rain.
    ok(d->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,0x40));D3DRECT names{0,0,LONG(W/4),LONG(H)};
    ok(d->Clear(1,&names,D3DCLEAR_STENCIL,0,1,0xc0));ok(d->SetDepthStencilSurface(nullptr));
    std::vector<bool> mushroom(W*H);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const float dx=(float(x)-W*.5f)/(W*.28f),dy=(float(y)-H*.34f)/(H*.13f);
        mushroom[y*W+x]=dx*dx+dy*dy<1||(std::abs(float(x)-W*.5f)<W*.045f&&y>H*.34f);
    }
    auto readPixels=[&](){ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> data(W*H);for(unsigned y=0;y<H;++y)std::memcpy(data.data()+y*W,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*4);
        ok(stage->UnlockRect());return data;};
    unsigned exteriorChanges[2]={},foregroundChanges=0;
    {
    NorthlightSurfaceRenderer surface(d);
    for(unsigned snow=0;snow<2;++snow)for(unsigned preset=0;preset<2;++preset)for(unsigned band=0;band<2;++band){
        // WoW partitions world depth (e.g. 0..0.94); both the world band and
        // full-range depth must give exactly the same visibility result.
        const float min=band?.06f:0.f,max=band?.94f:1.f;
        Image sky{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)},mask=sky;
        for(unsigned at=0;at<W*H;++at){
            const float skyDepth=1.f,nearDepth=min+(max-min)*(1000.f/999.9f*(1-.1f/1.5f));
            std::memcpy(sky.pixels.data()+at*4,&skyDepth,4);const float z=mushroom[at]?nearDepth:skyDepth;std::memcpy(mask.pixels.data()+at*4,&z,4);
        }
        auto* skyTexture=texture(d,sky);auto* maskTexture=texture(d,mask);
        NorthlightSurfaceFrame c{};c.c[0][0]=1.f/W;c.c[0][1]=1.f/H;c.c[0][2]=.1f;c.c[0][3]=1000;
        c.c[1][0]=1.3f;c.c[1][1]=1.7f;c.c[1][2]=-1;c.c[2][0]=min;c.c[2][1]=1.f/(max-min);
        c.c[3][0]=1;c.c[3][1]=float(snow);c.c[4][1]=1;c.c[5][2]=1;c.c[6][0]=-1;c.c[7][2]=3;c.c[7][3]=1;
        c.c[8][0]=c.c[8][1]=c.c[8][2]=.7f;
        // The wall may be behind the rain samples. Depth alone cannot shelter
        // those samples from a ceiling above them, including off-screen roofs.
        auto roof=std::make_shared<NorthlightWeatherShelter::Roof>();roof->x=roof->y=-48;
        roof->height.fill(100);
        auto halfRoof=std::make_shared<NorthlightWeatherShelter::Roof>(*roof);
        for(unsigned y=0;y<roof->Size;++y)for(unsigned x=0;x<roof->Size;++x)if(y>=roof->Size/2)halfRoof->height[y*roof->Size+x]=-100;
        for(unsigned phase=0;phase<64;++phase){
            c.c[1][3]=phase*.13f;
            std::vector<DWORD> result[3];
            for(unsigned occluded=0;occluded<3;++occluded){
                ok(d->ColorFill(target,nullptr,0xff143250));ok(d->BeginScene());
                surface.precipitation(target,occluded?maskTexture:skyTexture,W,H,c,preset?NorthlightQuality::Preset::Performance:NorthlightQuality::Preset::Quality,occluded==2?nameDepth:nullptr);
                ok(d->EndScene());result[occluded]=readPixels();surface.endFrame();
            }
            for(unsigned at=0;at<W*H;++at){
                if(at%W<W/4)assert(result[2][at]==0xff143250);
                else assert(result[2][at]==result[1][at]);
                if(mushroom[at]){assert(result[1][at]==0xff143250);foregroundChanges+=result[0][at]!=result[1][at];}
                else{assert(result[0][at]==result[1][at]);exteriorChanges[snow]+=result[1][at]!=0xff143250;}
            }
            for(const auto& shelter:{roof,halfRoof}){
                ok(d->ColorFill(target,nullptr,0xff143250));ok(d->BeginScene());
                surface.precipitation(target,skyTexture,W,H,c,preset?NorthlightQuality::Preset::Performance:NorthlightQuality::Preset::Quality,nullptr,nullptr,shelter);
                ok(d->EndScene());const auto protectedImage=readPixels();surface.endFrame();
                for(unsigned at=0;at<W*H;++at){
                    // World Y follows screen X in this fixture. Exterior must
                    // retain exactly the original drops, not a camera-wide fade.
                    const DWORD expected=shelter==roof||at%W<W/2?0xff143250:result[0][at];
                    if(protectedImage[at]!=expected)std::fprintf(stderr,"ROOF mismatch snow=%u preset=%u phase=%u half=%u x=%u y=%u actual=%08lx expected=%08lx\n",snow,preset,phase,unsigned(shelter==halfRoof),at%W,at/W,(unsigned long)protectedImage[at],(unsigned long)expected);
                    assert(protectedImage[at]==expected);
                }
            }
        }
        if(!band){
            // Following the physical particle velocity must preserve the image
            // better than following horizontally reversed velocity (snow sways).
            unsigned long long forwardError=0,reversedError=0;
            auto render=[&](){
                ok(d->ColorFill(target,nullptr,0xff143250));ok(d->BeginScene());
                surface.precipitation(target,skyTexture,W,H,c,preset?NorthlightQuality::Preset::Performance:NorthlightQuality::Preset::Quality,nullptr);
                ok(d->EndScene());auto image=readPixels();surface.endFrame();return image;
            };
            for(unsigned yaw=0;yaw<4;++yaw)for(unsigned phase=0;phase<4;++phase){
                const float angle=yaw*1.57079632679f,dt=.125f,time=phase*.31f;
                c.c[4][0]=-std::sin(angle);c.c[4][1]=std::cos(angle);
                c.c[6][0]=-std::cos(angle);c.c[6][1]=-std::sin(angle);
                c.c[1][3]=time;c.c[7][0]=phase*.73f;c.c[7][1]=-phase*.19f;c.c[7][2]=3;
                const auto before=render();
                for(int direction:{1,-1}){
                    c.c[1][3]=time+dt;c.c[7][0]=phase*.73f+direction*.5f*dt;
                    c.c[7][1]=-phase*.19f+direction*.2f*dt;c.c[7][2]=3-(snow?1.3f:19.f)*dt;
                    const auto after=render();
                    auto& error=direction>0?forwardError:reversedError;
                    for(unsigned at=0;at<W*H;++at)for(unsigned channel=0;channel<3;++channel)
                        error+=std::abs(int((before[at]>>(channel*8))&255)-int((after[at]>>(channel*8))&255));
                }
            }
            std::printf("WEATHER_DIRECTION snow=%u preset=%u followError=%llu reversedError=%llu\n",snow,preset,forwardError,reversedError);
            assert(reversedError>0&&forwardError<reversedError*.6);
        }
        d->SetTexture(3,nullptr);drop(skyTexture);drop(maskTexture);
    }
    }
    assert(exteriorChanges[0]>0&&exteriorChanges[1]>0&&foregroundChanges>0);
    if(capture)cryptCapture(d,capture);
    std::printf("PASS weather depth: exterior rain=%u snow=%u foregroundOccluded=%u; mushroom canopy+stem, clear sky, both presets, world depth bands, real name stencil and unrelated bits\n",exteriorChanges[0],exteriorChanges[1],foregroundChanges);
    drop(nameDepth);drop(stage);drop(target);drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);
}
