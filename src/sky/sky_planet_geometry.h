#pragma once
#include "sky_material_catalog.h"
#include <cmath>
#include <cstring>

namespace NorthlightSkyPlanet {
// Matching the actual input points also rejects CPU-deformed or unrelated
// meshes sharing a texture. Vertex order may differ between client uploads.
inline const NorthlightSkyComposition::PlanetMesh* mesh(const float positions[8][3],unsigned count){
    if(count!=4&&count!=8)return nullptr;
    for(const auto& candidate:NorthlightSkyComposition::PlanetMeshes){
        if(candidate.count!=count)continue;
        unsigned used=0;
        for(unsigned i=0;i<count;++i){
            for(unsigned j=0;j<count;++j){
                if(used&(1u<<j))continue;
                bool same=true;
                for(unsigned c=0;c<3;++c)same&=std::isfinite(positions[i][c])&&std::fabs(positions[i][c]-candidate.positions[j][c])<.0002f;
                if(same){used|=1u<<j;break;}
            }
        }
        if(used==(1u<<count)-1)return &candidate;
    }
    return nullptr;
}
inline bool scale(unsigned char* vertices,unsigned count,unsigned stride,unsigned positionOffset,float factor){
    if(!vertices||count>8||!stride||stride>128||positionOffset+12>stride||!std::isfinite(factor)||factor<=0||factor>1)return false;
    float positions[8][3]{};
    for(unsigned i=0;i<count;++i)std::memcpy(positions[i],vertices+i*stride+positionOffset,12);
    const auto* source=mesh(positions,count);if(!source)return false;
    for(unsigned i=0;i<count;++i){
        for(unsigned c=0;c<3;++c)positions[i][c]=source->anchor[c]+(positions[i][c]-source->anchor[c])*factor;
        std::memcpy(vertices+i*stride+positionOffset,positions[i],12);
    }
    return true;
}
}
