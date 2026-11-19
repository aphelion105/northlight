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

class Layout {
    using Batch=NorthlightWorldMeshPages::Batch;
    using Range=NorthlightWorldMeshPages::PointRange;
    uint64_t generation_=UINT64_MAX;
    std::vector<unsigned char> emptyTerrain_;
    size_t standalone_=0;
public:
    const std::vector<unsigned char>& prepare(uint64_t generation,const std::vector<Batch>& parents,
                                             size_t total,const std::vector<Range>& ranges){
        if(generation_!=generation||emptyTerrain_.size()!=total){
            emptyTerrain_.assign(total,63);
            standalone_=0;
            for(size_t i=0;i<parents.size();++i){
                if(parents[i].terrain)emptyTerrain_[i]=0;
                else if(ranges.size()!=parents.size()||!ranges[i].count)++standalone_;
            }
            generation_=generation;
        }
        return emptyTerrain_;
    }

    // Terrain already owns its face masks. Clear only static masks (including
    // all children), so gap merging can still read any rejected triangle range.
    template<class Classify,class Append>
    size_t classifyStatic(const std::vector<Batch>& parents,const std::vector<Batch>& children,
                          const std::vector<Range>& ranges,const unsigned char* nearby,unsigned faces,
                          std::vector<unsigned char>& masks,Classify classify,Append append)const{
        for(size_t i=0;i<masks.size();++i)masks[i]&=static_cast<unsigned char>(~emptyTerrain_[i]);
        size_t outside=standalone_;
        const auto visit=[&](size_t i){
            if(!emptyTerrain_[i])return;
            const auto mask=classify(parents[i],faces);
            masks[i]=static_cast<unsigned char>(mask);
            const Range range=ranges.size()==parents.size()?ranges[i]:Range{};
            if(!range.count){--outside;append(i,mask);return;}
            if(!mask)return;
            for(size_t n=range.first;n<size_t(range.first)+range.count;++n){
                const size_t index=parents.size()+n;
                const unsigned subMask=nearby&&!nearby[index]?0:classify(children[n],mask);
                masks[index]=static_cast<unsigned char>(subMask);
                append(index,subMask);
            }
        };
        if(nearby)visitNearby(nearby,parents.size(),visit);
        else for(size_t i=0;i<parents.size();++i)visit(i);
        return outside;
    }
};
}
