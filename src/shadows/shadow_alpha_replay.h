#pragma once

#include "shadow_alpha_draw.h"
#include "shadow_alpha_target.h"

namespace NorthlightShadowAlpha {

// Shared by directional, actor, flash and point shadow replays. The frame
// coordinator decides which maps to withhold when a scratch target is refused.
class ReplayAlpha {
    static constexpr DWORD RetryMs = 1000;
    IDirect3DDevice9* device_;
    Targets targets_;
    DWORD retryAt_ = 0;
    DWORD failureLogAt_ = 0;
    bool unavailable_ = false;
    bool fallback_ = false;

public:
    explicit ReplayAlpha(IDirect3DDevice9* device):device_(device),targets_(device){}

    void beginFrame(DWORD now){
        if(retryAt_&&DWORD(now-retryAt_)>=RetryMs)retryAt_=0;
        unavailable_=false;
    }
    bool deferIfUnavailable(){
        if(!unavailable_)return false;
        retryAt_=GetTickCount();
        return true;
    }
    DWORD retryAt()const{return retryAt_;}
    bool fallback()const{return fallback_;}
    bool setFallback(bool value){
        if(fallback_==value)return false;
        fallback_=value;
        return true;
    }

    bool supported(D3DFORMAT format){return targets_.supported(format);}
    size_t bytes()const{return targets_.bytes();}
    size_t trimTargets(){
        const size_t released=targets_.bytes();
        targets_.reset();
        return released;
    }
    void reset(){
        targets_.reset();
        retryAt_=failureLogAt_=0;
        unavailable_=fallback_=false;
    }
    template<class Visit> void forEachSurface(const Visit& visit)const{
        for(unsigned i=0;i<Targets::Count;++i)visit(targets_.surface(i));
    }

    // Source-format RT0 retains the native alpha conversion; RT1 keeps R32 depth.
    template<class Admit,class Draw>
    HRESULT draw(unsigned mode,const Admit& admit,void (*log)(const char*,...),Draw submit){
        bool refused=false;
        const HRESULT hr=NorthlightShadowAlpha::draw(device_,mode,[&](D3DFORMAT format,UINT width,UINT height){
            const size_t bytes=Targets::allocationBytes(format,width,height);
            const bool admitted=bytes&&(targets_.contains(format,width,height)||admit(bytes));
            auto* target=admitted?targets_.acquire(format,width,height):nullptr;
            refused=!target;
            if(refused){
                const DWORD now=GetTickCount();
                if(!failureLogAt_||DWORD(now-failureLogAt_)>=RetryMs){
                    failureLogAt_=now;
                    log("SHADOW alpha target deferred format=%u size=%ux%u bytes=%zu cause=%s",
                        unsigned(format),width,height,bytes,
                        !bytes?"unsupported-layout":admitted?"create-target":"address-space");
                }
            }
            return target;
        },submit);
        if(refused){unavailable_=true;return E_PENDING;}
        return hr;
    }
};

}
