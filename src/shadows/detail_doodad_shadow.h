#pragma once
#include <cstddef>
#include <cstdint>
#include "replay_bounds.h"

// Stock 3.3.5a DetailDoodad VS2/VS3, six lighting permutations each.
// The complete bytecode identities are distinct from Terrain. Their c4..7
// projection layout alone does not make their alpha-cut geometry terrain.
namespace NorthlightDetailShadow {
inline bool shader(std::uint64_t hash){
    constexpr std::uint64_t hashes[]={
        0x7f2707e33c6c97beULL,0x6bee6c0fc22ac5b2ULL,0x279074f2d7f6ea1eULL,
        0x87aacbe7c33bb84fULL,0x8e0b9bf5d6eff0ffULL,0xb4cb4906153b6683ULL,
        0xd8f18d8080e6bb3aULL,0x4963565c635e7d6dULL,0x28d133339d7839aeULL,
        0x723e54bfc91c3bffULL,0xe2e26d9aa7c0d59cULL,0xc8e41181431a84c7ULL};
    for(auto value:hashes)if(value==hash)return true;
    return false;
}
struct Budget {
    static constexpr unsigned MaxDraws=32,MaxVertices=8192;
    static constexpr std::size_t MaxBytes=512u*1024u;
    unsigned attempts=0,readVertices=0,draws=0,vertices=0;std::size_t bytes=0,readBytes=0;
    bool room(unsigned count)const{return count&&attempts<MaxDraws&&count<=MaxVertices-readVertices&&readBytes<MaxBytes;}
    bool reserve(unsigned count,std::size_t size){if(!room(count)||size>MaxBytes-readBytes)return false;++attempts;readVertices+=count;readBytes+=size;return true;}
    bool take(unsigned count,std::size_t size){
        if(count>MaxVertices-vertices||size>MaxBytes-bytes)return false;
        ++draws;vertices+=count;bytes+=size;return true;
    }
};
// DetailDoodad's signature-proved prefix is affine and consumes POSITION0
// only. Enclose its whole source box once instead of evaluating every vertex
// through the generic interpreter. Input uncertainty is propagated by the
// existing conservative interval evaluator, including matrix rounding/FTZ.
inline bool bounds(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
                   const D3DVERTEXELEMENT9* elements,unsigned count,const float* constants,const float* inverse,
                   NorthlightReplayBounds::Bounds& output){
    output={};
    if(!elements||!constants||!inverse||!mesh.vertexCount||mesh.vertexCount>Budget::MaxVertices||
       program.operations.size()!=4||program.inputs.size()!=1||program.inputs[0].reg!=0||program.inputs[0].usage!=0||program.inputs[0].index)return false;
    const D3DVERTEXELEMENT9* position=nullptr;
    for(unsigned i=0;i<count;++i){const auto& e=elements[i];if(e.Stream==0xff)break;if(e.Usage==0&&e.UsageIndex==0){if(position)return false;position=&e;}}
    if(!position||position->Stream!=0||position->Type!=D3DDECLTYPE_FLOAT3||position->Method!=D3DDECLMETHOD_DEFAULT)return false;
    const auto& stream=mesh.streams[0];
    if(unsigned(position->Offset)+12>stream.stride||stream.bytes.size()<size_t(mesh.vertexCount)*stream.stride)return false;
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    for(unsigned i=0;i<mesh.vertexCount;++i){float xyz[3];std::memcpy(xyz,stream.bytes.data()+size_t(i)*stream.stride+position->Offset,12);
        for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(xyz[axis]))return false;low[axis]=std::min(low[axis],xyz[axis]);high[axis]=std::max(high[axis],xyz[axis]);}}
    NorthlightReplayBounds::detail::Vector inputs[16]={},view;
    for(unsigned axis=0;axis<3;++axis){const float middle=float((double(low[axis])+high[axis])*.5);
        inputs[0][axis]={middle,std::max(double(middle)-low[axis],double(high[axis])-middle)};}
    inputs[0][3]={1,0};
    if(!NorthlightReplayBounds::detail::evaluateIntervals(program,inputs,constants,view))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(inverse[i]))return false;
    if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1)return false;
    for(unsigned axis=0;axis<3;++axis){using namespace NorthlightReplayBounds::detail;
        Value world=mul(view[3],{inverse[12+axis],0});double magnitude=std::fabs(world.value)+world.error;
        for(unsigned k=0;k<3;++k){const auto term=mul(view[k],{inverse[4*k+axis],0});magnitude+=std::fabs(term.value)+term.error;world=add(world,term);}
        world.error+=roundError(magnitude)*8+.001;
        if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)+world.error>1000000)return false;
        output.low[axis]=std::nextafter(float(double(world.value)-world.error),-INFINITY);
        output.high[axis]=std::nextafter(float(double(world.value)+world.error),INFINITY);
    }
    output.valid=true;return true;
}
// Read-only sampled-frame evidence for the receiver path. Depth-writing grass
// drawn before WorldComposite already receives the normal depth-based effects;
// a late or non-depth-writing draw needs a separate path, never a guessed mask.
struct ReceiverCensus {
    unsigned before=0,after=0,depthWrites=0,blended=0,alphaCut=0,alreadyResolved=0,worldDepth=0,stateFailures=0;
    void observe(bool late,bool states,bool zwrite,bool blend,bool alpha,bool resolved,bool mainDepth){
        ++(late?after:before);
        if(!states){++stateFailures;return;}
        depthWrites+=zwrite;blended+=blend;alphaCut+=alpha;alreadyResolved+=resolved;worldDepth+=mainDepth;
    }
};
}
