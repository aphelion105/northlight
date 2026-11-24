#pragma once
#include "shadow_terrain.h"
#include "world_mesh_pages.h"

// The committed mesh and fixed-chunk set are immutable until the cache
// worker settles. Both redraw and its cost estimate consume this order.
class NorthlightDirectionalCacheMesh {
    using Batch=NorthlightWorldMeshPages::Batch;
    using Chunks=std::set<std::pair<int,int>>;
    using Bits=NorthlightShadowTerrain::FixedChunkBits;
    static constexpr size_t MaxBatches=65536;
    std::vector<uint32_t> indices_;
    const Chunks* fixed_=nullptr;
    uint64_t generation_=0;
    size_t count_=0;

    static bool eligible(const Batch& batch,const Chunks& fixed,const Bits& bits,bool useBits){
        if(batch.pointOnly)return false;
        const auto chunk=std::make_pair(batch.chunkX,batch.chunkY);
        return !batch.terrain||(useBits?bits.contains(chunk):fixed.count(chunk)!=0);
    }

public:
    void invalidate(){indices_.clear();fixed_=nullptr;}
    bool published()const{return fixed_!=nullptr;}
    size_t retained()const{return indices_.size();}
    void publish(const std::vector<Batch>& batches,uint64_t generation,const Chunks& fixed,const Bits& bits){
        invalidate();
        if(batches.size()>MaxBatches)return;
        try{indices_.reserve(batches.size());}catch(const std::bad_alloc&){return;}
        const bool useBits=bits.source()==&fixed;
        for(size_t i=0;i<batches.size();++i)
            if(eligible(batches[i],fixed,bits,useBits))indices_.push_back(uint32_t(i));
        count_=batches.size();generation_=generation;fixed_=&fixed;
    }

    template<class Visit>
    bool visit(const std::vector<Batch>& batches,uint64_t generation,const Chunks& fixed,const Bits& bits,Visit&& visitor)const{
        if(generation_==generation&&count_==batches.size()&&fixed_==&fixed){
            for(uint32_t index:indices_)if(!visitor(batches[index]))return false;
        }else{
            const bool useBits=bits.source()==&fixed;
            for(const auto& batch:batches)
                if(eligible(batch,fixed,bits,useBits)&&!visitor(batch))return false;
        }
        return true;
    }
};
