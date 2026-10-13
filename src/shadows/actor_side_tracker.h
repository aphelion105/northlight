#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Diagnostics only (no behaviour change): which shadow map a skinned caster goes to, frame to frame. A replay inside the
// actor map's inset is drawn into the actor map, any other one into the near cascade; the bounds that decide this are
// rebuilt every frame, so a caster can alternate. The tracker records every skinned replay of one source's cascade-0 pass
// with a stable key (see key()), compares each frame with the previous one and counts the replays that changed side.
// Clones sharing one key (same mesh, same 1 yd root cell) are compared by count: the net number that moved to a side.
namespace NorthlightActorSide {
enum Self : unsigned char {SelfNone=0,SelfActor=1,SelfNear=2,SelfMixed=3};
inline const char* name(unsigned s){return s==SelfActor?"actor":s==SelfNear?"near":s==SelfMixed?"mixed":"none";}
inline std::uint64_t mix(std::uint64_t h,std::uint64_t n){return (h^n)*1099511628211ull;}
// Frame-independent identity of a replay: its immutable snapshot (shared mesh owner), declaration and index count, plus its palette
// root cell when it has one (the per-instance anchor; floor to 1 yd so animation noise does not change it). The ring slot offset,
// start and base move with the frame's layout and are not part of it. A replay without a snapshot (Unsupported) has no such
// identity: the caller counts it with unkeyed() instead.
inline std::uint64_t key(const void* snapshot,const void* decl,unsigned count,bool rooted,const float* root){
    std::uint64_t h=14695981039346656037ull;
    h=mix(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(snapshot)));h=mix(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(decl)));h=mix(h,count);
    if(rooted)for(unsigned k=0;k<3;++k)h=mix(h,std::uint64_t(std::int64_t(root[k]>=0?root[k]:root[k]-1.f)));
    return h;
}
class Tracker {
    std::vector<std::pair<std::uint64_t,unsigned char>> now_,prev_;
    unsigned self_=SelfNone,lastSelf_=SelfNone,invalid_=0,unsupported_=0,unkeyed_=0,unrootedMissing_=0,selfUnrooted_=0;bool havePrevious_=false,selfMissing_=false;
public:
    unsigned sideFlips=0,sideFlipFrames=0,sideMatched=0,frames=0,selfFlips=0; /* per log window; sideMatched: keys present in both frames (0 flips of 0 matched = not compared) */
    unsigned sideFlipsRooted=0,sideFlipsUnrooted=0,invalidBoundsNearSum=0,unrootedNoBoundsSum=0,selfNoBoundsFrames=0; /* per log window; the two flip counts add up to sideFlips */
    unsigned invalidBoundsNear=0,unsupportedNow=0,unkeyedNow=0,tracked=0,selfSide=SelfNone,unrootedNoBounds=0,selfUnrooted=0;   /* the last frame */
    void begin(){now_.clear();self_=SelfNone;invalid_=unsupported_=unkeyed_=unrootedMissing_=selfUnrooted_=0;selfMissing_=false;}
    // rooted: the replay has a palette root (its key carries the root cell). noBounds: it has no valid bounds (it can only be on the near side).
    void note(std::uint64_t k,bool actor,bool self=false,bool rooted=true,bool noBounds=false){
        now_.emplace_back(k,(unsigned char)((actor?1:0)|(rooted?2:0)));
        if(self){self_|=actor?SelfActor:SelfNear;selfUnrooted_+=!rooted;selfMissing_=selfMissing_||(noBounds&&!actor);}
        unrootedMissing_+=!rooted&&noBounds;}
    void invalidNear(){++invalid_;}
    void unsupportedNear(){++unsupported_;}
    void unkeyed(){++unkeyed_;}
    void endFrame(){
        std::sort(now_.begin(),now_.end());unsigned flips=0,matched=0,rootedFlips=0,unrootedFlips=0;
        if(havePrevious_){std::size_t i=0,j=0;
            while(i<now_.size()&&j<prev_.size()){
                if(now_[i].first<prev_[j].first){++i;continue;}
                if(prev_[j].first<now_[i].first){++j;continue;}
                const std::uint64_t k=now_[i].first;unsigned an=0,nn=0,ap=0,np=0;++matched;const bool rooted=(now_[i].second&2)!=0;
                for(;i<now_.size()&&now_[i].first==k;++i)((now_[i].second&1)?an:nn)+=1;
                for(;j<prev_.size()&&prev_[j].first==k;++j)((prev_[j].second&1)?ap:np)+=1;
                const unsigned f=std::min(an>ap?an-ap:0u,np)+std::min(ap>an?ap-an:0u,nn);
                flips+=f;(rooted?rootedFlips:unrootedFlips)+=f;}}
        sideFlipsRooted+=rootedFlips;sideFlipsUnrooted+=unrootedFlips;invalidBoundsNearSum+=invalid_;unrootedNoBoundsSum+=unrootedMissing_;selfNoBoundsFrames+=selfMissing_;
        unrootedNoBounds=unrootedMissing_;selfUnrooted=selfUnrooted_;
        sideFlips+=flips;sideMatched+=matched;sideFlipFrames+=flips>0;++frames;
        if(self_!=SelfNone){selfFlips+=lastSelf_!=SelfNone&&lastSelf_!=self_;lastSelf_=self_;}
        selfSide=self_;invalidBoundsNear=invalid_;unsupportedNow=unsupported_;unkeyedNow=unkeyed_;tracked=unsigned(now_.size());
        prev_.swap(now_);havePrevious_=true;
    }
    void resetWindow(){sideFlips=sideFlipFrames=sideMatched=frames=selfFlips=sideFlipsRooted=sideFlipsUnrooted=invalidBoundsNearSum=unrootedNoBoundsSum=selfNoBoundsFrames=0;}
    void reset(){prev_.clear();now_.clear();havePrevious_=false;lastSelf_=SelfNone;resetWindow();}
};
}
