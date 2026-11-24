#pragma once
#include <d3d9.h>
#include <array>
#include <cstring>
#include <new>
#include <vector>
#include "source_identity.h"

namespace NorthlightPointShadow {

// Records CPU provenance for a completed cube face. The slot carries a token
// only while that exact surface still contains the completed draw.
class OpaqueFaceCache {
public:
    static constexpr size_t FaceLimit=32,PacketLimit=256;
    struct Work {unsigned draws=0,packets=0;uint64_t triangles=0;};
    struct Key {
        uint64_t source,meshGeneration;
        unsigned shaderGeneration;
        float range;
        const IDirect3DTexture9* staticFace;
        const float* matrix;
        const float* replayRows;
    };

private:
    struct Packet {
        NorthlightSourceIdentity::Handle mesh;
        const void* shader;
        std::array<unsigned,13> draw;
    };
    struct Face {
        uint64_t token=0,source=0,meshGeneration=0;
        unsigned shaderGeneration=0;
        float range=0;
        const IDirect3DTexture9* staticFace=nullptr;
        Work work;
        std::array<float,32> projection;
        std::vector<Packet> packets;
        std::vector<float> constants;
    };
    // Bound proof storage independently of the number of resident cubes.
    std::array<Face,FaceLimit> faces_;
    uint64_t token_=0;

    template<class Replay> static const auto& identity(const Replay& replay){
        const auto& mesh=replay.mesh();
        return mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;
    }
    template<class Replay> static bool admitted(const Replay& replay){
        return replay.gpuCached&&replay.indexed&&replay.type==D3DPT_TRIANGLELIST&&replay.count&&
            replay.index&&replay.stream[0]&&replay.shader&&!replay.shadowSkinned&&replay.cutoff<=-1.f&&
            !replay.alphaMode&&replay.projectionKind==2&&identity(replay);
    }
    template<class Replay> static std::array<unsigned,13> draw(const Replay& replay){
        return {unsigned(replay.base),replay.min,replay.vertices,replay.start,replay.count,
            replay.offset[0],replay.offset[1],replay.offset[2],replay.offset[3],
            replay.stride[0],replay.stride[1],replay.stride[2],replay.stride[3]};
    }

public:
    void clear(){faces_={};}

    template<class Replay> static bool supportsReplay(const Replay& replay){return admitted(replay);}

    template<class Candidates,class At,class Rows>
    const Work* match(uint64_t token,const Key& key,const Candidates& candidates,const At& at,const Rows& rows)const{
        if(!token)return nullptr;
        const auto& face=faces_[token%FaceLimit];
        if(face.token!=token||face.source!=key.source||face.meshGeneration!=key.meshGeneration||
           face.shaderGeneration!=key.shaderGeneration||face.range!=key.range||face.packets.size()!=candidates.size()||
           face.staticFace!=key.staticFace||
           std::memcmp(face.projection.data(),key.matrix,16*sizeof(float))||
           std::memcmp(face.projection.data()+16,key.replayRows,16*sizeof(float)))return nullptr;
        size_t cursor=0;
        for(size_t i=0;i<face.packets.size();++i){
            const auto& old=face.packets[i];
            const auto& replay=at(candidates[i]);
            if(!admitted(replay)||old.shader!=replay.shader||old.draw!=draw(replay)||
               !NorthlightSourceIdentity::same(old.mesh,identity(replay)))return nullptr;
            const auto* proof=rows(replay);
            if(!proof||!proof->visitBank(replay,[&](const float* row){
                if(cursor+4>face.constants.size()||std::memcmp(face.constants.data()+cursor,row,4*sizeof(float)))return false;
                cursor+=4;
                return true;
            }))return nullptr;
        }
        return cursor==face.constants.size()?&face.work:nullptr;
    }

    template<class Candidates,class At,class Rows>
    uint64_t record(const Key& key,const Candidates& candidates,const At& at,const Rows& rows,Work work)noexcept{
        if(!work.draws||candidates.empty()||candidates.size()>PacketLimit||token_==UINT64_MAX)return 0;
        for(const auto index:candidates)if(!admitted(at(index)))return 0;
        auto& face=faces_[(token_+1)%FaceLimit];
        face.token=0;
        face.packets.clear();
        face.constants.clear();
        try{
            for(const auto index:candidates){
                const auto& replay=at(index);
                const auto* proof=rows(replay);
                if(!proof||!proof->visitBank(replay,[&](const float* row){
                    face.constants.insert(face.constants.end(),row,row+4);
                    return true;
                }))return 0;
                face.packets.push_back({identity(replay),replay.shader,draw(replay)});
            }
        }catch(const std::bad_alloc&){
            face.packets.clear();
            face.constants.clear();
            return 0;
        }
        face.source=key.source;
        face.meshGeneration=key.meshGeneration;
        face.shaderGeneration=key.shaderGeneration;
        face.range=key.range;
        face.staticFace=key.staticFace;
        face.work=work;
        std::memcpy(face.projection.data(),key.matrix,16*sizeof(float));
        std::memcpy(face.projection.data()+16,key.replayRows,16*sizeof(float));
        return face.token=++token_;
    }
};

} // namespace NorthlightPointShadow
