#pragma once
#include "adt_draw_bounds.h"
#include "gx_indexed_12340.h"
#include "gx_win32_stack_12340.h"
#include "shader_tags.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorthlightWorldBatch12340 {
enum class Kind { None, Wmo, Adt };
struct Batch {
    Kind kind=Kind::None;
    std::uintptr_t owner=0,record=0;
    float minimum[3]{},maximum[3]{};
    bool spatial=false;
};
enum class AdtRejection : unsigned { None, Draw, Sources, Mcvt, Finite, Origin, Height, Count };
struct AdtHeightMismatch {
    float x=0,y=0,altitude=0,low=0,high=0,minOffset=0,maxOffset=0;
};
struct Stats {
    unsigned wmo=0,wmoRanges=0,wmoBounds=0,adt=0,adtRanges=0,adtOrigins=0,adtBounds=0,unattributed=0;
    unsigned adtRejected[unsigned(AdtRejection::Count)]{};
    AdtHeightMismatch adtHeightMismatch;
};
inline bool verifiedWmo=false,verifiedAdt=false,verifiedAdtBounds=false;
using NorthlightGx12340::Range;

template<class Word,class Half>
Batch decode(std::uintptr_t frame,std::uintptr_t top,Word word,Half half,const Range& draw,
             unsigned worldTags,bool bindings,bool wmoAbi,bool adtAbi){
    const auto stack=[&](std::uintptr_t at,unsigned size){return at>=frame&&at<top&&size<=top-at&&!(at&3);};
    if(!stack(frame,8)||word(frame+4)!=0x6a36d1)return {};
    const auto gx=word(frame);
    if(gx<=frame||!stack(gx-12,20))return {};
    const auto submit=word(gx);
    if(submit<=gx||!stack(submit,16))return {};
    const auto site=word(gx+4),parent=word(submit+4);
    const bool regular=site==0x7a96d4||site==0x7a9bb3;
    const bool ranged=site==0x7ab9d5,combined=site==0x7aba91;
    const bool wmo=wmoAbi&&(worldTags&NorthlightShaderTags::kWmo)&&
        ((regular&&parent==0x7aca21)||((ranged||combined)&&parent==0x7bc368));
    const bool adt=adtAbi&&(worldTags&NorthlightShaderTags::kTerrain)&&site==0x7d2f82&&parent==0x798aa4;
    if(!wmo&&!adt)return {};
    Batch result;
    result.kind=wmo?Kind::Wmo:Kind::Adt;
    if(!bindings)return result;

    std::uintptr_t owner=0,record=0,input=0;
    unsigned first=0,low=0,high=0;
    std::uint64_t indices=0;
    if(wmo){
        const auto slot=ranged||combined?word(submit+12):0;
        owner=regular?word(submit+8):slot?word(slot):0;
        if(!owner)return result;
        if(combined){
            if(word(gx-4)!=owner)return result;
            record=owner+0x19c;
            first=word(record);
            const auto last=word(record+4);
            if(last<first)return result;
            indices=std::uint64_t(last)-first+1;
            low=half(record+8);high=half(record+10);
            input=submit-0x3c;
        }else{
            const auto saved=word(gx-(ranged?12:4));
            if(saved<(ranged?16u:1u))return result;
            record=saved-(ranged?16:0);
            const auto list=word(owner+0xf8),count=word(owner+0x16c);
            if(!list||record<list||(record-list)%24||(record-list)/24>=count)return result;
            first=word(record+12);indices=half(record+16);low=half(record+18);high=half(record+20);
            input=submit-(ranged?0x2c:site==0x7a96d4?0x38:0x78);
        }
        const auto prepared=[&](std::uintptr_t address){
            return address>gx&&stack(address,16)&&word(address)==3&&word(address+4)==first&&
                word(address+8)==indices&&half(address+12)==low&&half(address+14)==high;
        };
        // The two-pass path at 0x7a977d jumps to the shared call tail with a
        // different stack record. Both layouts must agree with the same MOBA.
        if(!prepared(input)){
            if(site!=0x7a9bb3||!prepared(submit-0x58))return result;
            input=submit-0x58;
        }
    }else{
        owner=word(gx-4);
        if(!owner)return result;
        input=record=owner+0x90;
        first=word(record+4);indices=word(record+8);low=half(record+12);high=half(record+14);
    }
    unsigned base=0;
    std::uint64_t start=0;
    if(word(input)!=3||high<low||!indices||indices%3||
       !NorthlightGx12340::indexPoolRange(word,first,base,start)||base>0x7fffffffu||start>0xffffffffu||
       !(draw==Range{static_cast<std::int32_t>(base),low,high-low+1,static_cast<unsigned>(start),static_cast<unsigned>(indices/3)}))
        return result;

    result.owner=owner;
    result.record=record;
    // WMO uses this group-local AABB in its native culler. The terrain
    // owner supplies its placement origin. Height extrema need separate proof.
    bool extent=false;
    for(unsigned axis=0;axis<3;++axis){
        auto bits=word(owner+(wmo?0x34:0x18)+axis*4);
        std::memcpy(&result.minimum[axis],&bits,sizeof bits);
        if(!std::isfinite(result.minimum[axis]))return result;
        if(wmo){
            bits=word(owner+0x40+axis*4);
            std::memcpy(&result.maximum[axis],&bits,sizeof bits);
            if(!std::isfinite(result.maximum[axis])||result.minimum[axis]>result.maximum[axis])return result;
            extent|=result.minimum[axis]<result.maximum[axis];
        }
    }
    result.spatial=adt||extent;
    return result;
}

