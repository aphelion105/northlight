#define main modelNormalOriginalMain
#include "model_normals_gpu.cpp"
#undef main
#include "model_normal_msaa.h"

int main(int argc,char** argv){
    assert(argc==2);std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(library,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));std::printf("ADAPTER %s\n",adapter.Description);
    using NorthlightWaterShaderPatch::dst;using NorthlightWaterShaderPatch::src;
    const DWORD code[]={0xffff0300u,0x02000001u,dst(8,0),src(2,0),65535u};
    IDirect3DPixelShader9* pixel=nullptr;ok(d->CreatePixelShader(code,&pixel));
    unsigned cases=0;
    {
        NorthlightModelNormalMsaa resolve;
        for(unsigned samples:{2u,4u,8u})for(auto dimensions:{std::pair<unsigned,unsigned>{37,23},{256,256}}){
            const unsigned w=dimensions.first,h=dimensions.second;
            D3DSURFACE_DESC description{};description.Width=w;description.Height=h;description.Format=D3DFMT_A8R8G8B8;description.MultiSampleType=D3DMULTISAMPLE_TYPE(samples);
            if(!resolve.ensure(d,description)){std::printf("ENSURE FAILED %s\n",resolve.failureReason());return 1;}
            auto average=target(w,h,D3DFMT_A16B16G16R16F,8);
            for(unsigned frame=0;frame<8;++frame){
                const float first[]={.25f,-.5f,.75f,2.f+frame};const float other[]={-.75f,.25f,-.5f,40.f};
                ok(d->BeginScene());ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,resolve.surface()));
                ok(d->SetPixelShader(pixel));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
                for(auto key:{D3DRS_ZENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(key,FALSE));
                ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));
                D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));
                const Quad vertices[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};
                ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,1));ok(d->SetPixelShaderConstantF(0,first,1));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Quad)));
                ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,~1u));ok(d->SetPixelShaderConstantF(0,other,1));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Quad)));ok(d->EndScene());
                assert(resolve.resolve());
                auto data=capturedPixels(resolve.texture(),w,h);const auto* values=reinterpret_cast<const uint16_t*>(data.data());
                for(size_t i=0;i<size_t(w)*h*4;++i)assert(half(values[i])==first[i%4]);
                // A conventional averaging resolve is an intentionally wrong
                // control; different sample normals/depths must not blend.
                ok(d->StretchRect(resolve.surface(),nullptr,average.s,nullptr,D3DTEXF_NONE));const auto wrong=pixels(average);assert(wrong!=data);
                IDirect3DSurface9* target=nullptr;ok(d->GetRenderTarget(0,&target));assert(target==resolve.surface());target->Release();
                DWORD mask=0;ok(d->GetRenderState(D3DRS_MULTISAMPLEMASK,&mask));assert(mask==~1u);
                ++cases;
            }
            average.read->Release();average.s->Release();average.t->Release();resolve.reset();
        }
    }
    pixel->Release();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
    std::printf("MODEL_NORMAL_MSAA_RESOLVE_PASS cases=%u samples=2/4/8 oddDimensions=1 sampleZero=exact averagingNegativeControl=1 reset=1 state=exact\n",cases);
}
