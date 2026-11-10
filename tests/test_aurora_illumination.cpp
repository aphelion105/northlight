#include "aurora_illumination.h"
#include "world_gi.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightAurora;
using namespace NorthlightGI;
static void quad(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,Vec3 d){
    const uint32_t first=uint32_t(s.vertices.size());const Vec3 n=normalized(cross(b-a,c-a));
    for(Vec3 p:{a,b,c,d})s.vertices.push_back({p,n,0,0});
    s.triangles.push_back({first,first+1,first+2,0});s.triangles.push_back({first,first+2,first+3,0});
}
int main(){
    const float ambient[]={.2f,.3f,.4f};float out[3];
    assert(weight("Northrend",true,.88,-.1f,-.1f)==1);
    for(const char* map:{static_cast<const char*>(nullptr),"","Azeroth","Kalimdor","Expansion01","northrend"})
        assert(weight(map,true,.88,-.1f,-.1f)==0);
    for(double day:{-.1,.2,.49,1.,std::numeric_limits<double>::quiet_NaN()})assert(weight("Northrend",true,day,-.1f,-.1f)==0);
    assert(weight("Northrend",false,.88,-.1f,-.1f)==0);
    for(float z:{0.f,.01f,1.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        assert(weight("Northrend",true,.88,z,-.1f)==0);assert(weight("Northrend",true,.88,-.1f,z)==0);
    }
    float previous[3]={},maxStep=0;unsigned active=0;double peakDay=0;float peak=0;bool green=false,purple=false;
    for(unsigned second=0;second<86400;++second){
        const double day=double(second)/86400;const auto orbit=NorthlightCelestialOrbit::evaluateNative(day);
        const bool on=skyRadiance("Northrend",100,true,day,ambient,out);
        const float w=weight("Northrend",orbit.valid,day,orbit.sun.direction[2],orbit.moon.direction[2]);
        assert(on==(w>0));active+=on;
        if(on){assert(day>.5&&orbit.sun.direction[2]<0&&orbit.moon.direction[2]<0);
            green|=out[1]-ambient[1]>out[2]-ambient[2];purple|=out[0]-ambient[0]>out[1]-ambient[1];}
        if(w>peak){peak=w;peakDay=day;}
        for(unsigned k=0;k<3;++k){assert(out[k]>=ambient[k]&&out[k]<=ambient[k]+.2f*.4f);
            if(second)maxStep=std::max(maxStep,std::fabs(out[k]-previous[k]));previous[k]=out[k];}
    }
    std::fprintf(stderr,"aurora orbit active=%u green=%d purple=%d maxStep=%.9f peak=%.9f\n",active,int(green),int(purple),maxStep,peak);
    assert(active>3600&&active<3*3600&&green&&purple&&maxStep<.00025f&&peak==1);
    assert(!skyRadiance("Northrend",0,true,peakDay,ambient,out));assert(std::equal(out,out+3,ambient));
    assert(!skyRadiance("Northrend",100,false,peakDay,ambient,out));assert(std::equal(out,out+3,ambient));
    const float black[]={0,0,0};assert(!skyRadiance("Northrend",100,true,peakDay,black,out));assert(std::equal(out,out+3,black));
    float tuned[3];assert(skyRadiance("Northrend",100,true,peakDay,ambient,tuned));
    assert(skyRadiance("Northrend",200,true,peakDay,ambient,out));
    for(unsigned k=0;k<3;++k)assert(std::fabs(out[k]-ambient[k]-2*(tuned[k]-ambient[k]))<1e-6f);

    // Real transport: a closed inward-facing black room blocks all sky paths.
    WorldScene scene;scene.materials.push_back({});scene.materials.back().albedo={0,0,0};
    quad(scene,{-4,-4,0},{4,-4,0},{4,4,0},{-4,4,0});
    quad(scene,{-4,-4,8},{-4,4,8},{4,4,8},{4,-4,8});
    quad(scene,{-4,-4,0},{-4,-4,8},{4,-4,8},{4,-4,0});
    quad(scene,{4,4,0},{4,4,8},{-4,4,8},{-4,4,0});
    quad(scene,{-4,4,0},{-4,4,8},{-4,-4,8},{-4,-4,0});
    quad(scene,{4,-4,0},{4,-4,8},{4,4,8},{4,4,0});
    BVH bvh;std::string error;assert(bvh.build(std::move(scene),error));
    Lighting base;base.sunIrradiance={0,0,0};base.skyRadiance={ambient[0],ambient[1],ambient[2]};
    Lighting aurora=base;aurora.skyRadiance={tuned[0],tuned[1],tuned[2]};
    const auto before=prepareLighting(base),after=prepareLighting(aurora);
    for(bool roof:{false,true}){
        const Vec3 point=roof?Vec3(0,0,4):Vec3(100,100,4);ProbeResponse response;
        const auto original=solveProbeResponse(bvh,point,before,256,17,response);
        const auto fresh=solveProbePrepared(bvh,point,after,256,17),cached=relightProbe(response,after);
        assert(original.valid&&fresh.valid&&cached.valid);
        for(unsigned i=0;i<4;++i){const Vec3 delta=fresh.sh[i]-cached.sh[i];assert(dot(delta,delta)<1e-8f);}
        if(roof)assert(dot(fresh.sh[0],fresh.sh[0])<1e-8f);
        else assert(fresh.sh[0].x>original.sh[0].x&&fresh.sh[0].y>original.sh[0].y&&fresh.sh[0].z>original.sh[0].z);
    }
    std::printf("PASS aurora fullDay activeSeconds=%u maxStep=%.9f gates=1 palettes=2 roofOcclusion=1 cachedRelight=1\n",active,maxStep);
}
