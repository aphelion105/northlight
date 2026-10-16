#pragma once
// Underwater fade: the camera's liquid state (wmo_context.h, proven predictor reads only) as a 0..1 fade for the sun and moon.
// Unknown frames (unread or unproven) keep the last state for HoldMs on the same map, then count as not underwater.
// Pure CPU, no game access.
#include <algorithm>
#include <cstdint>
#include <string>
namespace NorthlightUnderwater {
struct Fade {
    static constexpr std::uint32_t HoldMs=500,FadeMs=300;
    float value=0;            // 0 above water, 1 fully under
    bool underwater=false;    // the state the fade is heading to
    bool changed=false;       // the last update flipped `underwater` (for the log line)
    std::string map;std::uint32_t knownAt=0,last=0;bool started=false,held=false;
    // known: the read is proven this frame; in: the camera is in liquid (only meaningful when known).
    float update(const char* mapName,bool known,bool in,std::uint32_t now){
        changed=false;
        if(!started||map!=mapName){map=mapName;held=false;if(started&&!known)underwater=false;}
        const std::uint32_t dt=started?std::min<std::uint32_t>(now-last,250u):0;
        started=true;last=now;
        bool target=underwater;
        if(known){target=in;knownAt=now;held=true;}
        else if(!held||std::uint32_t(now-knownAt)>HoldMs){target=false;held=false;}
        if(target!=underwater){underwater=target;changed=true;}
        const float step=float(dt)/float(FadeMs);
        value=underwater?std::min(1.f,value+step):std::max(0.f,value-step);
        return value;
    }
    void reset(){*this=Fade{};}
    float scale()const{return 1.f-value;} // multiplies the sun and moon contributions
};
}
