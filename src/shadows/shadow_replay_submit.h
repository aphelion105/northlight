#pragma once
#include <d3d9.h>
#include "render_thread_probe.h"
#include "replay_draw_state.h"
#include "replay_pose_groups.h"

namespace NorthlightShadowReplay {
using Range=NorthlightReplayDrawState::Range;

struct IndexOverride {
    IDirect3DIndexBuffer9* buffer=nullptr;
    Range run{};
    unsigned packets=0;
};

struct ConstantStages {
    const char* floats;
    const char* booleans;
    const char* integers;
};
inline constexpr ConstantStages DirectionalConstants{"replay float constants","replay bool constants","replay integer constants"};
inline constexpr ConstantStages PointConstants{"cube replay float constants","cube replay bool constants","cube replay integer constants"};

// Lives for one cascade, actor map, flash projection, probe or point cube.
// Palette draws can modify both banks. Static draws and face resolves modify
// only the state communicated by the caller before the next replay.
class Pass {
    IDirect3DDevice9* device_;
    NorthlightReplayDrawState::Cache bindings_;
    NorthlightReplayPoses::Pass<BOOL> pose_;

public:
    explicit Pass(IDirect3DDevice9* device):device_(device),bindings_(device){}

    bool skipping() const {return bindings_.skipping();}
    const auto& bindingCounts() const {return bindings_.counts;}
    size_t posesPrepared() const {return pose_.prepared;}
    size_t posesReused() const {return pose_.reused;}

    void invalidateBindings(){bindings_.invalidate();}
    void afterPalette(){bindings_.invalidate();pose_.reset();}
    void beginFace(bool reuseBindings){
        if(!reuseBindings)invalidateBindings();
        bindings_.beginMaterialPass();
        pose_.beginProjection();
    }
    void recordFloatPrefix(const float* values,unsigned count){pose_.recordFloatPrefix(values,count);}

    template<class Replay> HRESULT bindGeometry(const Replay& packet,IndexOverride* indices){
        const HRESULT result=bindings_.geometry(packet,indices?indices->buffer:nullptr);
        if(FAILED(result)&&indices&&indices->buffer){
            indices->buffer=nullptr;
            return bindings_.geometry(packet);
        }
        return result;
    }

    template<class Replay> HRESULT bindMaterial(const Replay& packet){return bindings_.material(packet);}

    template<class Replay>
    bool preparePose(const Replay& packet,const float* rows,const float* matrix,NorthlightShaderConstants::Range floats){
        return pose_.prepare(packet,rows,matrix,floats);
    }

    template<class Replay,class Check,class Record>
    bool writeConstants(const Replay& packet,const ConstantStages& stages,const Check& check,const Record& record){
        using namespace NorthlightRenderThreadProbe;
        const auto f=pose_.floats,b=pose_.booleans,i=pose_.integers;
        if(f.count){
            if(!check(device_->SetVertexShaderConstantF(f.first,pose_.desired()+4*f.first,f.count),stages.floats))return false;
            record(Floats,f.count*16);
        }
        if(b.count){
            if(!check(device_->SetVertexShaderConstantB(b.first,packet.bools+b.first,b.count),stages.booleans))return false;
            record(Bools,b.count*sizeof(BOOL));
        }
        if(i.count){
            if(!check(device_->SetVertexShaderConstantI(i.first,packet.ints+4*i.first,i.count),stages.integers))return false;
            record(Ints,i.count*16);
        }
        return true;
    }

    template<class Replay> HRESULT draw(const Replay& packet,const Range& run){
        return packet.indexed?device_->DrawIndexedPrimitive(packet.type,packet.base,run.min,run.vertices,run.start,run.count):
                              device_->DrawPrimitive(packet.type,run.start,run.count);
    }

    template<class Replay,class Split,class Check,class AlphaDraw>
    bool submit(const Replay& packet,const float* rows,const float* matrix,Split& split,size_t& constantBytes,size_t& constantCalls,
                const Check& check,const AlphaDraw& alphaDraw,const Range* grouped,IndexOverride* indices,
                NorthlightShaderConstants::Range floats){
        using namespace NorthlightRenderThreadProbe;
        if(!grouped)floats=NorthlightReplayPoses::weightedUploadRange(packet.constantUsage,packet.directionalPaletteEnd);
        if constexpr(!Split::Calls)return preparePose(packet,rows,matrix,floats);
        if(!check(bindGeometry(packet,indices),"replay geometry state"))return false;
        split.mark(State);
        if(!preparePose(packet,rows,matrix,floats))return false;
        split.mark(Own);
        if(!writeConstants(packet,DirectionalConstants,check,[&](Constant kind,size_t bytes){
            constantBytes+=bytes;
            ++constantCalls;
            split.constant(kind);
        }))return false;
        if(!check(bindMaterial(packet),"replay material state"))return false;
        split.mark(State);
        HRESULT result=D3D_OK;
        if constexpr(Split::Draws){
            const auto run=indices&&indices->buffer?indices->run:grouped?*grouped:Range{packet.start,packet.count,packet.min,packet.vertices};
            const auto issue=[&]{return draw(packet,run);};
            result=grouped?issue():alphaDraw(issue);
        }
        split.mark(Draw);
        return result!=E_PENDING&&check(result,"animated shadow draw");
    }
};
}
