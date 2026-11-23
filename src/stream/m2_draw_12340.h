#pragma once
#include "m2_draw_section.h"
#include "gx_win32_stack_12340.h"
#include <atomic>
#include <cstring>

namespace NorthlightM2Draw {
using NorthlightGx12340::indexPoolRange;
inline bool verifiedClient=false;
inline bool verifiedGroupedClient=false;
inline std::atomic<bool> requested{false};
struct ProducerStats {
    unsigned candidates=0,ordinary=0,matched=0,mismatch=0,paletteChecks=0,paletteMismatch=0;
    std::uint64_t decodeNs=0;
};

struct GroupedStats {
    unsigned shaderDraws=0,softwareDraws=0,alphaTested=0,matched=0,multiInstance=0,instances=0;
    unsigned rangeMismatch=0,paletteUnavailable=0,paletteMismatch=0;
    std::uint64_t triangles=0;
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
    const auto bones=half(section+12),combo=half(section+14);
    const unsigned firstIndex=half(section+8),indices=half(section+10);
    unsigned expectedBase=0;
    std::uint64_t nativeStart=0;
    if(!indexPoolRange(word,firstIndex,expectedBase,nativeStart))return false;
    if(!bones||bones>MaxBones||word(batch-16)!=3||word(batch-12)!=firstIndex||word(batch-8)!=indices||
       indices%3||expectedBase>0x7fffffffu||nativeStart>0xffffffffu){
        ++stats.mismatch;
        return false;
    }
    const Range range{static_cast<std::int32_t>(expectedBase),half(section+4),half(section+6),
                      static_cast<std::uint32_t>(nativeStart),indices/3u};
    if(!(range==Range{base,minimum,vertices,start,triangles})){
        ++stats.mismatch;
        return false;
    }
    const bool paletteAudited=audit&&bank.allKnown(PaletteFirst,3u*bones);
    if(paletteAudited){
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
                std::memcpy(&sent,bank.at(PaletteFirst+3*b+row)+col,sizeof sent);
                if(sent!=word(matrix+16*col+4*row)){++stats.paletteMismatch;return false;}
            }
        }
    }
    out={model,section,material,bones,combo,range,paletteAudited,
         paletteAudited?paletteHash(bank.at(PaletteFirst),bones):0};
    ++stats.matched;
    return true;
}

