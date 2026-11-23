#pragma once
#include "replay_constant_ranges.h"

// Exact, frame-local constant sharing. This is deliberately not an actor-ID
// cache: equipment can use different palettes, and identical geometry can be
// drawn by different actors. Device readbacks remain authoritative. Matching
// consecutive captures once saves repeated copies/comparisons in every shadow
// cascade/face, without changing draw order, geometry or animation frequency.
namespace NorthlightReplayPoses {
inline bool sameRange(NorthlightShaderConstants::Range a,NorthlightShaderConstants::Range b){
    return a.first==b.first&&a.count==b.count;
}
inline bool validRange(NorthlightShaderConstants::Range r,unsigned limit){return r.first<=limit&&r.count<=limit-r.first;}

inline NorthlightShaderConstants::Range weightedUploadRange(const NorthlightShaderConstants::Usage& usage,unsigned paletteEnd){
    const auto fixed=usage.weightedFloats;
    if(!fixed.count||!validRange(fixed,256)||!paletteEnd||paletteEnd>75||!sameRange(usage.floats,{0,256}))return {};
    return {fixed.first,std::max(fixed.first+fixed.count,31+3*paletteEnd)-fixed.first};
}

template<class Replay> unsigned weightedPacketPaletteEnd(const Replay& p){
    return p.constantUsage.weightedFloats.count&&p.gpuCached&&p.pointBounds.valid?
        p.boundsWork.weightedPaletteEnd:0;
}

// A merged draw may read bones from any member. A missing proof selects the full bank.
template<class PaletteAt> NorthlightShaderConstants::Range weightedRunUploadRange(
        const NorthlightShaderConstants::Usage& usage,size_t count,PaletteAt paletteAt){
    if(!usage.weightedFloats.count)return {};
    unsigned paletteEnd=0;
    for(size_t i=0;i<count;++i){
        const unsigned end=paletteAt(i);
        if(!end)return {};
        paletteEnd=std::max(paletteEnd,end);
    }
    return weightedUploadRange(usage,paletteEnd);
}

// Keep a single range across consecutive draws with the same captured pose.
// Growing the range partway through a pose would issue another setter call.
template<class Packets> void groupDirectionalPalettes(Packets& packets){
    constexpr unsigned unproven=76;
    for(size_t first=0;first<packets.size();){
        const auto group=packets[first]->constantGroup;
        size_t next=first;
        unsigned paletteEnd=0;
        do {
            const unsigned end=weightedPacketPaletteEnd(*packets[next++]);
            paletteEnd=std::max(paletteEnd,end&&end<unproven?end:unproven);
        }while(next<packets.size()&&packets[next]->constantGroup==group);
        const auto provenEnd=static_cast<unsigned char>(paletteEnd<unproven?paletteEnd:0);
        for(;first<next;++first)packets[first]->directionalPaletteEnd=provenEnd;
    }
}

template<class Replay> bool sameCaptured(const Replay& a,const Replay& b){
    const auto& x=a.constantUsage;const auto& y=b.constantUsage;
    if(a.projectionKind!=b.projectionKind||(a.projectionKind!=1&&a.projectionKind!=2)||
       !sameRange(x.floats,y.floats)||!sameRange(x.booleans,y.booleans)||!sameRange(x.integers,y.integers)||
       !validRange(x.floats,256)||!validRange(x.booleans,16)||!validRange(x.integers,16))return false;
    const unsigned projection=a.projectionKind==1?4:2,end=x.floats.first+x.floats.count;
    if(x.floats.first>projection||end<projection+4)return false;
    // Projection is overwritten for this pass. Compare all other captured
    // registers bit-for-bit, including signed zero/NaN payloads and every bone
    // of relative-address shaders. Uncaptured pooled memory is never read.
    if(a.constants!=b.constants&&(std::memcmp(a.constants+4*x.floats.first,b.constants+4*x.floats.first,(projection-x.floats.first)*16)||
       std::memcmp(a.constants+4*(projection+4),b.constants+4*(projection+4),(end-projection-4)*16)))return false;
    return (!x.booleans.count||a.bools==b.bools||!std::memcmp(a.bools+x.booleans.first,b.bools+x.booleans.first,x.booleans.count*sizeof(a.bools[0])))&&
           (!x.integers.count||a.ints==b.ints||!std::memcmp(a.ints+4*x.integers.first,b.ints+4*x.integers.first,x.integers.count*4*sizeof(a.ints[0])));
}

// One instance per uninterrupted replay pass. A new projection drops pose
// equality while retaining known registers. Track exact external prefix writes
// and reset after untracked writes or state restoration.
// Group IDs must identify exact sameCaptured() banks within this frame. Shader
// switches are safe: DEF literals never mutate the API constant register bank.
template<class Bool> class Pass {
    NorthlightReplayConstants::BoundedDirtyRegisters<float,256,4> floatBank_;
    NorthlightReplayConstants::BoundedDirtyRegisters<Bool,16,1> boolBank_;
    NorthlightReplayConstants::BoundedDirtyRegisters<int,16,4> intBank_;
    unsigned group_=0;
    NorthlightShaderConstants::Range range_{};
    bool valid_=false;
public:
    NorthlightReplayConstants::DirtySpan floats,booleans,integers;
    size_t prepared=0,reused=0;
    void reset(){floatBank_.reset();boolBank_.reset();intBank_.reset();valid_=false;}
    void beginProjection(){valid_=false;}
    void recordFloatPrefix(const float* values,unsigned count){floatBank_.update(values,{0,count});valid_=false;}
    const float* desired()const{return floatBank_.data();}
    template<class Replay> bool prepare(const Replay& p,const float* rows,const float* worldMatrix=nullptr,
                                        NorthlightShaderConstants::Range uploadFloats={}){
        floats={};booleans={};integers={};
        auto usage=p.constantUsage;
        if(uploadFloats.count){
            if(!validRange(uploadFloats,256)||!validRange(usage.floats,256)||uploadFloats.first<usage.floats.first||
               uploadFloats.first+uploadFloats.count>usage.floats.first+usage.floats.count)return false;
            usage.floats=uploadFloats;
        }
        if(valid_&&group_==p.constantGroup&&sameRange(range_,usage.floats)){++reused;return true;}
        if(!NorthlightReplayConstants::updateProjectedConstants(floatBank_,p.constants,usage,p.projectionKind,rows,floats,worldMatrix))return false;
        booleans=boolBank_.update(p.bools,p.constantUsage.booleans);
        integers=intBank_.update(p.ints,p.constantUsage.integers);
        group_=p.constantGroup;range_=usage.floats;valid_=true;++prepared;return true;
    }
};
}
