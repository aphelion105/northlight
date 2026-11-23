#pragma once
#include <cstdint>
#include <cstring>
#include <d3d9.h>
#include "draw_snapshot.h"
#include "local_light_compiled_shaders.h"
#include "upload_lock.h"

// Shared by point, directional and actor shadow maps. The caller owns the
// map, compatible packet order and invalidation of its replay state caches.
class NorthlightShadowPalettePass {
    friend struct PointTerrainCoverageTest;
    IDirect3DVertexShader9* shader_=nullptr;
    IDirect3DVertexDeclaration9* declaration_=nullptr;
    IDirect3DVertexBuffer9* ids_=nullptr;
    bool unsupported_=false;
    std::uint64_t retryFrame_=0;

    template<class T> static void release(T*& value){if(value){value->Release();value=nullptr;}}

public:
    static constexpr UINT InstanceBytes=84*sizeof(float);

    IDirect3DVertexBuffer9* buffer()const{return ids_;}
    bool unsupported()const{return unsupported_;}
    std::uint64_t retryIn(std::uint64_t frame)const{return retryFrame_>frame?retryFrame_-frame:0;}
    bool canAttempt(std::uint64_t frame)const{return !unsupported_&&frame>=retryFrame_;}

    void reset(bool preserveShaders){
        release(ids_);
        unsupported_=false;
        retryFrame_=0;
        if(!preserveShaders){release(shader_);release(declaration_);}
    }

    template<class Admit,class AllocateShader,class AllocateBuffer>
    bool ensure(IDirect3DDevice9* device,std::uint64_t frame,Admit admit,
                AllocateShader allocateShader,AllocateBuffer allocateBuffer){
        if(ids_)return true;
        if(!canAttempt(frame))return false;
        retryFrame_=frame+120;
        D3DCAPS9 caps{};
        if(FAILED(device->GetDeviceCaps(&caps)))return false;
        if(caps.MaxStreams<2||caps.VertexShaderVersion<D3DVS_VERSION(3,0)){
            unsupported_=true;
            return false;
        }
        if(!admit(InstanceBytes))return false;
        if(!shader_&&(FAILED(allocateShader([&]{return device->CreateVertexShader(kLocalPaletteShadowVSShader,&shader_);}))||!shader_)){
            release(shader_);
            return false;
        }
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},
            {1,0,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
        if(!declaration_&&(FAILED(allocateShader([&]{return device->CreateVertexDeclaration(elements,&declaration_);}))||!declaration_)){
            release(declaration_);
            return false;
        }
        NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> ids;
        if(FAILED(allocateBuffer([&]{return device->CreateVertexBuffer(InstanceBytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,ids.out(),nullptr);}))||!ids.p)return false;
        void* data=nullptr;
        if(FAILED(ids->Lock(0,InstanceBytes,&data,NorthlightUpload::FreshBufferLock))){if(data)ids->Unlock();return false;}
        if(!data){ids->Unlock();return false;}
        for(unsigned i=0;i<84;++i)static_cast<float*>(data)[i]=float(3*i);
        if(FAILED(ids->Unlock())){ids->Unlock();return false;}
        ids_=ids.p;
        ids.p=nullptr;
        retryFrame_=0;
        return true;
    }

    template<class Replay,class Index,class Pose,class Constants,class Record>
    HRESULT submit(IDirect3DDevice9* device,const Replay& packet,UINT span,const float* rows,size_t& at,size_t end,
                   Index indexAt,Pose poseOf,Constants constantsOf,Record record){
        const UINT capacity=82/span;
        const float opaque[]={0,0,0,-1};
        HRESULT result=device->SetVertexDeclaration(declaration_);
        if(SUCCEEDED(result))result=device->SetVertexShader(shader_);
        if(SUCCEEDED(result))result=device->SetVertexShaderConstantF(0,rows,4);
        const auto firstBone=packet.boundsWork.paletteFirst;
        const float paletteInfo[]={float(3*firstBone),0,0,0};
        if(SUCCEEDED(result))result=device->SetVertexShaderConstantF(252,paletteInfo,1);
        if(SUCCEEDED(result))result=device->SetTexture(0,nullptr);
        if(SUCCEEDED(result))result=device->SetPixelShaderConstantF(0,opaque,1);
        if(SUCCEEDED(result))result=device->SetStreamSource(0,packet.stream[0],packet.offset[0],packet.stride[0]);
        if(SUCCEEDED(result))result=device->SetIndices(packet.index);
        if(SUCCEEDED(result))result=device->SetStreamSource(1,ids_,0,span*sizeof(float));
        if(SUCCEEDED(result))result=device->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1);
        const size_t poseBytes=size_t(span)*48;
        size_t previousPose=SIZE_MAX;
        while(at<end&&SUCCEEDED(result)){
            const size_t firstPacket=at;
            UINT count=0;
            float palette[248*4];
            while(at<end){
                const size_t packetIndex=indexAt(at),pose=poseOf(packetIndex);
                // An identical opaque pose writes the same depth.
                if(previousPose==pose){++at;continue;}
                if(count==capacity)break;
                std::memcpy(palette+count*span*12,constantsOf(packetIndex)+(31+3*firstBone)*4,poseBytes);
                previousPose=pose;
                ++count;
                ++at;
            }
            const unsigned packets=unsigned(at-firstPacket);
            result=device->SetVertexShaderConstantF(4,palette,count*span*3);
            if(SUCCEEDED(result))result=device->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|count);
            if(SUCCEEDED(result))result=device->DrawIndexedPrimitive(packet.type,packet.base,packet.min,packet.vertices,packet.start,packet.count);
            if(SUCCEEDED(result))record(packets,count);
        }
        for(unsigned stream=0;stream<2;++stream){
            auto hr=device->SetStreamSourceFreq(stream,1);
            if(FAILED(hr))hr=device->SetStreamSourceFreq(stream,1);
            if(FAILED(hr))result=hr;
        }
        return result;
    }
};
