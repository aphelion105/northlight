// 0.3.211 (task 31): the moon's cascades are gated by its colour (world_renderer.h sourceActive), lighting by
// its weight. The two agree because the renderer colour is direct*weight: a zero weight is an exactly zero
// colour, so no moon cascade, actor map or water moon map is drawn on a frame whose moon weight is zero.
#include "celestial_profiles.h"
#include "celestial_sources.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightCelestial;
static bool active(const NorthlightGI::Vec3& c){return NorthlightGI::dot(c,c)>1e-10f;} /* the renderer's sourceActive test */
int main(){
    const float directs[][3]={{1,1,1},{.6f,.5f,.4f},{40,30,20},{1e-3f,0,2e-3f}};
    NorthlightCelestialProfiles::Profile profiles[3];
    profiles[1].mix[0]=profiles[1].mix[1]=1;profiles[1].light[1][0]=3;profiles[1].strength[1]=2.5f;
    profiles[2].mix[1]=.5f;profiles[2].strength[0]=0;profiles[2].strength[1]=8;profiles[2].light[1][2]=.1f;
    unsigned cases=0,zeroWeight=0;
    for(const auto* direct:directs)for(const auto& profile:profiles)
    for(int sz=-20;sz<=20;++sz)for(int mz=-20;mz<=20;++mz){
        Context c;c.sun.direction[0]=1;c.moon.direction[0]=-1;
        c.sun.direction[2]=sz*.01f;c.moon.direction[2]=mz*.01f; /* through both .08 visibility ramps */
        applyRendererPolicy(c,direct);NorthlightCelestialProfiles::apply(profile,direct,c);
        const NorthlightGI::Vec3 d{direct[0],direct[1],direct[2]};
        auto r=NorthlightCelestialSources::resolve(d,d,NorthlightGI::Vec3{c.sun.direction[0],c.sun.direction[1],c.sun.direction[2]},
            NorthlightGI::Vec3{c.moon.direction[0],c.moon.direction[1],c.moon.direction[2]},c.sunWeight,c.moonWeight);
        r.sources[0].color=NorthlightGI::Vec3{c.sunColor[0],c.sunColor[1],c.sunColor[2]};
        r.sources[1].color=NorthlightGI::Vec3{c.moonColor[0],c.moonColor[1],c.moonColor[2]};
        for(unsigned s=0;s<2;++s)if(r.sources[s].weight<=0){
            assert(r.sources[s].color.x==0&&r.sources[s].color.y==0&&r.sources[s].color.z==0);
            assert(!active(r.sources[s].color));zeroWeight+=s==1;}
        // The day: the moon has no weight whatever its altitude.
        if(c.sun.direction[2]>=.08f)assert(c.moonWeight==0);
        ++cases;
    }
    // Invalid celestial state: the renderer resolves both weights as 0 (world_renderer.h updateWorldContext).
    for(const auto* direct:directs){const NorthlightGI::Vec3 d{direct[0],direct[1],direct[2]};
        const auto r=NorthlightCelestialSources::resolve(d,d,d,NorthlightGI::Vec3{0,0,1},0,0);
        for(unsigned s=0;s<2;++s)assert(r.sources[s].weight==0&&!active(r.sources[s].color));}
    assert(zeroWeight>0);
    std::printf("{\"cases\":%u,\"moon_zero_weight_cases\":%u}\n",cases,zeroWeight);
}
