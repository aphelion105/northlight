#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <new>
#include "snapshot_store.h"
#include "stream_hooks.h"
#include "world_context_x64_layout.h"

namespace NorthlightX64World {
inline constexpr uintptr_t MapLoaded=0x1b3e4f8; // Tideglass's independently verified legacy layout.

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

struct WorldImage {uintptr_t base=0;Layout layout{};};

inline const WorldImage& worldImage(){
    static const WorldImage verified=[]()->WorldImage{
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||
           dos.e_lfanew>0x1000||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
           nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
           nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64))return {};
        const uintptr_t sectionTable=dos.e_lfanew+sizeof nt;
        const auto imageSize=nt.OptionalHeader.SizeOfImage;
        const auto tableSize=uintptr_t(nt.FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER);
        if(imageSize>UINTPTR_MAX-base||nt.OptionalHeader.SizeOfHeaders>imageSize||
           !ImageRange{0,nt.OptionalHeader.SizeOfHeaders}.contains(sectionTable,tableSize))return {};
        ImageRange text{},data{},readOnly{};
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){
            IMAGE_SECTION_HEADER section{};
            if(!read(base+sectionTable+i*sizeof section,section))return {};
            const auto flags=section.Characteristics;
            const ImageRange range{section.VirtualAddress,section.Misc.VirtualSize};
            if(!range.size||range.rva<nt.OptionalHeader.SizeOfHeaders||
               !ImageRange{0,imageSize}.contains(range.rva,range.size))continue;
            if(!std::memcmp(section.Name,".text\0\0",8)&&(flags&IMAGE_SCN_MEM_EXECUTE))text=range;
            if(!std::memcmp(section.Name,".data\0\0",8)&&(flags&IMAGE_SCN_MEM_WRITE))data=range;
            if(!std::memcmp(section.Name,".rdata\0",8)&&(flags&IMAGE_SCN_MEM_READ)&&!(flags&IMAGE_SCN_MEM_WRITE))readOnly=range;
        }
        if(!text.size||!data.size||!readOnly.size)return {};
        std::unique_ptr<char[]> code(new(std::nothrow) char[text.size]);
        Layout layout{};
        if(!code||!read(base+text.rva,code.get(),text.size)||
           !resolveLayout({code.get(),text.size},text.rva,data,readOnly,layout))return {};
        return {base,layout};
    }();
    return verified;
}

// Tideglass and native shadow control still have their own fixed RVAs and code proofs.
inline uintptr_t legacyImageBase(){
    static const uintptr_t legacy=[]()->uintptr_t{
        const auto base=worldImage().base;
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        const auto legacy=base&&read(base,dos)&&read(base+dos.e_lfanew,nt)&&
                          nt.FileHeader.TimeDateStamp==0x6b05ba65&&nt.OptionalHeader.SizeOfImage==0x1edf000;
        return legacy?base:0;
    }();
    return legacy;
}

struct Scene {
    char map[64]={};
    float pose[12]={}; // world position, forward, side, up
    bool transformed=false;
};

inline bool readScene(Scene& out){
    const auto& [base,layout]=worldImage();
    if(!base)return false;
    Scene next;
    uint32_t loaded=0;
    uintptr_t frame=0,frameType=0,cam=0,camType=0,transform=0,after=0;
    if(!readFrame(base+layout.mapLoaded,loaded)||loaded!=1||!readFrame(base+layout.worldFrame,frame)||
       frame<0x10000||frame>UINTPTR_MAX-CameraField-sizeof(cam)||
       !readFrame(frame,frameType)||frameType!=base+layout.worldFrameVtable||
       !readFrame(frame+CameraField,cam)||cam<0x10000||cam>UINTPTR_MAX-CameraTransform-sizeof(transform)||
       !readFrame(cam,camType)||camType!=base+layout.cameraVtable||
       !readFrame(cam+CameraPose,next.pose)||!readFrame(cam+CameraTransform,transform)||
       !readFrame(base+layout.mapName,next.map)||!readFrame(base+layout.worldFrame,after)||after!=frame||
       !readFrame(frame+CameraField,after)||after!=cam||!readFrame(base+layout.mapLoaded,loaded)||loaded!=1)return false;
    next.transformed=transform!=0;
    out=next;return true;
}

inline bool readView(float (&rotation)[16]){
    const auto& [base,layout]=worldImage();
    if(!base)return false;
    uintptr_t device=0,type=0,after=0;
    uint32_t slot=0,slotAfter=0;
    return base&&readFrame(base+layout.gxDevice,device)&&device>=0x10000&&device<=UINTPTR_MAX-ViewStack-4*sizeof(rotation)&&
        readFrame(device,type)&&type==base+layout.d3dVtable&&readFrame(device+ViewSlot,slot)&&slot<4&&
        readFrame(device+ViewStack+sizeof(rotation)*slot,rotation)&&readFrame(base+layout.gxDevice,after)&&after==device&&
        readFrame(device+ViewSlot,slotAfter)&&slotAfter==slot;
}

inline bool readSky(unsigned char (&sky)[SkySize]){
    const auto& [base,layout]=worldImage();
    unsigned char after[SkySize];
    return base&&readFrame(base+layout.sky,sky)&&readFrame(base+layout.sky,after)&&!std::memcmp(sky,after,sizeof sky);
}
}
