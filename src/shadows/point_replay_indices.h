#pragma once
#include "draw_snapshot.h"
#include "lock_meter.h"
#include "upload_lock.h"
#include <algorithm>
#include <array>
#include <map>
#include <type_traits>
#include <utility>

namespace NorthlightPointShadow {
// The immutable index lists still address the original resident vertex page.
// Pose, shader and coverage are checked by the caller for each submission.
class ReplayIndexBatches {
    using Mesh=NorthlightDrawSnapshot::Mesh;
public:
    static constexpr unsigned MaxParts=4;
    struct Part {const std::shared_ptr<const Mesh>* owner=nullptr;UINT minimum=0;};
    using Parts=std::array<Part,MaxParts>;
private:
    using Key=std::array<std::pair<uintptr_t,UINT>,MaxParts>;
    struct Entry {
        std::array<std::weak_ptr<const Mesh>,MaxParts> owners;
        NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> buffer;
        size_t bytes=0;
        unsigned touched=0,count=0;
    };
    std::map<Key,Entry> entries_;
    size_t bytes_=0,newBytes_=0;
    unsigned frame_=0,newBuffers_=0;
    bool admissionChecked_=false,admitted_=false;
    static constexpr size_t ByteLimit=4*1024*1024,FrameLimit=256*1024;
    static constexpr unsigned EntryLimit=256,BuildLimit=8;

    void erase(std::map<Key,Entry>::iterator it){
        bytes_-=it->second.bytes;
        entries_.erase(it);
    }
public:
    unsigned builds=0,hits=0;
    size_t bytes()const{return bytes_;}
    size_t uploaded()const{return newBytes_;}
    template<class Visit> void buffers(Visit visit)const{
        for(const auto& entry:entries_)visit(entry.second.buffer.p);
    }

    size_t clear(){
        entries_.clear();
        return std::exchange(bytes_,0);
    }

    void begin(unsigned frame){
        frame_=frame;
        newBytes_=newBuffers_=builds=hits=0;
        admissionChecked_=admitted_=false;
        if(frame%32==0)
            for(auto it=entries_.begin();it!=entries_.end();){
                auto current=it++;
                const auto& entry=current->second;
                if(frame-entry.touched>32||std::any_of(entry.owners.begin(),entry.owners.begin()+entry.count,
                    [](const auto& owner){return owner.expired();}))
                    erase(current);
            }
    }

    template<class Admit>
    IDirect3DIndexBuffer9* get(IDirect3DDevice9* device,const std::shared_ptr<const Mesh>& first,UINT firstMin,
                               const std::shared_ptr<const Mesh>& second,UINT secondMin,Admit admit){
        return get(device,Parts{{{&first,firstMin},{&second,secondMin}}},2,admit);
    }

    template<class Admit>
    IDirect3DIndexBuffer9* get(IDirect3DDevice9* device,const Parts& parts,unsigned count,Admit admit){
        if(count<2||count>parts.size())return nullptr;
        Key key{};
        for(unsigned n=0;n<count;++n)key[n]={reinterpret_cast<uintptr_t>(parts[n].owner->get()),parts[n].minimum};
        const auto match=entries_.find(key);
        if(match!=entries_.end()){
            auto& entry=match->second;
            bool current=true;
            for(unsigned n=0;n<count&&current;++n)current=NorthlightSourceIdentity::sameOwner(entry.owners[n],*parts[n].owner);
            if(current){
                entry.touched=frame_;
                ++hits;
                return entry.buffer.p;
            }
            erase(match);
        }
        bool narrow=true;
        uint64_t words=0;
        for(unsigned n=0;n<count;++n){
            const auto& part=parts[n];
            const auto& mesh=**part.owner;
            const uint64_t end=uint64_t(part.minimum)+mesh.vertexCount;
            if(end>UINT_MAX)return nullptr;
            narrow=narrow&&end<=65536;
            words+=mesh.indices.size();
        }
        const uint64_t required=words*(narrow?2:4);
        if(!required||newBuffers_>=BuildLimit||required>FrameLimit-newBytes_||required>ByteLimit-bytes_||entries_.size()>=EntryLimit)
            return nullptr;
        const size_t bytes=size_t(required);
        if(!admissionChecked_){admitted_=admit(ByteLimit);admissionChecked_=true;}
        if(!admitted_)return nullptr;
        ++newBuffers_;
        const auto valid=[](const Mesh& mesh){
            return mesh.hasCheckedIndices()||std::all_of(mesh.indices.begin(),mesh.indices.end(),
                [&](uint32_t index){return index<mesh.vertexCount;});
        };
        for(unsigned n=0;n<count;++n)if(!valid(**parts[n].owner))return nullptr;
        NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> buffer;
        if(FAILED(device->CreateIndexBuffer(UINT(bytes),D3DUSAGE_WRITEONLY,narrow?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,buffer.out(),nullptr))||!buffer.p)
            return nullptr;
        void* data=nullptr;
        if(FAILED(buffer->Lock(0,UINT(bytes),&data,NorthlightUpload::FreshBufferLock)))return nullptr;
        if(data){
            const auto write=[&](auto* indices){
                using Index=std::remove_pointer_t<decltype(indices)>;
                for(unsigned n=0;n<count;++n)
                    for(auto index:(*parts[n].owner)->indices)*indices++=Index(parts[n].minimum+index);
            };
            if(narrow)write(static_cast<uint16_t*>(data));
            else write(static_cast<uint32_t*>(data));
        }
        if(FAILED(buffer->Unlock())||!data)return nullptr;
        NorthlightLockMeter::staging(bytes);
        newBytes_+=bytes;
        try{
            auto& entry=entries_.try_emplace(key).first->second;
            for(unsigned n=0;n<count;++n)entry.owners[n]=*parts[n].owner;
            entry.count=count;
            entry.bytes=bytes;
            entry.touched=frame_;
            entry.buffer.p=std::exchange(buffer.p,nullptr);
            bytes_+=bytes;
            ++builds;
            return entry.buffer.p;
        }catch(const std::bad_alloc&){return nullptr;}
    }
};
}
