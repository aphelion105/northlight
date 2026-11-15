// Real shader registration followed by the production SDR/HDR compositor.
#define main unused_screen_effects_main
#include "world_screen_effects_gpu.cpp"
#undef main

struct PipelineOffTest {
    static bool prelit(Device& device,const std::vector<DWORD>& words){
        IDirect3DVertexShader9* shader=nullptr;
        screenOk(device.CreateVertexShader(words.data(),&shader));
        const auto entry=device.classifyVs(device.mirrorResources.unwrap(shader)).entry;
        shader->Release();return (entry&Device::kPrelitTag)!=0;
    }
    static void lighting(WorldRenderer& world,bool occluded){
        WorldScreenEffectsTest::fixtureContext(world);world.pipelineActive=true;
        world.effects={false,false,false};world.quality.localLights=1;world.quality.localLightLimit=1;
        world.quality.gi=world.quality.weather=world.quality.contactAO=world.quality.autoBump=0;
        world.quality.equipmentLights=world.quality.nightBrightness=world.quality.horizonHaze=world.quality.fogClouds=0;
        world.quality.shadowStrength=125;world.frames=7;world.gpuDiagnosticCaptures=4;
        world.context.lightDirection[0]=world.context.lightDirection[1]=0;world.context.lightDirection[2]=-1;
        world.sourceDirections[0]=world.rawSourceDirections[0]={0,0,-1};
        const float direct[]={.12f,.45f,.8f};
        for(unsigned c=0;c<3;++c){world.context.direct[c]=direct[c];world.context.ambient[c]=.2f;}
        // The correction under complete directional occlusion is -nativeDirect.
        // Feed those same endpoints without depending on synthetic caster geometry.
        world.sourceColors[0]=world.sourceGlowColors[0]=occluded?NorthlightGI::Vec3{}:NorthlightGI::Vec3{.12f,.45f,.8f};
    }
};
static std::vector<DWORD> readShader(const std::string& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);
    const auto size=f.tellg();assert(size>0&&size%4==0);std::vector<DWORD> words(size_t(size)/4);
    f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),size);assert(f);return words;
}
int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned W=96,H=64;std::setvbuf(stdout,nullptr,_IONBF,0);
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto original=readShader(std::string(argv[3])+"/prelit.bin"),edited=original,lit=readShader(std::string(argv[3])+"/lit.bin");
    edited.insert(edited.end()-1,{0x0001fffeu,0x12345678u}); // Comment: exact native output, new identity.
    const auto identity=[](const auto& w){return NorthlightShaderTags::fnv1a(w.data(),w.size()*4);};
    assert(NorthlightShaderTags::inWmoSignatures(identity(original))&&!NorthlightShaderTags::inWmoSignatures(identity(edited)));
    assert(!NorthlightUnlitReceiver::prelitShader(reinterpret_cast<const uint32_t*>(edited.data()),edited.size(),false)); // Old admission loses the mask.
    bool marked[2];
    {
        NorthlightHdr::Presentation presentation;presentation.logical=pp;presentation.settings.requested=false;
        d->AddRef();auto* device=new Device(d,api,presentation);
        marked[0]=PipelineOffTest::prelit(*device,original);marked[1]=PipelineOffTest::prelit(*device,edited);
        assert(marked[0]&&marked[1]&&!PipelineOffTest::prelit(*device,lit));
        device->Release();
    }
    IDirect3DSurface9 *target=nullptr,*read=nullptr,*mask=nullptr;
    screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&read,nullptr));
    screenOk(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&mask,nullptr));
    const float rgb[]={.4f,.4f,.4f,1},linear[]={.132868f,.132868f,.132868f,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
    auto* scene=screenTexture(d,W,H,rgb);auto* hdrBase=screenTexture(d,W,H,linear);auto* depth=screenTexture(d,W,H,z,1);
    unsigned controls=0;
    for(bool hdr:{false,true}){
        float values[3][2][3]{};
        for(unsigned variant=0;variant<3;++variant)for(unsigned shadow=0;shadow<2;++shadow){
            WorldRenderer world(d);PipelineOffTest::lighting(world,shadow!=0);
            screenOk(d->SetRenderTarget(0,target));screenOk(d->SetDepthStencilSurface(mask));
            D3DVIEWPORT9 viewport{0,0,W,H,0,1};screenOk(d->SetViewport(&viewport));screenOk(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            screenOk(d->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,variant<2&&marked[variant]?NorthlightUnlitReceiver::PrelitBit:0));
            screenOk(d->SetDepthStencilSurface(nullptr));world.setPrelitMask(mask);
            assert(renderChecked(world,d,target,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,nullptr,7,hdr?hdrBase:nullptr));
            screenOk(d->GetRenderTargetData(target,read));D3DLOCKED_RECT lock{};screenOk(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned c=0;c<3;++c){unsigned short bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+(H/2)*lock.Pitch+(W/2)*8+c*2,2);values[variant][shadow][c]=screenHalf(bits);}
            screenOk(read->UnlockRect());
            std::printf("PRELIT_PIXEL hdr=%u variant=%u shadow=%u rgb=%.6f,%.6f,%.6f\n",unsigned(hdr),variant,shadow,values[variant][shadow][0],values[variant][shadow][1],values[variant][shadow][2]);
        }
        const float expected=hdr?linear[0]:rgb[0];
        for(unsigned variant=0;variant<2;++variant)for(unsigned shadow=0;shadow<2;++shadow)for(unsigned c=0;c<3;++c){
            assert(values[variant][shadow][c]==values[0][0][c]); // Same compositor dither as the unoccluded reference.
            assert(std::fabs(values[variant][shadow][c]-expected)<1.f/255);
        }
        const auto* bad=values[2][1];assert(bad[0]>bad[2]+expected*.05f&&bad[2]<expected*.7f);++controls;
        std::printf("PRELIT_COLOR hdr=%u preserved=%.6f oldMaskLoss=%.6f,%.6f,%.6f\n",unsigned(hdr),values[1][1][0],bad[0],bad[1],bad[2]);
    }
    for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);
    scene->Release();hdrBase->Release();depth->Release();mask->Release();target->Release();read->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("PRELIT_RECEIVER_GPU_PASS actualRegistration=1 unchangedNativeShader=1 SDR=1 HDR=1 outdoorRejected=1 negativeControls=%u\n",controls);
}
