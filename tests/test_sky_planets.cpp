#include <cassert>
#include <cstdio>
#include <algorithm>
#include "sky_planet_geometry.h"
#include "celestial_context.h"

int main(){
    using namespace NorthlightSkyPlanet;
    struct Vertex {float normal[3],position[3],uv[2];unsigned color;};
    unsigned cases=0,rings=0;
    const float factor=NorthlightForeverSky::planetScale();
    assert(std::fabs(factor-.46176318f)<1e-7f);
    for(const auto& m:NorthlightSkyComposition::PlanetMeshes){
        Vertex vertices[8]{},before[8]{};
        for(unsigned i=0;i<m.count;++i){
            auto& v=vertices[i];std::copy_n(m.positions[m.count-1-i],3,v.position);
            v.normal[0]=.3f;v.normal[1]=.4f;v.normal[2]=.5f;v.uv[0]=float(i%2);v.uv[1]=float(i/2);v.color=0x537abecd;
        }
        std::memcpy(before,vertices,sizeof vertices);
        assert(scale(reinterpret_cast<unsigned char*>(vertices),m.count,sizeof(Vertex),12,factor));
        for(unsigned i=0;i<m.count;++i){
            const auto& v=vertices[i];const auto& b=before[i];
            assert(!std::memcmp(v.normal,b.normal,12)&&!std::memcmp(v.uv,b.uv,8)&&v.color==b.color);
            for(unsigned c=0;c<3;++c)assert(std::fabs((v.position[c]-m.anchor[c])-(b.position[c]-m.anchor[c])*factor)<.0001f);
        }
        if(m.count==8){
            ++rings;bool parent=false;
            for(const auto& p:NorthlightSkyComposition::PlanetMeshes)if(p.count==4){
                float error=0;for(unsigned c=0;c<3;++c)error+=std::fabs(p.anchor[c]-m.anchor[c]);
                parent|=error<.0001f;
            }
            assert(parent); // ring and body use one center, including the offset ring halves
        }
        // A second attempt to scale the result must fail; no cumulative shrink.
        assert(!scale(reinterpret_cast<unsigned char*>(vertices),m.count,sizeof(Vertex),12,factor));
        before[0].position[0]+=1;
        assert(!scale(reinterpret_cast<unsigned char*>(before),m.count,sizeof(Vertex),12,factor));
        ++cases;
    }
    assert(cases>=15&&rings>=2);
    NorthlightCelestial::Context c;float direct[]={1,.8f,.6f};
    for(unsigned hour=0;hour<24;++hour)for(bool forever:{false,true}){
        c.dayFraction=hour/24.f;
        c.sun.direction[2]=c.moon.direction[2]=1;c.sun.nativeAngularRadius=.12f;c.moon.nativeAngularRadius=.15f;
        for(const char* map:{"Expansion01","Northrend","Azeroth","Expansion01","Northrend"}){
            NorthlightCelestial::resolveRendererSky(c,direct,forever,map);
            assert(c.sun.angularRadius==(forever?NorthlightForeverSky::sunRadius(c.dayFraction):.12f));
            assert(c.moon.angularRadius==(forever?NorthlightForeverSky::moonRadius(c.dayFraction):.15f));
            const bool outland=std::strcmp(map,"Expansion01")==0;
            assert(c.lunarBody!=outland&&c.moon.alpha==(outland?0.f:1.f));
            // The smoothed light path retains night illumination without
            // resurrecting an ordinary moon disc in Outland.
            NorthlightCelestial::applyRendererPolicy(c,direct);
            if(outland){
                assert(c.moon.alpha==0);auto visible=c;visible.lunarBody=true;
                NorthlightCelestial::applyRendererPolicy(visible,direct);
                assert(visible.moonWeight==c.moonWeight);
                assert(!std::memcmp(visible.moonColor,c.moonColor,sizeof c.moonColor));
            }
        }
    }
    std::printf("PASS %u authored planet meshes (%u rings), anchor/attribute preservation, no accumulation and map/style transitions\n",cases,rings);
}
