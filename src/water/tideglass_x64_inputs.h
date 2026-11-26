#pragma once
#include "host_liquid.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace NorthlightTideglassX64 {
struct Ship {
    std::uintptr_t identity=0;
    float position[3]={},speed=0;
};
struct Material {
    std::uintptr_t texture=0;
    WaterHostLiquidKind kind=WaterHostLiquidUnknown;
    int ramp=-1;
};
struct Inputs {
    WaterHostLiquidKind camera=WaterHostLiquidUnknown;
    unsigned cameraId=0;
    float depth=0;
    std::array<Ship,16> ships{};
    std::array<Material,256> materials{};
    unsigned shipCount=0,materialCount=0;
    Material material(std::uintptr_t texture)const{
        Material found{};
        for(unsigned i=0;i<materialCount;++i){
            const auto& candidate=materials[i];
            if(candidate.texture!=texture)continue;
            if(found.texture&&(candidate.kind!=found.kind||candidate.ramp!=found.ramp))return {};
            found=candidate;
        }
        return found;
    }
};

class Wakes {
    struct Track {
        Ship ship{};
        float heading[2]={};
        unsigned seen=0;
        struct Point {float x,y,speed;unsigned tick;};
        std::array<Point,kWaterBoatTrailPoints-1> trail{};
        unsigned count=0;
    };
    std::array<Track,kWaterMaxBoats> tracks{};
    static float distance(float x,float y){return x*x+y*y;}
public:
    void reset(){tracks={};}
    void sample(const Inputs& inputs, unsigned now, WaterFrameValues& output){
        for(auto& track:tracks){
            if(track.ship.identity&&now-track.seen>4000)track={};
        }
        for(unsigned i=0;i<inputs.shipCount&&output.boatCount<kWaterMaxBoats;++i){
            const auto& ship=inputs.ships[i];
            if(!ship.identity||!std::isfinite(ship.speed)||ship.speed<0||ship.speed>200||
               !std::all_of(ship.position,ship.position+3,[](float v){return std::isfinite(v)&&std::fabs(v)<100000.f;}))continue;
            const float cameraX=ship.position[0]-output.cameraPosition[0],cameraY=ship.position[1]-output.cameraPosition[1];
            if(distance(cameraX,cameraY)>600.f*600.f)continue;
            auto track=std::find_if(tracks.begin(),tracks.end(),[&](const Track& t){return t.ship.identity==ship.identity;});
            if(track==tracks.end())track=std::find_if(tracks.begin(),tracks.end(),[](const Track& t){return !t.ship.identity;});
            if(track==tracks.end())continue;
            const float dx=ship.position[0]-track->ship.position[0],dy=ship.position[1]-track->ship.position[1];
            const float movement=distance(dx,dy);
            if(!track->ship.identity||movement>80.f*80.f||std::fabs(ship.position[2]-track->ship.position[2])>20.f)*track={};
            else if(movement>.000001f&&ship.speed>=.6f){
                const float length=std::sqrt(movement);
                track->heading[0]=dx/length;track->heading[1]=dy/length;
                if(!track->count||distance(ship.position[0]-track->trail[0].x,ship.position[1]-track->trail[0].y)>=64.f){
                    track->count=std::min<unsigned>(track->count+1,track->trail.size());
                    std::move_backward(track->trail.begin(),track->trail.begin()+track->count-1,track->trail.begin()+track->count);
                    track->trail[0]={track->ship.position[0],track->ship.position[1],ship.speed,track->seen};
                }
            }
            track->ship=ship;track->seen=now;
            if(ship.speed<.6f||distance(track->heading[0],track->heading[1])<.5f)continue;
            auto& boat=output.boats[output.boatCount++];
            std::copy_n(ship.position,3,boat.position);std::copy_n(track->heading,2,boat.direction);
            boat.speed=ship.speed;
            boat.trail[boat.trailCount++]={ship.position[0],ship.position[1],0,ship.speed};
            for(unsigned j=0;j<track->count;++j){
                const auto& point=track->trail[j];
                if(now-point.tick>30000)break;
                boat.trail[boat.trailCount++]={point.x,point.y,float(now-point.tick)*.001f,point.speed};
            }
        }
    }
};
}