inline bool heightMatches(float native,float offset,float altitude){
    const double sum=double(offset)+altitude;
    const float rounded=static_cast<float>(sum);
    if(!std::isfinite(rounded))return false;
    if(native==rounded)return true;
    if(sum==rounded)return false;
    // The client computes this in x87 and stores a float. Only the two floats
    // bracketing the sum are valid if x87 and SSE use different rounding modes.
    const float infinity=std::numeric_limits<float>::infinity();
    return native==std::nextafter(rounded,sum>rounded?infinity:-infinity);
}

template<class Read>
AdtRejection terrainBounds(const Batch& batch,Read read,NorthlightAdtBounds::Evidence& bounds,
                           AdtHeightMismatch* mismatch=nullptr){
    if(batch.kind!=Kind::Adt||!batch.record||!batch.spatial)return AdtRejection::Draw;
    const auto copy=[&](std::uintptr_t address,auto& destination){
        return address>=0x10000&&address<=0x100000000ull-sizeof destination&&
            read(address,&destination,sizeof destination);
    };
    const auto asFloat=[](std::uint32_t bits){float value;std::memcpy(&value,&bits,sizeof value);return value;};

    // Both source chunks feed the exact vertex and index preparation of this batch.
    std::uint32_t sources[2];
    if(!copy(batch.owner+0x10,sources)||!sources[0]||sources[0]==sources[1]||
       ((sources[0]|sources[1])&3))return AdtRejection::Sources;
    for(unsigned part=0;part<2&&sources[part];++part){
        std::uint32_t chunk[0x120/4],heights[2+145];
        // The MPQ subchunk is a byte range. The client supports unaligned MCVT payloads.
        if(!copy(sources[part],chunk)||chunk[0x11c/4]<8||!copy(chunk[0x11c/4]-8,heights)||
           heights[0]!=0x4d435654||heights[1]!=145*sizeof(float))return AdtRejection::Mcvt;
        const float altitude=asFloat(chunk[0x84/4]);
        if(!std::isfinite(altitude))return AdtRejection::Finite;
        float lowest=asFloat(heights[2]),highest=lowest;
        for(unsigned i=2;i<2+145;++i){
            const float height=asFloat(heights[i]);
            if(!std::isfinite(height))return AdtRejection::Finite;
            lowest=std::min(lowest,height);highest=std::max(highest,height);
        }
        for(unsigned axis=0;axis<3;++axis){
            const float low=asFloat(chunk[0x4c/4+axis]),high=asFloat(chunk[0x58/4+axis]);
            const float origin=asFloat(chunk[0x7c/4+axis]);
            if(!std::isfinite(low)||!std::isfinite(high)||low>high||!std::isfinite(origin))return AdtRejection::Finite;
            if(!part&&std::fabs(origin-batch.minimum[axis])>.03f)return AdtRejection::Origin;
            // The client's cached box may enclose a wider height range than
            // the currently held MCVT. An inward edge must be a valid rounding.
            if(axis==2&&((double(low)>double(lowest)+altitude&&!heightMatches(low,lowest,altitude))||
                         (double(high)<double(highest)+altitude&&!heightMatches(high,highest,altitude)))){
                if(mismatch)*mismatch={asFloat(chunk[0x7c/4]),asFloat(chunk[0x80/4]),altitude,low,high,lowest,highest};
                return AdtRejection::Height;
            }
            bounds.low[axis]=part?std::min(bounds.low[axis],low):low;
            bounds.high[axis]=part?std::max(bounds.high[axis],high):high;
        }
    }
    return AdtRejection::None;
}

inline bool observe(const void* entryFrame,const Range& range,unsigned tags,bool bindings,Stats& stats,
                    NorthlightAdtBounds::Evidence& bounds){
#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
    if(!tags||(!verifiedAdt&&!verifiedWmo))return false;
    using NorthlightGx12340::LiveMemory;
    const auto proof=decode(reinterpret_cast<std::uintptr_t>(entryFrame),LiveMemory::stackTop(),LiveMemory::word,
                            LiveMemory::half,range,tags,bindings,verifiedWmo,verifiedAdt);
    if(proof.kind==Kind::Wmo){++stats.wmo;stats.wmoRanges+=proof.record!=0;stats.wmoBounds+=proof.spatial;}
    else if(proof.kind==Kind::Adt){
        ++stats.adt;stats.adtRanges+=proof.record!=0;stats.adtOrigins+=proof.spatial;
        if(verifiedAdtBounds){
            const auto result=terrainBounds(proof,[](std::uintptr_t address,void* data,std::size_t size){
                SIZE_T read=0;
                return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),data,size,&read)&&read==size;
            },bounds,stats.adtRejected[unsigned(AdtRejection::Height)]?nullptr:&stats.adtHeightMismatch);
            if(result==AdtRejection::None){++stats.adtBounds;return true;}
            ++stats.adtRejected[unsigned(result)];
        }
    }
    else ++stats.unattributed;
#else
    (void)entryFrame;(void)range;(void)tags;(void)bindings;(void)stats;(void)bounds;
#endif
    return false;
}
}
