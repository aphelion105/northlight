#pragma once
#include "auto_displacement_renderer.h"
#include "capture_buffer_metadata.h"
#include "capture_work_audit.h"
#include "render_thread_probe.h"

namespace NorthlightDrawCapture {
// One draw owns its handoff through all captures, including water. Device
// supplies the normal/water adapters and retains the submit and fault boundary.
template<bool userMemory,class World,class Material,class Raw,class Allowed,class Normals,class Water>
void capture(IDirect3DVertexShader9* vs,const NorthlightAutoDisplacement::Args& geometry,const unsigned& frame,
             const World& world,const Material& autoBump,Raw& raw,Allowed canCapture,Normals captureModelNormals,Water captureWater
#ifdef NORTHLIGHT_REPLAY_STAND
             ,NorthlightCaptureWork::Route* audit=nullptr
#endif
             ,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* sourceHint=nullptr,
             NorthlightAutoDisplacement::CaptureSource* captureHandoff=nullptr
             ){
    const auto& args=geometry.draw;
    NorthlightAutoDisplacement::CaptureSource localSource;
    auto& source=captureHandoff?*captureHandoff:localSource;
    NorthlightCaptureMetadata::PositionBounds positions;
    positions.owner=&geometry;positions.shader=vs;
    auto captureWorld=[&](NorthlightCaptureMetadata::PositionBounds* bounds=nullptr){
#ifdef NORTHLIGHT_REPLAY_STAND
        NorthlightCaptureWork::BufferReads reads(audit,NorthlightCaptureWork::Route::World);
#endif
        if constexpr(userMemory)world->captureUP(args.topology,args.minimum,args.vertices,args.primitives,geometry.indices,geometry.format,geometry.vertices,geometry.stride,args.indexed,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));
        else world->capture(args.topology,args.base,args.minimum,args.vertices,args.start,args.primitives,args.indexed,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame),bounds,&source.mesh,source.declaration,&source.pose,&source.bank);
    };
    // Only buffered indexed terrain publishes bounds before material work.
    const bool sharedTerrain=!userMemory&&args.indexed&&autoBump&&autoBump->active()&&canCapture()&&world->terrainShader(vs);
    if(sharedTerrain)captureWorld(&positions);
    const bool materialCapture=autoBump&&canCapture();
    if(materialCapture){
#ifdef NORTHLIGHT_REPLAY_STAND
        NorthlightCaptureWork::BufferReads reads(audit,NorthlightCaptureWork::Route::Material);
#endif
        autoBump->capture(vs,raw,userMemory,&geometry,!userMemory&&args.indexed?&positions:nullptr,&source,sourceHint);
    }
    // UP normals retain their material guard; buffered receivers do not.
    if(!userMemory||materialCapture){
#ifdef NORTHLIGHT_REPLAY_STAND
        NorthlightCaptureWork::BufferReads reads(audit,NorthlightCaptureWork::Route::Normals);
#endif
        captureModelNormals(source);
    }
    if(!sharedTerrain)captureWorld();
    {
#ifdef NORTHLIGHT_REPLAY_STAND
        NorthlightCaptureWork::BufferReads reads(nullptr,NorthlightCaptureWork::Route::World);
#endif
        captureWater();
    }
#ifdef NORTHLIGHT_REPLAY_STAND
    if(audit){
        using Route=NorthlightCaptureWork::Route;
        audit->lanes=Route::World|(materialCapture?Route::Material:0u)|
            (!userMemory||materialCapture?Route::Normals:0u)|(sharedTerrain?Route::TerrainFirst:0u);
        if(source.mesh){
            audit->sourceBytes=source.mesh->byteSize();
            if(source.mesh->sourceIdentity)audit->source=source.mesh->sourceIdentity->hash;
        }
        audit->pose=source.pose.bounds.valid;
        audit->bank=source.bank.bank!=nullptr;
    }
#endif
}
}
