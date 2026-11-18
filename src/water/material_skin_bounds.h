#pragma once
#include "draw_snapshot.h"
#include "material_view_bounds.h"
#include <cfenv>
#include <memory>

// One immutable mesh section owns its envelope and its last evaluated pose.
class NorthlightMaterialSkinBounds {
    using Program=NorthlightActorDeformation::Program;
    using Bounds=NorthlightReplayBounds::Bounds;
    using Skin=NorthlightReplayBounds::SkinEnvelope;
    using Mesh=NorthlightDrawSnapshot::Mesh;

    Skin skin;
    uint64_t programId=0;
    NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;
    std::unique_ptr<float[]> previousRows;
    static constexpr size_t MaxBones=std::extent_v<decltype(Skin::bones)>;
    struct Run {unsigned char first,count;};
    Run runs[(MaxBones+1)/2];
    unsigned runCount=0;
    NorthlightMaterialViewBounds previousCamera;
    unsigned previousMode=0;
    Bounds previous,previousView;

    static unsigned floatingPointMode(){
#if defined(__i386__) || defined(__x86_64__)
        unsigned short control;
        asm volatile("fnstcw %0":"=m"(control));
#if defined(__SSE2__)
        return control|(unsigned(_mm_getcsr()&0xffc0u)<<16);
#else
        return control;
#endif
#else
        return unsigned(std::fegetround());
#endif
    }
    static bool paletteOverrides(const Program& program){
        for(const auto& def:program.definitions)if(def.reg>=31&&def.reg<256)return true;
        return false;
    }
    // Keep the rare shader-defined palette and its 4 KiB stack frame off the hot path.
    __attribute__((noinline)) static bool withDefinitions(const Skin& geometry,const Program& program,
        const float* constants,const float* basis,Bounds& out,bool aligned){
        float effective[256*4];
        std::memcpy(effective+31*4,constants+31*4,(256-31)*16);
        for(const auto& def:program.definitions)if(def.reg>=31&&def.reg<256)
            std::memcpy(effective+4*def.reg,def.value.data(),16);
        return geometry.evaluate(effective,basis,out,nullptr,nullptr,aligned);
    }
    bool sameRows(const float* constants)const{
        const float* saved=previousRows.get();
        for(unsigned n=0;n<runCount;++n){
            const auto run=runs[n];
            if(std::memcmp(saved,constants+4*31+12*run.first,run.count*12*sizeof(float)))return false;
            saved+=12*run.count;
        }
        return true;
    }
public:
    struct Stats {unsigned reused=0,evaluated=0;};
    static std::unique_ptr<NorthlightMaterialSkinBounds> build(const Program& program,const Mesh& mesh,
        const D3DVERTEXELEMENT9* elements,UINT count,uint64_t id,IDirect3DVertexDeclaration9* decl){
        auto result=std::make_unique<NorthlightMaterialSkinBounds>();
        if(!result->skin.addMesh(program,mesh,elements,count))return {};
        for(unsigned bone=0;bone<std::size(result->skin.bones);){
            if(!result->skin.bones[bone].used){++bone;continue;}
            const auto first=bone++;
            while(bone<std::size(result->skin.bones)&&result->skin.bones[bone].used)++bone;
            result->runs[result->runCount++]={static_cast<unsigned char>(first),static_cast<unsigned char>(bone-first)};
        }
        result->programId=id;
        result->declaration.p=decl;
        decl->AddRef();
        result->previousRows.reset(new(std::nothrow) float[12*result->skin.usedBones]);
        return result;
    }
    bool matches(uint64_t id,IDirect3DVertexDeclaration9* decl)const{return programId==id&&declaration.p==decl;}
    const Skin& geometry()const{return skin;}
    size_t bytes()const{return sizeof(*this)+(previousRows?skin.usedBones*12*sizeof(float):0);}
    static bool evaluateUncached(const Skin& geometry,const Program& program,const float* constants,
        const float* basis,Bounds& out,bool aligned=false){
        if(!constants||!basis){out={};return false;}
        if(paletteOverrides(program))return withDefinitions(geometry,program,constants,basis,out,aligned);
        return geometry.evaluate(constants,basis,out,nullptr,nullptr,aligned);
    }
    bool evaluate(const Program& program,const float* constants,const NorthlightMaterialViewBounds& camera,
                  Bounds& world,Bounds& view,Stats& stats){
        const float* basis=camera.inverse();
        if(!previousRows||!constants||paletteOverrides(program)){
            ++stats.evaluated;
            const bool valid=evaluateUncached(skin,program,constants,basis,world,true);
            view=valid?camera.inView(world):Bounds{};
            return valid;
        }
        const unsigned mode=floatingPointMode();
        if(previous.valid&&mode==previousMode&&camera==previousCamera&&sameRows(constants)){
            world=previous;
            view=previousView;
            ++stats.reused;
            return true;
        }
        ++stats.evaluated;
        if(!skin.evaluate(constants,basis,world,nullptr,nullptr,true)){view={};return false;}
        view=camera.inView(world);
        float* saved=previousRows.get();
        for(unsigned n=0;n<runCount;++n){
            const auto run=runs[n];
            std::memcpy(saved,constants+4*31+12*run.first,run.count*12*sizeof(float));
            saved+=12*run.count;
        }
        previousCamera=camera;
        previousMode=mode;
        previous=world;
        previousView=view;
        return true;
    }
};
