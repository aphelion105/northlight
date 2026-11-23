#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#ifdef NORTHLIGHT_REPLAY_STAND
#include <charconv>
#include <vector>
#endif

// Opt-in parity clock for visual state and the recorded replay checkpoint.
// Normal work budgets and profiling use real clocks. Start above zero.
namespace NorthlightFrameParityClock {
inline constexpr unsigned Hz=60;
inline bool enabled(){
    static const bool fixed=[] {
        const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_CLOCK");
        return value&&std::strcmp(value,"60")==0;
    }();
    return fixed;
}
inline bool alignFarShadowPresents(){
    static const bool aligned=[] {
        const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_ALIGN_FAR_SHADOWS");
        return enabled()&&value&&std::strcmp(value,"1")==0;
    }();
    return aligned;
}
inline bool fixedStaticShadowStream(){
    static const bool fixed=[] {
        const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_STATIC_STREAM");
        return enabled()&&value&&std::strcmp(value,"1")==0;
    }();
    return fixed;
}
inline bool fixedShadowBounds(){
    static const bool fixed=[] {
        const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_SHADOW_BOUNDS");
        return enabled()&&value&&std::strcmp(value,"1")==0;
    }();
    return fixed;
}
inline bool fixedGiPublication(){
    static const bool fixed=[] {
        const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_GI_PUBLICATION");
        return enabled()&&value&&std::strcmp(value,"1")==0;
    }();
    return fixed;
}
inline bool auditLightHistoryInterval(){
    static const bool requested=[] {
        const char* value=std::getenv("NORTHLIGHT_FRAME_PARITY_LIGHT_HISTORY_BETWEEN");
        return value&&std::strcmp(value,"1")==0;
    }();
    return requested;
}
inline bool fixedProfileWork(){
#ifdef NORTHLIGHT_REPLAY_STAND
    static const bool enabled=[] {
        const char* value=std::getenv("NORTHLIGHT_RETRACE_PROFILE_FIXED_WORK");
        return value&&std::strcmp(value,"1")==0;
    }();
    return enabled;
#else
    return false;
#endif
}
inline bool fixedGeometryCheckpoint(){
    return enabled()||fixedProfileWork();
}
inline constexpr bool farShadowBoundary(unsigned present,unsigned interval){
    return interval>1&&present%interval==0;
}
inline constexpr std::uint32_t milliseconds(unsigned frame){
    return std::uint32_t(1000+std::uint64_t(frame)*1000/Hz);
}
inline constexpr std::int64_t counter(unsigned frame,std::int64_t frequency){
    return std::int64_t(frame/Hz+1)*frequency+std::int64_t(frame%Hz)*frequency/Hz;
}
#ifdef NORTHLIGHT_REPLAY_STAND
struct WorldCheckpoint {unsigned first=0,scene=0,geometry=0,end=0;};
inline std::vector<WorldCheckpoint> parseWorldCheckpoints(const char* text){
    std::vector<WorldCheckpoint> result;
    if(!text)return result;
    const char* end=text+std::strlen(text);
    while(text!=end){
        unsigned values[4]{};
        for(unsigned i=0;i<4;++i){
            const auto parsed=std::from_chars(text,end,values[i]);
            if(parsed.ec!=std::errc{})return {};
            text=parsed.ptr;
            if(i<3){if(text==end||*text!=':')return {};++text;}
        }
        const WorldCheckpoint point{values[0],values[1],values[2],values[3]};
        if(point.first>=point.end||point.geometry<point.first||point.geometry>point.end||
           (point.scene&&(point.scene<point.first||point.scene>point.geometry))||
           (!result.empty()&&point.first<result.back().end))return {};
        result.push_back(point);
        if(text!=end){if(*text!=';'||++text==end)return {};}
    }
    return result;
}
inline WorldCheckpoint replayWorldCheckpoint(unsigned present){
    static const auto checkpoints=parseWorldCheckpoints(std::getenv("NORTHLIGHT_RETRACE_WORLD_CHECKPOINTS"));
    for(const auto& point:checkpoints){
        if(present<point.first)break;
        if(present<point.end)return point;
    }
    return {};
}
#endif
}
