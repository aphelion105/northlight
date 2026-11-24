#pragma once
#include <d3d9.h>
#include "model_normal_slice.h"
#include "world_compiled_shaders.h"

// Directional and flash casters share the mesh declarations. Point shadows
// also borrow the world declaration and the union shader for their warmup.
class NorthlightWorldShadowPrograms {
public:
    struct CachePixel {
        IDirect3DPixelShader9* shader;
        bool textureIndependent;
    };
    struct CaptureWork {
        uint64_t candidates=0,compactCreated=0,fullFallback=0,fullWords=0,compactWords=0;
    };

    NorthlightWorldShadowPrograms()=default;
    NorthlightWorldShadowPrograms(const NorthlightWorldShadowPrograms&)=delete;
    NorthlightWorldShadowPrograms& operator=(const NorthlightWorldShadowPrograms&)=delete;
    ~NorthlightWorldShadowPrograms(){releaseUnion();releaseCasters();}

    IDirect3DPixelShader9* casterPixel()const{return casterPixel_;}
    IDirect3DVertexShader9* casterVertex()const{return casterVertex_;}
    IDirect3DPixelShader9* replay()const{return replay_;}
    IDirect3DVertexShader9* cacheVertex()const{return cacheVertex_;}
    IDirect3DPixelShader9* cachePixel()const{return cachePixel_;}
    IDirect3DPixelShader9* opaqueCache()const{return opaqueCache_;}
    IDirect3DPixelShader9* opaqueFastCache()const{return opaqueFastCache_;}
    IDirect3DPixelShader9* unionShader()const{return union_;}
    IDirect3DVertexDeclaration9* worldLayout()const{return worldLayout_;}
    IDirect3DVertexDeclaration9* liveLayout()const{return liveLayout_;}
    const CaptureWork& captureWork()const{return captureWork_;}

    // Registration uses the submitted bytecode to prove constant and pose ranges.
    const std::vector<uint32_t>* createCapture(IDirect3DDevice9* d,const std::vector<uint32_t>& full,
        std::vector<uint32_t>& compact,IDirect3DVertexShader9*& shader){
        if(NorthlightModelNormalSlice::shadowVertex(full.data(),full.size(),compact)&&compact.size()<full.size()){
            ++captureWork_.candidates;
            const HRESULT result=d->CreateVertexShader(reinterpret_cast<const DWORD*>(compact.data()),&shader);
            if(SUCCEEDED(result)&&shader){
                ++captureWork_.compactCreated;
                captureWork_.fullWords+=full.size();
                captureWork_.compactWords+=compact.size();
                return &compact;
            }
            ++captureWork_.fullFallback;
            drop(shader);
        }
        if(FAILED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(full.data()),&shader)))drop(shader);
        return shader?&full:nullptr;
    }

    CachePixel cachePixel(bool interior,bool opaque)const{
        auto* shader=interior?(opaque&&opaqueFastCache_?opaqueFastCache_:fastCache_):(opaque?opaqueCache_:cachePixel_);
        if(!shader)shader=cachePixel_;
        return {shader,opaque&&(shader==opaqueCache_||shader==opaqueFastCache_)};
    }

    template<class Check> bool createDirectional(IDirect3DDevice9* d,const Check& check){
        return check(pixel(d,kShadowPSShader,casterPixel_),"shadow shader")&&
               check(pixel(d,kShadowReplayPSShader,replay_),"replay shader")&&
               check(vertex(d,kShadowVSShader,casterVertex_),"shadow vertex shader")&&
               check(vertex(d,kShadowCacheVSShader,cacheVertex_),"cached shadow vertex shader")&&
               check(pixel(d,kStaticCasterPSShader,cachePixel_),"cached shadow depth shader")&&
               check(createUnion(d),"shadow union shader");
    }

    void createOptionalCache(IDirect3DDevice9* d){
        pixel(d,kStaticCasterFastPSShader,fastCache_);
        pixel(d,kStaticCasterOpaqueFastPSShader,opaqueFastCache_);
        pixel(d,kStaticCasterOpaquePSShader,opaqueCache_);
    }

    HRESULT createUnion(IDirect3DDevice9* d){return pixel(d,kShadowUnionShader,union_);}

    HRESULT createWorldLayout(IDirect3DDevice9* d){
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        return create(worldLayout_,[&]{return d->CreateVertexDeclaration(elements,&worldLayout_);});
    }

    HRESULT createLiveLayout(IDirect3DDevice9* d){
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        return create(liveLayout_,[&]{return d->CreateVertexDeclaration(elements,&liveLayout_);});
    }

    bool bindMesh(IDirect3DDevice9* d,IDirect3DVertexBuffer9* vertices,IDirect3DIndexBuffer9* indices,bool compact,UINT stride)const{
        return vertices&&indices&&SUCCEEDED(d->SetVertexDeclaration(compact?liveLayout_:worldLayout_))&&
               SUCCEEDED(d->SetStreamSource(0,vertices,0,stride))&&SUCCEEDED(d->SetIndices(indices));
    }

    void releaseUnion(){drop(union_);}
    void releaseCasters(){
        drop(casterPixel_);
        drop(replay_);
        drop(casterVertex_);
        drop(cacheVertex_);
        drop(cachePixel_);
        drop(fastCache_);
        drop(opaqueFastCache_);
        drop(opaqueCache_);
        drop(worldLayout_);
        drop(liveLayout_);
    }

private:
    CaptureWork captureWork_{};
    friend struct PointTerrainCoverageTest;
    friend struct FlashShadowTest;
    template<class T> static void drop(T*& resource){if(resource){resource->Release();resource=nullptr;}}
    template<class T,class Create> static HRESULT create(T*& resource,const Create& allocate){
        if(resource)return S_OK;
        const HRESULT hr=allocate();
        if(FAILED(hr))drop(resource);
        return hr;
    }
    static HRESULT pixel(IDirect3DDevice9* d,const DWORD* bytecode,IDirect3DPixelShader9*& shader){
        return create(shader,[&]{return d->CreatePixelShader(bytecode,&shader);});
    }
    static HRESULT vertex(IDirect3DDevice9* d,const DWORD* bytecode,IDirect3DVertexShader9*& shader){
        return create(shader,[&]{return d->CreateVertexShader(bytecode,&shader);});
    }

    IDirect3DPixelShader9 *casterPixel_=nullptr,*replay_=nullptr,*cachePixel_=nullptr,*union_=nullptr;
    IDirect3DPixelShader9 *fastCache_=nullptr,*opaqueFastCache_=nullptr,*opaqueCache_=nullptr;
    IDirect3DVertexShader9 *casterVertex_=nullptr,*cacheVertex_=nullptr;
    IDirect3DVertexDeclaration9 *worldLayout_=nullptr,*liveLayout_=nullptr;
};
