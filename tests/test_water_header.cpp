// Cross-compile only. No game/device is constructed or run by this harness.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
static void logf(const char*,...){}
template<class T> static void drop(T*&p){if(p){p->Release();p=nullptr;}}
#include "saved_state.h"
#include "water_renderer.h"
void compilePublicAPI(IDirect3DDevice9* d,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DSurface9* depth){
    NorthlightWaterRenderer water(d);
    water.registerVertex(vs,0,nullptr,0);water.registerPixel(ps,0,nullptr,0);
    D3DVIEWPORT9 vp={0,0,1920,1080,0,1};
    water.capture(vs,ps,1920,1080,depth,vp,NorthlightWaterRenderer::NoUserPointer,0,[&](){return d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1);});
    // 0.3.214 (water MRT): the mask written by the game's own draw
    water.setMrtCaps(true);
    if(water.plan(vs,ps,1920,1080,vp)==NorthlightWaterRenderer::Plan::Mrt&&water.beginMrt(vs,ps,1920,1080,D3DMULTISAMPLE_4_SAMPLES,0,vp)){
        d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1);water.endMrt(true);
    }
    water.unbindMrt();water.resolveMask();
    water.maskTextureForDepth(0,1);water.endFrame();water.recover();water.reset();
}
