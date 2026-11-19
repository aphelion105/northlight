#pragma once
#include "world_mesh_pages.h"
#include <cstring>

namespace NorthlightPointMesh {
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
        const unsigned mask=nearby&&!nearby[i]?0:classify(parents[i],faces);
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
            const unsigned subMask=nearby&&!nearby[index]?0:classify(children[range.first+n],mask);
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
