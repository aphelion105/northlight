#include "probe_cascade.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightGI;
int main(){
    assert(configureProbeLayout({4,4,6}));
    BVH bvh;std::string error;assert(bvh.build({},error));
    Lighting light;light.skyRadiance={.2f,.3f,.4f};auto prepared=prepareLighting(light);
    CoarseProbeLayer layer;unsigned calls=0,publishes=0;uint32_t clock=0;
    std::vector<std::shared_ptr<const std::vector<ProbeAtlasEntry>>> seen;
    auto publish=[&](auto a){++publishes;seen.push_back(a);};
    layer.run({}, {},288,bvh,prepared,64,1,"A",[&]{return ++calls>7;},publish,[&]{return clock+=20;});
    assert(!layer.completeFor({})&&publishes>0&&layer.atlas("A")&&!layer.atlas("B"));
    unsigned occupied=0;for(const auto& e:*seen.front())occupied+=e.occupied;
    calls=0;layer.run({}, {},288,bvh,prepared,64,1,"A",[]{return false;},publish,[&]{return clock+=20;});
    assert(layer.completeFor({}));unsigned count=0;
    for(const auto& e:*layer.atlas("A"))if(e.occupied){++count;assert(e.probe.samples==16);assert(e.probe.position.x==e.key.x*32&&e.probe.position.y==e.key.y*32&&e.probe.position.z==e.key.z*32);assert(e.probe.localVisibility);}
    assert(count==probeLayout().count());unsigned retained=0;for(const auto& e:*seen.front())retained+=e.occupied;assert(retained==occupied);
    auto before=layer.atlas("A");layer.run({}, {},288,bvh,prepared,64,1,"A",[&]{++calls;return false;},publish,[&]{return ++clock;});assert(calls==0&&before==layer.atlas("A"));
    assert(!layer.completeFor({64,0,0}));
    layer.run({}, {},288,bvh,prepared,64,2,"B",[]{return true;},publish,[&]{return ++clock;});assert(!layer.atlas("A")&&!layer.atlas("B"));
    assert(coarseProbeSupported({224,0,256},{},288));assert(!coarseProbeSupported({225,0,0},{},288));assert(!coarseProbeSupported({0,0,257},{},288));
    // The coarse visibility must see blockers beyond the near cell's 16 units.
    WorldScene s;s.materials.emplace_back();s.vertices={{{20,-100,-100},{-1,0,0},0,0},{{20,-100,100},{-1,0,0},0,0},{{20,100,100},{-1,0,0},0,0},{{20,100,-100},{-1,0,0},0,0}};
    s.triangles={{0,1,2,0},{0,2,3,0}};BVH wall;assert(wall.build(std::move(s),error));
    auto nearMap=LocalProbeVisibility::build(wall,{}),farMap=LocalProbeVisibility::build(wall,{},64);
    assert(LocalProbeVisibility::visibility(nearMap,{24,0,0})>.99f);assert(LocalProbeVisibility::visibility(farMap,{24,0,0})<.01f);
    std::puts("PROBE_CASCADE_PASS idleFill=1 preempt=1 immutable=1 mapReset=1 loadedBounds=1 coarseVisibility=1");
}
