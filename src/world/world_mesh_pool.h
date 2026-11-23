#pragma once
#include "geometry_memory.h"
#include "mesh_buffer_store.h"
#include "mesh_pool_trim.h"
#include "static_shadow_scene.h"
#include "upload_lock.h"
#include "world_mesh_pages.h"
#include <array>
#include <windows.h>
#include <d3d9.h>

// The device scene owns both the page table and its D3D buffers.
// Draw passes borrow the active pages. Only staging claims may replace buffers.
class NorthlightWorldMeshPool {
public:
    struct Buffers {
        IDirect3DVertexBuffer9* vb=nullptr;
        IDirect3DIndexBuffer9* ib=nullptr;
        D3DPOOL vbPool=D3DPOOL_MANAGED,ibPool=D3DPOOL_MANAGED;
        bool vbFresh=false,ibFresh=false;
    };
    NorthlightMeshBufferStore::Store store;
    std::vector<Buffers> buffers;
    NorthlightMeshPoolTrim::State trimState;

    NorthlightWorldMeshPool()=default;
    NorthlightWorldMeshPool(const NorthlightWorldMeshPool&)=delete;
    NorthlightWorldMeshPool& operator=(const NorthlightWorldMeshPool&)=delete;
    ~NorthlightWorldMeshPool(){clear();}

    void sync(){buffers.resize(store.entries().size());}
    void preload(uint32_t id,bool vertex){
        auto& page=buffers[id];
        if(vertex)page.vb->PreLoad();else page.ib->PreLoad();
    }

    template<class Trace>
    bool copy(uint32_t id,bool vertex,const NorthlightWorldMeshPages::Plan& source,
              const NorthlightWorldMeshPages::Page& cpu,size_t offset,size_t bytes,const Trace& trace){
        auto& page=buffers[id];
        const D3DPOOL pool=vertex?page.vbPool:page.ibPool;
        const bool compact=vertex&&pool==D3DPOOL_DEFAULT;
        const size_t gpuOffset=compact?offset/sizeof(NorthlightGI::WorldVertex)*sizeof(StaticShadow::Vertex):offset;
        const size_t gpuBytes=compact?bytes/sizeof(NorthlightGI::WorldVertex)*sizeof(StaticShadow::Vertex):bytes;
        // Fresh DEFAULT pages can use NOOVERWRITE. An idle buffer that was
        // previously drawable must still synchronize before its first write.
        const DWORD flags=(vertex?page.vbFresh:page.ibFresh)&&pool==D3DPOOL_DEFAULT?NorthlightUpload::FreshBufferLock:DWORD(0);
        trace(vertex?"page-vertex-lock":"page-index-lock",flags);
        void* data=nullptr;
        HRESULT hr=vertex?page.vb->Lock(UINT(gpuOffset),UINT(gpuBytes),&data,flags):page.ib->Lock(UINT(offset),UINT(bytes),&data,flags);
        if(FAILED(hr))return false;
        bool copied=data!=nullptr;
        if(compact){
            // Shadow programs read position and UV. The worker retains the
            // complete vertex for GI and the content key.
            std::array<NorthlightGI::WorldVertex,2048> input;
            size_t done=0,output=0;
            while(copied&&done<bytes){
                const size_t count=std::min(input.size(),(bytes-done)/sizeof(NorthlightGI::WorldVertex));
                copied=source.copy(cpu,true,offset+done,count*sizeof(NorthlightGI::WorldVertex),input.data());
                for(size_t i=0;copied&&i<count;++i){
                    const auto& v=input[i];
                    const StaticShadow::Vertex packed{v.position.x,v.position.y,v.position.z,v.u,v.v};
                    std::memcpy(static_cast<char*>(data)+output,&packed,sizeof packed);
                    output+=sizeof packed;
                }
                done+=count*sizeof(NorthlightGI::WorldVertex);
            }
        }else if(copied)copied=source.copy(cpu,vertex,offset,bytes,data);
        hr=vertex?page.vb->Unlock():page.ib->Unlock();
        return copied&&SUCCEEDED(hr);
    }

    template<class PressureReplay>
    bool acquire(IDirect3DDevice9* d,uint32_t id,bool vertex,UINT bytes,D3DPOOL pool,const PressureReplay& pressureReplay){
        if(!store.writable(id)||id>=buffers.size())return false;
        auto& slot=buffers[id];
        const auto& entry=store.entries()[id];
        if(vertex?slot.vb&&slot.vbPool==pool&&entry.vbCapacity>=bytes:slot.ib&&slot.ibPool==pool&&entry.ibCapacity>=bytes)return true;
        const UINT capacity=UINT(pool==D3DPOOL_DEFAULT?NorthlightGeometryMemory::roundUp(bytes,4096):NorthlightMeshBufferStore::roundBuffer(bytes));
        // Re-sample before each real driver allocation, including after partial progress.
        MEMORYSTATUSEX current={};current.dwLength=sizeof current;
        const bool valid=GlobalMemoryStatusEx(&current)!=FALSE;
        const auto available=valid?NorthlightGeometryMemory::corrected(current.ullAvailVirtual):0;
        const auto reserve=pool==D3DPOOL_DEFAULT&&pressureReplay()?NorthlightGeometryMemory::ResetReplayReserve:NorthlightGeometryMemory::ProcessReserve;
        if(!NorthlightGeometryMemory::admitsSmallPageGrowth(valid,available,capacity,reserve))return false;

        // Fresh DEFAULT pages can use NOOVERWRITE during staging.
        const DWORD usage=D3DUSAGE_WRITEONLY|(pool==D3DPOOL_DEFAULT?D3DUSAGE_DYNAMIC:0);
        if(vertex){
            drop(slot.vb);store.setCapacity(id,true,0);slot.vbFresh=false;
            if(FAILED(d->CreateVertexBuffer(capacity,usage,0,pool,&slot.vb,nullptr))||!slot.vb){drop(slot.vb);return false;}
            slot.vbPool=pool;slot.vbFresh=true;store.setCapacity(id,true,capacity);
        }else{
            drop(slot.ib);store.setCapacity(id,false,0);slot.ibFresh=false;
            if(FAILED(d->CreateIndexBuffer(capacity,usage,D3DFMT_INDEX16,pool,&slot.ib,nullptr))||!slot.ib){drop(slot.ib);return false;}
            slot.ibPool=pool;slot.ibFresh=true;store.setCapacity(id,false,capacity);
        }
        return true;
    }

    size_t trim(uint64_t maxBytes){
        const auto freed=store.trimIdle(maxBytes,[&](uint32_t id){
            if(id<buffers.size())release(buffers[id]);
        });
        sync();
        return size_t(freed);
    }

    void clear(){
        for(auto& slot:buffers)release(slot);
        buffers.clear();
        store.reset();
        trimState.reset();
    }
private:
    template<class T> static void drop(T*& value){if(value)value->Release();value=nullptr;}
    static void release(Buffers& slot){drop(slot.vb);drop(slot.ib);}
};
