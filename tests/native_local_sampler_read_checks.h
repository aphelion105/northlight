#pragma once
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

// A counted device with getter/setter-proven sampler state. The real GPU still
// receives every write and draw; only the native renderer's save reads differ.
struct NativeSamplerReadDevice:ForwardIDirect3DDevice9 {
    DWORD sampler[16][D3DSAMP_DMAPOFFSET]{};bool known[16][D3DSAMP_DMAPOFFSET]{};
    unsigned mode=0,gets=0,textures=0,callbacks=0,hits=0,wrongIdentity=0;
    static constexpr D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,
        D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
    explicit NativeSamplerReadDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){
        if(const char* value=std::getenv("NORTHLIGHT_TEST_SAMPLER_BULK_MODE"))mode=unsigned(std::atoi(value));
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE key,DWORD* out)override{
        ++gets;const auto hr=real->GetSamplerState(stage,key,out);
        if(SUCCEEDED(hr)&&out&&stage<16&&key>=1&&key<=D3DSAMP_DMAPOFFSET){sampler[stage][key-1]=*out;known[stage][key-1]=true;}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE key,DWORD value)override{
        const auto hr=real->SetSamplerState(stage,key,value);
        if(stage<16&&key>=1&&key<=D3DSAMP_DMAPOFFSET){known[stage][key-1]=SUCCEEDED(hr);if(SUCCEEDED(hr))sampler[stage][key-1]=value;}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage,IDirect3DBaseTexture9** out)override{++textures;return real->GetTexture(stage,out);}
    static bool read(void* context,DWORD stage,unsigned mask,DWORD* out){
        auto& d=*static_cast<NativeSamplerReadDevice*>(static_cast<IDirect3DDevice9*>(context));++d.callbacks;
        unsigned expected=0;for(auto key:keys)expected|=1u<<(key-1);assert(mask==expected&&stage<16);
        if(d.mode==2||d.mode==3){std::fill_n(out,D3DSAMP_DMAPOFFSET,0xdeadbeefu);return false;}
        for(unsigned i=0;i<D3DSAMP_DMAPOFFSET;++i)if((mask&(1u<<i))&&!d.known[stage][i])return false;
        if(d.mode==5)d.known[stage][D3DSAMP_BORDERCOLOR-1]=false; // Unrequested state is irrelevant.
        for(unsigned i=0;i<D3DSAMP_DMAPOFFSET;++i)if(mask&(1u<<i))out[i]=d.sampler[stage][i];
        ++d.hits;return true;
    }
    void attach(NorthlightNativeLocalLighting& renderer){
        renderer.setSamplerStateSource(mode==4?static_cast<void*>(&wrongIdentity):static_cast<IDirect3DDevice9*>(this),mode?read:nullptr);
    }
    void verify(const NorthlightNativeLocalDraw& input,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,unsigned count,
                const NorthlightNativePOM* pom=nullptr,const NorthlightNativeDisplacement* displacement=nullptr){
        DWORD saved[16][7];for(unsigned s=0;s<16;++s)for(unsigned i=0;i<7;++i)assert(SUCCEEDED(GetSamplerState(s,keys[i],&saved[s][i])));
        NorthlightNativeLocalLighting probe(this);probe.setColdTraceSink([](void*,const NorthlightNativeLocalLighting::ColdTrace&)noexcept{});
        auto light=input,second=input,third=input;light.slot=0;second.slot=1;third.slot=2;
        auto draw=[&]{++light.rendererFrame;return probe.draw(light,vs,ps,[]{return S_OK;},count>1?&second:nullptr,pom,displacement,count>2?&third:nullptr);};
        for(unsigned i=0;i<count;++i)assert(SUCCEEDED(draw()));
        const unsigned selected=probe.tripleDraws?3:probe.multiDraws?2:probe.draws?1:0;assert(selected>0&&selected<=count);
        const unsigned previousMode=mode;
        for(mode=0;mode<=5;++mode){attach(probe);const auto before=gets,beforeTextures=textures,beforeCallbacks=callbacks,beforeHits=hits;
            const auto bulkHits=probe.samplerBulkHits,fallbacks=probe.samplerBulkFallbacks;
            assert(SUCCEEDED(draw()));
            const bool bulk=mode==1||mode==5;
            assert(gets-before==(bulk?0:7*selected));assert(textures-beforeTextures==selected);
            assert(callbacks-beforeCallbacks==((mode==0||mode==4)?0:selected));assert(hits-beforeHits==(bulk?selected:0));
            assert(probe.samplerBulkHits-bulkHits==(bulk?selected:0));assert(probe.samplerBulkFallbacks-fallbacks==(bulk?0:selected));
            for(unsigned s=0;s<16;++s)for(unsigned i=0;i<7;++i){DWORD value;assert(SUCCEEDED(real->GetSamplerState(s,keys[i],&value)));assert(value==saved[s][i]);}
        }
        mode=previousMode;
        std::printf("NATIVE_SAMPLER_READ_PASS sources=%u requested=%u knownGetterCalls=0 fallbackGetterCalls=%u bulkCallbacks=%u wrongIdentity=disabled partialFailure=ignored unrequestedState=ignored textureReads=preserved restored=exact\n",selected,count,7*selected,selected);
    }
};
