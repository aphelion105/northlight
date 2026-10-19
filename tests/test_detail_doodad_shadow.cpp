#include "detail_doodad_shadow.h"
#include "actor_deformation.h"
#include "replay_bounds.h"
#include "shader_tags.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
using Word=std::uint32_t;
static void policy(){
    using namespace NorthlightDetailShadow;
    assert(shader(0x7f2707e33c6c97beULL)&&shader(0xc8e41181431a84c7ULL));
    assert(!shader(0)&&!shader(kTerrainVS[0]));
    Budget b;assert(!b.reserve(0,0)&&!b.reserve(Budget::MaxVertices+1,1));
    assert(b.reserve(8,256)&&b.take(8,256));
    assert(!b.reserve(8,Budget::MaxBytes)&&b.attempts==1&&b.readVertices==8);
    for(unsigned i=1;i<Budget::MaxDraws;++i)assert(b.reserve(8,256));
    assert(!b.reserve(8,256));
    b={};assert(b.reserve(8192,512*1024)&&!b.room(1));assert(b.take(8192,512*1024));assert(!b.take(1,1));
    float camera[]={0,0,0};NorthlightReplayBounds::Bounds bounds;bounds.valid=true;
    bounds.low[0]=64;bounds.high[0]=65;assert(!NorthlightReplayBounds::outsideSphere(bounds,camera,64));
    bounds.low[0]=64.01f;assert(NorthlightReplayBounds::outsideSphere(bounds,camera,64));
}
static std::vector<Word> synthetic(){
    // Affine column model-view followed by the native DetailDoodad column projection.
    return {0xfffe0300,0x0200001f,0x80000000,0x900f0000,
        0x0200001f,0x80000000,0xe00f0000,
        0x03000005,0x800f0000,0xa0e40001,0x90550000,
        0x04000004,0x800f0000,0x90000000,0xa0e40000,0x80e40000,
        0x04000004,0x800f0000,0x90aa0000,0xa0e40002,0x80e40000,
        0x03000002,0x800f0000,0x80e40000,0xa0e40003,
        0x03000005,0x800f0001,0x80550000,0xa0e40005,
        0x04000004,0x800f0001,0x80000000,0xa0e40004,0x80e40001,
        0x04000004,0x800f0001,0x80aa0000,0xa0e40006,0x80e40001,
        0x04000004,0xe00f0000,0x80ff0000,0xa0e40007,0x80e40001,0xffff};
}
static void positions(const NorthlightActorDeformation::Program& program){
    using namespace NorthlightActorDeformation;
    assert(!program.skinned&&program.inputs.size()==1&&program.operations.size()==4);
    float constants[1024]={};constants[0]=constants[5]=constants[10]=constants[15]=1;
    constants[12]=10;constants[13]=20;constants[14]=30;
    Four input[16]={},position;input[0]={2,3,4,1};
    assert(evaluate(program,input,constants,position)&&position==Four({12,23,34,1}));
    NorthlightDrawSnapshot::Mesh mesh;mesh.topology=D3DPT_TRIANGLELIST;mesh.vertexCount=3;mesh.primitiveCount=1;mesh.indexed=true;mesh.indices={0,1,2};
    mesh.streams[0].stride=36;mesh.streams[0].bytes.resize(3*36);
    const float points[3][3]={{2,3,4},{-8,7,20},{32,-3,-4}};
    for(unsigned i=0;i<3;++i)std::memcpy(mesh.streams[0].bytes.data()+36*i,points[i],12);
    D3DVERTEXELEMENT9 declaration[]={{0,0,2,0,0,0},{0,12,2,0,3,0},{0,24,4,0,10,0},{0,28,1,0,5,0},{0xff,0,17,0,0,0}};
    float inverse[16]={};for(unsigned i=0;i<4;++i)inverse[5*i]=1;
    inverse[12]=1000;inverse[13]=-500;inverse[14]=200;
    NorthlightReplayBounds::Budget budget;NorthlightReplayBounds::Bounds original;
    assert(NorthlightReplayBounds::calculate(program,mesh,declaration,5,constants,inverse,budget,original)==NorthlightReplayBounds::Status::Valid);
    assert(original.low[0]<=1002&&original.high[0]>=1042&&original.high[2]>=250);
    // Rotation of the camera leaves the absolute submitted bounds unchanged.
    constants[0]=constants[5]=0;constants[1]=-1;constants[4]=1;constants[12]=20;constants[13]=-10;
    inverse[0]=inverse[5]=0;inverse[1]=1;inverse[4]=-1;
    budget.reset();NorthlightReplayBounds::Bounds rotated;
    assert(NorthlightReplayBounds::calculate(program,mesh,declaration,5,constants,inverse,budget,rotated)==NorthlightReplayBounds::Status::Valid);
    for(unsigned i=0;i<3;++i){assert(std::fabs(original.low[i]-rotated.low[i])<.01f);assert(std::fabs(original.high[i]-rotated.high[i])<.01f);}
}
int main(int argc,char** argv){
    policy();auto code=synthetic();NorthlightActorDeformation::Program program;
    assert(NorthlightActorDeformation::compile(code.data(),code.size(),program,false,1));positions(program);
    assert(!NorthlightActorDeformation::compile(code.data(),code.size(),program));
    for(unsigned i=27;i<code.size()-1;++i){auto bad=code;bad[i]^=1;assert(!NorthlightActorDeformation::compile(bad.data(),bad.size(),program,false,1));}
    assert(!NorthlightActorDeformation::compile(code.data(),code.size(),program,false,3));
    for(int i=1;i<argc;++i){
        FILE* file=std::fopen(argv[i],"rb");assert(file);std::fseek(file,0,SEEK_END);const auto size=std::ftell(file);std::rewind(file);
        assert(size>0&&size%4==0);std::vector<Word> native(size/4);assert(std::fread(native.data(),1,size,file)==size_t(size));std::fclose(file);
        assert(NorthlightDetailShadow::shader(NorthlightShaderTags::fnv1a(native.data(),size)));
        NorthlightShadowShader::PatchInfo info;std::vector<Word> patched;
        assert(NorthlightShadowShader::patch(native.data(),native.size(),patched,&info)&&info.texcoord0XY&&info.color0W);
        assert(NorthlightActorDeformation::compile(native.data(),native.size(),program,false,1));positions(program);
        NorthlightActorDeformation::Program uv;assert(NorthlightActorDeformation::compile(native.data(),native.size(),uv,true));
        float constants[1024]={};NorthlightActorDeformation::Four inputs[16]={},result;inputs[3]={.125f,.875f,0,1};
        assert(NorthlightActorDeformation::evaluate(uv,inputs,constants,result)&&result[0]==.125f&&result[1]==.875f);
    }
    std::printf("PASS grass policy, column-projection negatives, full mesh bounds/camera invariance, %d native shader/UV/alpha replay permutations\n",argc-1);
}
