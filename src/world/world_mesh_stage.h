#pragma once
#include "world_mesh_material_upload.h"

namespace NorthlightWorldMeshPublication {
// A single upload attempt owns its staging textures until commit or retirement.
template<class PreparedCommit>
struct Pending {
    std::shared_ptr<NorthlightGI::BVH> bvh;
    std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> plan;
    std::shared_ptr<const PreparedCommit> prepared; /* from the same snapshot as plan; may be null */
    std::shared_ptr<NorthlightWorldMesh::UploadPayload> payload; /* 0.3.214 (task 42): pages + cutout textures for the whole staging */
    std::string map;
    NorthlightWorldMeshPages::Upload upload;
    IDirect3DTexture9* white=nullptr;
    std::vector<IDirect3DTexture9*> textures;
    size_t material=0;
    MaterialUpload materialUpload;
    DWORD started=GetTickCount();unsigned frames=0,textureUploads=0,textureReuses=0;
    // The upload cursor owns no GPU data; only textures are owned here.
    ~Pending(){materialUpload.reset();drop(white);for(auto& t:textures)drop(t);}
};
}
