#pragma once
#include "point_light_shadow.h"
#include <d3d9.h>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace NorthlightPointShadow {
inline constexpr UINT CubeResolution=256;
inline constexpr size_t CubeFaceBytes=size_t(CubeResolution)*CubeResolution*4;

struct ContentMemo {
    const NorthlightLocalShadowSignature::Records* records=nullptr;
    uint64_t generation=0;
    float position[3]={},range=0;
    bool valid=false;
    NorthlightLocalShadowSignature::Digest faces[6];
};

struct TerrainMemo {
    // Use one dense byte per batch or a shorter list of visible terrain.
    static constexpr size_t Limit=65536;
    struct Visible {uint16_t index;unsigned char mask;};
    std::vector<Visible> visible;
    std::vector<unsigned char> masks;
    uint64_t generation=0;float position[3]={},range=0;bool valid=false;
    size_t batchCount=0,terrainCount=0;unsigned occupiedFaces=0;
};

struct CubeSlot {
    IDirect3DCubeTexture9* cube=nullptr;
    IDirect3DSurface9* faces[6]={};
    std::array<uint64_t,6> opaqueTokens{},mixedTokens{};
    IDirect3DTexture9 *cacheFace[6]={};
    IDirect3DSurface9 *cacheSurface[6]={};
    unsigned staticOnlyFaces=0;
    unsigned geometryOnlyFaces=0;uint64_t geometrySerial[6]={},terrainSerial[6]={};
    bool cacheValid=false,ready=false,prewarmed=false;float selectionGain=1;uint64_t cacheSource=0;
    NorthlightLocalShadowSignature::Digest faceContent[6];bool faceKnown[6]={};uint64_t faceSerial[6]={};
    mutable ContentMemo contentMemo;
    TerrainMemo terrainMemo;
    FaceCycle faceCycle;unsigned faceWeights[6]={};
    RefreshSchedule schedule;
    uint64_t completeTerrainRevision=0;
    bool budgetHeld=false;
    NorthlightLocalLights::Light selected{};std::string map;DWORD retryAt=0;

    const float* cubeOrigin()const{return budgetHeld?schedule.light.position:selected.position;}

    template<class Allocate>
    HRESULT ensureCube(Allocate allocate){
        if(!cube){
            const HRESULT hr=allocate(&cube);
            if(FAILED(hr))return hr;
        }
        if(!cube)return E_FAIL;
        for(unsigned i=0;i<6;++i){
            if(!faces[i]){
                const HRESULT hr=cube->GetCubeMapSurface(D3DCUBEMAP_FACES(i),0,&faces[i]);
                if(FAILED(hr))return hr;
            }
            if(!faces[i])return E_FAIL;
        }
        return S_OK;
    }

    template<class Allocate,class Check>
    bool ensureStaticFace(unsigned face,Allocate allocate,Check check){
        if(!cacheFace[face]){
            release(cacheSurface[face]);
            if(!check(allocate(&cacheFace[face]),"cube cache face"))return false;
        }
        if(!cacheFace[face])return check(E_FAIL,"empty cube cache face");
        if(!cacheSurface[face]&&!check(cacheFace[face]->GetSurfaceLevel(0,&cacheSurface[face]),"cube cache surface"))
            return false;
        return cacheSurface[face]?true:check(E_FAIL,"empty cube cache surface");
    }

    void invalidateReplayFaces(){
        opaqueTokens.fill(0);
        mixedTokens.fill(0);
    }
    void swapCube(CubeSlot& other){
        invalidateReplayFaces();
        other.invalidateReplayFaces();
        std::swap(cube,other.cube);
        std::swap(faces,other.faces);
    }
    void releaseCube(){
        invalidateReplayFaces();
        for(auto& face:faces)release(face);
        release(cube);
    }
    void reset(){
        schedule.invalidate();
        releaseCube();
        for(auto& texture:cacheFace)release(texture);
        for(auto& surface:cacheSurface)release(surface);
        *this=CubeSlot{};
    }

private:
    template<class T> static void release(T*& resource){
        if(resource){resource->Release();resource=nullptr;}
    }
};

// Retention belongs to the cube cache. The render pass supplies the map, spare
// capacity and lights selected for the upcoming refresh.
class RetainedCubes {
    struct Entry {CubeSlot slot;uint64_t age=0;bool wanted=false;};
    std::array<Entry,8> entries_;
    uint64_t age_=0;

public:
    auto begin()const{return entries_.begin();}
    auto end()const{return entries_.end();}
    size_t size()const{return entries_.size();}
    const auto& operator[](size_t index)const{return entries_[index];}

    size_t trim(){
        size_t bytes=0;
        for(auto& entry:entries_){
            for(auto* texture:entry.slot.cacheFace)if(texture)bytes+=CubeFaceBytes;
            entry.slot.reset();
            entry.age=0;
            entry.wanted=false;
        }
        return bytes;
    }
    void reset(){trim();age_=0;}

    template<class IsWanted>
    void prepare(unsigned capacity,IsWanted isWanted){
        for(size_t i=0;i<entries_.size();++i){
            auto& entry=entries_[i];
            entry.wanted=false;
            if(i>=capacity){
                if(entry.slot.cacheValid)entry.slot.reset();
                entry.age=0;
            }else if(entry.slot.cacheValid){
                entry.wanted=isWanted(entry.slot.cacheSource);
            }
        }
    }

    void retain(CubeSlot& slot,unsigned capacity,const std::string* map){
        bool complete=slot.cacheValid&&map&&slot.map==*map&&slot.cacheSource==slot.selected.sourceId;
        for(unsigned face=0;face<6;++face)
            complete=complete&&(bool(slot.cacheFace[face])==bool(slot.cacheSurface[face]));
        if(!capacity||!complete){slot.reset();return;}

        unsigned victim=capacity;
        for(unsigned i=0;i<capacity;++i){
            const auto& entry=entries_[i];
            if(!entry.slot.cacheValid||entry.slot.cacheSource==slot.cacheSource){victim=i;break;}
            if(!entry.wanted&&(victim==capacity||entry.age<entries_[victim].age))victim=i;
        }
        if(victim==capacity){slot.reset();return;}

        auto& entry=entries_[victim];
        entry.slot.reset();
        // Only static faces survive retirement. A returning source must
        // render all dynamic faces before it can publish a usable cube.
        slot.releaseCube();
        slot.ready=false;
        slot.staticOnlyFaces=slot.geometryOnlyFaces=0;
        slot.schedule.invalidate();
        slot.faceCycle.reset();
        slot.contentMemo.valid=false;
        slot.retryAt=0;
        slot.terrainMemo=TerrainMemo{};
        std::swap(entry.slot,slot);
        entry.age=++age_;
        entry.wanted=false;
    }

    bool restore(CubeSlot& slot,uint64_t source,const std::string& map){
        if(slot.cacheValid&&slot.cacheSource==source&&slot.map==map)return false;
        for(auto& entry:entries_)if(entry.slot.cacheValid&&entry.slot.cacheSource==source&&entry.slot.map==map){
            // Transfer the current allocation while discarding its old
            // contents and refresh state.
            slot.swapCube(entry.slot);
            slot.reset();
            std::swap(slot,entry.slot);
            entry.age=0;
            entry.wanted=false;
            return true;
        }
        return false;
    }
};
}
