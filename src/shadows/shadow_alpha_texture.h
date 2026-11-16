#pragma once

// Caster shaders sample alpha only. Preserve its eight bits without retaining
// three unused colour channels in the GPU resource and upload staging.
namespace NorthlightShadowAlphaTexture {
struct Format {
    D3DFORMAT value=D3DFMT_A8R8G8B8;
    unsigned stride=4;
    bool known=false;
    bool probe(IDirect3DDevice9* device){
        if(known)return true;
        IDirect3DTexture9* texture=nullptr;
        const HRESULT hr=device->CreateTexture(1,1,1,0,D3DFMT_A8,D3DPOOL_MANAGED,&texture,nullptr);
        if(SUCCEEDED(hr)&&texture){value=D3DFMT_A8;stride=1;known=true;}
        else if(hr!=E_OUTOFMEMORY&&hr!=D3DERR_OUTOFVIDEOMEMORY)known=true;
        if(texture)texture->Release();
        return known;
    }
    uint64_t bytes(uint64_t bgraBytes)const{return bgraBytes/4*stride;}
};
inline void write(void* row,unsigned x,unsigned stride,uint8_t alpha){
    if(stride==1)static_cast<uint8_t*>(row)[x]=alpha;
    else static_cast<uint32_t*>(row)[x]=(uint32_t(alpha)<<24)|0x00ffffffu;
}
}
