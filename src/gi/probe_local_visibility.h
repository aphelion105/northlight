#pragma once
#include "world_probe_cache.h"
#include <array>
#include <cmath>
#include <mutex>

namespace NorthlightGI {
namespace LocalProbeVisibility {
constexpr unsigned Side=ProbeLocalVisibility::Side;
constexpr float Reach=16.f,PlaneBias=.04f;
inline uint16_t half(float value){
    uint32_t bits;std::memcpy(&bits,&value,4);const unsigned sign=(bits>>16)&0x8000,m=bits&0x7fffff;
    int e=int((bits>>23)&255)-112;
    if(e<=0){if(e<-10)return uint16_t(sign);const unsigned shift=unsigned(14-e),v=m|0x800000;
        return uint16_t(sign|((v+((1u<<(shift-1))-1)+((v>>shift)&1))>>shift));}
    if(e>=31)return uint16_t(sign|0x7bff);
    const unsigned rounded=m+0xfff+((m>>13)&1);
    return uint16_t(sign|std::min(0x7bffu,(unsigned(e)<<10)+(rounded>>13)));
}
inline float scalar(uint16_t bits){
    const unsigned e=(bits>>10)&31,m=bits&1023;
    const float value=e?std::ldexp(float(1024+m),int(e)-25):std::ldexp(float(m),-24);
    return bits&0x8000?-value:value;
}
inline Vec3 plane(const std::array<uint16_t,4>& p){return {scalar(p[0]),scalar(p[1]),scalar(p[2])};}
inline Vec3 direction(unsigned x,unsigned y){
    Vec3 d(2.f*x/(Side-1)-1,2.f*y/(Side-1)-1,0);d.z=1-std::fabs(d.x)-std::fabs(d.y);
    const float t=std::max(-d.z,0.f);d.x+=d.x>=0?-t:t;d.y+=d.y>=0?-t:t;return normalized(d);
}
inline std::array<uint16_t,4> pack(const RayHit& hit,Vec3 origin,Vec3 ray){
    if(!hit.hit)return {};
    const float d=dot(hit.geometricNormal,hit.position-origin);
    const Vec3 p=std::fabs(d)>1e-5f?hit.geometricNormal/d:ray/std::max(hit.distance,.0005f);
    return {half(p.x),half(p.y),half(p.z),0};
}
inline void uniformTiles(ProbeLocalVisibility& value){
    for(unsigned y=0;y<Side;++y)for(unsigned x=0;x<Side;++x){auto& p=value.texels[y*Side+x];bool same=true;
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){const auto& q=value.texels[std::min(y+dy,Side-1)*Side+std::min(x+dx,Side-1)];
            same&=p[0]==q[0]&&p[1]==q[1]&&p[2]==q[2];}
        p[3]=same?0x3c00:0;
    }
}
inline std::shared_ptr<const ProbeLocalVisibility> build(const BVH& bvh,Vec3 position){
    auto value=std::make_shared<ProbeLocalVisibility>();
    for(unsigned y=0;y<Side;++y)for(unsigned x=0;x<Side;++x){const Vec3 ray=direction(x,y);
        value->texels[y*Side+x]=pack(bvh.trace(position,ray,.0005f,Reach),position,ray);}
    uniformTiles(*value);return value;
}
inline std::shared_ptr<const ProbeLocalVisibility> overlay(const std::shared_ptr<const ProbeLocalVisibility>& base,const BVH* moving,Vec3 position){
    if(!base||!moving)return base;
    ProbeLocalVisibility value=*base;bool changed=false;
    for(unsigned y=0;y<Side;++y)for(unsigned x=0;x<Side;++x){const Vec3 ray=direction(x,y),p=plane(base->texels[y*Side+x]);
        const float denominator=dot(p,ray),limit=denominator>1e-6f?std::min(Reach,1/denominator):Reach;
        const auto hit=moving->trace(position,ray,.0005f,limit);
        if(hit.hit){value.texels[y*Side+x]=pack(hit,position,ray);changed=true;}}
    if(!changed)return base;
    uniformTiles(value);return std::make_shared<const ProbeLocalVisibility>(value);
}
inline float planeVisibility(Vec3 p,Vec3 delta){
    const float length=std::sqrt(dot(p,p));
    return length>1e-8f?std::clamp((PlaneBias-(dot(p,delta)-1)/length)/PlaneBias,0.f,1.f):1.f;
}
inline float visibility(const ProbeLocalVisibility& value,Vec3 delta){
    const float norm=std::fabs(delta.x)+std::fabs(delta.y)+std::fabs(delta.z);if(norm<1e-6f)return 1;
    Vec3 d=delta/norm;
    if(d.z<0){const float x=d.x;d.x=(1-std::fabs(d.y))*(x>=0?1:-1);d.y=(1-std::fabs(x))*(d.y>=0?1:-1);}
    const float x=std::clamp((d.x*.5f+.5f)*(Side-1),0.f,float(Side-1)),y=std::clamp((d.y*.5f+.5f)*(Side-1),0.f,float(Side-1));
    const unsigned ix=unsigned(x),iy=unsigned(y);const auto& first=value.texels[iy*Side+ix];
    if(first[3]){float v=planeVisibility(plane(first),delta);v*=v;return v*v;}
    const float fx=x-ix,fy=y-iy;float result=0;
    for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){const auto& p=value.texels[std::min(iy+dy,Side-1)*Side+std::min(ix+dx,Side-1)];
        result+=(dx?fx:1-fx)*(dy?fy:1-fy)*planeVisibility(plane(p),delta);}
    result*=result;return result*result;
}
inline float visibility(const std::shared_ptr<const ProbeLocalVisibility>& value,Vec3 delta){
    return value?visibility(*value,delta):0.f;
}
inline float facing(Vec3 normal,Vec3 delta){
    return std::max(0.f,(dot(normal,-delta)/std::max(std::sqrt(dot(delta,delta)),.00001f)+.15f)/1.15f);
}
struct Atlas {unsigned columns=0,width=0,height=0;};
inline Atlas atlas(unsigned n){
    if(!n||n>ProbeGridMaxN+2)return {};
    const unsigned count=n*n*n,columns=unsigned(std::ceil(std::sqrt(double(count))));
    return {columns,columns*Side,((count+columns-1)/columns)*Side};
}
}

