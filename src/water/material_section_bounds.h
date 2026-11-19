#pragma once
#include "material_skin_bounds.h"
#include "position_bounds_scan.h"

// An immutable section can be drawn by both skinned and rigid programs.
// Only a rigid consumer needs the raw POSITION0 box.
struct NorthlightMaterialSectionBounds {
    NorthlightReplayBounds::Bounds object;
    UINT positionOffset=0;
    std::shared_ptr<NorthlightMaterialSkinBounds> skin;

    bool hasPositions(UINT offset)const{return object.valid&&offset==positionOffset;}
    bool scanPositions(const NorthlightDrawSnapshot::Mesh& mesh,UINT offset){
        const auto& stream=mesh.streams[0];
        object.valid=false;
        if(!mesh.vertexCount||uint64_t(mesh.vertexCount-1)*stream.stride+offset+12>stream.bytes.size())return false;
        positionOffset=offset;
        object.valid=NorthlightPositionBounds::scanPositions(stream.bytes.data()+offset,mesh.vertexCount,stream.stride,object.low,object.high);
        return object.valid;
    }
};
