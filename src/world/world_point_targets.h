#pragma once
#include "point_cube_cache.h"
#include "world_compiled_shaders.h"

// Lamps share the working attachments and own their completed faces separately.
class NorthlightWorldPointTargets {
public:
    using Slot=NorthlightPointShadow::CubeSlot;
    using Slots=std::array<Slot,NorthlightPointShadow::MaxLights>;
    static constexpr UINT Size=NorthlightPointShadow::CubeResolution;

    NorthlightWorldPointTargets()=default;
    NorthlightWorldPointTargets(const NorthlightWorldPointTargets&)=delete;
    NorthlightWorldPointTargets& operator=(const NorthlightWorldPointTargets&)=delete;
    ~NorthlightWorldPointTargets(){releaseCasterShaders();}
    Slots& slots(){return slots_;}
    const Slots& slots()const{return slots_;}
    NorthlightPointShadow::RetainedCubes& keeps(){return keeps_;}
    IDirect3DTexture9* scratch()const{return scratch_;}
    IDirect3DSurface9* scratchSurface()const{return scratchSurface_;}
    IDirect3DSurface9* depth()const{return depth_;}
    IDirect3DTexture9* empty()const{return empty_;}
    IDirect3DVertexShader9* casterVertex()const{return casterVertex_;}
    IDirect3DPixelShader9* casterPixel()const{return casterPixel_;}

    HRESULT ensureCasterVertex(IDirect3DDevice9* device){
        if(casterVertex_)return S_OK;
        const HRESULT hr=device->CreateVertexShader(kLocalShadowVSShader,&casterVertex_);
        if(FAILED(hr))drop(casterVertex_);
        return hr;
    }
    HRESULT ensureCasterPixel(IDirect3DDevice9* device){
        if(casterPixel_)return S_OK;
        const HRESULT hr=device->CreatePixelShader(kLocalShadowPSShader,&casterPixel_);
        if(FAILED(hr))drop(casterPixel_);
        return hr;
    }
    void releaseCasterShaders(){drop(casterVertex_);drop(casterPixel_);}

    HRESULT createScratch(IDirect3DDevice9* device){return createFace(device,&scratch_);}
    HRESULT acquireScratchSurface(){return scratch_->GetSurfaceLevel(0,&scratchSurface_);}
    HRESULT createDepth(IDirect3DDevice9* device){
        return device->CreateDepthStencilSurface(Size,Size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth_,nullptr);
    }

    template<class Allocate>
    HRESULT ensureCube(IDirect3DDevice9* device,Slot& slot,const Allocate& allocate){
        return slot.ensureCube([&](IDirect3DCubeTexture9** cube){
            return allocate([&]{return device->CreateCubeTexture(Size,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,cube,nullptr);});
        });
    }

    template<class Allocate,class Check>
    bool ensureStaticFace(IDirect3DDevice9* device,Slot& slot,unsigned face,const Allocate& allocate,const Check& check){
        return slot.ensureStaticFace(face,[&](IDirect3DTexture9** target){
            return allocate([&]{return createFace(device,target);});
        },check);
    }

    template<class Allocate>
    HRESULT ensureEmpty(IDirect3DDevice9* device,const Allocate& allocate){
        if(empty_)return S_OK;
        IDirect3DTexture9* texture=nullptr;
        HRESULT hr=allocate([&]{return device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr);});
        if(SUCCEEDED(hr)&&texture){
            D3DLOCKED_RECT lock{};
            hr=texture->LockRect(0,&lock,nullptr,0);
            if(SUCCEEDED(hr)){
                if(lock.pBits)*static_cast<DWORD*>(lock.pBits)=0xffffffffu;
                const HRESULT unlocked=texture->UnlockRect(0);
                hr=lock.pBits?unlocked:E_FAIL;
            }
        }
        if(FAILED(hr)||!texture){drop(texture);return FAILED(hr)?hr:E_FAIL;}
        empty_=texture;
        return S_OK;
    }

    void resetSlots(){for(auto& slot:slots_)slot.reset();}

    void releaseShared(){
        drop(depth_);
        drop(scratchSurface_);
        drop(scratch_);
        drop(empty_);
    }

    template<class Cube,class Texture,class Surface>
    void visit(const Cube& cube,const Texture& texture,const Surface& surface)const{
        for(const auto& slot:slots_){
            cube(slot.cube);
            for(auto* face:slot.cacheFace)texture(face);
        }
        for(const auto& keep:keeps_)for(auto* face:keep.slot.cacheFace)texture(face);
        surface(depth_);
        texture(scratch_);
        texture(empty_);
    }

private:
    Slots slots_;
    NorthlightPointShadow::RetainedCubes keeps_;
    IDirect3DSurface9 *depth_=nullptr,*scratchSurface_=nullptr;
    IDirect3DTexture9 *scratch_=nullptr,*empty_=nullptr;
    IDirect3DVertexShader9* casterVertex_=nullptr;
    IDirect3DPixelShader9* casterPixel_=nullptr;

    static HRESULT createFace(IDirect3DDevice9* device,IDirect3DTexture9** texture){
        return device->CreateTexture(Size,Size,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,texture,nullptr);
    }

    template<class T> static void drop(T*& resource){
        if(resource){resource->Release();resource=nullptr;}
    }
};
