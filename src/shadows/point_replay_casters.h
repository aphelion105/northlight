#pragma once
#include "point_light_shadow.h"
#include <vector>

namespace NorthlightPointShadow {
// Owns the validated boxes for one ordered list of captured casters. Each
// light retains its own sphere and face projection in FaceClip.
class ReplayCasters {
    struct Caster {
        FaceClip::PreparedBounds bounds;
        size_t index;
        bool known,skinned;
    };
    std::vector<Caster> casters_;
    std::vector<FaceClip::PreparedBounds> groups_;
    bool ready_=false;

public:
    struct Source {size_t index;const float (&low)[3];const float (&high)[3];bool valid,skinned;};
    struct Counts {unsigned culled=0,unboundedSkinned=0,unboundedRigid=0;};

    void invalidate(){ready_=false;}
    void release(){
        std::vector<Caster>().swap(casters_);
        std::vector<FaceClip::PreparedBounds>().swap(groups_);
        invalidate();
    }

    template<class Read> void prepare(size_t count,Read read){
        if(ready_&&casters_.size()==count)return;
        invalidate();
        casters_.clear();
        groups_.clear();
        casters_.reserve(count);
        const bool grouped=ReplayGroup::useful(count,read);
        if(grouped)groups_.reserve((count+ReplayGroup::Width-1)/ReplayGroup::Width);
        for(size_t first=0;first<count;first+=ReplayGroup::Width){
            const size_t end=std::min(first+ReplayGroup::Width,count);
            ReplayGroup group;
            for(size_t n=first;n<end;++n){
                const Source source=read(n);
                casters_.push_back({FaceClip::PreparedBounds(source.low,source.high,source.valid),
                                    source.index,source.valid,source.skinned});
                if(grouped)group.include(source.low,source.high,source.valid,n==first);
            }
            if(grouped)groups_.emplace_back(group.low,group.high,group.valid);
        }
        ready_=true;
    }

    template<class Append> Counts classify(const FaceClip& clip,unsigned faces,Append append)const{
        Counts counts;
        const unsigned faceCount=unsigned(__builtin_popcount(faces));
        for(size_t first=0;first<casters_.size();first+=ReplayGroup::Width){
            const size_t end=std::min(first+ReplayGroup::Width,casters_.size());
            const unsigned groupFaces=groups_.empty()?faces:clip.mask(groups_[first/ReplayGroup::Width],faces);
            if(!groupFaces){counts.culled+=unsigned(end-first)*faceCount;continue;}
            for(size_t i=first;i<end;++i){
                const auto& caster=casters_[i];
                const unsigned mask=clip.mask(caster.bounds,groupFaces);
                if(!mask){counts.culled+=faceCount;continue;}
                if(!caster.known)(caster.skinned?counts.unboundedSkinned:counts.unboundedRigid)+=unsigned(__builtin_popcount(mask));
                append(caster.index,mask);
            }
        }
        return counts;
    }
};
}
