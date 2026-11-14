// Compare shipped/current fused WorldComposite on the same complete HDR graph.
#define NORTHLIGHT_HDR_PIPELINE_LIBRARY
#include "hdr_pipeline_gpu.cpp"

int main(int argc,char** argv){
    assert(argc==6);const bool capture=std::strcmp(argv[4],"-")!=0;
    const bool control=std::strcmp(argv[5],"control")==0;
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE library=LoadLibraryA(argv[1]);assert(library);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {
        DeviceMeter meter(raw);
        IDirect3DPixelShader9* variants[2]={};
        for(unsigned i=0;i<2;++i){auto code=read(argv[2+i]);ok(raw->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&variants[i]));}
        for(unsigned test=0;test<(capture?6u:5u);++test){
            const bool captured=test==5;
            const char* names[]={"authored-1080","authored-4k","authored-4k-msaa2","expansion2-1080-msaa4","odd-sky-names","capture-4k-msaa2"};const char* name=names[test];
            const unsigned w=test==4?129:test==0||test==3?1920:3840,h=test==4?97:test==0||test==3?1080:2160;
            // Construct the optional shader, then force the same expanded base
            // on both paths. The loader's backend policy is tested separately.
            Scenario scenario{name,w,h,test==3?4u:test==2||captured?2u:0u,32,16,true,test==4,false,test==4,true,true,false};
            Graph graph(&meter,scenario,captured?argv[4]:"");
            graph.eligible=false;graph.settings.legacyExpansion=test==3?2.f:.5f;graph.settings.wideBloom=true;
            if(captured){
                const std::string prefix=argv[4];const auto constants=constantBank(read(prefix+"constants.f32"));
                // Preserve captured projection, pose and material depth. A
                // constant authored height would make captured detail inert.
                std::memcpy(graph.bank,constants.values,7*4*sizeof(float));
                auto replace=[&](IDirect3DTexture9*& target,const char* suffix){target->Release();target=texture(&meter,image(prefix+suffix));};
                replace(graph.height,"material-height.fgr");replace(graph.normal,"normals.fgr");replace(graph.distance,"distance.fgr");
            }
            assert(graph.combined);
            auto* combined=graph.combined;
            auto draw=[&](bool merge,bool oracle=false){graph.combined=variants[merge?1:0];graph.frame(2,oracle);graph.combined=combined;};
            draw(false,true);const auto before=graph.pixels();draw(true,true);const auto after=graph.pixels();
            const float gain=graph.bank[73][0];graph.bank[73][0]=0;draw(false,true);const auto without=graph.pixels();graph.bank[73][0]=gain;
            double error=0,contribution=0;unsigned alpha=0,nonfinite=0,outside=0,changed=0;
            for(std::size_t i=0;i<before.size();++i){
                const double a=half(before[i]),b=half(after[i]),delta=std::fabs(a-b);
                changed+=before[i]!=after[i];error=std::max(error,delta);nonfinite+=!std::isfinite(delta);
                if(i%4==3)alpha+=before[i]!=after[i];
                else {outside+=delta>.003+.002*std::fabs(a);contribution=std::max(contribution,std::fabs(a-half(without[i])));}
            }
            draw(false);meter.calls={};draw(false);const auto oldCalls=meter.calls;
            draw(true);meter.calls={};draw(true);const auto newCalls=meter.calls;
            assert(!oldCalls.allocations&&!newCalls.allocations&&oldCalls.draws==newCalls.draws&&oldCalls.resolves==newCalls.resolves);
            std::printf("{\"event\":\"image\",\"case\":\"%s\",\"max_abs\":%.9g,\"alpha_changes\":%u,\"nonfinite\":%u,\"outside_tolerance\":%u,\"material_control\":%.9g,\"draws_before\":%u,\"draws_after\":%u,\"resolves\":%u}\n",name,error,alpha,nonfinite,outside,contribution,oldCalls.draws,newCalls.draws,oldCalls.resolves);std::fflush(stdout);
            assert(!alpha&&!nonfinite&&!outside&&contribution>.001);
            std::printf("COMPOSITE_INPUTS_IMAGE case=%s changed=%u maxAbs=%.9g\n",name,changed,error);std::fflush(stdout);
            auto preparePass=[&](bool candidate){
                graph.quad(graph.output.surface,variants[candidate?1:0],graph.legacy.texture);
                for(unsigned s=1;s<16;++s)ok(meter.SetTexture(s,graph.black));
                ok(meter.SetTexture(1,graph.depth));ok(meter.SetTexture(2,graph.hdr.texture()));
                ok(meter.SetTexture(4,graph.fog));ok(meter.SetTexture(5,graph.legacy.texture));ok(meter.SetTexture(6,graph.white));
                ok(meter.SetTexture(8,graph.lighting));ok(meter.SetTexture(9,graph.height));ok(meter.SetTexture(10,graph.white));
                ok(meter.SetTexture(12,graph.baseline));ok(meter.SetTexture(14,graph.normal));ok(meter.SetTexture(15,graph.distance));
                ok(meter.SetPixelShaderConstantF(0,graph.bank[0],79));graph.maskBind();
            };
            if(std::getenv("NORTHLIGHT_BENCHMARK")){
            for(unsigned block=0;block<10;++block)for(unsigned step=0;step<4;++step){
                const bool candidate=step==1||step==2;
                preparePass(!control&&candidate);
                const auto t=timeGraph(raw,[&]{ok(meter.BeginScene());for(unsigned n=0;n<32;++n)ok(NorthlightHdr::drawQuad(&meter,w,h));ok(meter.EndScene());},8);
                std::printf("{\"event\":\"pass_timing\",\"case\":\"%s\",\"block\":%u,\"step\":%u,\"merged\":%s,\"gpu_ms\":%.9f,\"cpu_ms\":%.9f}\n",name,block,step,candidate?"true":"false",t.gpu/32,t.cpu/32);std::fflush(stdout);
            }
            // ABBA blocks; timings include identical native input, HDR tail,
            // particles and UI. Never compare against Graph's legacy mode 1.
            for(unsigned block=0;block<10;++block)for(unsigned step=0;step<4;++step){
                const bool merge=step==1||step==2;
                const auto t=timeGraph(raw,[&]{draw(control?false:merge);},16);
                std::printf("{\"event\":\"timing\",\"case\":\"%s\",\"block\":%u,\"step\":%u,\"merged\":%s,\"gpu_ms\":%.9f,\"cpu_ms\":%.9f}\n",name,block,step,merge?"true":"false",t.gpu,t.cpu);std::fflush(stdout);
            }
            }
        }
        variants[0]->Release();variants[1]->Release();
    }
    raw->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
    std::puts("WORLD_COMPOSITE_INPUTS_PASS");
}
