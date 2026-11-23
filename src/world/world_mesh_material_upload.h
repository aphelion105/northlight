#pragma once
#include <d3d9.h>
#include "shadow_alpha_texture.h"
#include "world_mesh_plan.h"
#include <algorithm>
#include <array>
#include <utility>

namespace NorthlightWorldMeshPublication {

// A pending material is private until every row and the optional device
// transfer have succeeded. Cancelling its generation releases both references.
class MaterialUpload {
    IDirect3DTexture9* texture_=nullptr;
    IDirect3DTexture9* staging_=nullptr;
    size_t row_=0;
    static void drop(IDirect3DTexture9*& texture){if(texture)texture->Release();texture=nullptr;}
public:
    struct Result {
        HRESULT hr=S_OK;
        const char* stage=nullptr;
        size_t bytes=0;
    };
    MaterialUpload()=default;
    MaterialUpload(const MaterialUpload&)=delete;
    MaterialUpload& operator=(const MaterialUpload&)=delete;
    ~MaterialUpload(){reset();}
    IDirect3DTexture9* texture()const{return texture_;}
    size_t rowsWritten()const{return row_;}
    static void preload(IDirect3DTexture9* texture){texture->PreLoad();}

    void reset(){drop(staging_);drop(texture_);row_=0;}
    Result begin(IDirect3DDevice9* d,const NorthlightWorldMesh::MaterialUpload& material,
                 D3DFORMAT format,bool deviceLocal){
        HRESULT hr=d->CreateTexture(material.width,material.height,1,0,format,
                                     deviceLocal?D3DPOOL_DEFAULT:D3DPOOL_MANAGED,&texture_,nullptr);
        if(FAILED(hr)||!texture_){reset();return {FAILED(hr)?hr:E_FAIL,"staged material"};}
        if(deviceLocal)
            hr=d->CreateTexture(material.width,material.height,1,0,format,D3DPOOL_SYSTEMMEM,&staging_,nullptr);
        else{staging_=texture_;staging_->AddRef();}
        if(FAILED(hr)||!staging_){reset();return {FAILED(hr)?hr:E_FAIL,"material staging"};}
        row_=0;
        return {};
    }

    template<class CopyPixels>
    Result copy(const NorthlightWorldMesh::MaterialUpload& material,const NorthlightShadowAlphaTexture::Format& format,
                size_t budget,const CopyPixels& copyPixels){
        const size_t sourceRow=size_t(material.width)*material.stride(),targetRow=size_t(material.width)*format.stride;
        if(targetRow>budget||row_==material.height)return {};
        if(!targetRow||!sourceRow||sourceRow>4096*4)return {E_FAIL,"material width"};
        const UINT rows=UINT(std::min({size_t(material.height)-row_,budget/targetRow,size_t(64)}));
        const RECT rect={0,LONG(row_),LONG(material.width),LONG(row_+rows)};
        D3DLOCKED_RECT lock={};
        HRESULT hr=staging_->LockRect(0,&lock,&rect,0);
        if(FAILED(hr))return {hr,"staged material lock"};
        if(!lock.pBits||lock.Pitch<INT(targetRow)){staging_->UnlockRect(0);return {E_FAIL,"staged material layout"};}
        bool copied=true;
        if(material.alphaOnly&&format.stride==1){
            if(size_t(lock.Pitch)==targetRow)
                copied=copyPixels(row_*targetRow,rows*targetRow,lock.pBits);
            else for(UINT y=0;y<rows&&copied;++y)
                copied=copyPixels((row_+y)*targetRow,targetRow,static_cast<char*>(lock.pBits)+y*lock.Pitch);
        }else{
            std::array<uint8_t,4096*4> scratch;
            for(UINT y=0;y<rows&&copied;){
                const UINT n=std::min(rows-y,UINT(scratch.size()/sourceRow));
                copied=copyPixels((row_+y)*sourceRow,n*sourceRow,scratch.data());
                for(UINT j=0;copied&&j<n;++j)for(UINT x=0;x<material.width;++x)
                    NorthlightShadowAlphaTexture::write(static_cast<char*>(lock.pBits)+(y+j)*lock.Pitch,x,format.stride,
                        scratch[j*sourceRow+(x+1)*material.stride()-1]);
                y+=n;
            }
        }
        hr=staging_->UnlockRect(0);
        if(!copied)return {E_FAIL,"staged material temporary copy"};
        if(FAILED(hr))return {hr,"staged material unlock"};
        row_+=rows;
        return {S_OK,nullptr,rows*targetRow};
    }

    Result finish(IDirect3DDevice9* d){
        if(staging_!=texture_){
            const HRESULT hr=d->UpdateTexture(staging_,texture_);
            if(FAILED(hr))return {hr,"material upload"};
        }else preload(texture_);
        drop(staging_);
        return {};
    }

    IDirect3DTexture9* take(){row_=0;return std::exchange(texture_,nullptr);}
};
}
