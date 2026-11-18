#pragma once
#include "actor_deformation.h"
#include "actor_texture.h"
#include "world_gi.h"
#include "mesh_page_key.h"
#include <chrono>
#include <memory>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

// Immutable, CPU-only packets. Texture readback and stream snapshots are complete
// before queueing. Texture decoding and resolve() run on the existing GI worker,
// never the render thread. No D3D interfaces or game pointers survive in this job.
namespace NorthlightActorGeometry {
// The worker consumes this scene into its BVH. Exclusive ownership avoids a
// second copy of every posed vertex and decoded texture at that handoff.
using TexelKeys=std::vector<NorthlightMeshPageKey::Key>;
struct Result {std::unique_ptr<NorthlightGI::WorldScene> scene;std::uint64_t hash=0;unsigned texturesDecoded=0;std::size_t textureEncodedBytes=0;double decodeMs=0,geometryMs=0,hashMs=0;TexelKeys texelKeys;};
inline std::array<std::uint32_t,3> poseCells(NorthlightGI::Vec3 p){
#if defined(__SSE2__)
    const __m128 scaled=_mm_mul_ps(_mm_set_ps(0,p.z,p.y,p.x),_mm_set1_ps(32));
    const __m128 sign=_mm_set1_ps(-0.f);
    if(_mm_movemask_ps(_mm_cmplt_ps(_mm_andnot_ps(sign,scaled),_mm_set1_ps(2147483648.f)))==15){
        const __m128i whole=_mm_cvttps_epi32(scaled);
        const __m128 fraction=_mm_sub_ps(scaled,_mm_cvtepi32_ps(whole));
        // Round ties away from zero exactly as llround. Adding 0.5 before
        // truncation would incorrectly round the float immediately below a tie.
        const __m128i adjust=_mm_castps_si128(_mm_cmpge_ps(_mm_andnot_ps(sign,fraction),_mm_set1_ps(.5f)));
        const __m128i direction=_mm_or_si128(_mm_srai_epi32(_mm_castps_si128(scaled),31),_mm_set1_epi32(1));
        std::uint32_t cells[4];_mm_storeu_si128(reinterpret_cast<__m128i*>(cells),_mm_add_epi32(whole,_mm_and_si128(adjust,direction)));
        return {cells[0],cells[1],cells[2]};
    }
#endif
    auto cell=[](float v){return std::uint32_t(std::int32_t(std::llround(v*32)));};
    return {cell(p.x),cell(p.y),cell(p.z)};
}
inline std::uint64_t sceneHash(const NorthlightGI::WorldScene& scene,const NorthlightGI::WorldScene* previous=nullptr,
                               const TexelKeys* previousKeys=nullptr,TexelKeys* keys=nullptr){
    NorthlightMeshPageKey::Hasher hash;
    auto bits=[](float value){std::uint32_t word;std::memcpy(&word,&value,sizeof word);return word;};
    auto pair=[&](std::uint32_t a,std::uint32_t b){hash.word((std::uint64_t(a)<<32)|b);};
    // Pose data stays full precision. Quantization affects only generation keys
    // so inverse-camera roundoff does not continuously invalidate probe lighting.
    for(const auto& v:scene.vertices){
        const auto cells=poseCells(v.position);
        pair(cells[0],cells[1]);hash.word(cells[2]);pair(bits(v.u),bits(v.v));
    }
    for(const auto& t:scene.triangles){pair(t.v0,t.v1);pair(t.v2,t.material);}
    hash.word(scene.materials.size());
    if(keys)keys->resize(scene.materials.size());
    for(size_t i=0;i<scene.materials.size();++i){
        const auto& m=scene.materials[i];
        pair(bits(m.albedo.x),bits(m.albedo.y));pair(bits(m.albedo.z),bits(m.alphaCutoff));
        pair(m.width,m.height);pair(m.addressU,m.addressV);
        const auto texels=m.texels();hash.word(texels.size());
        if(texels.empty())continue;
        // The previous BVH owns its pixels. A material slot is only a lookup hint:
        // even a reordered draw can reuse a digest only after exact byte equality.
        NorthlightMeshPageKey::Key key;
        if(previous&&previousKeys&&i<previous->materials.size()&&i<previousKeys->size()){
            const auto old=previous->materials[i].texels();
            if(old.size()==texels.size()&&!std::memcmp(old.data(),texels.data(),texels.size()))key=(*previousKeys)[i];
        }
        if(key.vertexBytes!=texels.size())key=NorthlightMeshPageKey::keyOf(texels.data(),texels.size(),nullptr,0);
        hash.word(key.lo);hash.word(key.hi);if(keys)(*keys)[i]=key;
    }
    return hash.finish(std::uint32_t(scene.vertices.size()),std::uint32_t(scene.triangles.size())).lo;
}
struct Packet {
    NorthlightDrawSnapshot::Mesh mesh; // owned fallback for transient UP/dynamic captures
    std::shared_ptr<const NorthlightDrawSnapshot::Mesh> sharedMesh;
    const NorthlightDrawSnapshot::Mesh& meshView()const{return sharedMesh?*sharedMesh:mesh;}
    NorthlightActorDeformation::Program position,uv;
    std::shared_ptr<const NorthlightActorDeformation::Program> sharedPosition,sharedUV;
    const NorthlightActorDeformation::Program& positionView()const{return sharedPosition?*sharedPosition:position;}
    const NorthlightActorDeformation::Program& uvView()const{return sharedUV?*sharedUV:uv;}
    std::vector<D3DVERTEXELEMENT9> elements;
    std::array<float,1024> constants{};
    std::array<float,16> inverseView{};
    NorthlightGI::WorldMaterial material;
    NorthlightActorTexture::Snapshot texture;
    bool hasUV=false,alphaTest=false;
};
struct ActorJob {
    NorthlightGI::Vec3 center;
    std::vector<Packet> packets;
    bool menu=false;
    Result resolve(const NorthlightGI::WorldScene* previous=nullptr,const TexelKeys* previousKeys=nullptr)const{
        using namespace NorthlightGI;
        const auto resolveStart=std::chrono::steady_clock::now();double decodeMs=0;
        auto scene=std::make_unique<WorldScene>();
        // One bounded, metadata-only cache per worker; it keeps no jobs or
        // mutable resources alive and does not memoize any evaluated pose.
        thread_local NorthlightActorDeformation::PreparedBindingCache bindingCache;
        const unsigned vertexLimit=menu?262144u:16384u;const float radius=menu?1024.f:48.f;
        unsigned evaluated=0,texturesDecoded=0;std::size_t textureEncodedBytes=0;
        std::vector<NorthlightActorDeformation::Position> positions;
        std::vector<std::array<float,2>> uvs;
        std::vector<WorldTriangle> triangles;
        for(auto& packet:packets){
            const auto& mesh=packet.meshView();if(mesh.primitiveCount==0||mesh.primitiveCount>87380||mesh.vertexCount>vertexLimit-evaluated)continue;
            if(mesh.topology!=D3DPT_TRIANGLELIST&&mesh.topology!=D3DPT_TRIANGLESTRIP)continue;
            unsigned required=mesh.topology==D3DPT_TRIANGLELIST?mesh.primitiveCount*3:mesh.primitiveCount+2;
            if(mesh.indexed?mesh.indices.size()<required:mesh.vertexCount<required)continue;
            evaluated+=mesh.vertexCount;
            const auto& position=packet.positionView();const auto& uv=packet.uvView();
            auto positionBindings=bindingCache.acquire(position,mesh,packet.elements.data(),packet.elements.size());
            if(!positionBindings||!NorthlightActorDeformation::worldPositionsPrepared(position,mesh,*positionBindings,packet.constants.data(),packet.inverseView.data(),positions))continue;
            triangles.clear();triangles.reserve(mesh.primitiveCount);
            auto index=[&](UINT i){return mesh.indexed?mesh.indices[i]:i;};
            for(UINT triangle=0;triangle<mesh.primitiveCount;++triangle){UINT a,b,c;
                if(mesh.topology==D3DPT_TRIANGLELIST){a=index(triangle*3);b=index(triangle*3+1);c=index(triangle*3+2);}
                else if(mesh.topology==D3DPT_TRIANGLESTRIP){a=index(triangle);b=index(triangle+1);c=index(triangle+2);if(triangle&1)std::swap(a,b);}else continue;
                if(a>=positions.size()||b>=positions.size()||c>=positions.size()||a==b||b==c||c==a)continue;
                auto& pa=positions[a];auto& pb=positions[b];auto& pc=positions[c];
                if(std::max({pa.x,pb.x,pc.x})<center.x-radius||std::min({pa.x,pb.x,pc.x})>center.x+radius||std::max({pa.y,pb.y,pc.y})<center.y-radius||std::min({pa.y,pb.y,pc.y})>center.y+radius||std::max({pa.z,pb.z,pc.z})<center.z-radius||std::min({pa.z,pb.z,pc.z})>center.z+radius)continue;
                triangles.push_back({a,b,c,0});}
            if(triangles.empty())continue;
            auto uvBindings=packet.hasUV?bindingCache.acquire(uv,mesh,packet.elements.data(),packet.elements.size()):nullptr;
            bool exactUV=uvBindings&&NorthlightActorDeformation::textureUVsPrepared(uv,mesh,*uvBindings,packet.constants.data(),uvs);
            if(packet.alphaTest&&!exactUV)continue;
            unsigned offset=unsigned(scene->vertices.size()),material=unsigned(scene->materials.size());WorldMaterial m=packet.material;
            if(exactUV&&!packet.texture.bytes.empty()){
                const auto& texture=packet.texture;
                const auto decodeStart=std::chrono::steady_clock::now();
                const bool decoded=NorthlightActorTexture::decode(texture.bytes.data(),texture.bytes.size(),texture.width,texture.height,texture.pitch,texture.format,m.rgba);
                decodeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decodeStart).count();
                if(decoded){
                    m.width=texture.width;m.height=texture.height;m.albedo=Vec3(1,1,1);
                    ++texturesDecoded;textureEncodedBytes+=texture.bytes.size();
                }else{m.rgba.clear();m.width=m.height=0;m.albedo=Vec3(.35f,.35f,.35f);}
            }
            if(packet.alphaTest&&m.rgba.empty())continue;
            if(!exactUV){m.rgba.clear();m.width=m.height=0;m.albedo=Vec3(.35f,.35f,.35f);}
            scene->materials.push_back(std::move(m));
            const size_t vertexCount=offset+positions.size();
            if(vertexCount>scene->vertices.capacity())scene->vertices.reserve(std::min<size_t>(vertexLimit,std::max(vertexCount,scene->vertices.capacity()*2)));
            scene->vertices.resize(vertexCount);
            for(unsigned j=0;j<positions.size();++j){auto& p=positions[j];auto& v=scene->vertices[offset+j];v.position=Vec3(p.x,p.y,p.z);if(exactUV){v.u=uvs[j][0];v.v=uvs[j][1];}}
            const size_t triangleCount=scene->triangles.size()+triangles.size();
            if(triangleCount>scene->triangles.capacity())scene->triangles.reserve(std::max(triangleCount,scene->triangles.capacity()*2));
            for(auto t:triangles){t.v0+=offset;t.v1+=offset;t.v2+=offset;t.material=material;
                auto& a=scene->vertices[t.v0];auto& b=scene->vertices[t.v1];auto& c=scene->vertices[t.v2];Vec3 normal=cross(b.position-a.position,c.position-a.position);
                a.normal=a.normal+normal;b.normal=b.normal+normal;c.normal=c.normal+normal;scene->triangles.push_back(t);}
            for(unsigned i=offset;i<scene->vertices.size();++i)scene->vertices[i].normal=normalized(scene->vertices[i].normal);
        }
        const auto hashStart=std::chrono::steady_clock::now();const double geometryMs=std::chrono::duration<double,std::milli>(hashStart-resolveStart).count()-decodeMs;
        TexelKeys keys;
        std::uint64_t hash=sceneHash(*scene,previous,previousKeys,previousKeys?&keys:nullptr);const double hashMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hashStart).count();
        return {std::move(scene),hash,texturesDecoded,textureEncodedBytes,decodeMs,geometryMs,hashMs,std::move(keys)};
    }
};
} // namespace NorthlightActorGeometry
