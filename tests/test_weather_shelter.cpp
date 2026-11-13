#include "weather_shelter.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace NorthlightGI;
static std::shared_ptr<BVH> plane(float z){
    WorldScene s;s.materials.push_back({});
    for(auto p:{Vec3(-10,-10,z),Vec3(10,-10,z),Vec3(10,10,z),Vec3(-10,10,z)})s.vertices.push_back({p,{0,0,1},0,0});
    s.triangles={{0,1,2,0},{0,2,3,0}};
    auto b=std::make_shared<BVH>();std::string error;assert(b->build(std::move(s),error));return b;
}
int main(){
    auto floor=plane(-2),roof=plane(5);NorthlightWeatherShelter::Worker worker;
    assert(!worker.sample(nullptr,{0,0,0},"Azeroth",100));
    bool outside=false;
    for(unsigned i=0;i<1000&&!outside;++i){outside=worker.sample(floor,{0,0,0},"Azeroth",100);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(outside&&floor.use_count()==1);
    // A fast orbit can move the camera over two yards in one frame. Keep the
    // last proven outdoor answer while the worker checks the new position.
    for(unsigned frame=1;frame<=20;++frame){
        assert(worker.sample(floor,{float(frame*3),0,0},"Azeroth",100+frame*16));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(!worker.sample(floor,{120,0,0},"Azeroth",421)); // Teleport: no reused exposure.
    assert(!worker.sample(floor,{0,0,0},"Azeroth",1100));
    for(unsigned i=0;i<100;++i){assert(!worker.sample(roof,{0,0,0},"Northrend",1200));std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(roof.use_count()==1);
    outside=false;
    for(unsigned i=0;i<1000&&!outside;++i){outside=worker.sample(floor,{0,0,0},"Azeroth",1500);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(outside);
    std::cout<<"Shelter: outdoor/roof, unknown, moving-camera continuity, teleport, staleness, map change and BVH release passed\n";
}
