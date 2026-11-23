#pragma once
#include "actor_texture.h"
#include <d3d9.h>
#include <utility>

namespace NorthlightActorTexture {

// Copy the selected mip on the render thread. The worker takes only CPU bytes.
inline bool read(IDirect3DBaseTexture9* base,Snapshot& snapshot){
    if(!base||base->GetType()!=D3DRTYPE_TEXTURE)return false;
    auto* texture=static_cast<IDirect3DTexture9*>(base);
    const UINT levels=texture->GetLevelCount();
    D3DSURFACE_DESC desc={};
    UINT level=0;
    for(;level<levels;++level){
        if(FAILED(texture->GetLevelDesc(level,&desc)))return false;
        if(desc.Width<=128&&desc.Height<=128)break;
    }
    if(level==levels||!desc.Width||!desc.Height)return false;
    Format format;
    switch(desc.Format){
    case D3DFMT_A8R8G8B8: format=Format::BGRA8; break;
    case D3DFMT_X8R8G8B8: format=Format::BGRX8; break;
    case D3DFMT_R5G6B5: format=Format::RGB565; break;
    case D3DFMT_DXT1: format=Format::BC1; break;
    case D3DFMT_DXT3: format=Format::BC2; break;
    case D3DFMT_DXT5: format=Format::BC3; break;
    case D3DFMT_A1R5G5B5: format=Format::ARGB1555; break;
    case D3DFMT_X1R5G5B5: format=Format::XRGB1555; break;
    case D3DFMT_A4R4G4B4: format=Format::ARGB4444; break;
    default: return false;
    }
    if(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))return false;
    const UINT rows=rowCount(desc.Height,format);
    const UINT pitch=UINT(rowBytes(desc.Width,format));
    std::vector<std::uint8_t> raw(std::size_t(rows)*pitch); // Allocate before taking a game texture lock.
    D3DLOCKED_RECT locked={};
    if(FAILED(texture->LockRect(level,&locked,nullptr,D3DLOCK_READONLY)))return false;
    const bool copyable=locked.pBits&&locked.Pitch>=INT(pitch);
    if(copyable){
        const auto* source=static_cast<const std::uint8_t*>(locked.pBits);
        for(UINT row=0;row<rows;++row)
            std::memcpy(raw.data()+std::size_t(row)*pitch,source+std::size_t(row)*unsigned(locked.Pitch),pitch);
    }
    if(FAILED(texture->UnlockRect(level))||!copyable)return false;
    if(!readable(raw.data(),raw.size(),desc.Width,desc.Height,pitch,format))return false;
    snapshot.bytes=std::move(raw);
    snapshot.width=desc.Width;
    snapshot.height=desc.Height;
    snapshot.pitch=pitch;
    snapshot.format=format;
    return true;
}

} // namespace NorthlightActorTexture
