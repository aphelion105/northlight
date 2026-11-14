#include "weather_shelter.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
using namespace NorthlightGI;
static std::shared_ptr<BVH> plane(float z){
    WorldScene s;s.materials.push_back({});
    for(auto p:{Vec3(-10,-10,z),Vec3(10,-10,z),Vec3(10,10,z),Vec3(-10,10,z)})s.vertices.push_back({p,{0,0,1},0,0});
    s.triangles={{0,1,2,0},{0,2,3,0}};
    auto b=std::make_shared<BVH>();std::string error;assert(b->build(std::move(s),error));return b;
}
int main(int argc,char** argv){
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
    auto waitRoof=[&](std::shared_ptr<const BVH> bvh,Vec3 eye,const std::string& map,const NorthlightWeatherShelter::Roof* previous=nullptr){
        std::shared_ptr<const NorthlightWeatherShelter::Roof> grid;
        for(unsigned i=0;i<5000&&!grid;++i){grid=worker.roof(bvh,eye,map,2000+i);if(grid.get()==previous)grid.reset();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        assert(grid);return grid;
    };
    auto height=[](const auto& g,float x,float y){assert(g->contains(x,y));return g->height[unsigned((y-g->y)/g->Step)*g->Size+unsigned((x-g->x)/g->Step)];};
    const auto roofGrid=waitRoof(roof,{0,0,0},"Azeroth");assert(height(roofGrid,0,0)==5&&height(roofGrid,20,0)<0);
    assert(worker.roof(roof,{1,0,0},"Azeroth",9000)==roofGrid); // No periodic retrace when stationary.
    assert(roofGrid->tracedCells==128*128);
    auto scrolling=roofGrid;
    for(const auto eye:{Vec3(8,0,0),Vec3(8,8,0),Vec3(0,0,0),Vec3(8,0,.1f),Vec3(120,0,.1f)}){
        scrolling=waitRoof(roof,eye,"Azeroth",scrolling.get());
        NorthlightWeatherShelter::Worker reference;
        std::shared_ptr<const NorthlightWeatherShelter::Roof> full;
        for(unsigned i=0;i<5000&&!full;++i){full=reference.roof(roof,eye,"Azeroth",10000+i);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        assert(full&&full->tracedCells==128*128&&full->height==scrolling->height);
        if(eye.z==0){assert(scrolling->tracedCells<128*128);if(eye.y==0&&eye.x==8)assert(scrolling->tracedCells==10*128);}
        else assert(scrolling->tracedCells==128*128); // New Z interval or non-overlapping teleport.
        std::printf("ROOF reuse eye=(%.1f %.1f %.1f) rays=%u full=%u exact=1\n",eye.x,eye.y,eye.z,scrolling->tracedCells,full->tracedCells);
    }
    assert(!worker.roof(floor,{0,0,0},"Azeroth",9001)); // Same-map geometry revision cannot reuse a roof.
    const auto floorGrid=waitRoof(floor,{0,0,0},"Azeroth");assert(height(floorGrid,0,0)==-2);
    assert(floorGrid->tracedCells==128*128);
    assert(!worker.roof(floor,{120,0,0},"Azeroth",9002));
    assert(!worker.roof(floor,{0,0,0},"Northrend",9003));
    if(argc==6){
        const Vec3 eye(std::stof(argv[2]),std::stof(argv[3]),std::stof(argv[4]));std::vector<std::string> tiles;
        const int tx=int(std::floor(32-eye.y/(1600.f/3))),ty=int(std::floor(32-eye.x/(1600.f/3)));
        for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){const auto p=std::string(argv[1])+"/world-cache/Azeroth/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(std::filesystem::exists(p))tiles.push_back(p);}
        WorldScene scene;std::string error;
        assert(loadInstancedScenes(tiles,std::string(argv[1])+"/world-cache/models",eye-Vec3(96,96,512),eye+Vec3(96,96,512),scene,error));
        const auto bvh=std::make_shared<BVH>();assert(bvh->build(std::move(scene),error));
        const auto start=std::chrono::steady_clock::now();const auto grid=waitRoof(bvh,eye,"Azeroth");
        const float origin[]={grid->x,grid->y,grid->z};std::ofstream output(argv[5],std::ios::binary);output.write((const char*)origin,sizeof origin);output.write((const char*)grid->height.data(),grid->height.size()*sizeof(float));assert(output);
        std::printf("CLIENT roof triangles=%zu eyeZ=%.3f ceilingZ=%.3f queryMs=%.3f\n",bvh->triangleCount(),eye.z,height(grid,eye.x,eye.y),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        assert(height(grid,eye.x,eye.y)>eye.z); // Recorded crypt camera is beneath geometry.
    }
    std::cout<<"Shelter: outdoor/roof, unknown, moving-camera continuity, teleport, staleness, map change and BVH release passed\n";
}