// Type 2 submissions use consecutive slices of the same ordered doodad group.
// Check the current Gx batch and compare each transposed matrix against the
// live producer's constant bank while the dispatcher still owns the poses.
template<class Word,class Half,class Bank>
void auditGrouped(std::uintptr_t frame,std::uintptr_t top,Word word,Half half,const Range& draw,
                  bool bindings,bool alphaTest,const Bank& bank,GroupedStats& stats){
    const auto onStack=[&](std::uintptr_t at,unsigned size){return at>=frame&&at<top&&size<=top-at&&!(at&3);};
    if(!onStack(frame,8)||word(frame+4)!=0x6a36d1)return;
    const auto gx=word(frame);
    if(gx<=frame||!onStack(gx-4,12))return;
    const auto caller=word(gx+4);
    if(caller!=0x820d50&&caller!=0x820f1b)return;
    const auto batch=word(gx);
    if(batch<=gx||batch-gx<=0x30||!onStack(batch-0x30,0x40)||word(batch+4)!=0x823a9f)return;
    if(caller==0x820f1b){++stats.softwareDraws;return;}
    ++stats.shaderDraws;
    stats.alphaTested+=alphaTest;
    stats.triangles+=draw.triangles;
    const auto ctx=word(gx-4);
    if(ctx<=batch||!onStack(ctx,0x9c)){++stats.rangeMismatch;return;}

    const auto group=word(ctx+0x50),section=word(ctx+0x90),shared=word(ctx+0x68),data=word(ctx+0x48);
    const unsigned first=word(batch-4),count=word(batch-8),total=word(batch-0x14);
    if(!group||!section||!shared||!data||word(group)!=2||word(group+0x2c)!=section||
       !count||count>MaxBones||first>=total||count>total-first||word(group+0x1c)!=total){
        ++stats.rangeMismatch;return;
    }
    const auto pool=word(shared+0x170);
    const unsigned firstIndex=half(section+8),indices=half(section+10)*count;
    const unsigned vertices=pool?half(pool+4)*count:0;
    unsigned base=0;
    std::uint64_t start=0;
    if(!bindings||!vertices||vertices>65536||!indices||indices%3||
       word(batch-0x30)!=3||word(batch-0x2c)!=firstIndex||word(batch-0x28)!=indices||
       word(batch-0x24)!=(vertices-1)*65536u||!indexPoolRange(word,firstIndex,base,start)||
       base>0x7fffffffu||start>0xffffffffu||
       !(draw==Range{static_cast<std::int32_t>(base),0,vertices,static_cast<unsigned>(start),indices/3})){
        ++stats.rangeMismatch;return;
    }
    const unsigned bones=half(section+12),combo=half(section+14);
    if(!bones||bones*count>MaxBones||!bank.allKnown(PaletteFirst,3*bones*count)){
        ++stats.paletteUnavailable;return;
    }
    const auto selection=word(data+0x7c),elements=word(batch+8),order=word(batch+12);
    if(!selection||!elements||!order||combo+bones>word(data+0x78)||
       std::uint64_t(order)+4ull*(first+count)>0x100000000ull){++stats.paletteMismatch;return;}
    const unsigned boneCount=word(data+0x2c);
    unsigned reg=PaletteFirst;
    for(unsigned instance=0;instance<count;++instance){
        const auto index=word(order+4*(first+instance));
        const auto element=std::uint64_t(elements)+68ull*index;
        if(element>0xfffffff8u){++stats.paletteMismatch;return;}
        const auto model=word(static_cast<std::uintptr_t>(element)+4);
        const auto palette=model?word(model+0x98):0;
        if(!palette){++stats.paletteMismatch;return;}
        for(unsigned bone=0;bone<bones;++bone){
            const auto selected=half(selection+2*(combo+bone));
            if(selected>=boneCount){++stats.paletteMismatch;return;}
            const auto matrix=palette+64u*selected;
            for(unsigned row=0;row<3;++row,++reg)for(unsigned col=0;col<4;++col){
                std::uint32_t sent;
                std::memcpy(&sent,bank.at(reg)+col,sizeof sent);
                if(sent!=word(matrix+16*col+4*row)){++stats.paletteMismatch;return;}
            }
        }
    }
    ++stats.matched;
    stats.multiInstance+=count>1;
    stats.instances+=count;
}

#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
using NorthlightGx12340::LiveMemory;
#endif

template<class Bank> bool readSection(const void* entryFrame,int base,unsigned minimum,unsigned vertices,
                                      unsigned start,unsigned triangles,const Bank& bank,bool audit,
                                      Section& out,ProducerStats& stats){
#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
    if(!verifiedClient)return false;
    return decodeSection(reinterpret_cast<std::uintptr_t>(entryFrame),LiveMemory::stackTop(),LiveMemory::word,
                         LiveMemory::half,base,minimum,vertices,start,triangles,bank,audit,out,stats);
#else
    (void)entryFrame;(void)base;(void)minimum;(void)vertices;(void)start;(void)triangles;(void)bank;(void)audit;(void)out;(void)stats;
    return false;
#endif
}

template<class Bank> void observeGrouped(const void* entryFrame,const Range& draw,bool bindings,bool alphaTest,
                                        const Bank& bank,GroupedStats& stats){
#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
    if(verifiedGroupedClient)
        auditGrouped(reinterpret_cast<std::uintptr_t>(entryFrame),LiveMemory::stackTop(),LiveMemory::word,
                     LiveMemory::half,draw,bindings,alphaTest,bank,stats);
#else
    (void)entryFrame;(void)draw;(void)bindings;(void)alphaTest;(void)bank;(void)stats;
#endif
}
}
