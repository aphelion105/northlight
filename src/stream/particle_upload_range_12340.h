#pragma once
#include "gx_win32_stack_12340.h"
#include <cstdint>

namespace NorthlightGx12340 {
inline bool verifiedParticleUpload=false;
inline constexpr unsigned ParticleVertexBytes=24,ParticleUploadReservation=2048*ParticleVertexBytes;

// Gx locks the reservation, then the particle batch passes the used byte
// count to its typed Gx unlock. D3D9's Unlock has no size argument.
template<class Word>
bool decodeParticleUpload(std::uintptr_t frame,std::uintptr_t top,Word word,unsigned& used){
    const auto within=[&](std::uintptr_t at,unsigned bytes){return !(at&3)&&at>=frame&&at<top&&bytes<=top-at;};
    if(!within(frame,8)||word(frame+4)!=0x68face)return false;
    const auto d3dUnlock=word(frame);
    if(d3dUnlock<=frame||!within(d3dUnlock,12)||word(d3dUnlock+4)!=0x68fafc)return false;
    const auto gxUnlock=word(d3dUnlock);
    if(gxUnlock<=d3dUnlock||!within(gxUnlock,16))return false;
    const auto reservation=word(gxUnlock+8),bytes=word(gxUnlock+12);
    if(!reservation||reservation!=word(d3dUnlock+8)||bytes>ParticleUploadReservation||bytes%(4*ParticleVertexBytes))return false;
    const auto caller=word(gxUnlock+4);
    if(caller==0x6c4cbf){if(bytes)return false;} // the unused reservation at the end of the batch
    else{
        if(caller!=0x6c490c||!bytes)return false;
        const auto submit=word(gxUnlock);
        if(submit<=gxUnlock||!within(submit,8))return false;
        const auto parent=word(submit+4);
        if(parent!=0x6c4c80&&parent!=0x6c4c41)return false; // tail and full quad batches
    }
    used=bytes;
    return true;
}

inline bool particleUploadBytes(const void* frame,unsigned& used){
#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto at=reinterpret_cast<std::uintptr_t>(frame);
    return verifiedParticleUpload&&at>=reinterpret_cast<std::uintptr_t>(tib->StackLimit)&&
        decodeParticleUpload(at,LiveMemory::stackTop(),LiveMemory::word,used);
#else
    (void)frame;(void)used;return false;
#endif
}
}