// Worker-owned geometry cache. Light colours never invalidate visibility.
// Exact world keys reject wrapping collisions; generation checks reject stale
// helper completions. Published probes hold immutable shared map storage.
class ProbeVisibilityCache {
    struct Entry {ProbeGridKey key;bool occupied=false;std::shared_ptr<const ProbeLocalVisibility> value;};
    std::vector<Entry> entries_;const BVH* source_=nullptr;uint64_t generation_=0;
    mutable std::mutex mutex_;
public:
    struct Stats {uint64_t built=0,reused=0,failed=0;};
private:
    Stats stats_;
public:
    explicit ProbeVisibilityCache(size_t count=probeLayout().atlasSize()):entries_(std::max<size_t>(1,count)){}
    void reset(){std::lock_guard<std::mutex> lock(mutex_);source_=nullptr;++generation_;for(auto& e:entries_)e=Entry{};}
    std::shared_ptr<const ProbeLocalVisibility> get(const BVH& source,ProbeGridKey key,Vec3 position){
        ProbeGridKey checked;if(!probeGridKey(position,checked)||!(checked==key))return {};
        const size_t slot=probeAtlasIndex(key)%entries_.size();uint64_t generation;
        {std::lock_guard<std::mutex> lock(mutex_);
         if(source_!=&source){source_=&source;++generation_;for(auto& e:entries_)e=Entry{};}
         const auto& e=entries_[slot];if(e.occupied&&e.key==key){++stats_.reused;return e.value;}generation=generation_;}
        std::shared_ptr<const ProbeLocalVisibility> value;
        try{value=LocalProbeVisibility::build(source,position);}catch(const std::bad_alloc&){std::lock_guard<std::mutex> lock(mutex_);++stats_.failed;}
        {std::lock_guard<std::mutex> lock(mutex_);if(source_==&source&&generation_==generation){entries_[slot]={key,true,value};++stats_.built;}}
        return value;
    }
    Stats stats()const{std::lock_guard<std::mutex> lock(mutex_);return stats_;}
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
        std::lock_guard<std::mutex> lock(mutex_);memory.vector(NorthlightMemoryFootprint::Probes,entries_);
        for(const auto& e:entries_)if(e.value)memory.allocation(NorthlightMemoryFootprint::Probes,e.value.get(),sizeof(ProbeLocalVisibility));
    }
};
}
