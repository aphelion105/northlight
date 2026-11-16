#include "world_gi.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <set>
using namespace NorthlightGI;
static void word(FILE* f,uint32_t v){assert(fwrite(&v,4,1,f)==1);}
static void scalar(FILE* f,float v){assert(fwrite(&v,4,1,f)==1);}
int main(int argc,char** argv){
    assert(argc==2);const std::filesystem::path root=argv[1];
    std::filesystem::create_directories(root);const auto path=root/"lamps.fg3";
    FILE* f=fopen(path.string().c_str(),"wb");assert(f);
    assert(fwrite("FGS3",1,4,f)==4);word(f,3);word(f,300);
    for(unsigned i=0;i<300;++i){
        const unsigned char key[32]={};assert(fwrite(key,1,32,f)==32);
        word(f,i);word(f,0);word(f,2);
        for(float v:{-2.f,-2.f,-2.f,2.f,2.f,2.f,1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f})scalar(f,v);
    }fclose(f);
    auto model=std::make_shared<WorldScene>();
    model->vertices={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{0,1,0},{0,0,1},.5f,1}};
    // Two identical lamps and one different image. Ownership and metadata must
    // stay distinct even when all repeated placements share their texel bytes.
    for(unsigned i=0;i<3;++i){WorldMaterial m;m.width=m.height=512;m.rgba.assign(512*512*4,255);
        m.lampLocalId=i+1;m.albedo={.2f+.1f*i,.4f,.6f};m.addressU=i+1;
        if(i==2)m.rgba[0]=17;model->materials.push_back(std::move(m));model->triangles.push_back({0,1,2,i});}
    SceneSourceProvider provider=[&](const std::string&,std::string&,const AllocationAdmission&){return model;};
    BVH held;std::string error;
    for(bool acceleration:{false,true}){
        LocalSceneCache cache;BVH result;
        const bool ok=cache.build({path.string()},root.string(),"test",{-8,-8,-8},{8,8,8},result,error,{},acceleration,provider);
        if(!ok)std::fprintf(stderr,"%s\n",error.c_str());assert(ok);
        assert(result.scene().materials.size()==900&&result.triangleCount()==900);
        std::set<const uint8_t*> storage;std::set<uint64_t> owners;
        for(size_t i=0;i<900;++i){const auto& m=result.scene().materials[i];const auto& source=model->materials[i%3];
            assert(m.texels()==source.texels()&&m.rgba.empty()&&m.sharedTexels);
            assert(m.albedo.x==source.albedo.x&&m.addressU==source.addressU&&m.wmo&&m.lampLocalId==source.lampLocalId);
            storage.insert(m.texels().data());owners.insert(m.lampSourceId);
        }
        assert(storage.size()==2&&owners.size()==900);
        assert(result.trace({0,0,1},{0,0,-1},0,4).hit);
        cache.reset();assert(result.scene().materials[2].texels()[0]==17);
        LocalSceneCache replacement(LocalSceneCache::Limits{0});BVH next;
        assert(replacement.build({path.string()},root.string(),"test",{-7,-8,-8},{9,8,8},next,error,{},false,provider,{},true,&result));
        for(size_t i=0;i<900;++i)assert(next.material(i).sharedTexels==result.material(i).sharedTexels);
        // A changed pixel cannot borrow a same-sized block from the old scene.
        model->materials[0].rgba[0]=99;BVH changed;replacement.reset();
        assert(replacement.build({path.string()},root.string(),"test",{-7,-8,-8},{9,8,8},changed,error,{},false,provider,{},true,&result));
        assert(changed.material(0).texels()[0]==99&&result.material(0).texels()[0]==255);
        assert(changed.material(0).sharedTexels!=result.material(0).sharedTexels);
        assert(changed.material(1).sharedTexels==result.material(1).sharedTexels);
        model->materials[0].rgba[0]=255;held=std::move(result);
    }
    provider={};model.reset();assert(held.scene().materials[0].texels()[0]==255&&held.scene().materials[899].texels()[0]==17);
    assert(held.trace({0,0,1},{0,0,-1},0,4).hit);
    std::puts("PASS 900 MiB logical materials use 2 MiB immutable texels; distinct lamp owners, bytes, metadata, rays and lifetime retained");
}
