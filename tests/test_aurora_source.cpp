#include "aurora_source.h"
#include "aurora_source_projector.h"
#include "aurora_emission.h"
#include "aurora_emission_patch.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
    using namespace NorthlightAuroraSource;
    assert(sector(0,0,0)==-1&&sector(0,1,-1)==-1);
    assert(sector(std::numeric_limits<float>::infinity(),0,1)==-1);
    // Every equal-solid-angle cell center must map to itself, independently
    // of radial distance. This also exercises the azimuth seam and zenith.
    for(unsigned z=0;z<HeightCount;++z)for(unsigned a=0;a<AzimuthCount;++a){
        const float height=(z+.5f)/HeightCount,angle=((a+.5f)/AzimuthCount-.5f)*2*Pi;
        const float r=std::sqrt(1-height*height),x=r*std::sin(angle),y=r*std::cos(angle);
        assert(sector(x,y,height)==int(z*AzimuthCount+a));
        assert(sector(x*120,y*120,height*120)==sector(x,y,height));
    }
    Snapshot s;s.selection=5;s.revision=1;s.complete=true;
    s.radiance.fill({.1f,.2f,.3f});assert(valid(s));
    assert(evaluate(s,0,0,1)[1]==.2f);assert(evaluate(s,0,0,-1)==RGB{});
    s.complete=false;assert(evaluate(s,0,0,1)==RGB{});s.complete=true;
    s.radiance[2][1]=-1;assert(!valid(s));s.radiance[2][1]=std::numeric_limits<float>::quiet_NaN();assert(!valid(s));
    const auto style=NorthlightAurora::Style{true,4,0,1};const auto scale=emissionScale(style);
    const auto light=additional({1,1,1,.5f},scale,true);assert(std::fabs(light[1]-2)<1e-6);
    assert(additional({1,1,1,0},scale,true)==RGB{});assert(additional({1,1,1,0},scale,false)[1]>3.99f);
    assert(emissionScale({})==RGB{});
    for(unsigned major:{2u,3u}){
        // Minimal native COLOR material. The emitted transfer must preserve
        // the shader model, fit its budget and allocate runtime style away
        // from any native constant declaration.
        const std::uint32_t code[]={0xffff0000u|(major<<8),0x0200001fu,0x8000000au,0x900f0000u,0x02000001u,0x800f0800u,0x90e40000u,65535u};
        for(bool linear:{false,true}){std::vector<std::uint32_t> output;NorthlightAuroraNative::EmissionPatch info;
            assert(NorthlightAuroraNative::patchEmission(code,sizeof code/4,linear,output,info));
            std::vector<NorthlightHdrUi::Instruction> instructions;unsigned slots=0;
            assert(NorthlightHdrUi::parse(output.data(),output.size(),instructions,slots));
            assert(slots==info.originalSlots+info.addedSlots);assert(slots<=(major==2?64u:512u));
        }
    }
    NativeTriangle triangle{};triangle.vertices[0].position={-1,-1,1};triangle.vertices[1].position={2,-1,1};triangle.vertices[2].position={-1,2,1};
    float u=0,v=0;assert(intersect({0,0,1},triangle,u,v));assert(std::fabs(u-1.f/3)<1e-6&&std::fabs(v-1.f/3)<1e-6);
    assert(!intersect({0,0,-1},triangle,u,v));
    Snapshot projected;
    assert(project(std::vector<NativeTriangle>{triangle},5,9,[](unsigned,const NativeVertex&,RGB& out){out={.1f,.2f,.3f};return true;},projected,4));
    assert(valid(projected));float energy=0;for(const auto& cell:projected.radiance)energy+=cell[1];assert(energy>0);
    assert(!project(std::vector<NativeTriangle>{triangle},5,9,[](unsigned,const NativeVertex&,RGB& out){out={-1,0,0};return true;},projected,4));assert(!projected.complete);
    std::puts("PASS aurora source equal-solid-angle directions, scale invariance and incomplete/nonfinite rejection");
}
