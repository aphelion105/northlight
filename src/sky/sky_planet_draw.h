#pragma once
#include <d3d9.h>
#include "sky_planet_geometry.h"
#include "replay_copy_reader.h"

namespace NorthlightSkyPlanet {
// A bounded copy of an authored 4/8-vertex planet. Native shaders, UVs, color,
// animation, blending and draw order remain the client's. No source writes.
class Draw {
    unsigned char vertices_[8*128]{};
    std::uint16_t indices_[12]{};
    unsigned count_=0,stride_=0,primitives_=0;
    template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}};
    template<class T> static bool copy(T* buffer,std::uint64_t offset,unsigned size,void* out){
        if(offset+size>UINT32_MAX)return false;
        NorthlightReplayCopies::Reader<T> reader(buffer);void* bytes=nullptr;
        if(FAILED(reader.lock(UINT(offset),size,&bytes)))return false;
        if(bytes)std::memcpy(out,bytes,size);
        const bool ok=bytes!=nullptr;return SUCCEEDED(reader.unlock())&&ok;
    }
public:
    void clear(){primitives_=0;}
    bool ready()const{return primitives_!=0;}
    template<class Args> bool prepare(IDirect3DDevice9* d,const Args& args,float factor){
        clear();
        if(args.type!=decltype(args.type)::Indexed||args.prim!=D3DPT_TRIANGLELIST||
           (args.primCount!=2&&args.primCount!=4)||!(factor>0&&factor<1))return false;
        Ref<IDirect3DVertexDeclaration9> decl;D3DVERTEXELEMENT9 elements[65];UINT elementsCount=65;
        if(FAILED(d->GetVertexDeclaration(&decl.p))||!decl.p||FAILED(decl.p->GetDeclaration(elements,&elementsCount))||elementsCount>65)return false;
        unsigned pos=~0u,weights=~0u,bones=~0u,weightBytes=0,boneBytes=0;
        constexpr unsigned sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
        for(unsigned i=0;i<elementsCount;++i){const auto& e=elements[i];if(e.Stream==0xff)break;
            if(e.Stream||e.Method!=D3DDECLMETHOD_DEFAULT||e.Type>=sizeof(sizes)/sizeof(sizes[0]))return false;
            if(e.Usage==D3DDECLUSAGE_POSITION){if(e.UsageIndex||e.Type!=D3DDECLTYPE_FLOAT3||pos!=~0u)return false;pos=e.Offset;}
            if(e.Usage==D3DDECLUSAGE_BLENDWEIGHT){weights=e.Offset;weightBytes=sizes[e.Type];}
            if(e.Usage==D3DDECLUSAGE_BLENDINDICES){bones=e.Offset;boneBytes=sizes[e.Type];}
        }
        Ref<IDirect3DVertexBuffer9> vb;Ref<IDirect3DIndexBuffer9> ib;UINT offset=0,stride=0,freq=0;
        D3DINDEXBUFFER_DESC desc{};D3DVERTEXBUFFER_DESC vdesc{};
        if(FAILED(d->GetStreamSource(0,&vb.p,&offset,&stride))||!vb.p||!stride||stride>128||pos>=stride||pos+12>stride||
           FAILED(d->GetStreamSourceFreq(0,&freq))||freq!=1||FAILED(vb.p->GetDesc(&vdesc))||
           FAILED(d->GetIndices(&ib.p))||!ib.p||FAILED(ib.p->GetDesc(&desc))||
           (desc.Format!=D3DFMT_INDEX16&&desc.Format!=D3DFMT_INDEX32))return false;
        for(unsigned i=0;i<elementsCount&&elements[i].Stream!=0xff;++i)if(elements[i].Offset+sizes[elements[i].Type]>stride)return false;
        const unsigned size=desc.Format==D3DFMT_INDEX16?2:4,indexCount=args.primCount*3;
        unsigned char raw[48]{};const auto at=std::uint64_t(args.first)*size;
        if(at+indexCount*size>desc.Size||!copy(ib.p,at,indexCount*size,raw))return false;
        unsigned unique[8]{};count_=0;stride_=stride;
        for(unsigned i=0;i<indexCount;++i){
            unsigned index=0;std::memcpy(&index,raw+i*size,size);
            if(index<args.minIndex||std::uint64_t(index)>=std::uint64_t(args.minIndex)+args.numVertices)return false;
            unsigned j=0;while(j<count_&&unique[j]!=index)++j;
            if(j==count_){
                if(count_==8)return false;
                const auto vertex=std::int64_t(args.baseVertex)+index;if(vertex<0)return false;
                const auto source=std::uint64_t(offset)+std::uint64_t(vertex)*stride;
                if(source+stride>vdesc.Size||!copy(vb.p,source,stride,vertices_+count_*stride))return false;
                unique[count_++]=index;
            }
            indices_[i]=std::uint16_t(j);
        }
        // Scaling commutes with a shared bone transform, never with different
        // bone blends on different corners of an animated mesh.
        for(unsigned i=1;i<count_;++i){
            if(weightBytes&&std::memcmp(vertices_+weights,vertices_+i*stride+weights,weightBytes))return false;
            if(boneBytes&&std::memcmp(vertices_+bones,vertices_+i*stride+bones,boneBytes))return false;
        }
        if(!scale(vertices_,count_,stride,pos,factor))return false;
        primitives_=args.primCount;return true;
    }
    template<class Submit> HRESULT submit(IDirect3DDevice9* d,Submit native){
        if(!ready())return native();
        // UP submission clears stream 0 and the index buffer. Restore both for
        // the next sky layer and for transmission/HDR replays of this layer.
        Ref<IDirect3DVertexBuffer9> vb;Ref<IDirect3DIndexBuffer9> ib;UINT offset=0,stride=0;
        if(FAILED(d->GetStreamSource(0,&vb.p,&offset,&stride))||FAILED(d->GetIndices(&ib.p)))return native();
        const HRESULT hr=d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,count_,primitives_,indices_,D3DFMT_INDEX16,vertices_,stride_);
        const HRESULT stream=d->SetStreamSource(0,vb.p,offset,stride),indices=d->SetIndices(ib.p);
        return FAILED(hr)?hr:FAILED(stream)?stream:indices;
    }
};
}
