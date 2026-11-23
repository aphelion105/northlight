#pragma once
#include <d3d9.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

enum Op {Texture, Target, Frequency, State, Depth, Viewport, Clear, Pixel, PixelRows, Layout,
         Vertex, VertexRows, Stream, Index, Sampler, Fvf, Stage, Copy, Geometry, StaticDraw, DynamicDraw, Quad, IndexedDraw, Touch, Scissor, GetSampler};
struct Call {
    Op op;
    std::uintptr_t a=0,b=0,c=0;
    bool operator==(const Call& other) const {return op==other.op&&a==other.a&&b==other.b&&c==other.c;}
};
template<class T> static auto handle(std::uintptr_t id){return reinterpret_cast<T*>(id);}
template<class T> static auto id(T* object){return reinterpret_cast<std::uintptr_t>(object);}

struct Device {
    std::vector<Call> calls;
    std::vector<D3DRECT> cleared;
    Call fault={Quad,999};
    std::uintptr_t attached=0;
    DWORD scissor=FALSE;
    DWORD minFilter=D3DTEXF_LINEAR,magFilter=D3DTEXF_LINEAR;
    UINT viewportWidth=0,viewportHeight=0;
    HRESULT record(Call call){
        calls.push_back(call);
        if(call==fault){fault={Quad,999};return E_FAIL;}
        return D3D_OK;
    }
    HRESULT SetTexture(DWORD slot,IDirect3DBaseTexture9* value){return record({Texture,slot,id(value)});}
    HRESULT SetRenderTarget(DWORD slot,IDirect3DSurface9* value){
        const auto hr=record({Target,slot,id(value)});
        if(slot==1&&SUCCEEDED(hr))attached=id(value);
        return hr;
    }
    HRESULT SetStreamSourceFreq(DWORD stream,UINT rate){return record({Frequency,stream,rate});}
    HRESULT SetRenderState(D3DRENDERSTATETYPE state,DWORD value){
        const auto hr=record({State,state,value});
        if(state==D3DRS_SCISSORTESTENABLE&&SUCCEEDED(hr))scissor=value;
        return hr;
    }
    HRESULT GetRenderState(D3DRENDERSTATETYPE state,DWORD* value){
        assert(state==D3DRS_SCISSORTESTENABLE);
        *value=scissor;
        return D3D_OK;
    }
    HRESULT SetScissorRect(const RECT* rect){
        return record({Scissor,std::uintptr_t(rect->left),std::uintptr_t(rect->top),
                               (std::uintptr_t(rect->right)<<16)|std::uintptr_t(rect->bottom)});
    }
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* value){return record({Depth,id(value)});}
    HRESULT SetViewport(const D3DVIEWPORT9* value){
        assert(!value->X&&!value->Y&&value->MinZ==0&&value->MaxZ==1);
        const auto hr=record({Viewport,value->Width,value->Height});
        if(SUCCEEDED(hr)){viewportWidth=value->Width;viewportHeight=value->Height;}
        return hr;
    }
    HRESULT Clear(DWORD count,const D3DRECT* rect,DWORD flags,D3DCOLOR color,float z,DWORD stencil){
        assert(bool(count)==bool(rect)&&z==1&&!stencil);
        if(count){assert(!scissor);cleared.assign(rect,rect+count);}
        else cleared.clear();
        return record({Op::Clear,flags,color,count});
    }
    HRESULT SetPixelShader(IDirect3DPixelShader9* shader){return record({Pixel,id(shader)});}
    HRESULT SetPixelShaderConstantF(UINT first,const float* rows,UINT count){
        if(first==1)assert(count==1&&rows[0]==144&&rows[1]==1.f/256&&rows[2]==0&&rows[3]==0);
        if(count==2)assert(first==0&&rows[3]==256&&rows[4]==1.f/256&&rows[5]==1.f/256);
        return record({PixelRows,first,count});
    }
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* value){return record({Layout,id(value)});}
    HRESULT SetVertexShader(IDirect3DVertexShader9* value){return record({Vertex,id(value)});}
    HRESULT SetVertexShaderConstantF(UINT first,const float*,UINT count){return record({VertexRows,first,count});}
    HRESULT SetStreamSource(UINT stream,IDirect3DVertexBuffer9* value,UINT offset,UINT stride){
        assert(!stream&&!offset);
        return record({Stream,id(value),stride});
    }
    HRESULT SetIndices(IDirect3DIndexBuffer9* value){return record({Index,id(value)});}
    HRESULT GetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE state,DWORD* value){
        assert(!sampler&&(state==D3DSAMP_MINFILTER||state==D3DSAMP_MAGFILTER));
        *value=state==D3DSAMP_MINFILTER?minFilter:magFilter;
        return record({GetSampler,sampler,state});
    }
    HRESULT SetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE state,DWORD value){
        const auto hr=record({Sampler,sampler,state,value});
        if(SUCCEEDED(hr)&&sampler==0){
            if(state==D3DSAMP_MINFILTER)minFilter=value;
            if(state==D3DSAMP_MAGFILTER)magFilter=value;
        }
        return hr;
    }
    HRESULT SetFVF(DWORD value){return record({Fvf,value});}
    HRESULT SetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD value){return record({Stage,stage,state,value});}
    void checkQuad(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride) const {
        assert(type==D3DPT_TRIANGLELIST&&count==1&&stride==6*sizeof(float)&&viewportWidth&&viewportHeight);
        const float expected[]={-.5f,-.5f,0,1,0,0,2.f*viewportWidth-.5f,-.5f,0,1,2,0,
                                -.5f,2.f*viewportHeight-.5f,0,1,0,2};
        assert(!std::memcmp(data,expected,sizeof expected));
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride){
        checkQuad(type,count,data,stride);
        return record({Quad,viewportWidth,viewportHeight});
    }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT count){
        assert(type==D3DPT_TRIANGLELIST&&!base);
        return record({IndexedDraw,start,count,std::uintptr_t(min)<<16|vertices});
    }
    HRESULT StretchRect(IDirect3DSurface9* from,const RECT* source,IDirect3DSurface9* to,const RECT* target,D3DTEXTUREFILTERTYPE filter){
        assert(!source&&!target&&filter==D3DTEXF_NONE);
        return record({Copy,id(from),id(to)});
    }
};

