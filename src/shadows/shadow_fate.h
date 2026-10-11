#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

// Diagnostic "shadow fate" tracker: during a short sampled window, every model
// draw that reaches replay capture is keyed by its buffers and draw range and
// records one outcome per frame (why it was rejected, or how the actor quota
// treated it). At the end of the window the keys whose shadow flipped between
// drawn and not drawn most often are reported with their outcome histogram.
// Fixed table, no allocation; outside the window nothing runs. The renderer
// enables it only with an actor quota (budget 0 keeps 0.3.138's per-draw work).
namespace NorthlightShadowFate {
enum Fate : unsigned char {Absent,Unknown,Cap4096,Small,CaptureBudget,Blend,AlphaFunc,Projection,Snapshot,Constants,Material,
    NotRanked,Kept,Dropped,Waiting,Rigid,Attached,Exempt,RadiusGate,DistanceCut,FateCount}; /* 0.3.210 (task 31): the capture admission's rejections */
inline const char* name(unsigned f){
    static const char* names[FateCount]={"absent","unknown","cap4096","small","captureBudget","blend","alphaFunc","projection","snapshot","constants","material",
        "notRanked","kept","dropped","waiting","rigid","attached","exempt","radiusGate","distanceCut"};
    return f<FateCount?names[f]:"?";
}
// cell: the instance's palette root quantized to 1 yd (identical model instances
// share every buffer and range).
struct Key {std::uint64_t vb=0,ib=0;const void* shader=nullptr;std::int32_t base=0,cell[3]={};std::uint32_t start=0,count=0,minimum=0;
    bool operator==(const Key& o)const{return vb==o.vb&&ib==o.ib&&shader==o.shader&&base==o.base&&start==o.start&&count==o.count&&minimum==o.minimum&&cell[0]==o.cell[0]&&cell[1]==o.cell[1]&&cell[2]==o.cell[2];}};
class Tracker {
public:
    static constexpr unsigned Slots=16384,WindowFrames=120,Period=1200; /* 2048 overflowed in Durotar crowds (~2.4k keys, more per instance); about 3 MiB */
    struct Slot {Key key;bool used=false,drawn=false,lastDrawn=false,skinned=false,rigid=false;unsigned lastFrame=0,lastPresent=~0u,flips=0,present=0,absentRuns=0,absentRun=0,maxAbsentRun=0;
        unsigned histogram[FateCount]={};float distance=0;unsigned triangles=0;unsigned char fate=Absent;};
    // Frame bookkeeping: a window of WindowFrames every Period frames.
    void beginFrame(bool enabled=true){++frame_;active_=enabled&&(frame_%Period)<WindowFrames;if(active_&&frame_%Period==0)reset();}
    bool active()const{return active_;}
    bool windowEnded()const{return (frame_%Period)==WindowFrames-1;}
    unsigned frame()const{return frame_;}
    int slot(const Key& key,bool skinned,unsigned triangles){
        std::uint64_t h=key.vb*0x9e3779b97f4a7c15ULL^key.ib*0xc2b2ae3d27d4eb4fULL^std::uint64_t(reinterpret_cast<std::uintptr_t>(key.shader))*0x165667b19e3779f9ULL^
            (std::uint64_t(key.start)<<32|key.count)^std::uint64_t(std::uint32_t(key.base))*0x27d4eb2f165667c5ULL^key.minimum^
            (std::uint64_t(std::uint32_t(key.cell[0]))*0x9e3779b1u)^(std::uint64_t(std::uint32_t(key.cell[1]))<<21)^(std::uint64_t(std::uint32_t(key.cell[2]))<<42);
        h^=h>>31;
        for(unsigned probe=0;probe<32;++probe){auto& s=slots_[(h+probe)%Slots];
            if(s.used&&s.key==key){s.skinned=skinned;s.triangles=triangles;return int(&s-slots_.data());}
            if(!s.used){s=Slot{};s.used=true;s.key=key;s.skinned=skinned;s.triangles=triangles;++keys_;return int(&s-slots_.data());}}
        ++overflow_;return -1;
    }
    // One outcome per key and frame; the last recorded outcome of the frame wins
    // (a key drawn twice in a frame keeps its drawn state if either draw casts).
    void record(int index,Fate fate,bool drawn,float distance=0){
        if(index<0||!active_)return;auto& s=slots_[unsigned(index)];
        if(s.lastFrame!=frame_){s.lastFrame=frame_;s.drawn=false;++s.present;}
        s.fate=fate;s.drawn=s.drawn||drawn;if(distance>0)s.distance=distance;if(fate==Rigid||fate==Attached)s.rigid=true;
    }
    // After selection: count this frame's outcome and drawn<->not flips.
    void endFrame(){
        if(!active_)return;
        for(auto& s:slots_){if(!s.used)continue;
            if(s.lastFrame!=frame_){++s.histogram[Absent];if(s.lastPresent==frame_-1)++s.absentRuns;s.maxAbsentRun=std::max(s.maxAbsentRun,++s.absentRun);continue;}
            s.absentRun=0;
            ++s.histogram[s.fate];
            if(s.lastPresent==frame_-1&&s.drawn!=s.lastDrawn)++s.flips;
            s.lastPresent=frame_;s.lastDrawn=s.drawn;}
    }
    template<class Log> void report(Log&& log){
        std::array<const Slot*,10> top{};
        for(const auto& s:slots_){if(!s.used||!s.flips)continue;
            for(unsigned i=0;i<top.size();++i)if(!top[i]||s.flips>top[i]->flips){for(unsigned j=top.size()-1;j>i;--j)top[j]=top[j-1];top[i]=&s;break;}}
        unsigned flipping=0;for(const auto& s:slots_)flipping+=s.used&&s.flips;
        log("SHADOW fate window frames=%u keys=%u overflow=%u flippingKeys=%u",WindowFrames,keys_,overflow_,flipping);
        for(const auto* s:top){if(!s)break;std::string h;char buffer[48];
            for(unsigned f=0;f<FateCount;++f)if(s->histogram[f]){std::snprintf(buffer,sizeof buffer,"%s%s:%u",h.empty()?"":",",name(f),s->histogram[f]);h+=buffer;}
            log("SHADOW fate key vb=%llu ib=%llu start=%u count=%u base=%d cell=%d,%d,%d flips=%u present=%u absentRuns=%u maxAbsentRun=%u triangles=%u skinned=%d rigid=%d distance=%.1f fates=%s",
                (unsigned long long)s->key.vb,(unsigned long long)s->key.ib,s->key.start,s->key.count,s->key.base,s->key.cell[0],s->key.cell[1],s->key.cell[2],s->flips,s->present,s->absentRuns,s->maxAbsentRun,s->triangles,s->skinned,s->rigid,s->distance,h.c_str());}
    }
    void reset(){for(auto& s:slots_)s=Slot{};keys_=overflow_=0;}
private:
    std::array<Slot,Slots> slots_{};unsigned frame_=0,keys_=0,overflow_=0;bool active_=false;
};
}
