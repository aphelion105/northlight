#pragma once
#include "world_draw_domain.h"
#include <cstring>

namespace NorthlightDrawPreparation {
struct WorldState {
    D3DVIEWPORT9 viewport={};
    DWORD depthEnabled=FALSE;
    bool read=false,inWorld=false;
};

struct TerrainAdmission {
    // The caller retains the validated target and depth surfaces.
    IDirect3DSurface9* target=nullptr;
    IDirect3DSurface9* depth=nullptr;
    D3DVIEWPORT9 viewport{};
    float projection[16]{};

    template<class Mirror>
    bool matches(const Mirror& mirror)const{
        if(!target||!depth)return false;
        const auto* rows=mirror.borrowKnownVertexFloats(4,4);
        if(!rows||mirror.targetWrites||mirror.viewportWrites||
           !mirror.targetKnown[0]||mirror.targets[0]!=target||!mirror.depthKnown||mirror.depth!=depth||
           !mirror.viewportKnown||std::memcmp(&viewport,&mirror.viewport,sizeof viewport))return false;
        return !std::memcmp(projection,rows,sizeof projection);
    }
};

template<class Device>
WorldState readWorldState(Device* device,bool projectionValid,float minDepth,float maxDepth){
    WorldState state;
    state.read=SUCCEEDED(device->GetViewport(&state.viewport))&&
        SUCCEEDED(device->GetRenderState(D3DRS_ZENABLE,&state.depthEnabled));
    state.inWorld=state.read&&NorthlightWorldDrawDomain::accepts(projectionValid,state.depthEnabled!=FALSE,
        minDepth,maxDepth,state.viewport.MinZ,state.viewport.MaxZ);
    return state;
}

template<class Device,class Mirror>
WorldState readHeldWorldState(Device* device,Mirror& mirror,bool projectionValid,float minDepth,float maxDepth){
    // The Draw adapter holds the mirror gate. Use the same validity checks as
    // both getters, including viewport changes implicit in a target write.
    if(!mirror.active()||mirror.viewportWrites||mirror.targetWrites||!mirror.viewportKnown||
       !mirror.renderStateKnown[D3DRS_ZENABLE])return readWorldState(device,projectionValid,minDepth,maxDepth);
    const auto& viewport=mirror.viewport;
    const DWORD depth=mirror.renderStates[D3DRS_ZENABLE];
    mirror.answered+=2;
    return {viewport,depth,true,NorthlightWorldDrawDomain::accepts(projectionValid,depth!=FALSE,
        minDepth,maxDepth,viewport.MinZ,viewport.MaxZ)};
}

// Shared across receivers, early resolve and census for this draw. The reader
// is the Device's mirror-aware fallback and runs only on first demand.
template<class Reader>
auto lazyState(const Reader& read,D3DRENDERSTATETYPE type){
    return [&read,type,value=DWORD(0),known=false]()mutable{
        if(!known){value=read(type);known=true;}
        return value;
    };
}
}
