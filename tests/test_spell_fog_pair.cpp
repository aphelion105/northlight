#include "spell_fog_march.h"
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <random>

using namespace NorthlightSpellFog;

int main(){
    using namespace NorthlightRegionalFog;
    {
        Field moving;moving.originX=moving.originY=-256;
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x)moving.texels[y*N+x]={float(x)*.25f,.02f,0,5};
        March medium;medium.volume[2]=medium.volume[3]=48;medium.airFloor=.0017f;
        // A moving emitter crosses a metadata-cell boundary on sloping ground.
        // The shared lamp/veil medium must interpolate through it continuously.
        FieldSample sample;const float boundary=moving.originX+32*Spacing;
        const float a=sigma(moving,medium,boundary-.001f,0,10,sample);
        const float b=sigma(moving,medium,boundary+.001f,0,10,sample);
        assert(a>0&&b>0&&std::abs(a-b)<1e-6f);
    }
    Field field;field.originX=field.originY=-256;
    for(unsigned i=0;i<N*N;++i)field.texels[i]={float(int(i%23)-10),.02f,.01f,i%7?5.f:0.f};
    March march;march.night=.5f;march.volume[0]=.02f;march.volume[1]=.01f;march.volume[2]=60;march.volume[3]=30;
    float from[3]={0,0,4};
    auto volume=NorthlightFogClouds::generate(42);
    auto quantiles=NorthlightFogClouds::quantiles(volume.data());
    NorthlightFogClouds::Wind wind;
    auto clouds=NorthlightFogClouds::derive(1,100,.5,.5,wind,from,&quantiles);
    auto extra=[&](float x,float y,float z,const FieldSample& sample){
        const float point[]={x,y,z};
        return NorthlightFogClouds::sigmaAt(volume.data(),clouds,from,point,sample.ground,sample.height,sample.coverage);
    };
    auto none=[](float,float,float,const FieldSample&){return 0.f;};
    std::mt19937 rng(104);
    for(int i=0;i<10000;++i){
        float to[]={float(int(rng()%500)-250),float(int(rng()%500)-250),float(int(rng()%100)-50)};
        const unsigned steps=rng()%60;
        const float clear=opticalDepth(field,march,from,to,steps,none);
        const float cloudy=opticalDepth(field,march,from,to,steps,extra);
        float pairedClear,pairedCloudy;
        opticalDepthPair(field,march,from,to,steps,extra,pairedClear,pairedCloudy);
        assert(!std::memcmp(&clear,&pairedClear,4)&&!std::memcmp(&cloudy,&pairedCloudy,4));
    }
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!cloudRayAboveSupport({0,nan,1},12,200,200));
    assert(!cloudRayAboveSupport({0,50,0},12,200,200));
    assert(!cloudRayAboveSupport({0,50,1},12,200,100));
    assert(cloudRayAboveSupport({0,50,1},12,200,200));
    const auto bounds=globalHeightBounds(field);
    from[2]=200;const float highTo[]={40,30,190};
    assert(cloudRayAboveSupport(bounds,clouds.height,from[2],highTo[2]));
    float clear,cloudy;opticalDepthPair(field,march,from,highTo,24,extra,clear,cloudy);
    assert(!std::memcmp(&clear,&cloudy,4));

    NorthlightForever::Frame imported;
    imported.valid=true;
    for(unsigned i=0;i<3;++i){
        auto& layer=imported.layer[i];
        layer.flag[1]=layer.flag[3]=1;
        layer.p[0]=float(i)*3;layer.p[1]=2+float(i);layer.p[3]=25;layer.p[5]=40;
        layer.p[6]=15;layer.p[7]=0;layer.p[18]=.2f;layer.p[19]=1.1f;
    }
    unsigned nonzero=0;
    for(unsigned i=0;i<2400;++i){
        const float reach=float(6+rng()%123);
        const float eye[]={float(int(rng()%512)-256),float(int(rng()%512)-256),float(int(rng()%62)-12)};
        const float to[]={eye[0]+reach*.8f,eye[1]-reach*.4f,eye[2]+float(int(rng()%38)-19)};
        // Freeze the original production imported loop and its operation order.
        float expected=0;
        for(unsigned j=0;j<48;++j){
            const float aa=reach*float(j*j)/2304.f,bb=reach*float((j+1)*(j+1))/2304.f,t=(aa+bb)*.5f;
            const float px=eye[0]+(to[0]-eye[0])*t/reach,py=eye[1]+(to[1]-eye[1])*t/reach,pz=eye[2]+(to[2]-eye[2])*t/reach;
            const auto fs=fieldAt(field,px,py);
            if(fs.coverage>0&&pz>=fs.ground)expected+=NorthlightForever::sigma(imported,t,pz,fs.ground)*fs.coverage*(bb-aa);
        }
        const float actual=importedOpticalDepth(field,imported,eye,to,reach);
        assert(!std::memcmp(&expected,&actual,sizeof actual));
        nonzero+=actual>0;
    }
    assert(nonzero>0&&nonzero<2400);
}
