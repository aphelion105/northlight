#pragma once
// Scene data in the installed AMD64 WowClient build (source 8a1b491e7f66).
// RVAs are relative to the executable's loaded image. Never reuse the x86 layout.
#include <windows.h>
#include <cstdint>
#include "snapshot_store.h"
#include "stream_hooks.h"

namespace NorthlightX64World {
inline constexpr uintptr_t WorldFrame=0x1aeb640, MapName=0x1b56ee0, MapLoaded=0x1b3e4f8;
inline constexpr uintptr_t GxDevice=0xedb5c0, D3dVtable=0xaab038, WorldFrameVtable=0xafcdd8, CameraVtable=0xaecd48;
inline constexpr uintptr_t CameraField=0x8008, CameraPose=0x10, CameraTransform=0xb8;
inline constexpr uintptr_t ViewSlot=0x1b98, ViewStack=0x1ba0, Sky=0x1b73560;
inline constexpr unsigned SkyDirection=0x1c0, SkyDirect=0x1cc, SkyAmbient=0x1d0, SkySize=0x1d4;

inline bool read(uintptr_t address,void* output,size_t size){
    SIZE_T copied=0;
    return address>=0x10000&&size<=UINTPTR_MAX-address&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&copied)&&copied==size;
}
template<class T> bool read(uintptr_t address,T& value){return read(address,&value,sizeof value);}

inline bool readFrame(uintptr_t address,void* output,size_t size){
    using NorthlightStream::GameSnapshot;
    auto* snapshot=NorthlightStream::activeSnapshot;
    if(!snapshot)
        return GetCurrentThreadId()!=NorthlightStream::replayTid.load(std::memory_order_relaxed)&&read(address,output,size);
    if(snapshot->mode==GameSnapshot::Mode::Recording){
        const bool ok=read(address,output,size);
        snapshot->record(address,output,size,ok);
        return ok;
    }
    // A miss on the replay thread must not use a later game frame's state.
    const auto result=snapshot->serve(address,output,size);
    if(result==GameSnapshot::Serve::Miss)++snapshot->misses;
    else ++snapshot->hits;
    return result==GameSnapshot::Serve::Ok;
}
template<class T> bool readFrame(uintptr_t address,T& value){return readFrame(address,&value,sizeof value);}

struct CodeProof {uintptr_t rva;unsigned size;uint64_t hash;};
template<class Proofs> bool verifyCode(uintptr_t base,const Proofs& proofs){
    if(!base)return false;
    for(const auto& proof:proofs){
        unsigned char bytes[80];
        if(proof.size>sizeof bytes||!read(base+proof.rva,bytes,proof.size))return false;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<proof.size;++i)hash=(hash^bytes[i])*UINT64_C(1099511628211);
        if(hash!=proof.hash)return false;
    }
    return true;
}

inline uintptr_t imageBase(){
    static const uintptr_t verified=[]()->uintptr_t{
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||
           dos.e_lfanew>0x1000||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
           nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.FileHeader.TimeDateStamp!=0x6b05ba65||
           nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt.OptionalHeader.SizeOfImage!=0x1edf000)return 0;
        // Map load, world-camera pose and attachment, Gx view getter, and the
        // outdoor light block used by the native renderer.
        constexpr CodeProof proofs[]={
            {0x683753,22,UINT64_C(0xcc7eb061f4a53f34)}, {0x6837f7,19,UINT64_C(0x65c674dd259a0c29)},
            {0x53be38,39,UINT64_C(0x871731a238dd5290)}, {0x4d3b80,74,UINT64_C(0x57360f3e1ddda931)},
            {0x4d85a0,72,UINT64_C(0x01bdbe253221fbce)}, {0x108490,32,UINT64_C(0xd14b5e07e9c59fb3)},
            {0x6bfda0,8,UINT64_C(0x5bd6859dfa09e5ce)}, {0x6b668a,14,UINT64_C(0x9e573de76661f7c8)},
            {0x6b6748,31,UINT64_C(0x25c5e9d36e22e115)}, {0x6b662d,16,UINT64_C(0xe28285b53f127ba5)},
        };
        return verifyCode(base,proofs)?base:0;
    }();
    return verified;
}

struct Scene {
    char map[64]={};
    float pose[12]={}; // world position, forward, side, up
    bool transformed=false;
};

inline bool readScene(Scene& out){
    const auto base=imageBase();
    if(!base)return false;
    Scene next;
    uint32_t loaded=0;
    uintptr_t frame=0,frameType=0,cam=0,camType=0,transform=0,after=0;
    if(!readFrame(base+MapLoaded,loaded)||loaded!=1||!readFrame(base+WorldFrame,frame)||
       frame<0x10000||frame>UINTPTR_MAX-CameraField-sizeof(cam)||
       !readFrame(frame,frameType)||frameType!=base+WorldFrameVtable||
       !readFrame(frame+CameraField,cam)||cam<0x10000||cam>UINTPTR_MAX-CameraTransform-sizeof(transform)||
       !readFrame(cam,camType)||camType!=base+CameraVtable||
       !readFrame(cam+CameraPose,next.pose)||!readFrame(cam+CameraTransform,transform)||
       !readFrame(base+MapName,next.map)||!readFrame(base+WorldFrame,after)||after!=frame||
       !readFrame(frame+CameraField,after)||after!=cam||!readFrame(base+MapLoaded,loaded)||loaded!=1)return false;
    next.transformed=transform!=0;
    out=next;return true;
}

inline bool readView(float (&rotation)[16]){
    const auto base=imageBase();
    if(!base)return false;
    uintptr_t device=0,type=0,after=0;
    uint32_t slot=0,slotAfter=0;
    return readFrame(base+GxDevice,device)&&device>=0x10000&&device<=UINTPTR_MAX-ViewStack-4*sizeof(rotation)&&
        readFrame(device,type)&&type==base+D3dVtable&&readFrame(device+ViewSlot,slot)&&slot<4&&
        readFrame(device+ViewStack+sizeof(rotation)*slot,rotation)&&readFrame(base+GxDevice,after)&&after==device&&
        readFrame(device+ViewSlot,slotAfter)&&slotAfter==slot;
}

inline bool readSky(unsigned char (&sky)[SkySize]){
    const auto base=imageBase();
    unsigned char after[SkySize];
    return base&&readFrame(base+Sky,sky)&&readFrame(base+Sky,after)&&!std::memcmp(sky,after,sizeof sky);
}
}
