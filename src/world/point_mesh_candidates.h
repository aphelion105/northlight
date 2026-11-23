#pragma once
#include "point_shadow_clip.h"
#include "world_mesh_pages.h"
#include <cstring>

namespace NorthlightPointMesh {
// Mesh publication owns these immutable bounds. A cube only supplies its
// sphere and planes. Oversized meshes and allocation failures use raw bounds.
class PreparedBounds {
    using Box=NorthlightPointShadow::FaceClip::PreparedBounds;
    using Batch=NorthlightWorldMeshPages::Batch;
    std::vector<Box> boxes_;
    uint64_t generation_=UINT64_MAX;

public:
    void release(){std::vector<Box>().swap(boxes_);generation_=UINT64_MAX;}
    void prepare(uint64_t generation,const std::vector<Batch>& parents,const std::vector<Batch>& children){
        if(generation_==generation)return;
        generation_=generation;
        boxes_.clear();
        constexpr size_t limit=(2u<<20)/sizeof(Box);
        if(parents.size()>limit||children.size()>limit-parents.size())return;
        try{boxes_.reserve(parents.size()+children.size());}catch(const std::bad_alloc&){return;}
        for(const auto* source:{&parents,&children})for(const auto& batch:*source)
            boxes_.emplace_back(&batch.boundsLow.x,&batch.boundsHigh.x,true);
    }
    size_t size()const{return boxes_.size();}
    unsigned mask(size_t index,const Batch& batch,const NorthlightPointShadow::FaceClip& clip,unsigned faces)const{
        if(index<boxes_.size())return clip.mask(boxes_[index],faces);
        return clip.mask(&batch.boundsLow.x,&batch.boundsHigh.x,true,faces);
    }
};

// PointTree marks conservative leaf candidates, including unknown bounds.
// Enumerate its byte mask in mesh order so draw merging sees the same neighbors.
template<class Visit>
void visitNearby(const unsigned char* marks,size_t count,Visit visit){
    for(size_t i=0;i<count;){
        if(!marks[i]){
            const auto* next=static_cast<const unsigned char*>(std::memchr(marks+i,1,count-i));
            if(!next)break;
            i=size_t(next-marks);
        }
        visit(i++);
    }
}

// Update only the static entries of the face mask shared with terrain. Empty
// child ranges must still be cleared: draw merging consults gaps between hits.
template<class Classify,class Append>
size_t classifyStatic(const std::vector<NorthlightWorldMeshPages::Batch>& parents,
                      const std::vector<NorthlightWorldMeshPages::Batch>& children,
                      const std::vector<NorthlightWorldMeshPages::PointRange>& ranges,
                      const std::vector<uint32_t>* staticParents,const unsigned char* nearby,unsigned faces,
                      std::vector<unsigned char>& masks,Classify classify,Append append){
    using Range=NorthlightWorldMeshPages::PointRange;
    size_t outside=0;
    const auto visit=[&](size_t i){
        const unsigned mask=nearby&&!nearby[i]?0:classify(i,parents[i],faces);
        masks[i]=static_cast<unsigned char>(mask);
        const Range range=ranges.size()==parents.size()?ranges[i]:Range{};
        if(!range.count){
            if(mask)append(i,mask);else ++outside;
            return;
        }
        const size_t first=parents.size()+range.first;
        if(!mask){std::fill_n(masks.data()+first,range.count,0);return;}
        for(size_t n=0;n<range.count;++n){
            const size_t index=first+n;
            const unsigned subMask=nearby&&!nearby[index]?0:classify(index,children[range.first+n],mask);
            masks[index]=static_cast<unsigned char>(subMask);
            if(subMask)append(index,subMask);else ++outside;
        }
    };
    if(staticParents){
        for(auto i:*staticParents)visit(i);
    }else{
        for(size_t i=0;i<parents.size();++i)if(!parents[i].terrain)visit(i);
    }
    return outside;
}
}
