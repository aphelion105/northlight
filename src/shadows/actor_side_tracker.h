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
// Geometry identity of a replay (mesh range and declaration, stable while the game keeps the buffers) plus its palette root
// cell when it has one (the per-instance anchor; floor to 1 yd so animation noise does not change it).
inline std::uint64_t key(const void* stream,const void* decl,unsigned offset,unsigned start,unsigned count,int base,bool rooted,const float* root){
    std::uint64_t h=14695981039346656037ull;
    h=mix(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(stream)));h=mix(h,std::uint64_t(reinterpret_cast<std::uintptr_t>(decl)));
    h=mix(h,offset);h=mix(h,start);h=mix(h,count);h=mix(h,std::uint64_t(std::int64_t(base)));
    if(rooted)for(unsigned k=0;k<3;++k)h=mix(h,std::uint64_t(std::int64_t(root[k]>=0?root[k]:root[k]-1.f)));
    return h;
}
class Tracker {
    std::vector<std::pair<std::uint64_t,unsigned char>> now_,prev_;
    unsigned self_=SelfNone,lastSelf_=SelfNone,invalid_=0;bool havePrevious_=false;
public:
    unsigned sideFlips=0,sideFlipFrames=0,frames=0,selfFlips=0; /* per log window */
    unsigned invalidBoundsNear=0,tracked=0,selfSide=SelfNone;   /* the last frame */
    void begin(){now_.clear();self_=SelfNone;invalid_=0;}
    void note(std::uint64_t k,bool actor,bool self=false){now_.emplace_back(k,(unsigned char)(actor?1:0));if(self)self_|=actor?SelfActor:SelfNear;}
    void invalidNear(){++invalid_;}
    void endFrame(){
        std::sort(now_.begin(),now_.end());unsigned flips=0;
        if(havePrevious_){std::size_t i=0,j=0;
            while(i<now_.size()&&j<prev_.size()){
                if(now_[i].first<prev_[j].first){++i;continue;}
                if(prev_[j].first<now_[i].first){++j;continue;}
                const std::uint64_t k=now_[i].first;unsigned an=0,nn=0,ap=0,np=0;
                for(;i<now_.size()&&now_[i].first==k;++i)(now_[i].second?an:nn)+=1;
                for(;j<prev_.size()&&prev_[j].first==k;++j)(prev_[j].second?ap:np)+=1;
                flips+=std::min(an>ap?an-ap:0u,np)+std::min(ap>an?ap-an:0u,nn);}}
        sideFlips+=flips;sideFlipFrames+=flips>0;++frames;
        if(self_!=SelfNone){selfFlips+=lastSelf_!=SelfNone&&lastSelf_!=self_;lastSelf_=self_;}
        selfSide=self_;invalidBoundsNear=invalid_;tracked=unsigned(now_.size());
        prev_.swap(now_);havePrevious_=true;
    }
    void resetWindow(){sideFlips=sideFlipFrames=frames=selfFlips=0;}
    void reset(){prev_.clear();now_.clear();havePrevious_=false;lastSelf_=SelfNone;resetWindow();}
};
}
