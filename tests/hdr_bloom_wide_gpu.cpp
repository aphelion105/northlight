// Compare paired extraction with the original wide filter and compact bloom. No game
// assets, performance timings or changes to the game/client configuration.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_bloom_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_compiled_shaders.h"
#include "hdr_quad.h"

int main(int argc,char** argv){
    assert(argc==2);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,256,256,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=256;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shader[6]{};const std::uint32_t* code[]={kHdrBloomHorizontal,kHdrBloomVertical,kHdrBloomWideHorizontal,kHdrBloomWideVertical,kHdrCopy,kHdrBloomWideResolve};
    for(unsigned i=0;i<6;++i)ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code[i]),&shader[i]));
    D3DCAPS9 pairCaps{};ok(d->GetDeviceCaps(&pairCaps));
    IDirect3DPixelShader9* pairShaders[2]{};if(pairCaps.NumSimultaneousRTs>=2&&(pairCaps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)){ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrBloomWidePairHorizontal),pairShaders));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrBloomWidePairVertical),pairShaders+1));}
    float pairError=0;double pairEnergyError=0;
    unsigned cases=0,compactMisses=0;double minEnergy=1e9,maxEnergy=0,maxConstantError=0;unsigned wideRadius=0,compactRadius=0;double minIsotropy=10,maxIsotropy=0,maxCentroidError=0;
    for(unsigned size:{128u,257u}){
        const unsigned w=size,h=size==128?128:129;
        IDirect3DTexture9* rt[4]{};IDirect3DSurface9* surface[4]{},*staging=nullptr;
        ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        auto referenceRun=[&](const std::vector<float>& input,bool wide,bool halo=true){
            Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(input.size()*4)};std::memcpy(im.pixels.data(),input.data(),im.pixels.size());auto* source=texture(d,im);
            unsigned bw=wide?std::max(1u,(w+15)/16):w/2,bh=wide?std::max(1u,(h+15)/16):h/2;
            unsigned count=wide&&halo?4:3;
            unsigned widths[]={bw,bw,count==4?bw:w,w},heights[]={wide?h:bh,bh,count==4?bh:h,h};
            for(unsigned i=0;i<count;++i){ok(d->CreateTexture(widths[i],heights[i],1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&rt[i],nullptr));ok(rt[i]->GetSurfaceLevel(0,&surface[i]));}
            ok(d->BeginScene());
            for(unsigned i=0;i<count;++i){
                ok(NorthlightHdr::quadState(d,surface[i],widths[i],heights[i]));ok(d->SetTexture(0,i?rt[i-1]:source));ok(d->SetPixelShader(shader[i==count-1?4:i==2?5:(wide?2:0)+i]));
                float p[]={wide?1.f/w:2.f/w,wide?1.f/h:1.f/bh,float(w)/bw,float(h)/bh};if(i==2&&count==4){p[0]=1.f/bw;p[1]=1.f/bh;}ok(d->SetPixelShaderConstantF(2,p,1));
                if(i&&(!wide||i>=2)){ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));}
                ok(NorthlightHdr::drawQuad(d,widths[i],heights[i]));
            }
            ok(d->EndScene());ok(d->GetRenderTargetData(surface[count-1],staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            std::vector<float> result(w*h*4);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){std::uint16_t bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*2,2);result[y*w*4+x]=half(bits);assert(std::isfinite(result[y*w*4+x]));}
            ok(staging->UnlockRect());d->SetTexture(0,nullptr);source->Release();for(unsigned i=0;i<count;++i){surface[i]->Release();rt[i]->Release();}return result;
        };
        auto run=[&](const std::vector<float>& input,bool wide,bool halo=true){
            auto reference=referenceRun(input,wide,halo);if(!wide||!pairShaders[0])return reference;
            Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(input.size()*4)};std::memcpy(im.pixels.data(),input.data(),im.pixels.size());auto* source=texture(d,im);
            const unsigned bw=(w+15)/16,bh=(h+15)/16,pw=(bw+1)/2,count=halo?5:4,last=count-1;
            const unsigned widths[]={pw,pw,bw,halo?bw:w,w},heights[]={h,h,bh,halo?bh:h,h};
            IDirect3DTexture9* textures[5]{};IDirect3DSurface9* targets[5]{};
            for(unsigned i=0;i<count;++i){ok(d->CreateTexture(widths[i],heights[i],1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,textures+i,nullptr));ok(textures[i]->GetSurfaceLevel(0,targets+i));}
            const float params[]={1.f/w,1.f/h,float(w)/bw,float(h)/bh},vertical[]={1.f/w,1.f/h,float(pw),float(h)/bh};
            ok(d->BeginScene());ok(NorthlightHdr::quadState(d,targets[0],pw,h,3));ok(d->SetRenderTarget(1,targets[1]));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));ok(d->SetTexture(0,source));ok(d->SetPixelShader(pairShaders[0]));ok(d->SetPixelShaderConstantF(2,params,1));ok(NorthlightHdr::drawQuad(d,pw,h));ok(d->SetRenderTarget(1,nullptr));
            ok(NorthlightHdr::quadState(d,targets[2],bw,bh,3));ok(d->SetTexture(0,textures[0]));ok(d->SetTexture(1,textures[1]));ok(d->SetPixelShader(pairShaders[1]));ok(d->SetPixelShaderConstantF(2,vertical,1));ok(NorthlightHdr::drawQuad(d,bw,bh));d->SetTexture(1,nullptr);
            if(halo){const float hp[]={1.f/bw,1.f/bh,0,0};ok(NorthlightHdr::quadState(d,targets[3],bw,bh,3));ok(d->SetTexture(0,textures[2]));ok(d->SetPixelShader(shader[5]));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));ok(d->SetPixelShaderConstantF(2,hp,1));ok(NorthlightHdr::drawQuad(d,bw,bh));}
            ok(NorthlightHdr::quadState(d,targets[last],w,h,3));ok(d->SetTexture(0,textures[last-1]));ok(d->SetPixelShader(shader[4]));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));ok(NorthlightHdr::drawQuad(d,w,h));ok(d->EndScene());ok(d->GetRenderTargetData(targets[last],staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(w*h*4);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){std::uint16_t bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*2,2);result[y*w*4+x]=half(bits);assert(std::isfinite(result[y*w*4+x]));}ok(staging->UnlockRect());
            double oldEnergy=0,newEnergy=0;for(unsigned i=0;i<result.size();++i){pairError=std::max(pairError,std::fabs(result[i]-reference[i]));if(i%4<3){oldEnergy+=reference[i];newEnergy+=result[i];}}if(oldEnergy)pairEnergyError=std::max(pairEnergyError,std::fabs(newEnergy/oldEnergy-1));
            d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);source->Release();for(unsigned i=0;i<count;++i){targets[i]->Release();textures[i]->Release();}return result;
        };
        auto shape=[&](const std::vector<float>& pixels){
                    double total=0,cx=0,cy=0;
                    for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx){double v=pixels[(yy*w+xx)*4];total+=v;cx+=xx*v;cy+=yy*v;}cx/=total;cy/=total;
                    double fourth=0,mixed=0;
                    for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx){double v=pixels[(yy*w+xx)*4],dx=xx-cx,dy=yy-cy;fourth+=(dx*dx*dx*dx+dy*dy*dy*dy)*v;mixed+=dx*dx*dy*dy*v;}
                    return std::array<double,3>{cx,cy,fourth/(6*mixed)};
                };
        // Every phase of a one-pixel source, including odd target dimensions.
        for(unsigned phase=0;phase<256;++phase){
            unsigned x=w/2+(phase%16)-8,y=h/2+(phase/16)-8;
            std::vector<float> input(w*h*4,0);for(unsigned i=0;i<w*h;++i)input[i*4+3]=1;
            input[(y*w+x)*4]=8;input[(y*w+x)*4+1]=4;input[(y*w+x)*4+2]=2;
            auto wide=run(input,true);double energy=0;for(unsigned i=0;i<w*h;++i){energy+=wide[i*4];assert(std::fabs(wide[i*4+1]-.5f*wide[i*4])<.00002f);}
            assert(energy>0);minEnergy=std::min(minEnergy,energy/7);maxEnergy=std::max(maxEnergy,energy/7);
            // Exact integer-grid tent conserves energy; odd dimensions retain
            // every impulse but expose the fixed-kernel decimation error.
            if(size==128)assert(std::fabs(energy/7-1)<.005);
            assert(energy/7>.98&&energy/7<1.02);
            auto phaseShape=shape(wide);minIsotropy=std::min(minIsotropy,phaseShape[2]);maxIsotropy=std::max(maxIsotropy,phaseShape[2]);
            double centroidError=std::hypot(phaseShape[0]-x,phaseShape[1]-y);maxCentroidError=std::max(maxCentroidError,centroidError);
            assert(std::fabs(phaseShape[2]-1)<.12);assert(centroidError<.15);
            if(size==257&&phase==136){
                auto before=run(input,true,false);
                auto oldShape=shape(before),newShape=shape(wide);
                assert(std::hypot(newShape[0]-x,newShape[1]-y)<.03);
                assert(std::fabs(newShape[2]-1)<std::fabs(oldShape[2]-1));
                std::printf("HDR_BLOOM_ISOTROPY before=%g after=%g centroid=(%g,%g) source=(%u,%u)\n",oldShape[2],newShape[2],newShape[0],newShape[1],x,y);
            }
            if(phase==17){auto compact=run(input,false);for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx){
                unsigned radius=unsigned(std::max(std::abs(int(xx)-int(x)),std::abs(int(yy)-int(y))));
                if(wide[(yy*w+xx)*4]>.00001f)wideRadius=std::max(wideRadius,radius);
                if(compact[(yy*w+xx)*4]>.00001f)compactRadius=std::max(compactRadius,radius);
            }}
            if(size==128){auto compact=run(input,false);double sum=0;for(unsigned i=0;i<w*h;++i)sum+=compact[i*4];compactMisses+=sum==0;}
            ++cases;
        }
        // Constant fields, subthreshold backgrounds and completely occluded
        // sources must not acquire radiance. Negative source channels clamp.
        for(float value:{-1.f,0.f,.9f,1.f,1.01f,8.f}){
            std::vector<float> input(w*h*4,value);auto result=run(input,true);float expected=std::max(value-1,0.f);
            for(unsigned i=0;i<w*h;++i){double error=std::fabs(result[i*4]-expected);maxConstantError=std::max(maxConstantError,error);assert(error<.012);assert(result[i*4+3]==1);}
            ++cases;
        }
        // Visible half-disc behind an opaque silhouette: the filter may spill
        // across the edge, but it must not invent energy from the hidden half.
        std::vector<float> disc(w*h*4,0);double sourceEnergy=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            float radius=std::hypot(float(x)-w/2.f,float(y)-h/2.f);
            float value=x>=w/2&&radius<9?8.f:0.f;
            for(unsigned c=0;c<3;++c)disc[(y*w+x)*4+c]=value;disc[(y*w+x)*4+3]=1;
            sourceEnergy+=std::max(value-1,0.f);
        }
        auto halo=run(disc,true);double haloEnergy=0;for(unsigned i=0;i<w*h;++i)haloEnergy+=halo[i*4];
        assert(std::fabs(haloEnergy/sourceEnergy-1)<.01);
        assert(halo[((h/2)*w+w/2-8)*4]>0);
        ++cases;
        if(size==257){
            // A lavender disc and tiny warm emitter on a subthreshold field.
            // Export actual GPU results for core/halo and hue inspection.
            std::vector<float> scene(w*h*4,0);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                float* c=&scene[(y*w+x)*4];c[0]=.025f;c[1]=.018f;c[2]=.03f;c[3]=1;
                if(std::hypot(float(x)-72,float(y)-64)<12){c[0]=4;c[1]=3;c[2]=5;}
                if(std::abs(int(x)-180)<=1&&std::abs(int(y)-64)<=2){c[0]=12;c[1]=3;c[2]=.3f;}
            }
            auto before=run(scene,true,false),after=run(scene,true);
            double oldEnergy=0,newEnergy=0;unsigned newHaloPixels=0;
            for(unsigned i=0;i<w*h;++i){oldEnergy+=before[i*4];newEnergy+=after[i*4];
                if(before[i*4]<.00001f&&after[i*4]>.0001f)++newHaloPixels;}
            assert(std::fabs(newEnergy/oldEnergy-1)<.003);assert(newHaloPixels>100);
            unsigned core=(64*w+72)*4;assert(after[core]>.25f*before[core]);assert(after[core]<=before[core]);
            for(auto item:{std::make_pair("scene",&scene),std::make_pair("before",&before),std::make_pair("after",&after)}){
                std::string path=std::string("bloom-")+item.first+".f32";auto* file=std::fopen(path.c_str(),"wb");assert(file);
                assert(std::fwrite(item.second->data(),sizeof(float),item.second->size(),file)==item.second->size());std::fclose(file);
            }
            std::printf("HDR_BLOOM_HALO_IMAGE width=%u height=%u energyRatio=%g coreRatio=%g newHaloPixels=%u\n",w,h,newEnergy/oldEnergy,after[core]/before[core],newHaloPixels);++cases;
        }
        // Clamp addressing at all four corners stays finite and nonnegative.
        for(unsigned corner=0;corner<4;++corner){
            std::vector<float> input(w*h*4,0);unsigned x=corner&1?w-1:0,y=corner&2?h-1:0;
            input[(y*w+x)*4]=8;auto edge=run(input,true);double sum=0;
            for(unsigned i=0;i<w*h;++i){assert(edge[i*4]>=0&&edge[i*4]<=7.01f);sum+=edge[i*4];}
            assert(sum>0);++cases;
        }
        staging->Release();
    }
    assert(wideRadius>compactRadius*2);
    std::printf("HDR_BLOOM_PHASE_SHAPE cases=512 isotropyMin=%g isotropyMax=%g centroidErrorMax=%g\n",minIsotropy,maxIsotropy,maxCentroidError);
    assert(pairError<.01f&&pairEnergyError<.0005);
    std::printf("HDR_BLOOM_PAIR_REFERENCE maxError=%g energyError=%g timings=none\n",pairError,pairEnergyError);
    std::printf("HDR_BLOOM_WIDE_PASS cases=%u phaseEnergyMin=%g phaseEnergyMax=%g constantError=%g compactMissingImpulses=%u wideRadius=%u compactRadius=%u timings=not_measured\n",cases,minEnergy,maxEnergy,maxConstantError,compactMisses,wideRadius,compactRadius);
    d->SetPixelShader(nullptr);for(auto* s:pairShaders)if(s)s->Release();for(auto* s:shader)s->Release();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
