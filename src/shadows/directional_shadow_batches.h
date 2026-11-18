#pragma once
#include "world_mesh_pages.h"
#include "shadow_bounds.h"
#include <d3d9.h>

// A cascade submits adjacent ranges in their original index order. Every
// change affecting coverage closes the pending draw before changing state.
class NorthlightDirectionalShadowBatches {
    using Batch=NorthlightWorldMeshPages::Batch;
    using Rect=NorthlightShadowBounds::TexelRect;
    Batch run{};
    IDirect3DPixelShader9* shader=nullptr;
    const Rect* scissor=nullptr;
    bool ignoresTexture=false;
    unsigned limit;
public:
    unsigned submitted=0;
    explicit NorthlightDirectionalShadowBatches(unsigned primitiveLimit):limit(primitiveLimit){}
    template<class Draw> bool flush(Draw& draw){
        if(!run.count)return true;
        if(!draw(run,shader,scissor))return false;
        ++submitted;
        run.count=0;
        return true;
    }
    template<class Draw> bool add(const Batch& batch,IDirect3DPixelShader9* ps,const Rect* rect,bool opaqueShader,Draw& draw){
        if(run.count&&ps==shader&&rect==scissor&&(run.material==batch.material||(ignoresTexture&&opaqueShader))&&
           NorthlightWorldMeshPages::appendDrawRange(run,batch,limit))return true;
        if(!flush(draw))return false;
        run=batch;
        shader=ps;
        scissor=rect;
        ignoresTexture=opaqueShader;
        return true;
    }
};
