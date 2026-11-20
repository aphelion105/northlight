#pragma once
#include "m2_draw_section.h"
#include <atomic>
#include <cstring>

namespace NorthlightM2Draw {
inline bool verifiedClient=false;
inline std::atomic<bool> requested{false};
struct ProducerStats {
    unsigned candidates=0,ordinary=0,matched=0,mismatch=0,paletteChecks=0,paletteMismatch=0;
};

// This is the ordinary shader batch in Wow.exe 3.3.5.12340. The dispatcher
// owns its context on the stack. Copy values while that invocation is live.
template<class Word,class Half,class Bank>
bool decodeSection(std::uintptr_t frame,std::uintptr_t top,Word word,Half half,int base,unsigned minimum,
                   unsigned vertices,unsigned start,unsigned triangles,const Bank& bank,bool audit,
                   Section& out,ProducerStats& stats){
    const auto onStack=[&](std::uintptr_t at,unsigned bytes){
        return at>=frame&&at<top&&bytes<=top-at&&!(at&3);
    };
    if(!onStack(frame,8)||word(frame+4)!=0x6a36d1)return false;
    ++stats.candidates;
    const auto gx=word(frame);
    if(gx<=frame||!onStack(gx,8)||word(gx+4)!=0x820610)return false;
    const auto batch=word(gx);
    if(batch<=gx||batch-gx<=20||!onStack(batch-20,28)||word(batch+4)!=0x823a88)return false;
    const auto ctx=word(batch-20); // ESI saved at entry to CM2SceneRender::DrawBatch
    if(ctx<=batch||!onStack(ctx,0x9c))return false;
    ++stats.ordinary;
    const auto section=word(ctx+0x90),model=word(ctx+0x60),material=word(ctx+0x98);
    if(word(ctx+0x4c)!=0||!section||!model||!material||half(material+2)!=0)return false;

    // Gx converts a section-local range into offsets within its bound pools.
    const auto device=word(0xc5df88);
    if(!device)return false;
    const auto ib=word(device+0x28bc);
    if(!ib)return false;
    unsigned expectedBase=0;
    if(!word(device+0x224)){
        const auto vb=word(device+0x2870);
        if(!vb||!word(vb+12))return false;
        expectedBase=word(vb+24)/word(vb+12);
    }
    const auto bones=half(section+12),combo=half(section+14);
    const unsigned firstIndex=half(section+8),indices=half(section+10);
    if(!bones||bones>75||word(batch-16)!=3||word(batch-12)!=firstIndex||word(batch-8)!=indices||
       half(section+4)!=minimum||half(section+6)!=vertices||unsigned(base)!=expectedBase||
       start!=(word(ib+24)>>1)+firstIndex||std::uint64_t(triangles)*3!=indices){
        ++stats.mismatch;
        return false;
    }
    if(audit&&bank.allKnown(31,3u*bones)){
        ++stats.paletteChecks;
        const auto data=word(ctx+0x48),palette=word(model+0x98);
        const auto selection=data?word(data+0x7c):0;
        if(!palette||!selection||unsigned(combo)+bones>word(data+0x78)){
            ++stats.paletteMismatch;
            return false;
        }
        const auto boneCount=word(data+0x2c);
        for(unsigned b=0;b<bones;++b){
            const auto index=half(selection+2*(combo+b));
            if(index>=boneCount){++stats.paletteMismatch;return false;}
            const auto matrix=palette+64u*index;
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col){
                std::uint32_t sent;
                std::memcpy(&sent,bank.at(31+3*b+row)+col,sizeof sent);
                if(sent!=word(matrix+16*col+4*row)){++stats.paletteMismatch;return false;}
            }
        }
    }
    out={model,section,material,bones,combo};
    ++stats.matched;
    return true;
}

template<class Bank> bool readSection(const void* entryFrame,int base,unsigned minimum,unsigned vertices,
                                      unsigned start,unsigned triangles,const Bank& bank,bool audit,
                                      Section& out,ProducerStats& stats){
#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
    if(!verifiedClient)return false;
    const auto top=reinterpret_cast<std::uintptr_t>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    const auto word=[](std::uintptr_t at){std::uint32_t v;std::memcpy(&v,reinterpret_cast<const void*>(at),4);return v;};
    const auto half=[](std::uintptr_t at){std::uint16_t v;std::memcpy(&v,reinterpret_cast<const void*>(at),2);return v;};
    return decodeSection(reinterpret_cast<std::uintptr_t>(entryFrame),top,word,half,base,minimum,vertices,
                         start,triangles,bank,audit,out,stats);
#else
    (void)entryFrame;(void)base;(void)minimum;(void)vertices;(void)start;(void)triangles;(void)bank;(void)audit;(void)out;(void)stats;
    return false;
#endif
}
}
