#pragma once
#include <d3d9.h>
#include "probe_texture_upload.h"

class NorthlightWorldProbeAtlas {
    using Atlas=std::vector<NorthlightGI::ProbeAtlasEntry>;
    static constexpr unsigned Layers=NorthlightGI::ProbeCascadeCount;
    static constexpr unsigned Channels=5;

public:
    struct UploadWork {
        unsigned blendedSlots=0;
        bool current=false,previous=false;
    };

    NorthlightWorldProbeAtlas()=default;
    NorthlightWorldProbeAtlas(const NorthlightWorldProbeAtlas&)=delete;
    NorthlightWorldProbeAtlas& operator=(const NorthlightWorldProbeAtlas&)=delete;
    ~NorthlightWorldProbeAtlas(){releaseTextures();}

    IDirect3DTexture9* channel(unsigned i)const{return current_[i];}
    IDirect3DTexture9* previous()const{return previous_;}
    IDirect3DTexture9* localVisibility()const{return local_;}

    template<class Visit> void visitTextures(const Visit& visit)const{
        for(auto* texture:current_)visit(texture);
        visit(previous_);
        visit(local_);
    }

    void accountMirror(NorthlightMemoryFootprint::Counter& cpu)const{localMirror_.accountMemory(cpu);}

    void releaseTextures(){
        for(auto*& texture:current_)drop(texture);
        drop(previous_);
        drop(local_);
        invalidateCoefficientUploads();
        resetVisibility();
    }

    void resetVisibility(){localMirror_.reset();}
    void resetHistory(){
        activation_.reset();
        blend_.reset();
        coarseActivation_.reset();
        coarseBlend_.reset();
    }

    void validateRetained(){
        const unsigned n=NorthlightGI::probeLayout().atlas;
        for(unsigned i=0;i<Channels;++i)
            if(current_[i]&&!matches(current_[i],n*n,channelHeight(n,i),D3DFMT_A32B32G32R32F))drop(current_[i]);
        if(previous_&&!matches(previous_,n*n*3,n*Layers,D3DFMT_A32B32G32R32F))drop(previous_);
        const auto local=NorthlightGI::LocalProbeVisibility::atlas(n,Layers);
        if(local_&&!matches(local_,local.width,local.height,D3DFMT_A16B16G16R16F)){
            drop(local_);
            resetVisibility();
        }
    }

    template<class Permit,class Required,class Log>
    bool create(IDirect3DDevice9* d,const Permit& permit,const Required& required,const Log& log){
        const unsigned n=NorthlightGI::probeLayout().atlas;
        for(unsigned i=0;i<Channels;++i)if(!current_[i]){
            const UINT height=channelHeight(n,i);
            if(!permit(uint64_t(n)*n*height*16)||!required(allocate(d,n*n,height,D3DFMT_A32B32G32R32F,current_[i]),"GI probe texture"))return false;
            if(i<3)invalidateCoefficientUploads();
        }
        if(!local_){
            const auto layout=NorthlightGI::LocalProbeVisibility::atlas(n,Layers);
            if(!permit(uint64_t(layout.width)*layout.height*8)||!required(allocate(d,layout.width,layout.height,D3DFMT_A16B16G16R16F,local_),"GI local visibility texture"))return false;
            resetVisibility();
            log("GI local visibility atlas=%ux%u probes=%llu bytesPerProbe=648 staticGeometryOnly=1",layout.width,layout.height,(unsigned long long)(uint64_t(n)*n*n*Layers));
        }
        if(previous_)return true;
        if(!permit(uint64_t(n)*n*3*n*Layers*16))return false;
        const HRESULT hr=allocate(d,n*n*3,n*Layers,D3DFMT_A32B32G32R32F,previous_);
        if(hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY)return required(hr,"GI probe blend texture");
        if(FAILED(hr)){
            static bool logged=false;
            if(!logged){logged=true;log("GI probe blend texture unavailable; same-key probe re-publication switches instantly");}
        }else invalidateCoefficientUploads();
        return true;
    }

