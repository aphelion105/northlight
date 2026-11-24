#pragma once
#include <d3d9.h>
#include "dither_noise.h"

// Managed textures shared by screen and shadow passes when a producer has no image.
class NorthlightWorldScreenFallbacks {
    IDirect3DTexture9 *ao_=nullptr,*white_=nullptr,*zero_=nullptr,*dither_=nullptr;

    static void drop(IDirect3DTexture9*& texture){
        if(texture){texture->Release();texture=nullptr;}
    }

    template<class Check>
    static bool solid(IDirect3DDevice9* d,IDirect3DTexture9*& texture,DWORD color,
                      const char* action,const char* lockAction,const Check& check){
        if(texture)return true;
        if(!check(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr),action)){
            drop(texture);return false;
        }
        D3DLOCKED_RECT lock{};
        if(!check(texture->LockRect(0,&lock,nullptr,0),lockAction)){
            drop(texture);return false;
        }
        *static_cast<DWORD*>(lock.pBits)=color;
        texture->UnlockRect(0);
        return true;
    }

    void createDither(IDirect3DDevice9* d){
        if(dither_)return;
        using namespace NorthlightDitherNoise;
        if(FAILED(d->CreateTexture(N,N,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&dither_,nullptr))){
            drop(dither_);return;
        }
        static const std::vector<float> tile=offsets();
        D3DLOCKED_RECT lock{};
        if(FAILED(dither_->LockRect(0,&lock,nullptr,0))){drop(dither_);return;}
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
            auto* p=reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch)+x*4;
            p[0]=p[1]=p[2]=tile[y*N+x];p[3]=0;
        }
        dither_->UnlockRect(0);
    }

public:
    NorthlightWorldScreenFallbacks()=default;
    NorthlightWorldScreenFallbacks(const NorthlightWorldScreenFallbacks&)=delete;
    NorthlightWorldScreenFallbacks& operator=(const NorthlightWorldScreenFallbacks&)=delete;
    ~NorthlightWorldScreenFallbacks(){releaseScreen();releaseShadow();}

    IDirect3DTexture9* ao()const{return ao_;}
    IDirect3DTexture9* white()const{return white_;}
    IDirect3DTexture9* zero()const{return zero_;}
    IDirect3DTexture9* dither()const{return dither_;}
    void releaseScreen(){drop(ao_);drop(zero_);drop(dither_);}
    void releaseShadow(){drop(white_);}

    template<class Check> bool create(IDirect3DDevice9* d,const Check& check){
        if(!solid(d,ao_,0xff000000u,"neutral AO texture","neutral AO lock",check)||
           !solid(d,white_,0xffffffffu,"neutral shadow texture","neutral shadow lock",check))return false;
        solid(d,zero_,0,nullptr,nullptr,[](HRESULT result,const char*){return SUCCEEDED(result);});
        createDither(d);
        return true;
    }
};
