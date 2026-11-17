// Appended to the shared hidden-device fixture by the Python runner.
#include "world_compiled_shaders.h"
#include "fixtures/fog_temporal_no_sky_depth.h"
int main(int argc,char** argv){
    assert(argc==2);GPU gpu(argv[1]);
    IDirect3DPixelShader9 *before=nullptr,*after=nullptr;
    ok(gpu.d->CreatePixelShader(kFogTemporalNoSkyDepth,&before));
    ok(gpu.d->CreatePixelShader(kFogTemporalShader,&after));
    unsigned cases=0,negativeControls=0;
    for(unsigned fullW:{64u,65u})for(unsigned mode=0;mode<10;++mode){
        const unsigned fullH=fullW==64?48:49,W=fullW/2,H=fullH/2,x=W/2,y=H/4;
        Constants c{};c[0]={1.f/fullW,1.f/fullH,.2f,1000};c[1]={1,1,1,0};c[2]={1.f/.94f,48,288,1.f/1024};c[33]={float(fullW),float(fullH),float(W),float(H)};
        for(unsigned i=0;i<4;++i)c[3+i][i]=c[53+i][i]=1;
        c[64][1]=mode==5?0:.85f;
        const bool geometry=mode==3||mode==4||mode==6||mode==7||mode==9;
        if(mode==2||mode==8||mode==9){const float angle=std::atan(.25f/W),a=std::cos(angle),b=std::sin(angle);c[53]={a,0,b,0};c[55]={-b,0,a,0};}
        const float rawDepth=geometry?.94f*(1000.f/999.8f)*(1-.2f/10):1;
        Image depth{fullW,fullH,D3DFMT_R32F,4,std::vector<unsigned char>(fullW*fullH*4)};
        for(unsigned i=0;i<fullW*fullH;++i)std::memcpy(depth.pixels.data()+i*4,&rawDepth,4);
        const auto currentColor=mode==6?std::array<float,4>{-0.f,0.f,.06f,.1f}:std::array<float,4>{.02f,.04f,.06f,.1f};
        const auto oldColor=mode==0||mode==6?currentColor:std::array<float,4>{.03f,.05f,.07f,.2f};
        auto current=rgbaImage(W,H,[&](unsigned px,unsigned py){return px==x&&py==y?currentColor:std::array<float,4>{.18f,.16f,.14f,.9f};});
        auto history=rgbaImage(W,H,[&](unsigned px,unsigned py){
            if((mode==1&&px==x&&py==y)||(mode==2&&px<x))return std::array<float,4>{.16f,.14f,.12f,.9f};
            return oldColor;
        });
        Image previous{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};
        for(unsigned py=0;py<H;++py)for(unsigned px=0;px<W;++px){float z=geometry?10.f:1000.f;
            if(mode==4||mode==7)z=20;
            if(mode==9&&px<x)z=20;
            if((mode==1&&px==x&&py==y)||(mode==2&&px<x))z=10;
            std::memcpy(previous.pixels.data()+(py*W+px)*4,&z,4);}
        gpu.bind(1,gpu.tex(depth));gpu.bind(9,gpu.tex(current));gpu.bind(14,gpu.tex(history));gpu.bind(15,gpu.tex(previous));
        for(unsigned slot:{9u,14u})for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(gpu.d->SetSamplerState(slot,state,D3DTEXF_LINEAR));
        const auto a=gpu.capture(before,c,W,H,0,D3DFMT_A32B32G32R32F),b=gpu.capture(after,c,W,H,0,D3DFMT_A32B32G32R32F);
        const size_t pixel=(y*W+x)*4;
        const bool reject=mode==1||mode==4||mode==5||mode==7;
        for(unsigned k=0;k<4;++k){const float expected=reject?currentColor[k]:currentColor[k]+.85f*(oldColor[k]-currentColor[k]);
            if(std::fabs(b[pixel+k]-expected)>.0001f){std::printf("FOG_ERROR size=%u mode=%u channel=%u old=%g new=%g expected=%g\n",fullW,mode,k,a[pixel+k],b[pixel+k],expected);std::fflush(stdout);}
            assert(std::isfinite(b[pixel+k])&&std::fabs(b[pixel+k]-expected)<.0001f);}
        if(mode==1||mode==2){assert(a[pixel+3]-b[pixel+3]>.05f);++negativeControls;}
        if(mode==6)assert(!std::memcmp(a.data()+pixel,b.data()+pixel,4*sizeof(float)));
        for(unsigned slot:{1u,9u,14u,15u})gpu.bind(slot,nullptr);
        for(auto* t:gpu.owned)t->Release();gpu.owned.clear();++cases;
    }
    before->Release();after->Release();
    std::printf("FOG_DISOCCLUSION_PASS cases=%u negativeControls=%u skyPoint=reject skyBilinear=validatedPoint stableSky=retained geometry=retained weightZero=exact signedZero=retained\n",cases,negativeControls);
}
