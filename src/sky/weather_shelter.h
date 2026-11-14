#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "world_gi.h"
#include "weather_roof.h"

namespace NorthlightWeatherShelter {
// A small, lazy CPU worker. Moving cameras refresh before leaving the cached
// region; the last answer bridges the short asynchronous query, not a teleport.
class Worker {
    struct Request{std::shared_ptr<const NorthlightGI::BVH> bvh;NorthlightGI::Vec3 eye;std::string map;std::uint64_t tick=0;};
    std::mutex mutex;std::condition_variable wake;std::thread worker;bool stopping=false,pending=false,known=false,outdoors=false;
    Request request,result;
    Request roofRequest,roofResult;
    std::weak_ptr<const NorthlightGI::BVH> roofRequestedScene,roofResultScene;
    std::shared_ptr<const Roof> roofResultGrid;
    bool roofPending=false;
    void work(){for(;;){Request r;bool makeRoof=false;{std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||pending||roofPending;});if(stopping)return;
        if(pending){r=request;request.bvh.reset();pending=false;}
        else{r=roofRequest;roofRequest.bvh.reset();roofPending=false;makeRoof=true;}}
        if(makeRoof){
            std::shared_ptr<Roof> grid;
            try{grid=std::make_shared<Roof>();grid->x=std::floor(r.eye.x/Roof::Step)*Roof::Step-Roof::Span*.5f;
                grid->y=std::floor(r.eye.y/Roof::Step)*Roof::Step-Roof::Span*.5f;grid->z=r.eye.z;
                for(unsigned y=0;y<Roof::Size;++y)for(unsigned x=0;x<Roof::Size;++x){
                    const auto hit=r.bvh->trace({grid->x+(x+.5f)*Roof::Step,grid->y+(y+.5f)*Roof::Step,r.eye.z+4096},{0,0,-1},.001f,8192);
                    grid->height[y*Roof::Size+x]=hit.hit?hit.position.z:r.eye.z-4096;
                }
            }catch(...){grid.reset();}
            std::lock_guard<std::mutex> lock(mutex);roofResultScene=r.bvh;r.bvh.reset();roofResult=std::move(r);roofResultGrid=std::move(grid);continue;
        }
        bool open=true;try{for(auto offset:{NorthlightGI::Vec3(0,0,0),NorthlightGI::Vec3(.6f,0,0),NorthlightGI::Vec3(-.6f,0,0),NorthlightGI::Vec3(0,.6f,0),NorthlightGI::Vec3(0,-.6f,0)})if(r.bvh->occluded(r.eye+offset,NorthlightGI::Vec3(0,0,1),80.f,.1f)){open=false;break;}}
        catch(...){open=false;}
        r.bvh.reset();{std::lock_guard<std::mutex> lock(mutex);result=std::move(r);outdoors=open;known=true;}}}
public:
    Worker(){worker=std::thread([this]{work();});}
    ~Worker(){{std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();worker.join();}
    std::shared_ptr<const Roof> roof(std::shared_ptr<const NorthlightGI::BVH> bvh,NorthlightGI::Vec3 eye,const std::string& map,std::uint64_t now){
        if(!bvh||!bvh->triangleCount())return {};std::lock_guard<std::mutex> lock(mutex);
        const auto moved=eye-roofRequest.eye;
        // Static geometry needs no timer-driven retrace. Refresh with eight yards
        // of travel, a new scene generation, map change or a large vertical move.
        if(roofRequest.map!=map||roofRequestedScene.lock()!=bvh||moved.x*moved.x+moved.y*moved.y>=64||std::abs(moved.z)>=256){
            roofRequestedScene=bvh;roofRequest={bvh,eye,map,now};roofPending=true;wake.notify_one();
        }
        // Never use another generation's roof during streaming or a teleport.
        if(!roofResultGrid||roofResult.map!=map||roofResultScene.lock()!=bvh||
           !roofResultGrid->contains(eye.x,eye.y,32)||std::abs(eye.z-roofResultGrid->z)>=256)return {};
        return roofResultGrid;
    }
    bool sample(std::shared_ptr<const NorthlightGI::BVH> bvh,NorthlightGI::Vec3 eye,const std::string& map,std::uint64_t now){
        if(!bvh||!bvh->triangleCount())return false;std::lock_guard<std::mutex> lock(mutex);
        const auto moved=eye-request.eye;
        if(request.map!=map||now-request.tick>=250||NorthlightGI::dot(moved,moved)>.25f){request={std::move(bvh),eye,map,now};pending=true;wake.notify_one();}
        const auto d=eye-result.eye;const float distance2=NorthlightGI::dot(d,d);
        const auto age=now-result.tick;
        return known&&outdoors&&result.map==map&&age<1000&&
            (distance2<=4||(age<100&&distance2<=40*40));
    }
};
}
