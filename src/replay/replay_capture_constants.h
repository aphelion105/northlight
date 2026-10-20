#pragma once
#include <cassert>
#include "captured_constant_epoch.h"
#include "replay_pose_groups.h"

// Certificates authorize only successful captures from this frame. The caller
// serializes capture with device calls and invalidates certificates on source
// changes. Sharing flattens to immutable storage of an earlier accepted packet.
namespace NorthlightReplayCaptureConstants {
// Block reuse: per-16-register write serials extend the whole-bank epoch hit
// to captures after partial writes (typically material rows between submeshes
// of one skinned model). Clean blocks are copied from, or shared with, the
// previous certified packet; dirty blocks are re-read. Only a byte-identical
// dirty span is shared by pointer. Compile-time switch; false is the 0.3.135 path.
constexpr bool BlockReuse=true;
// Sampled frames only: every Nth block capture re-reads its full float range
// and repairs/counts a mismatch. 0 disables. Diagnostic, never a proof. The
// re-read goes through the device mirror: selfCheckMismatches=0 validates the
// serials against the mirror, not the backend (the mirror audit covers that).
constexpr unsigned SelfCheckInterval=512;
// Validation build: re-read and compare EVERY serial-certified float capture
// (whole-bank epoch hits included), on every frame. Tests only; costs a read.
// Build switch -DNORTHLIGHT_PALETTE_VALIDATE=1 (default in captured_constant_epoch.h).
constexpr bool ValidateEvery=NORTHLIGHT_PALETTE_VALIDATE!=0;
struct Stats {std::uint64_t tests=0,hits=0,bytesAvoided=0,poseFastHits=0,
    blockTests=0,blockShared=0,blockRewrites=0,blockCopies=0,registersReused=0,registersFetched=0,
    poseBlockHits=0,poseBytesAvoided=0,selfChecks=0,selfCheckMismatches=0;
    // Granularity diagnostic: how often each 16-register block was dirty in a
    // block capture. Bone blocks near blockTests mean per-batch palette writes.
    std::uint64_t dirtyBlocks[NorthlightConstantEpoch::FloatBlocks]={};};
// Compare certified floats with a fresh read; repair and uncertify on mismatch.
template<class Replay,class FetchFloats> void verify(Replay& p,FetchFloats& fetchFloats,Stats* stats){
    const auto f=p.constantUsage.floats;float check[1024];
    if(!f.count||!fetchFloats(f.first,f.count,check+4*f.first))return;
    if(stats)++stats->selfChecks;
    if(!std::memcmp(check+4*f.first,p.constants+4*f.first,std::size_t(f.count)*16))return;
    if(stats)++stats->selfCheckMismatches;
    std::memcpy(p.constantStorage+4*f.first,check+4*f.first,std::size_t(f.count)*16);
    p.constants=p.constantStorage;p.constantStamp={};
}
template<class Replay> void reset(Replay& p){
    p.constants=p.constantStorage;p.bools=p.boolStorage;p.ints=p.intStorage;p.constantStamp={};
}
template<class Replay> bool compatible(const Replay& a,const Replay& b){
    const auto& x=a.constantUsage;const auto& y=b.constantUsage;
    if(a.projectionKind!=b.projectionKind||(a.projectionKind!=1&&a.projectionKind!=2)||
       !NorthlightReplayPoses::sameRange(x.floats,y.floats)||
       !NorthlightReplayPoses::sameRange(x.booleans,y.booleans)||
       !NorthlightReplayPoses::sameRange(x.integers,y.integers)||
       !NorthlightReplayPoses::validRange(x.floats,256)||
       !NorthlightReplayPoses::validRange(x.booleans,16)||
       !NorthlightReplayPoses::validRange(x.integers,16))return false;
    const unsigned projection=a.projectionKind==1?4:2;
    return x.floats.first<=projection&&x.floats.first+x.floats.count>=projection+4;
}
inline NorthlightConstantEpoch::Stamp read(void* context,NorthlightConstantEpoch::Reader reader){
    NorthlightConstantEpoch::Stamp stamp;
    if(!reader||!reader(context,stamp))stamp={};
    return stamp;
}
template<class Replay,class Fetch> bool capture(Replay& p,const Replay* previous,
    void* context,NorthlightConstantEpoch::Reader reader,Fetch&& fetch,Stats* stats=nullptr){
    reset(p);
    const auto before=read(context,reader);
    if(stats&&reader)++stats->tests;
    if(previous&&compatible(*previous,p)&&NorthlightConstantEpoch::exact(previous->constantStamp,before)){
        p.constants=previous->constants;p.bools=previous->bools;p.ints=previous->ints;p.constantStamp=before;
        if(stats){++stats->hits;const auto& u=p.constantUsage;
            stats->bytesAvoided+=u.floats.count*16+u.booleans.count*sizeof(p.bools[0])+u.integers.count*16;}
        return true;
    }
    if(!fetch())return false;
    const auto after=read(context,reader);
    if(NorthlightConstantEpoch::exact(before,after))p.constantStamp=after;
    return true;
}
// Same result and bytes as capture(); fetchFloats(first,count,out) reads only
// float registers. Every valid stamp certifies "used ranges equal the device
// bank at that stamp", so clean blocks of two certified packets are equal.
template<class Replay,class Fetch,class FetchFloats> bool captureBlocks(Replay& p,const Replay* previous,
    void* context,NorthlightConstantEpoch::Reader reader,Fetch&& fetch,FetchFloats&& fetchFloats,Stats* stats=nullptr){
    if(!BlockReuse)return capture(p,previous,context,reader,fetch,stats);
    reset(p);
    const auto before=read(context,reader);
    if(stats&&reader)++stats->tests;
    const auto& u=p.constantUsage;
    const bool usable=previous&&compatible(*previous,p);
    if(usable&&NorthlightConstantEpoch::exact(previous->constantStamp,before)){
        p.constants=previous->constants;p.bools=previous->bools;p.ints=previous->ints;p.constantStamp=before;
        if(stats){++stats->hits;stats->bytesAvoided+=u.floats.count*16+u.booleans.count*sizeof(p.bools[0])+u.integers.count*16;}
        if(ValidateEvery)verify(p,fetchFloats,stats);
        return true;
    }
    if(usable&&NorthlightConstantEpoch::banks(previous->constantStamp,before,u.booleans.count,u.integers.count)){
        const unsigned lo=u.floats.first,hi=lo+u.floats.count;unsigned first=lo,last=lo;
        NorthlightConstantEpoch::dirtySpan(previous->constantStamp,before,lo,hi,first,last);
        bool shared=first==last,raced=false;
        if(!shared){
            if(!fetchFloats(first,last-first,p.constantStorage+4*first))return false;
            // Nothing may write between the stamps; if it did, read everything.
            raced=!NorthlightConstantEpoch::exact(before,read(context,reader));
            shared=!raced&&!std::memcmp(p.constantStorage+4*first,previous->constants+4*first,std::size_t(last-first)*16);
        }
        if(!raced){
            if(shared)p.constants=previous->constants;
            else{
                std::memcpy(p.constantStorage+4*lo,previous->constants+4*lo,std::size_t(first-lo)*16);
                std::memcpy(p.constantStorage+4*last,previous->constants+4*last,std::size_t(hi-last)*16);
            }
            p.bools=previous->bools;p.ints=previous->ints;p.constantStamp=before;
            if(stats){++stats->blockTests;stats->blockShared+=shared;stats->blockRewrites+=shared&&first!=last;stats->blockCopies+=!shared;
                stats->registersReused+=u.floats.count-(last-first);stats->registersFetched+=last-first;
                stats->bytesAvoided+=(u.floats.count-(last-first))*16+u.booleans.count*sizeof(p.bools[0])+u.integers.count*16;
                for(unsigned block=first/NorthlightConstantEpoch::BlockRegisters;first<last&&block*NorthlightConstantEpoch::BlockRegisters<last;++block)
                    stats->dirtyBlocks[block]+=previous->constantStamp.floatBlock[block]!=before.floatBlock[block];}
            if(ValidateEvery||(stats&&SelfCheckInterval&&stats->blockTests%SelfCheckInterval==0))verify(p,fetchFloats,stats);
            return true;
        }
    }
    if(!fetch())return false;
    const auto after=read(context,reader);
    if(NorthlightConstantEpoch::exact(before,after))p.constantStamp=after;
    return true;
}
// 0.3.180 (C0): the self-check apart from the diagnostic Stats. On a self-check frame every block hit
// advances serial, as that frame's Stats::blockTests did in 0.3.179, and every SelfCheckInterval-th is
// verified; verify() counts into stats. The caller clears serial together with that object.
struct SelfCheck {std::uint64_t serial=0;Stats* stats=nullptr;};
// 0.3.180 (C1): the epoch source of the templated captureBlocks(). valid(): a stamp taken now would
// certify; live(): the serial words, read in place (valid() decides, not live().valid); snapshot(): the
// certifying copy. ReaderSource is the 0.3.179 Reader and context (tests); ClockSource reads the
// mirror's Clock in place. Precondition: the caller owns the mirror's gate from valid() to the last
// snapshot() (capture runs inside the game draw's guard); validation builds assert it.
struct ReaderSource {
    void* context=nullptr;NorthlightConstantEpoch::Reader reader=nullptr;
    mutable NorthlightConstantEpoch::Stamp current;
    bool attached()const{return reader!=nullptr;}
    bool valid()const{current=read(context,reader);return current.valid;}
    const NorthlightConstantEpoch::Stamp& live()const{return current;}
    NorthlightConstantEpoch::Stamp snapshot()const{return read(context,reader);}
};
template<class Mirror> struct ClockSource {
    const NorthlightConstantEpoch::Clock* clock=nullptr;const Mirror* mirror=nullptr;
    bool attached()const{return clock!=nullptr;}
    bool valid()const{
        if constexpr(ValidateEvery)assert(!clock||mirror->heldByThisThread());
        return clock&&mirror->active()&&!clock->overflow();
    }
    const NorthlightConstantEpoch::Stamp& live()const{return clock->live();}
    NorthlightConstantEpoch::Stamp snapshot()const{return clock->stamp(mirror->active());}
};
// 0.3.180 (C1+C2): the Reader overload's result, bytes and Stats, comparing the stamp in place and
// copying it once, only to certify. No post-fetch re-read: the caller holds the gate from before to
// after and a fetch never advances the Clock, so after==before and nothing races the block fetch.
// Validation builds keep both re-reads, as assertions. selfCheck/check: C0 above.
template<class Replay,class Source,class Fetch,class FetchFloats> bool captureBlocks(Replay& p,const Replay* previous,
    const Source& source,Fetch&& fetch,FetchFloats&& fetchFloats,Stats* stats,bool selfCheck,SelfCheck& check){
    namespace E=NorthlightConstantEpoch;
    reset(p);
    const bool valid=source.valid();
    [[maybe_unused]] const E::Stamp before=ValidateEvery&&valid?source.snapshot():E::Stamp{};
    static const E::Stamp detached{}; /* a source without a clock: never bind live() through null */
    const E::Stamp& now=source.attached()?source.live():detached;
    if(stats&&source.attached())++stats->tests;
    const auto& u=p.constantUsage;
    const bool usable=valid&&previous&&previous->constantStamp.valid&&compatible(*previous,p);
    if(usable&&E::sameWords(previous->constantStamp,now)){
        p.constants=previous->constants;p.bools=previous->bools;p.ints=previous->ints;p.constantStamp=source.snapshot();
        if(stats){++stats->hits;stats->bytesAvoided+=u.floats.count*16+u.booleans.count*sizeof(p.bools[0])+u.integers.count*16;}
        if(ValidateEvery)verify(p,fetchFloats,check.stats);
        return true;
    }
    if(BlockReuse&&usable&&E::sameBanks(previous->constantStamp,now,u.booleans.count,u.integers.count)){
        const unsigned lo=u.floats.first,hi=lo+u.floats.count;unsigned first=lo,last=lo;
        E::dirtySpan(previous->constantStamp,now,lo,hi,first,last);
        bool shared=first==last;
        if(!shared){
            if(!fetchFloats(first,last-first,p.constantStorage+4*first))return false;
            if constexpr(ValidateEvery)assert(E::exact(before,source.snapshot()));
            shared=!std::memcmp(p.constantStorage+4*first,previous->constants+4*first,std::size_t(last-first)*16);
        }
        if(shared)p.constants=previous->constants;
        else{
            std::memcpy(p.constantStorage+4*lo,previous->constants+4*lo,std::size_t(first-lo)*16);
            std::memcpy(p.constantStorage+4*last,previous->constants+4*last,std::size_t(hi-last)*16);
        }
        p.bools=previous->bools;p.ints=previous->ints;p.constantStamp=source.snapshot();
        if(stats){++stats->blockTests;stats->blockShared+=shared;stats->blockRewrites+=shared&&first!=last;stats->blockCopies+=!shared;
            stats->registersReused+=u.floats.count-(last-first);stats->registersFetched+=last-first;
            stats->bytesAvoided+=(u.floats.count-(last-first))*16+u.booleans.count*sizeof(p.bools[0])+u.integers.count*16;
            for(unsigned block=first/E::BlockRegisters;first<last&&block*E::BlockRegisters<last;++block)
                stats->dirtyBlocks[block]+=previous->constantStamp.floatBlock[block]!=now.floatBlock[block];}
        if(selfCheck)++check.serial;
        if(ValidateEvery||(selfCheck&&SelfCheckInterval&&check.serial%SelfCheckInterval==0))verify(p,fetchFloats,check.stats);
        return true;
    }
    if(!fetch())return false;
    if constexpr(ValidateEvery)assert(source.valid()==valid&&(!valid||E::exact(before,source.snapshot())));
    if(valid)p.constantStamp=source.snapshot();
    return true;
}
template<class Replay> bool samePose(const Replay& a,const Replay& b,Stats* stats=nullptr){
    if(compatible(a,b)&&NorthlightConstantEpoch::pose(a.constantStamp,b.constantStamp,b.projectionKind)){
        if(stats)++stats->poseFastHits;
        return true;
    }
    if(BlockReuse&&compatible(a,b)){
        // Identical storage compares equal; otherwise only a span of blocks
        // written between two certified captures can differ. Projection rows
        // are excluded exactly as in sameCaptured().
        const auto& u=b.constantUsage;const unsigned lo=u.floats.first,hi=lo+u.floats.count;
        const std::uint64_t full=u.floats.count*16+u.booleans.count*sizeof(b.bools[0])+u.integers.count*16;
        if(a.constants==b.constants&&a.bools==b.bools&&a.ints==b.ints){
            if(stats){++stats->poseBlockHits;stats->poseBytesAvoided+=full;}
            return true;
        }
        if(NorthlightConstantEpoch::banks(a.constantStamp,b.constantStamp,u.booleans.count,u.integers.count)){
            unsigned first=lo,last=lo;NorthlightConstantEpoch::dirtySpan(a.constantStamp,b.constantStamp,lo,hi,first,last);
            const unsigned projection=b.projectionKind==1?4:2;
            const unsigned endA=last<projection?last:projection,beginB=first>projection+4?first:projection+4;
            if(stats){++stats->poseBlockHits;stats->poseBytesAvoided+=full-std::uint64_t(last-first)*16;}
            return (first>=endA||!std::memcmp(a.constants+4*first,b.constants+4*first,std::size_t(endA-first)*16))&&
                   (beginB>=last||!std::memcmp(a.constants+4*beginB,b.constants+4*beginB,std::size_t(last-beginB)*16));
        }
    }
    return NorthlightReplayPoses::sameCaptured(a,b);
}
}
