#pragma once
#include <d3d9.h>
#include <cstddef>

namespace NorthlightShadowAlpha {
// Only the dynamic replay sizes: point faces (256), all other maps (1024).
// Four exact source formats per size, created lazily. Alternating formats reuse
// their own surfaces; never allocate a backbuffer-sized target or evict a packet.
class Targets {
    struct Entry {IDirect3DSurface9* surface=nullptr;};
    IDirect3DDevice9* d_;
    Entry entries_[8];
    unsigned char supported_[4]={};
    static int formatIndex(D3DFORMAT format){
        switch(format){case D3DFMT_A8R8G8B8:return 0;case D3DFMT_X8R8G8B8:return 1;
        case D3DFMT_A16B16G16R16F:return 2;case D3DFMT_A32B32G32R32F:return 3;default:return -1;}
    }
    static int sizeIndex(UINT width,UINT height){return width==height?(width==256?0:width==1024?1:-1):-1;}
public:
    static constexpr unsigned Count=8;
    bool supported(D3DFORMAT format){
        const int index=formatIndex(format);if(index<0)return false;
        if(supported_[index])return supported_[index]==2;
        supported_[index]=1;
        D3DCAPS9 caps{};D3DDEVICE_CREATION_PARAMETERS creation{};
        constexpr DWORD required=D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS|D3DPMISCCAPS_INDEPENDENTWRITEMASKS|D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;
        if(FAILED(d_->GetDeviceCaps(&caps))||caps.NumSimultaneousRTs<2||(caps.PrimitiveMiscCaps&required)!=required||
           FAILED(d_->GetCreationParameters(&creation)))return false;
        IDirect3D9* api=nullptr;if(FAILED(d_->GetDirect3D(&api)))return false;
        D3DDISPLAYMODE display{};
        const bool ok=SUCCEEDED(api->GetAdapterDisplayMode(creation.AdapterOrdinal,&display))&&
            SUCCEEDED(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_SURFACE,format))&&
            SUCCEEDED(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE,D3DFMT_R32F))&&
            SUCCEEDED(api->CheckDepthStencilMatch(creation.AdapterOrdinal,creation.DeviceType,display.Format,format,D3DFMT_D24S8))&&
            SUCCEEDED(api->CheckDepthStencilMatch(creation.AdapterOrdinal,creation.DeviceType,display.Format,D3DFMT_R32F,D3DFMT_D24S8));
        api->Release();if(ok)supported_[index]=2;return ok;
    }
    explicit Targets(IDirect3DDevice9* device):d_(device){}
    Targets(const Targets&)=delete;Targets& operator=(const Targets&)=delete;
    ~Targets(){reset();}
    static size_t allocationBytes(D3DFORMAT format,UINT width,UINT height){
        const int index=formatIndex(format);return index<0||sizeIndex(width,height)<0?0:size_t(width)*height*(index<2?4:index==2?8:16);
    }
    bool contains(D3DFORMAT format,UINT width,UINT height)const{
        const int size=sizeIndex(width,height),index=formatIndex(format);return size>=0&&index>=0&&entries_[size*4+index].surface;
    }
    IDirect3DSurface9* acquire(D3DFORMAT format,UINT width,UINT height){
        const int index=sizeIndex(width,height);if(index<0||!supported(format))return nullptr;
        auto& entry=entries_[index*4+formatIndex(format)];if(entry.surface)return entry.surface;
        if(FAILED(d_->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_NONE,0,FALSE,&entry.surface,nullptr))){if(entry.surface)entry.surface->Release();entry.surface=nullptr;return nullptr;}
        return entry.surface;
    }
    void reset(){for(auto& entry:entries_){if(entry.surface)entry.surface->Release();entry={};}for(auto& state:supported_)state=0;}
    size_t bytes()const{size_t result=0;for(unsigned i=0;i<Count;++i)if(entries_[i].surface){const UINT side=i<4?256:1024;result+=size_t(side)*side*(i%4<2?4:i%4==2?8:16);}return result;}
    IDirect3DSurface9* surface(unsigned index)const{return index<Count?entries_[index].surface:nullptr;}
};
}
