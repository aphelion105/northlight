#pragma once
#include "blp_mip_guard.h"
#include "native_more_lights_code.h"
#include <array>
#include <algorithm>

namespace NorthlightNativeMoreLights {
using namespace NorthlightNativeMoreLightsCode;
inline bool available=false;
inline void put(void* p,std::uint32_t v){std::memcpy(p,&v,4);}
inline void original(const Hook& h,unsigned char* out){
    std::fill_n(out,h.size,0x90);
    if(h.address==0x834fc7){const unsigned char v[]={0x0f,0x8a,0xbf,0,0,0};std::memcpy(out,v,sizeof v);}
    else if(h.address==0x834fcd){const unsigned char v[]={0x83,0xfe,4,0x72,3,0x83,0xee,1};std::memcpy(out,v,sizeof v);}
    else if(h.address==0x83510e){const unsigned char v[]={0x8b,0x4e,0x14,0x83,0xe1,0x60};std::memcpy(out,v,sizeof v);}
    else if(h.address==0x7d0292){const unsigned char v[]={0x8b,0x0d,0x88,0xdf,0xc5,0};std::memcpy(out,v,sizeof v);}
    else{out[0]=0xe8;put(out+1,std::uint32_t(0x872900-(h.address+5)));}
}
inline void replacement(const Hook& h,std::uintptr_t code,unsigned char* out){
    std::fill_n(out,h.size,0x90);unsigned n=1;out[0]=h.opcode;
    if(h.opcode==0x8a){out[0]=0x0f;out[1]=0x8a;n=2;}
    put(out+n,std::uint32_t(code+h.entry-LogicalCode-(h.address+n+4)));
}
inline void program(unsigned char* out,std::uintptr_t code,std::uintptr_t data){
    std::memcpy(out,Program,sizeof Program);
    for(const auto& r:Relocations)put(out+r.offset,r.kind==Data?std::uint32_t(data+r.target):std::uint32_t(r.target-(code+r.offset+4)));
}
// Validate the upstream or Northlight thunk without assuming its allocation VA.
inline bool existing(std::uintptr_t& code){
    unsigned char first[6];if(!NorthlightBlpMipGuard::read(Hooks[0].address,first,sizeof first)||first[0]!=0x0f||first[1]!=0x8a)return false;
    std::int32_t jump;std::memcpy(&jump,first+2,4);code=Hooks[0].address+6+jump-(Hooks[0].entry-LogicalCode);
    std::uint32_t data=0;unsigned char saved[sizeof Program],expected[sizeof Program];
    if(!NorthlightBlpMipGuard::read(code+Relocations[0].offset,&data,4)||!NorthlightBlpMipGuard::read(code,saved,sizeof saved))return false;
    program(expected,code,data);if(std::memcmp(saved,expected,sizeof saved))return false;
    for(const auto& h:Hooks){unsigned char actual[8],wanted[8];replacement(h,code,wanted);
        if(!NorthlightBlpMipGuard::read(h.address,actual,h.size)||std::memcmp(actual,wanted,h.size))return false;}
    return true;
}
inline bool verified(bool patched){
    for(const auto& p:Proofs){unsigned char bytes[1024];if(p.size>sizeof bytes||!NorthlightBlpMipGuard::read(p.address,bytes,p.size))return false;
        if(patched)for(const auto& h:Hooks)if(h.address>=p.address&&h.address+h.size<=p.address+p.size)original(h,bytes+(h.address-p.address));
        if(!NorthlightBlpMipGuard::digest(bytes,p.size,p.hash))return false;}
    return true;
}
inline const char* install(bool enabled=true,HMODULE image=GetModuleHandleW(nullptr)
#if defined(__i386__) || defined(_M_IX86)
    ,NorthlightBlpMipGuard::MemoryOps ops={}
#endif
){
    if(!enabled)return "disabled";
#if defined(__i386__) || defined(_M_IX86)
    using namespace NorthlightBlpMipGuard;
    if(reinterpret_cast<std::uintptr_t>(image)!=0x400000)return "unsupported-image-base";
    std::uintptr_t prior=0;
    if(existing(prior)){if(!verified(true))return "client-layout-mismatch";available=true;return "already-active";}
    if(!verified(false))return "client-layout-mismatch";
    SYSTEM_INFO sys={};GetSystemInfo(&sys);const size_t page=sys.dwPageSize;
    if(page<DataSize||page<sizeof Program)return "unsupported-page-size";
    auto* storage=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!storage)return "allocation-failed";
    auto* ring=VirtualAlloc(nullptr,RingSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ring){VirtualFree(storage,0,MEM_RELEASE);return "allocation-failed";}
    auto* code=storage+page;program(code,reinterpret_cast<std::uintptr_t>(code),reinterpret_cast<std::uintptr_t>(storage));
    put(storage,std::uint32_t(reinterpret_cast<std::uintptr_t>(ring)));
    for(unsigned i=0;i<4;++i)put(storage+0xc0+4*i,0x3f800000);
    const auto discard=[&]{VirtualFree(ring,0,MEM_RELEASE);VirtualFree(storage,0,MEM_RELEASE);};
    DWORD old=0;
    if(!ops.protect(code,page,PAGE_EXECUTE_READ,&old)||!ops.flush(GetCurrentProcess(),code,sizeof Program)){discard();return "thunk-publication-failed";}
    std::array<std::array<unsigned char,8>,std::size(Hooks)> before{},after{};
    for(unsigned i=0;i<std::size(Hooks);++i){const auto& h=Hooks[i];original(h,before[i].data());replacement(h,reinterpret_cast<std::uintptr_t>(code),after[i].data());}
    std::uintptr_t low=Hooks[0].address,high=low;
    for(const auto& h:Hooks){low=std::min(low,h.address);high=std::max(high,h.address+h.size);}
    std::array<std::array<unsigned char,1024>,std::size(Proofs)> proofs{};
    for(unsigned i=0;i<std::size(Proofs);++i)if(!read(Proofs[i].address,proofs[i].data(),Proofs[i].size)||!digest(proofs[i].data(),Proofs[i].size,Proofs[i].hash)){discard();return "client-changed-concurrently";}
    // No hashing, allocation, logger or C++ static locks while other threads pause.
    PausedThreads threads;if(const auto* error=threads.pause(low,high-low)){discard();return error;}
    for(unsigned i=0;i<std::size(Proofs);++i){unsigned char current[1024];
        if(!read(Proofs[i].address,current,Proofs[i].size)||std::memcmp(current,proofs[i].data(),Proofs[i].size)){discard();return "client-changed-concurrently";}}
    struct Page {std::uintptr_t address=0;DWORD protection=0;};std::array<Page,std::size(Hooks)> pages{};unsigned count=0,opened=0;
    for(const auto& h:Hooks){const auto address=h.address-h.address%page;bool found=false;for(unsigned i=0;i<count;++i)found|=pages[i].address==address;if(!found)pages[count++].address=address;}
    for(;opened<count;++opened)if(!ops.protect(reinterpret_cast<void*>(pages[opened].address),page,PAGE_EXECUTE_READWRITE,&pages[opened].protection))break;
    const auto restore=[&]{bool ok=true;for(unsigned i=0;i<opened;++i){DWORD ignored;ok=(ops.protect(reinterpret_cast<void*>(pages[i].address),page,pages[i].protection,&ignored)!=FALSE)&&ok;}return ok;};
    if(opened!=count){restore();discard();return "code-protection-failed";}
    for(unsigned i=0;i<std::size(Hooks);++i)std::memcpy(reinterpret_cast<void*>(Hooks[i].address),after[i].data(),Hooks[i].size);
    const auto flush=[&]{bool ok=true;for(const auto& h:Hooks)ok=(ops.flush(GetCurrentProcess(),reinterpret_cast<void*>(h.address),h.size)!=FALSE)&&ok;return ok;};
    const bool flushed=flush();if(flushed&&restore()){available=true;return "active";}
    // Some pages may already be RX after a failed protection restore.
    bool writable=true;for(unsigned i=0;i<count;++i){DWORD ignored;writable=(ops.protect(reinterpret_cast<void*>(pages[i].address),page,PAGE_EXECUTE_READWRITE,&ignored)!=FALSE)&&writable;}
    if(!writable){available=true;return "active-protection-restore-failed";} // Published native-only thunks must stay allocated.
    for(unsigned i=0;i<std::size(Hooks);++i)std::memcpy(reinterpret_cast<void*>(Hooks[i].address),before[i].data(),Hooks[i].size);
    const bool undone=flush(),protectedAgain=restore();discard();
    return !undone?"rollback-cache-failed":!protectedAgain?"rollback-protection-failed":flushed?"code-protection-restore-failed":"code-cache-flush-failed";
#else
    (void)image;return "requires-x86";
#endif
}
} // namespace NorthlightNativeMoreLights