    template<class Check>
    bool upload(const std::string& map,const Atlas& fine,const Atlas* coarseBank,float now,UploadWork& work,const Check& check){
        const unsigned n=NorthlightGI::probeLayout().atlas;
        const Atlas empty(coarseBank?0:fine.size());
        const auto& coarse=coarseBank?*coarseBank:empty;
        if(coarse.size()!=fine.size()||!uploadVisibility(n,fine,coarse,check))return false;
        activation_.begin(map);
        coarseActivation_.begin(map);
        blend_.begin(map);
        coarseBlend_.begin(map);
        work.blendedSlots=previous_?blend_.publish(fine,now)+coarseBlend_.publish(coarse,now):0;
        work.current=!previous_||blend_.currentDirty()||coarseBlend_.currentDirty();
        work.previous=previous_&&(blend_.previousDirty()||coarseBlend_.previousDirty());
        for(unsigned i=work.current?0:3;i<Channels;++i){
            D3DLOCKED_RECT lock{};
            if(!check(current_[i]->LockRect(0,&lock,nullptr,0),"probe upload"))return false;
            for(unsigned level=0;level<Layers;++level){
                auto* bits=static_cast<char*>(lock.pBits)+size_t(level)*n*(i==3?6:1)*lock.Pitch;
                NorthlightProbeUpload::write(i,bits,size_t(lock.Pitch),n,level?coarse:fine,
                    level?coarseBlend_:blend_,level?coarseActivation_:activation_,now,previous_!=nullptr,true);
            }
            if(!check(current_[i]->UnlockRect(0),"probe unlock"))return false;
        }
        if(work.current&&previous_){blend_.currentUploaded();coarseBlend_.currentUploaded();}
        if(work.previous){
            D3DLOCKED_RECT lock{};
            if(!check(previous_->LockRect(0,&lock,nullptr,0),"probe blend upload"))return false;
            NorthlightProbeUpload::previous(lock.pBits,size_t(lock.Pitch),n,blend_);
            NorthlightProbeUpload::previous(static_cast<char*>(lock.pBits)+size_t(n)*lock.Pitch,size_t(lock.Pitch),n,coarseBlend_);
            if(!check(previous_->UnlockRect(0),"probe blend unlock"))return false;
            blend_.previousUploaded();
            coarseBlend_.previousUploaded();
        }
        return true;
    }

private:
    friend struct WorldScreenEffectsTest;
    static unsigned channelHeight(unsigned n,unsigned i){return (i==3?n*6:n)*Layers;}
    static void drop(IDirect3DTexture9*& texture){if(texture){texture->Release();texture=nullptr;}}
    static HRESULT allocate(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,IDirect3DTexture9*& texture){
        const HRESULT hr=d->CreateTexture(w,h,1,0,format,D3DPOOL_MANAGED,&texture,nullptr);
        if(FAILED(hr))drop(texture);
        return hr;
    }
    static bool matches(IDirect3DTexture9* texture,UINT w,UINT h,D3DFORMAT format){
        D3DSURFACE_DESC desc{};
        return SUCCEEDED(texture->GetLevelDesc(0,&desc))&&desc.Width==w&&desc.Height==h&&desc.Format==format&&desc.Pool==D3DPOOL_MANAGED;
    }
    void invalidateCoefficientUploads(){blend_.invalidateUploads();coarseBlend_.invalidateUploads();}

    template<class Check> bool uploadVisibility(unsigned n,const Atlas& fine,const Atlas& coarse,const Check& check){
        if(!localMirror_.changed(fine,coarse))return true;
        if(!local_||!localMirror_.prepare(fine.size()+coarse.size()))return false;
        D3DLOCKED_RECT lock{};
        if(!check(local_->LockRect(0,&lock,nullptr,D3DLOCK_NO_DIRTY_UPDATE),"probe local visibility upload"))return false;
        const auto dirty=localMirror_.write(lock.pBits,size_t(lock.Pitch),n,fine,Layers,coarse);
        const RECT rect={LONG(dirty.left),LONG(dirty.top),LONG(dirty.right),LONG(dirty.bottom)};
        if(check(local_->UnlockRect(0),"probe local visibility unlock")&&check(local_->AddDirtyRect(&rect),"probe local visibility dirty"))return true;
        resetVisibility();
        return false;
    }

    IDirect3DTexture9* current_[Channels]={};
    IDirect3DTexture9 *previous_=nullptr,*local_=nullptr;
    NorthlightProbeActivation activation_,coarseActivation_;
    NorthlightProbeBlend::Mirror blend_,coarseBlend_;
    NorthlightProbeUpload::LocalVisibilityMirror localMirror_;
};
