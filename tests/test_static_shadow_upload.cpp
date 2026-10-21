// CPU/fake-D3D regression for the production worker-prepared upload path.
#define STATIC_SHADOW_GPU_TEST
#include "test_static_shadow_fake_d3d.h"
#include "static_shadow_gpu.h"
#include <cassert>
#include <iostream>
using namespace StaticShadow;
static std::shared_ptr<Model> makeModel(const std::string& key){
    NorthlightGI::WorldScene source;source.vertices={{{0,0,.5f},{0,0,1},0,0},{{.2f,0,.5f},{0,0,1},1,0},{{0,.2f,.5f},{0,0,1},0,1}};source.triangles={{0,1,2,0}};
    NorthlightGI::WorldMaterial mat;mat.width=2;mat.height=1;mat.alphaCutoff=.37f;mat.addressU=3;mat.addressV=2;mat.rgba={2,3,4,17,5,6,7,254};source.materials.push_back(mat);
    auto model=std::make_shared<Model>();std::string error;assert(packModel(std::move(source),key,*model,error));return model;
}
static std::shared_ptr<Snapshot> snapshot(unsigned count){auto s=std::make_shared<Snapshot>();s->map="test";s->mapGeneration=1;
    for(unsigned i=0;i<count;++i){auto m=makeModel("tree"+std::to_string(i));s->models[m->key]=m;Placement p;p.uid=i;p.category=1;p.modelKey=m->key;s->placementGroups[m->key].push_back(s->placements.size());s->placements.push_back(p);}s->preparedIdentity=s.get();return s;}
