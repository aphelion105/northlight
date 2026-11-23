#include "directional_gpu_pass.h"
#include "shadow_pass_device.h"
#include "world_directional_visibility.h"
#include <cstdio>
#include <cstring>

using NorthlightDirectionalShadow::GpuPass;
using NorthlightDirectionalShadow::TerrainWork;
using NorthlightWorldMeshPages::Batch;

struct Check {
    const char*& failure;
    bool operator()(HRESULT hr,const char* operation) const {
        if(FAILED(hr))failure=operation;
        return SUCCEEDED(hr);
    }
};
static IDirect3DSurface9* surface(unsigned n){return handle<IDirect3DSurface9>(n);}
static IDirect3DPixelShader9* pixel(unsigned n){return handle<IDirect3DPixelShader9>(n);}
static const float matrix[16]={};

static void cascadeAndActor(){
    Device device;
    const char* failure=nullptr;
    GpuPass pass(&device,Check{failure},surface(1));
    const GpuPass<Device,Check>::Geometry terrain{handle<IDirect3DVertexDeclaration9>(2),handle<IDirect3DVertexBuffer9>(3),handle<IDirect3DIndexBuffer9>(4),36};
    const auto vertex=handle<IDirect3DVertexShader9>(5);
    pass.configureCasterSampler();
    assert(device.calls==(std::vector<Call>{{Sampler,0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP},
        {Sampler,0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP},{Sampler,0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR},
        {Sampler,0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{Sampler,0,D3DSAMP_MIPFILTER,D3DTEXF_NONE},
        {Sampler,0,D3DSAMP_SRGBTEXTURE,FALSE}}));
    device.calls.clear();
    assert(pass.beginCascade(surface(6),terrain,matrix,vertex,pixel(7)));
    const std::vector<Call> expected{{Depth},{Target,0,6},{Depth,1},{Viewport,1024,1024},{Op::Clear,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff},
        {Layout,2},{Stream,3,36},{Frequency,0,1},{Frequency,1,1},{Frequency,2,1},{Frequency,3,1},{Index,4},{Vertex,5},{VertexRows,0,4},{Pixel,7}};
    assert(device.calls==expected&&!failure);

    device.calls.clear();
    assert(pass.beginNearReplay(surface(6),true));
    const std::vector<Call> preserved{{Depth},{Target,0,6},{Depth,1},{Viewport,1024,1024},
        {Sampler,0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP},{Sampler,0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP}};
    assert(device.calls==preserved);
    device.calls.clear();
    assert(pass.beginNearReplay(surface(6),false));
    auto cleared=preserved;
    cleared.insert(cleared.begin()+4,{Op::Clear,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff});
    assert(device.calls==cleared);

    device.calls.clear();
    assert(pass.beginActor(surface(8)));
    pass.bindActorProjection(matrix,pixel(9));
    const std::vector<Call> actors{{Depth},{Target,0,8},{Depth,1},{Viewport,1024,1024},
        {Op::Clear,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff},{VertexRows,0,4},{Pixel,9}};
    assert(device.calls==actors);
    device.calls.clear();
    assert(pass.beginActor(surface(8),false));
    assert(device.calls==std::vector<Call>(actors.begin(),actors.begin()+4));

    device.calls.clear();
    device.fault={Target,0,6};
    assert(!pass.beginCascade(surface(6),terrain,matrix,vertex,pixel(7)));
    assert(device.calls==std::vector<Call>(expected.begin(),expected.begin()+2));
    assert(failure&&std::strcmp(failure,"shadow scratch target")==0);
    device.calls.clear();
    device.fault={Op::Clear,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff};
    assert(!pass.beginActor(surface(8)));
    assert(device.calls==std::vector<Call>(actors.begin(),actors.begin()+5));
    assert(std::strcmp(failure,"clear actor shadow")==0);
}

static void terrain(bool legacy){
    Device device;
    const char* failure=nullptr;
    GpuPass pass(&device,Check{failure},surface(1));
    Batch a,b,c;
    a.count=2;a.vertexCount=4;
    b.start=6;b.count=1;b.minVertex=4;b.vertexCount=3;
    c.start=9;c.count=1;c.minVertex=9;c.vertexCount=3;c.material=1;
    const float cutoffs[]={-1,.5f};
    IDirect3DTexture9* materials[]={handle<IDirect3DTexture9>(11),handle<IDirect3DTexture9>(12)};
    const auto visit=[&](const auto& submit){return submit(a)&&submit(b)&&submit(c);};
    const auto bindPage=[&](const Batch& batch,UINT& page){
        if(page==batch.page)return true;
        page=batch.page;
        return SUCCEEDED(device.record({Geometry,page}));
    };
    TerrainWork work;
    assert(pass.terrain(pixel(7),100,legacy,cutoffs,materials,work,visit,bindPage));
    assert(work.batches==3&&work.materialBinds==(legacy?3u:2u)&&work.draws==(legacy?3u:2u));
    std::vector<Call> expected{{PixelRows,0,1},{Texture,0,11},{Geometry,0}};
    if(legacy)expected.insert(expected.end(),{{IndexedDraw,0,2,4},{PixelRows,0,1},{Texture,0,11},{IndexedDraw,6,1,(4u<<16)|3}});
    else expected.push_back({IndexedDraw,0,3,7});
    expected.insert(expected.end(),{{PixelRows,0,1},{Texture,0,12},{IndexedDraw,9,1,(9u<<16)|3}});
    assert(device.calls==expected&&!failure);

    device.calls.clear();
    const GpuPass<Device,Check>::Geometry live{handle<IDirect3DVertexDeclaration9>(13),handle<IDirect3DVertexBuffer9>(14),handle<IDirect3DIndexBuffer9>(15),20};
    const auto restore=handle<IDirect3DVertexDeclaration9>(2);
    const auto touch=[&]{device.record({Touch});};
    assert(pass.liveTerrain(live,restore,25,7,4,touch));
    const std::vector<Call> liveCalls{{PixelRows,0,1},{Texture,0},{Layout,13},{Stream,14,20},{Index,15},{Touch},{IndexedDraw,7,4,25},{Layout,2}};
    assert(device.calls==liveCalls);
    device.calls.clear();
    device.fault={Stream,14,20};
    assert(!pass.liveTerrain(live,restore,25,7,4,touch));
    assert(device.calls==std::vector<Call>(liveCalls.begin(),liveCalls.begin()+4));
    assert(failure&&std::strcmp(failure,"live terrain vertices")==0);
}

static void allocationAndReset(){
    const std::vector<unsigned> order{1024,1024,1024,1024,1024,1280,1280,1280,1280,1280,1024};
    NorthlightWorldDirectionalTargets targets;
    for(size_t refusal=0;refusal<order.size();++refusal){
        std::vector<unsigned> requests;
        const auto allocate=[&](UINT w,UINT h){assert(w==h);requests.push_back(w);return requests.size()!=refusal+1;};
        assert(!targets.create([&](UINT w,UINT h,D3DFORMAT fmt,IDirect3DTexture9**,IDirect3DSurface9**){
            assert(fmt==D3DFMT_R32F);return allocate(w,h);
        },[&](UINT w,UINT h,IDirect3DSurface9**,const char*){return allocate(w,h);}));
        assert(requests==std::vector<unsigned>(order.begin(),order.begin()+refusal+1));
        targets.releaseMaps();
        targets.visit([](IDirect3DTexture9* p){assert(!p);},[](IDirect3DSurface9* p){assert(!p);});
    }
    Device device;
    NorthlightDirectionalShadow::prepareDevice(&device);
    const Call first{Depth},last{State,D3DRS_MULTISAMPLEMASK,0xffffffff};
    assert(device.calls.size()==35&&device.calls.front()==first&&device.calls.back()==last);
}

static void cacheDraw(){
    using Raster=GpuPass<Device,Check>;
    constexpr auto DepthFlags=D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER;
    Device device;
    const char* failure=nullptr;
    Raster pass(&device,Check{failure},surface(1));
    const Raster::Geometry geometry{handle<IDirect3DVertexDeclaration9>(2),handle<IDirect3DVertexBuffer9>(3),handle<IDirect3DIndexBuffer9>(4),36};
    const Raster::CachePrograms programs{handle<IDirect3DVertexShader9>(5),pixel(7),pixel(8),pixel(9)};
    const D3DRECT rectangles[]={{10,20,30,40},{50,60,70,80}};
    const NorthlightShadowBounds::TexelRect first{10,20,30,40},second{50,60,70,80};
    const float cutoffs[]={0,.5f};
    IDirect3DTexture9* materials[]={handle<IDirect3DTexture9>(11),handle<IDirect3DTexture9>(12)};
    Batch opaque,cutout;
    opaque.count=2;opaque.vertexCount=4;
    cutout.count=1;cutout.vertexCount=3;cutout.minVertex=9;cutout.start=17;cutout.material=1;
    const auto bindPage=[&](const Batch& batch,UINT& page){
        if(page==batch.page)return true;
        page=batch.page;
        return SUCCEEDED(device.record({Geometry,page}));
    };
    const std::vector<Call> bind{{Depth},{Target,0,6},{Depth,10},{Viewport,1280,1280}};
    {
        auto cache=pass.cache(false,programs);
        assert(cache.bind(surface(6),surface(10))&&cache.clear());
        auto expected=bind;
        expected.push_back({Op::Clear,DepthFlags,0xffffffff});
        assert(device.calls==expected);
    }
    device.calls.clear();
    {
        auto cache=pass.cache(true,programs);
        assert(cache.bind(surface(6),surface(10))&&cache.clear(2,rectangles));
        assert(device.cleared.size()==2&&!std::memcmp(device.cleared.data(),rectangles,sizeof rectangles));
        cache.bindCasters(geometry,matrix);
        assert(!cache.scissorDisabled());
        assert(cache.draw(opaque,pixel(8),&first,cutoffs,materials,bindPage));
        assert(cache.draw(cutout,pixel(7),&first,cutoffs,materials,bindPage));
        assert(cache.draw(cutout,pixel(7),&second,cutoffs,materials,bindPage));
    }
    auto expected=bind;
    expected.insert(expected.end(),{
        {State,D3DRS_SCISSORTESTENABLE,FALSE},{Op::Clear,DepthFlags,0xffffffff,2},{State,D3DRS_SCISSORTESTENABLE,TRUE},
        {Layout,2},{Stream,3,36},{Frequency,0,1},{Frequency,1,1},{Frequency,2,1},{Frequency,3,1},{Index,4},
        {Vertex,5},{VertexRows,0,4},{Pixel,7},
        {Pixel,8},{Geometry,0},{Scissor,10,20,(30u<<16)|40},{IndexedDraw,0,2,4},
        {Pixel,7},{PixelRows,0,1},{Texture,0,12},{IndexedDraw,17,1,(9u<<16)|3},
        {Scissor,50,60,(70u<<16)|80},{IndexedDraw,17,1,(9u<<16)|3},
        {State,D3DRS_SCISSORTESTENABLE,FALSE}
    });
    assert(device.calls==expected&&!device.scissor&&!failure);

    const Call faults[]={{Target,0,6},{Op::Clear,DepthFlags,0xffffffff,2},{Scissor,10,20,(30u<<16)|40},{IndexedDraw,0,2,4}};
    const char* stages[]={"shadow cache target","clear shadow cache rects","static cache scissor","static shadow cache draw"};
    for(unsigned i=0;i<4;++i){
        failure=nullptr;device.calls.clear();device.fault=faults[i];
        {
            auto cache=pass.cache(true,programs);
            const bool ok=cache.bind(surface(6),surface(10))&&cache.clear(2,rectangles)&&
                          cache.draw(opaque,pixel(8),&first,cutoffs,materials,bindPage);
            assert(!ok&&failure&&std::strcmp(failure,stages[i])==0);
        }
        const Call scissorOff{State,D3DRS_SCISSORTESTENABLE,FALSE};
        assert(device.calls.size()>=2&&device.calls.back()==scissorOff&&!device.scissor);
        assert(device.calls[device.calls.size()-2]==faults[i]);
    }
}

static void visibilityResolve(){
    struct VisibilityDevice:Device {
        unsigned draws=0;
        HRESULT SetPixelShaderConstantF(UINT first,const float* rows,UINT count){
            if(first==68)assert(rows==matrix&&count==4);
            if(first==72)assert(count==1&&rows[0]==1&&rows[1]==.25f&&rows[2]==16&&rows[3]==0);
            return Device::SetPixelShaderConstantF(first,rows,count);
        }
        HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride){
            assert(viewportWidth==128&&viewportHeight==80);
            checkQuad(type,count,data,stride);
            return record({Quad,++draws});
        }
    } device;
    NorthlightWorldDirectionalVisibility visibility;
    const float info[]={0,.25f,16,0};
    IDirect3DTexture9* textures[16]={};
    for(unsigned slot=6;slot<10;++slot)textures[slot]=handle<IDirect3DTexture9>(slot);
    const auto bindSource=[&](int source){device.record({Geometry,unsigned(source)});};
    const auto bindTransition=[&](int source,bool previous){device.record({Copy,unsigned(source),previous});};
    const std::vector<Call> setup{{Texture,15},{Target},{Op::Clear,D3DCLEAR_TARGET,0xffffffff},{Pixel}};
    NorthlightWorldDirectionalVisibility::Resolve input{128,80,true,false,info,nullptr,nullptr,
        {{true,true,false,matrix,handle<IDirect3DTexture9>(12),handle<IDirect3DTexture9>(10)}, {}}};
    const auto resolve=[&]{return visibility.resolve(&device,input,textures,bindSource,bindTransition);};
    assert(resolve());
    auto shared=setup;
    shared.insert(shared.end(),{{Geometry,0},{PixelRows,68,4},{PixelRows,72,1},{Texture,12,12},{Texture,10,10},
        {State,D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED},{Viewport,128,80},{Quad,1},{State,D3DRS_COLORWRITEENABLE,15}});
    assert(device.calls==shared);

    input={128,80,false,true,info,pixel(1),pixel(2),{{true,false,false},{true,false,true}}};
    const std::vector<Call> raster{{Geometry,0},{Copy,0},{Pixel,1},{State,D3DRS_ALPHABLENDENABLE,FALSE},
        {State,D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED},{Viewport,128,80},{Quad,1},
        {Geometry,1},{Copy,1},{Pixel,1},{State,D3DRS_ALPHABLENDENABLE,FALSE},
        {State,D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_GREEN},{Viewport,128,80},{Quad,2},
        {Copy,1,1},{Pixel,2},{State,D3DRS_ALPHABLENDENABLE,TRUE},{State,D3DRS_SRCBLEND,D3DBLEND_ONE},
        {State,D3DRS_DESTBLEND,D3DBLEND_ONE},{Viewport,128,80},{Quad,3}};
    std::vector<Call> restore;
    for(unsigned slot=6;slot<10;++slot){
        const auto filter=slot==9?D3DTEXF_LINEAR:D3DTEXF_POINT;
        restore.insert(restore.end(),{{Texture,slot,slot},{Sampler,slot,D3DSAMP_MINFILTER,filter},
            {Sampler,slot,D3DSAMP_MAGFILTER,filter}});
    }
    restore.insert(restore.end(),{{State,D3DRS_ALPHABLENDENABLE,FALSE},{State,D3DRS_COLORWRITEENABLE,15}});
    for(bool fail:{false,true}){
        device.draws=0;
        device.calls.clear();
        if(fail)device.fault={Quad,2};
        assert(resolve()!=fail);
        auto expected=setup;
        expected.insert(expected.end(),raster.begin(),fail?raster.begin()+14:raster.end());
        expected.insert(expected.end(),restore.begin(),restore.end());
        assert(device.calls==expected);
    }
    std::puts("DIRECTIONAL_VISIBILITY_PASS shared actor textures, two-source channel order and both epochs, sampler and blend restoration on failure");
}

int main(){
    cascadeAndActor();
    terrain(false);
    terrain(true);
    allocationAndReset();
    cacheDraw();
    visibilityResolve();
    std::puts("DIRECTIONAL_GPU_PASS cascade/actor/terrain order, cache scissors, near rebind, allocation refusal and reset baseline");
}
