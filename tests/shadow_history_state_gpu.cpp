// Appended to the shared hidden-device fixture by test_shadow_history_state_gpu.py.
#include "shadow_transition.h"
#include "screen_quad.h"
#include <atomic>
static bool memoryPressure=false;
static std::atomic<bool> geometryMemoryDemand{false};
namespace NorthlightGeometryMemory {
struct Budget {};
inline Budget dynamicBudget(uint64_t){return {};}
inline bool admits(bool allowed,Budget){return allowed;}
}
template<class T>static void drop(T*& p){if(p){p->Release();p=nullptr;}}
struct HistoryWorld {
    IDirect3DDevice9* d;
    struct {bool shadows=true;} effects;
    struct {bool softShadows=true;} quality;
    bool geometryAdmission(NorthlightGeometryMemory::Budget){return true;}
    static constexpr UINT ShadowCacheMargin=128,ShadowCacheSize=1280;
    struct Key {bool valid=true;NorthlightWorldMath::ShadowFrame frame;} shadowCacheKey[4];
    IDirect3DTexture9 *shadowCache[4]={},*neutralWhite=nullptr;
    IDirect3DPixelShader9* unionPS=nullptr;
    float sourceMatrices[2][2][16]={};NorthlightGI::Vec3 sourceDirections[2];
    void logf(const char*,...){}
    HRESULT quad(UINT w,UINT h){D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));return NorthlightScreen::drawQuad(d,w,h);}
#include "world_shadow_transition.inl"
};
int main(int argc,char** argv){
    assert(argc==3);GPU gpu(argv[1]);auto* d=gpu.d;const std::string dir=argv[2];
    HistoryWorld world;world.d=d;
    world.neutralWhite=gpu.tex(rgbaImage(1,1,[](unsigned,unsigned){return std::array<float,4>{1,1,1,1};}));
    world.shadowCache[0]=world.neutralWhite;world.unionPS=gpu.shader(dir+"/ShadowUnion.bin");
    world.shadowCacheKey[0].frame=NorthlightWorldMath::shadowFrame({0,0,0},{0,0,1},48);
    world.shadowTransition[0].working=world.shadowCacheKey[0].frame;world.shadowTransition[0].workingValid=true;
    assert(world.shadowTransitionResources(0));
    auto code=read(dir+"/ShadowVS.bin");IDirect3DVertexShader9* vs=nullptr;
    ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vs));
    auto* ps=gpu.shader(dir+"/ShadowPS.bin");
    auto* cutout=gpu.tex(rgbaImage(4,4,[](unsigned x,unsigned y){return std::array<float,4>{1,1,1,float((x+y)%2)};}));
    constexpr unsigned N=64;IDirect3DSurface9 *target=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(N,N,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    struct Caster{float x,y,z,nx,ny,nz,u,v;};
    const Caster vertices[]={{-1,1,.4f,0,0,1,-1,-1},{1,1,.4f,0,0,1,2,-1},{-1,-1,.4f,0,0,1,-1,2},{1,-1,.4f,0,0,1,2,2}};
    std::vector<float> outputs[3];
    for(unsigned mode=0;mode<3;++mode){
        for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(0,state,D3DTADDRESS_WRAP));
        ok(d->BeginScene());
        if(mode)assert(world.preserveShadowStatic(0));
        if(mode==2)for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(0,state,D3DTADDRESS_CLAMP));
        // Same ownership as the following static-cache draw: it sets geometry,
        // shader and material, relying on the preceding WRAP sampler setup.
        ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,N,N,0,1};ok(d->SetViewport(&vp));
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        ok(d->SetVertexShader(vs));ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1));ok(d->SetPixelShader(ps));ok(d->SetTexture(0,cutout));
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,state,D3DTEXF_LINEAR));
        const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},material[]={1,1,1,.5f};
        ok(d->SetVertexShaderConstantF(0,matrix,4));ok(d->SetPixelShaderConstantF(0,material,1));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Caster)));ok(d->EndScene());
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        outputs[mode].resize(N*N);for(unsigned y=0;y<N;++y)std::memcpy(outputs[mode].data()+y*N,static_cast<const char*>(lock.pBits)+y*lock.Pitch,N*4);ok(staging->UnlockRect());
    }
    unsigned changed=0,control=0;for(unsigned i=0;i<N*N;++i){changed+=outputs[0][i]!=outputs[1][i];control+=outputs[0][i]!=outputs[2][i];}
    std::printf("HISTORY_STATE pixels=%u changed_after_history=%u clamp_negative_control=%u\n",N*N,changed,control);std::fflush(stdout);
    assert(changed==0&&control>1000);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);
    world.releaseShadowTransitions();vs->Release();target->Release();staging->Release();
}
