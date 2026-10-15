#include "world_draw_domain.h"
#include "world_gi.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightGI;
using NorthlightWorldDrawDomain::accepts;
static void plane(WorldScene& s,float lowX,float highX,float z){
    unsigned base=unsigned(s.vertices.size());
    for(auto p:{Vec3(lowX,-1000,z),Vec3(highX,-1000,z),Vec3(highX,1000,z),Vec3(lowX,1000,z)})s.vertices.push_back({p,{0,0,1},0,0});
    s.triangles.push_back({base,base+1,base+2,0});s.triangles.push_back({base,base+2,base+3,0});
}
static WorldScene casters(bool gate){
    WorldScene s;s.materials.push_back({});
    // Same model shader and perspective projection. Only the native depth
    // domain distinguishes the camera-centred sky from a physical roof.
    if(!gate||accepts(true,true,0,.94f,.999023f,1))plane(s,-1000,1000,200);
    if(!gate||accepts(true,true,0,.94f,0,.94f))plane(s,-10,-1,10);
    return s;
}
int main(){
    unsigned cases=0;
    for(float lo:{0.f,.1f})for(float hi:{.5f,.94f,1.f}){
        assert(accepts(true,true,lo,hi,lo,hi));
        assert(!accepts(false,true,lo,hi,lo,hi));assert(!accepts(true,false,lo,hi,lo,hi));
        assert(!accepts(true,true,lo,hi,.999023f,1));++cases;
    }
    for(float bad:{-1.f,2.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        assert(!accepts(true,true,0,.94f,bad,1));assert(!accepts(true,true,0,bad,0,.94f));++cases;
    }
    {using NorthlightWorldDrawDomain::depthTested; // the client's depth-off is ZFUNC ALWAYS (8) with ZENABLE still TRUE
     for(unsigned long f=1;f<=8;++f){assert(depthTested(1,f)==(f!=8));assert(!depthTested(0,f));++cases;}}
    assert(!accepts(true,true,0,0,0,0));assert(!accepts(true,true,0,.94f,.5f,.4f));
    BVH old,corrected;std::string error;
    assert(old.build(casters(false),error));assert(corrected.build(casters(true),error));
    for(int x=-9;x<=9;++x)if(x){
        for(float altitude:{.01f,2.f,6.f}){
            Vec3 p(float(x),0,altitude);
            assert(old.occluded(p,{0,0,1},320)); // fake sky shadows every sample
            assert(corrected.occluded(p,{0,0,1},320)==(x<0)); // real roof retained
            ++cases;
        }
    }
    std::printf("PASS %u depth-domain and actual BVH caster cases: sky excluded; physical roof/surface/volume shadows retained\n",cases);
}
