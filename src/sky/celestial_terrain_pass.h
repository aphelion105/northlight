#pragma once
#include <d3d9.h>
#include "celestial_terrain.h"

namespace NorthlightCelestialTerrain {

// The celestial renderer sets the mask target, shaders and raster state.
// World mesh publication supplies the committed batches and binds their pages.
template<class Device,class Batches,class List,class BindPage>
bool submitMask(Device* device,const Batches& batches,const List& list,bool listed,
                UINT maxIndices,const float* matrix,BindPage bindPage,RunStats& stats){
    UINT page=UINT_MAX;
    const Frustum frustum(matrix);
    const auto accept=[&](const auto& batch){
        return batch.terrain&&!frustum.reject(batch.boundsLow,batch.boundsHigh);
    };
    const auto emit=[&](const Run& run){
        return bindPage(run.page,page)&&SUCCEEDED(device->DrawIndexedPrimitive(
            D3DPT_TRIANGLELIST,0,run.minVertex,UINT(run.vertexEnd-run.minVertex),run.start,run.count));
    };
    if(listed)return forEachRun(batches,list,maxIndices,accept,emit,stats);

    for(const auto& batch:batches){
        if(!accept(batch))continue;
        ++stats.accepted;
        ++stats.runs;
        stats.triangles+=batch.count;
        if(!emit({batch.page,batch.start,batch.count,batch.minVertex,
                  std::uint64_t(batch.minVertex)+batch.vertexCount}))return false;
    }
    return true;
}

} // namespace NorthlightCelestialTerrain