int main(){
    IDirect3DDevice9 d;GpuCache::Limits limits;limits.uploadMs=1000;GpuCache gpu(limits);auto s=snapshot(1);const auto& model=*s->models.begin()->second;
    assert(model.uploadPrepared&&model.indices.empty()&&model.indexCount()==3&&model.indexAt(2)==2&&model.drawModel);
    const std::vector<uint8_t> expected={255,255,255,17,255,255,255,254};
    assert(model.materials[0].rgba.capacity()==0&&model.alphaAt(0,0)==17&&model.alphaAt(0,1)==254);
    NorthlightStreaming::Budget expired(100,0);gpu.update(&d,s,1,true,&expired);assert(!d.indexBuffers&&!gpu.stats().frameBytes);
    for(unsigned f=2;f<12;++f){NorthlightStreaming::Budget budget(20,1000);assert(gpu.update(&d,s,f,true,&budget));assert(gpu.stats().frameBytes<=20&&budget.remainingBytes()==20-gpu.stats().frameBytes);}
    assert(gpu.stats().readyModels==1&&gpu.coverage(s->placements[0],model.contentRevision));
    assert(d.lastIndex->bytes==std::vector<uint8_t>({0,0,1,0,2,0}));assert(d.lastTexture->bytes==expected);
    float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};assert(gpu.draw(&d,matrix)&&gpu.stats().instances==1);assert(d.pixelDraws[1]==1&&!d.pixelDraws[2]);
    // Copy then edit snapshot: worker group identity must not be trusted.
    auto copy=std::make_shared<Snapshot>(*s);copy->placements.push_back(copy->placements[0]);copy->placements.back().uid=55;gpu.update(&d,copy,12);assert(gpu.draw(&d,matrix)&&gpu.stats().instances==2);
    gpu.reset();assert(FakeResource::alive==0);
    // Admission callbacks reserve host/device bytes: charge each allocation once.
    GpuCache admitted(limits);size_t reserved=0;unsigned admissionCalls=0;
    admitted.setAdmission([&](size_t bytes){++admissionCalls;if(bytes>148-reserved)return false;reserved+=bytes;return true;});
    assert(admitted.update(&d,s,1));assert(admitted.stats().readyModels==1&&reserved==148&&admissionCalls==2);
    admitted.reset();assert(FakeResource::alive==0);
    // Identical prepared masks share a texture. A forced hash collision with
    // a different alpha must not share; empty RGBA does not mean opaque.
    for(bool collision:{false,true})for(int raw=-1;raw<2;++raw){
        IDirect3DDevice9 device;GpuCache cache(limits);auto pair=snapshot(2);
        if(collision){auto changed=std::make_shared<Model>(*pair->models.at("tree1"));auto mask=std::make_shared<std::vector<uint8_t>>(*changed->uploadAlpha[0]);(*mask)[0]^=1;changed->uploadAlpha[0]=mask;pair->models["tree1"]=changed;}
        if(raw>=0){const auto key="tree"+std::to_string(raw);auto legacy=std::make_shared<Model>(*pair->models.at(key));auto& rgba=legacy->materials[0].rgba;rgba.assign(8,255);for(size_t i=0;i<2;++i)rgba[i*4+3]=legacy->alphaAt(0,i);legacy->indices={0,1,2};legacy->uploadIndices16.clear();legacy->uploadAlpha.clear();legacy->drawModel.reset();legacy->uploadPrepared=false;pair->models[key]=legacy;}
        assert(cache.update(&device,pair,1));assert(cache.stats().readyModels==2&&device.textures==(collision?2u:1u));
        assert(cache.draw(&device,matrix)&&device.pixelDraws[1]==2&&!device.pixelDraws[2]);cache.reset();assert(FakeResource::alive==0);
    }
    // Odd width, multiple upload slices, every alpha byte value and white RGB.
    {
        auto scene=NorthlightGI::WorldScene();scene.vertices={{{0,0,.5f},{0,0,1},0,0},{{.2f,0,.5f},{0,0,1},1,0},{{0,.2f,.5f},{0,0,1},0,1}};scene.triangles={{0,1,2,0}};
        NorthlightGI::WorldMaterial mat;mat.width=131;mat.height=65;mat.alphaCutoff=.37f;mat.rgba.resize(131*65*4);std::vector<uint8_t> pixels(mat.rgba.size(),255);
        for(size_t i=0;i<131*65;++i){mat.rgba[i*4]=uint8_t(i*7);mat.rgba[i*4+3]=pixels[i*4+3]=uint8_t(i);}
        scene.materials.push_back(std::move(mat));auto prepared=std::make_shared<Model>();std::string error;assert(packModel(std::move(scene),"odd-mask",*prepared,error));
        assert(prepared->materials[0].rgba.capacity()==0&&prepared->uploadAlpha[0]->size()==131*65);
        auto one=std::make_shared<Snapshot>();one->map="test";one->mapGeneration=1;one->models[prepared->key]=prepared;Placement p;p.category=1;p.modelKey=prepared->key;one->placements.push_back(p);
        IDirect3DDevice9 device;GpuCache cache(limits);
        for(unsigned frame=1;frame<100&&!cache.stats().readyModels;++frame){NorthlightStreaming::Budget budget(3000,1000);assert(cache.update(&device,one,frame,true,&budget));assert(cache.stats().frameBytes<=3000);}
        assert(cache.stats().readyModels==1&&device.lastTexture->bytes==pixels);assert(cache.draw(&device,matrix)&&device.pixelDraws[1]==1);cache.reset();assert(FakeResource::alive==0);
    }
    // Retirement stays accounted until bounded D3D releases actually finish.
    GpuCache::Limits bounded;bounded.uploadMs=1000;bounded.retainFrames=0;GpuCache bulk(bounded);auto large=snapshot(100);for(unsigned f=1;f<20&&bulk.stats().readyModels<100;++f)assert(bulk.update(&d,large,f));assert(bulk.stats().readyModels==100);
    auto empty=std::make_shared<Snapshot>();empty->map="test";empty->mapGeneration=1;
    {NorthlightStreaming::Budget budget(2u<<20,1000);bulk.update(&d,empty,30,false,&budget);assert(bulk.stats().residentBytes>0&&bulk.stats().readyModels>0);}
    for(unsigned f=31;f<80;++f){NorthlightStreaming::Budget budget(2u<<20,1000);bulk.update(&d,empty,f,false,&budget);}assert(!bulk.stats().residentBytes&&!bulk.stats().readyModels);bulk.reset();assert(FakeResource::alive==0);
    std::cout<<"worker-prepared upload, shared budget, copied groups and bounded retirement tests passed\n";
}
