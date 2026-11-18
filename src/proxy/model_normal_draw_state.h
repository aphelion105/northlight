#pragma once
#include <d3d9.h>

struct NorthlightModelNormalDrawState {
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    inline static constexpr D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,
        D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,
        D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_COLORWRITEENABLE,D3DRS_WRAP5,D3DRS_WRAP7};
    static constexpr unsigned Count=sizeof(keys)/sizeof(keys[0]);
    IDirect3DDevice9* d;IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    IDirect3DSurface9 *rt[4]={},*ds=nullptr;IDirect3DVertexBuffer9* stream=nullptr;IDirect3DIndexBuffer9* index=nullptr;
    UINT offset=0,stride=0;D3DVIEWPORT9 viewport{};DWORD values[Count]={};float constant[4]={};unsigned control,renderChanged=0,targetChanged=0;bool changed=false,up;
    NorthlightModelNormalDrawState(IDirect3DDevice9* device,unsigned c,bool user):d(device),control(c),up(user){}
    NorthlightModelNormalDrawState(const NorthlightModelNormalDrawState&)=delete;
    NorthlightModelNormalDrawState& operator=(const NorthlightModelNormalDrawState&)=delete;

    bool save(bool full=true){
        if(FAILED(d->GetViewport(&viewport))||FAILED(d->GetRenderTarget(0,&rt[0])))return false;
        return !full||saveRest();
    }
    bool saveRest(IDirect3DSurface9* frameTarget=nullptr){
        // A deferred draw proved target identity without taking a reference.
        // Immediate replay must own the target before changing any binding.
        if(!rt[0]){
            if(!frameTarget)return false;
            frameTarget->AddRef();
            rt[0]=frameTarget;
        }
        if(FAILED(d->GetVertexShader(&vs))||FAILED(d->GetPixelShader(&ps))||
           FAILED(d->GetDepthStencilSurface(&ds))||FAILED(d->GetPixelShaderConstantF(control,constant,1)))return false;
        for(unsigned i=1;i<4;++i){auto hr=d->GetRenderTarget(i,&rt[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
        for(unsigned i=0;i<Count;++i)if(FAILED(d->GetRenderState(keys[i],values+i)))return false;
        return !up||(SUCCEEDED(d->GetStreamSource(0,&stream,&offset,&stride))&&SUCCEEDED(d->GetIndices(&index)));
    }
    bool restore(){
        if(!changed)return true;changed=false;
        auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
        ok=retry([&]{return d->SetDepthStencilSurface(nullptr);})&&ok;
        for(unsigned i=1;i<4;++i)if(targetChanged&(1u<<i))ok=retry([&]{return d->SetRenderTarget(i,nullptr);})&&ok;
        for(unsigned i=0;i<4;++i)if(!i||(targetChanged&(1u<<i)))ok=retry([&]{return d->SetRenderTarget(i,rt[i]);})&&ok;
        ok=retry([&]{return d->SetDepthStencilSurface(ds);})&&ok;
        ok=retry([&]{return d->SetViewport(&viewport);})&&ok;
        ok=retry([&]{return d->SetVertexShader(vs);})&&ok;ok=retry([&]{return d->SetPixelShader(ps);})&&ok;
        ok=retry([&]{return d->SetPixelShaderConstantF(control,constant,1);})&&ok;
        for(unsigned i=0;i<Count;++i)if(renderChanged&(1u<<i))ok=retry([&]{return d->SetRenderState(keys[i],values[i]);})&&ok;
        if(up){ok=retry([&]{return d->SetStreamSource(0,stream,offset,stride);})&&ok;ok=retry([&]{return d->SetIndices(index);})&&ok;}
        return ok;
    }
    ~NorthlightModelNormalDrawState(){restore();release(vs);release(ps);for(auto*& p:rt)release(p);release(ds);release(stream);release(index);}
};
