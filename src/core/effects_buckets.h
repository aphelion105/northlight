#pragma once
// 0.3.175 (S3) effects buckets, RenderProfile applying frames: the effects wall time (renderEffects,
// the cpuEffects scope) split into contiguous buckets. Each mark(b) reads the clock once and bills the
// span since the previous mark to b, so the buckets sum to the whole (begin to end) and a mark that is
// not reached (an early return) rolls its span into the next one. The mod's draw calls (the counted
// raw DrawIndexedPrimitive/DrawPrimitive/UP calls) are billed the same way on sample frames only. Off:
// begin(false) and every mark() return at once, no clock read. Portable: the caller supplies the clock.
#include <cstdint>
namespace NorthlightEffectsBuckets {
enum Bucket : unsigned {Setup,Celestial,AO,Selection,Upload,Static,ShadowSetup,TerrainSelection,SunNear,SunFar,MoonNear,MoonFar,SunActor,MoonActor,ActorShadow,Point,
    LightingSetup,Lighting,GI,PointLighting,Fog,Composite,Veil,Tail,Count};
inline const char* name(unsigned b){static const char* n[Count]={"setup","celestial","ao","selection","upload","static","shadowSetup","terrainSelection",
    "sunNear","sunFar","moonNear","moonFar","sunActor","moonActor","actorShadow","point","lightingSetup","lighting","gi","pointLighting","fog","composite","veil","tail"};return b<Count?n[b]:"?";}
constexpr unsigned MaxReads=31; /* begin, at most 29 marks (including the atmosphere join), end */
constexpr double SlowMs=8.0;
inline bool report(bool sampled,double effectsMs){return sampled||effectsMs>=SlowMs;}
class Frame {
public:
    using ClockFn=std::int64_t(*)();
private:
    bool on_=false,drawsMeasured_=false;ClockFn clock_=nullptr;std::int64_t start_=0,last_=0,total_=0;std::int64_t ticks_[Count]={};std::uint32_t draws_[Count]={};
    const std::uint32_t* counters_[4]={};std::uint32_t lastDraws_=0;unsigned reads_=0;
    std::uint32_t draws()const{std::uint32_t n=0;for(auto* c:counters_)if(c)n+=*c;return n;}
public:
    // counters: the raw draw-call counters (null entries allowed).
    void begin(bool on,ClockFn clock,const std::uint32_t* const* counters=nullptr,unsigned count=0){
        on_=on&&clock;if(!on_)return;clock_=clock;
        for(auto& t:ticks_)t=0;for(auto& d:draws_)d=0;for(auto& c:counters_)c=nullptr;
        drawsMeasured_=false;for(unsigned i=0;counters&&i<count&&i<4;++i){counters_[i]=counters[i];drawsMeasured_|=counters[i]!=nullptr;}
        lastDraws_=draws();reads_=1;start_=last_=clock_();total_=0;
    }
    bool on()const{return on_;}
    bool drawsMeasured()const{return drawsMeasured_;}
    // The span since the previous mark belongs to `ended`.
    void mark(Bucket ended){if(!on_)return;const std::int64_t now=clock_();++reads_;ticks_[ended]+=now-last_;last_=now;if(drawsMeasured_){const auto n=draws();draws_[ended]+=n-lastDraws_;lastDraws_=n;}}
    // The rest (state restore, anything after the last mark) is Tail; the frame is then closed.
    void end(){if(!on_)return;mark(Tail);total_=last_-start_;on_=false;}
    std::int64_t ticks(unsigned b)const{return b<Count?ticks_[b]:0;}
    std::uint32_t drawCalls(unsigned b)const{return b<Count?draws_[b]:0;}
    std::int64_t total()const{return total_;}
    unsigned reads()const{return reads_;}
};
}
