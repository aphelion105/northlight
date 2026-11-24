#pragma once
#ifndef NORTHLIGHT_DEVICE_MIRROR_TEST_API
#include <d3d9.h>
#endif

namespace NorthlightParticleDraw {
inline constexpr bool supported(D3DPRIMITIVETYPE type,UINT primitives){
    return primitives&&type>=D3DPT_TRIANGLELIST&&type<=D3DPT_TRIANGLEFAN;
}
inline constexpr bool sourceBlend(DWORD value){return value==D3DBLEND_ONE||value==D3DBLEND_SRCALPHA;}
inline constexpr bool destinationBlend(DWORD value){return value==D3DBLEND_ONE||value==D3DBLEND_INVSRCALPHA;}
inline constexpr unsigned blendSlot(D3DRENDERSTATETYPE type){
    switch(type){
    case D3DRS_ALPHABLENDENABLE:return 1;
    case D3DRS_SRCBLEND:return 2;
    case D3DRS_DESTBLEND:return 4;
    default:return 0;
    }
}

// Owned by the device mirror. An unknown slot contributes no rejection.
struct State {
    enum class Shaders { Dirty, Reject, Inspect };
    Shaders shaders=Shaders::Dirty;
    unsigned rejectedBlend=0,writes=0,samplers=0;
    void invalidate(){rejectedBlend=0;shaders=Shaders::Dirty;}
    void blend(unsigned slot,DWORD value,bool known){
        const bool allowed=slot==1?value!=0:slot==2?sourceBlend(value):destinationBlend(value);
        rejectedBlend=(rejectedBlend&~slot)|((known&&!allowed)?slot:0);
    }
    struct Write {
        State& state;bool relevant,shaders;
        Write(State& state,bool relevant,bool shaders=false):state(state),relevant(relevant),shaders(shaders){if(relevant)++state.writes;}
        ~Write(){if(shaders)state.shaders=Shaders::Dirty;if(relevant)--state.writes;}
    };
};
}
