#pragma once
// Task 34: which device state slots the spell layer's replay Model is known to equal. A bit is set by the layer when it has READ the slot from the device at a record and the
// Model took (or already had) that value; EVERY change of the device slot clears it: the device mirror's setters (device_mirror.h) clear the slot they set, invalidate() (state
// block Apply/Reset/RawScope entry and exit/vendor states/failed Sets/disable) clears all, and the layer clears all when it resets its Model (a drain). A record then examines only
// the slots of its read set whose bit is clear. A slot nobody can report cheaply and exactly (scissor, clip planes, shaders, declaration, int/bool constants, the vertex texture
// samplers) has no bit and is examined every time. Written and read on the thread that executes the device (the mirror's gate serializes the rest); `epoch` changes with every
// clear, so a record that a foreign write interleaved with does not mark anything synced.
#include <cstdint>
#include <cstring>
struct SyncedSlots{
    uint64_t rs[4]={},vsF[4]={},psF[4]={};  // render states by D3DRS id, shader float constant rows
    uint16_t ss[16]={};                      // sampler states of stage 0..15: bit per D3DSAMP type
    uint16_t tex=0;                          // texture bindings of stage 0..15
    bool vp=false;                           // viewport
    uint32_t epoch=0;
    void clearAll(){std::memset(rs,0,sizeof rs);std::memset(vsF,0,sizeof vsF);std::memset(psF,0,sizeof psF);std::memset(ss,0,sizeof ss);tex=0;vp=false;++epoch;}
    void renderState(unsigned id){if(id<256)rs[id>>6]&=~(1ull<<(id&63));++epoch;}
    void sampler(unsigned stage,unsigned type){if(stage<16&&type<16)ss[stage]=uint16_t(ss[stage]&~(1u<<type));++epoch;}
    void texture(unsigned stage){if(stage<16)tex=uint16_t(tex&~(1u<<stage));++epoch;}
    void viewport(){vp=false;++epoch;}
    static void clearRows(uint64_t* m,unsigned size,unsigned start,unsigned count){for(unsigned i=start;i<size&&i-start<count;++i)m[i>>6]&=~(1ull<<(i&63));}
    void vsFloat(unsigned start,unsigned count){clearRows(vsF,256,start,count);++epoch;}
    void psFloat(unsigned start,unsigned count){clearRows(psF,224,start,count);++epoch;}
};
