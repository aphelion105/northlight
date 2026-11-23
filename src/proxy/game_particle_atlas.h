#pragma once
#include "device_mirror.h"
#include "draw_snapshot.h"
#include "particle_atlas_shader.h"
#include "particle_atlas_texels.h"
#include "particle_texture_stats.h"
#include "vertex_declaration_cache.h"
#include "tracked_buffers.h"
#include <cmath>

class NorthlightGameParticleAtlas {
    template<class T> using Ref=NorthlightDrawSnapshot::Ref<T>;
    static constexpr unsigned VertexBytes=1u<<19,TextureBytes=3u<<20;
    struct Layout {
        D3DFORMAT format=D3DFMT_UNKNOWN;
        unsigned width=0,height=0,levels=0,capacity=0;
        bool wrap=false;
        bool operator==(const Layout& other)const{
            return format==other.format&&width==other.width&&height==other.height&&levels==other.levels&&
                   capacity==other.capacity&&wrap==other.wrap;
        }
    };
    struct Material {
        struct Tile {const void* source=nullptr;std::uint64_t revision=0;};
        Layout layout;
        std::array<Tile,4> tiles;
        std::uint64_t used=0;
        Ref<IDirect3DTexture9> atlas;
        unsigned find(const void* source)const{
            for(unsigned i=0;i<layout.capacity;++i)if(tiles[i].source==source)return i;
            return unsigned(tiles.size());
        }
        unsigned vacant(unsigned avoid)const{
            for(unsigned i=0;i<layout.capacity;++i)if(i!=avoid&&!tiles[i].source)return i;
            return unsigned(tiles.size());
        }
    };
    struct Candidate {
        const void* first=nullptr;
        const void* second=nullptr;
        std::uint64_t used=0;
        unsigned encounters=0;
    };
    struct Programs {
        Ref<IDirect3DVertexShader9> vs;
        Ref<IDirect3DPixelShader9> ps;
        int uv=-1;
    };
    std::array<Material,4> materials;
    std::array<Candidate,32> candidates;
    std::array<Programs,8> programs;
    Ref<IDirect3DVertexBuffer9> vertices;
    IDirect3DDevice9* d;
    std::vector<unsigned char> scratch;
    unsigned scratchBytes=0;
    std::uint64_t age=0;
    unsigned cursor=VertexBytes,nextProgram=0;
    bool builtThisFrame=false;
    unsigned maxTextureWidth=0;

    bool prepareScratch(unsigned begin,unsigned count,unsigned stride){
        const auto end=std::uint64_t(begin)+std::uint64_t(count)*stride;
        if(end==begin||end>VertexBytes)return false;
        if(!begin)scratchBytes=0;
        if(begin!=scratchBytes)return false;
        // All used bytes are overwritten. Keep storage at its high-water mark.
        if(end>scratch.size()){
            try{scratch.resize(unsigned(end));}catch(...){return false;}
        }
        scratchBytes=unsigned(end);
        return true;
    }
    bool copySource(IDirect3DVertexBuffer9* source,std::uint64_t offset,unsigned count,unsigned stride,unsigned begin){
        if(!prepareScratch(begin,count,stride))return false;
        const unsigned bytes=count*stride;
        auto* target=scratch.data()+begin;
        return NorthlightParticleUploads::atlasUploads().read(source,offset,bytes,target)||
               NorthlightParticleUploads::cache().read(source,offset,bytes,target);
    }

    template<class Shader,class Proof> static int certify(Shader* shader,Proof proof){
        std::uint32_t code[4096];
        UINT size=0;
        if(FAILED(shader->GetFunction(nullptr,&size))||!size||size>sizeof code||(size%4)||
           FAILED(shader->GetFunction(code,&size)))return -1;
        return proof(code,size/4);
    }
    int coordinates(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps){
        for(const auto& pair:programs)if(pair.vs.p==vs&&pair.ps.p==ps)return pair.uv;
        auto& pair=programs[nextProgram++%programs.size()];
        vs->AddRef();ps->AddRef();
        if(pair.vs.p)pair.vs.p->Release();
        if(pair.ps.p)pair.ps.p->Release();
        pair.vs.p=vs;pair.ps.p=ps;
        const int uv=certify(ps,[](const auto* code,unsigned size){return NorthlightParticleAtlas::Proof::pixelUv(code,size,0);});
        return pair.uv=uv<0?-1:certify(vs,[&](const auto* code,unsigned size){
            const int direct=NorthlightParticleAtlas::Proof::vertexUv(code,size,unsigned(uv));
            if(direct>=0)return direct;
            const int transformed=NorthlightParticleAtlas::Proof::affineVertexUv(code,size,unsigned(uv));
            return transformed<0?-1:transformed+16;
        });
    }
    static unsigned blockBytes(D3DFORMAT format){
        if(format==D3DFMT_DXT1)return 8;
        return format==D3DFMT_DXT3||format==D3DFMT_DXT5?16:0;
    }
    Layout layout(IDirect3DTexture9* a,IDirect3DTexture9* b,bool wrap)const{
        using namespace NorthlightParticleAtlas;
        D3DSURFACE_DESC da{},db{};
        const UINT levels=a->GetLevelCount();
        if(!levels||levels>10||levels!=b->GetLevelCount()||
           FAILED(a->GetLevelDesc(0,&da))||FAILED(b->GetLevelDesc(0,&db))||
           da.Pool!=D3DPOOL_MANAGED||db.Pool!=da.Pool||da.Usage||db.Usage||
           da.Format!=db.Format||!da.Width||!da.Height||da.Width!=db.Width||da.Height!=db.Height||
           (da.Width&(da.Width-1))||da.Width>512||da.Height>512||(da.Width>>(levels-1))==0)
            return {};
        const unsigned block=blockBytes(da.Format);
        if(!block&&da.Format!=D3DFMT_A8R8G8B8&&da.Format!=D3DFMT_X8R8G8B8)return {};
        const unsigned cellWidth=da.Width*TileWidthScale;
        const unsigned cellBytes=block?(cellWidth/4)*((da.Height+3)/4)*block:cellWidth*da.Height*4;
        const unsigned limit=std::min(maxTextureWidth/cellWidth,(TextureBytes*3/4)/cellBytes);
        if(limit<2)return {};
        return {da.Format,da.Width,da.Height,levels,limit>=4?4u:2u,wrap};
    }
    static bool writeTile(IDirect3DTexture9* atlas,const Layout& layout,unsigned tile,IDirect3DTexture9* texture){
        using namespace NorthlightParticleAtlas;
        const unsigned block=blockBytes(layout.format);
        const unsigned alphaBits=layout.format==D3DFMT_DXT3?4:layout.format==D3DFMT_DXT5?3:0;
        for(UINT level=0;level<layout.levels;++level){
            const unsigned w=layout.width>>level,h=std::max(1u,layout.height>>level);
            const unsigned rowBytes=block?(w+3)/4*block:w*4;
            const unsigned dstWidth=w*TileWidthScale*layout.capacity;
            const unsigned dstBytes=block?dstWidth/4*block:dstWidth*4;
            D3DLOCKED_RECT target{};
            if(FAILED(atlas->LockRect(level,&target,nullptr,0)))return false;
            D3DLOCKED_RECT source{};
            bool ok=target.pBits&&target.Pitch>=INT(dstBytes)&&
                    SUCCEEDED(texture->LockRect(level,&source,nullptr,D3DLOCK_READONLY));
            if(ok){
                ok=source.pBits&&source.Pitch>=INT(rowBytes);
                if(ok)tileRows(static_cast<const unsigned char*>(source.pBits),source.Pitch,
                               static_cast<unsigned char*>(target.pBits),target.Pitch,w,h,tile,layout.wrap,block,alphaBits);
                ok=SUCCEEDED(texture->UnlockRect(level))&&ok;
            }
            if(FAILED(atlas->UnlockRect(level))||!ok)return false;
        }
        return true;
    }
public:
    struct Selection {IDirect3DTexture9* texture=nullptr;unsigned firstTile=0,secondTile=0,widthScale=0;};
    struct Transform {
        double a=1,b=0,x=0,c=0,d=1,y=0,invU=1,invV=0;
        bool read(ExtensionDevice& device){
            float rows[8];
            if(FAILED(device.GetVertexShaderConstantF(6,rows,2)))return false;
            a=rows[0];b=rows[1];x=rows[3];c=rows[4];d=rows[5];y=rows[7];
            const double determinant=a*d-b*c;
            if(!std::isfinite(determinant)||!std::isfinite(x)||!std::isfinite(y)||std::abs(determinant)<1e-5)return false;
            invU=d/determinant;invV=-c/determinant;
            return true;
        }
    };
    unsigned built=0;
    explicit NorthlightGameParticleAtlas(IDirect3DDevice9* device):d(device){
        D3DCAPS9 caps{};
        if(SUCCEEDED(d->GetDeviceCaps(&caps)))maxTextureWidth=caps.MaxTextureWidth;
    }
    void nextFrame(){builtThisFrame=false;built=0;}
    void reset(){
        for(auto& material:materials){
            if(material.atlas.p)material.atlas.p->Release();
            material.atlas.p=nullptr;
            material.layout={};material.tiles={};material.used=0;
        }
        candidates={};
        if(vertices.p){vertices.p->Release();vertices.p=nullptr;}
        cursor=VertexBytes;
        nextFrame();
    }
    int uvOffset(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,
                 IDirect3DVertexDeclaration9* decl,NorthlightVertexDeclarations::Cache& declarations,unsigned stride,bool& transformed){
        if(!vs||!ps)return -1;
        const int uv=coordinates(vs,ps);
        const D3DVERTEXELEMENT9* layout=nullptr;
        UINT count=0;
        if(uv<0||!declarations.get(decl,layout,count))return -1;
        transformed=uv>=16;
        for(UINT i=0;i<count;++i){
            const auto& e=layout[i];
            if(e.Usage==D3DDECLUSAGE_TEXCOORD&&e.UsageIndex==(uv&15))
                return !e.Stream&&e.Type==D3DDECLTYPE_FLOAT2&&e.Offset+8u<=stride?e.Offset:-1;
        }
        return -1;
    }
    Selection material(IDirect3DBaseTexture9* first,IDirect3DBaseTexture9* second,bool wrap){
        if(!first||!second||first==second)return {};
        const bool reversed=reinterpret_cast<std::uintptr_t>(first)>reinterpret_cast<std::uintptr_t>(second);
        if(reversed)std::swap(first,second);
        ++age;
        auto select=[&](Material& m,unsigned a,unsigned b){
            m.used=age;
            return Selection{m.atlas.p,reversed?b:a,reversed?a:b,m.layout.capacity*NorthlightParticleAtlas::TileWidthScale};
        };
        auto& stamps=NorthlightParticleTextures::atlasRevisions;
        Material* slot=nullptr;
        for(auto& m:materials){
            if((!m.atlas.p||age-m.used>4096)&&(!slot||m.used<slot->used))slot=&m;
            if(!m.atlas.p||m.layout.wrap!=wrap)continue;
            const unsigned a=m.find(first),b=m.find(second);
            if(a<m.layout.capacity&&b<m.layout.capacity&&m.tiles[a].revision==stamps.get(first)&&
               m.tiles[b].revision==stamps.get(second))
                return first->GetLOD()||second->GetLOD()?Selection{}:select(m,a,b);
        }
        Candidate* candidate=nullptr;
        for(auto& c:candidates){
            if(c.first==first&&c.second==second){candidate=&c;break;}
            if(!candidate||c.used<candidate->used)candidate=&c;
        }
        if(candidate->first!=first||candidate->second!=second)*candidate={first,second,age,0};
        candidate->used=age;
        if(candidate->encounters<3)++candidate->encounters;
        if(candidate->encounters!=3||builtThisFrame)return {};
        if(first->GetType()!=D3DRTYPE_TEXTURE||second->GetType()!=D3DRTYPE_TEXTURE||first->GetLOD()||second->GetLOD())return {};
        auto* a=static_cast<IDirect3DTexture9*>(first);
        auto* b=static_cast<IDirect3DTexture9*>(second);
        const auto shape=layout(a,b,wrap);
        if(!shape.capacity){candidate->encounters=4;return {};}
        Material fresh;
        fresh.layout=shape;
        Material* group=nullptr;
        const unsigned missing=unsigned(fresh.tiles.size());
        unsigned ai=missing,bi=missing,reused=0;
        const auto consider=[&](Material& m){
            const unsigned aTile=m.find(first),bTile=m.find(second);
            const unsigned shared=(aTile!=missing)+(bTile!=missing);
            if(group&&shared<=reused)return;
            const unsigned x=aTile==missing?m.vacant(bTile):aTile;
            const unsigned y=bTile==missing?m.vacant(x):bTile;
            if(x==missing||y==missing)return;
            group=&m;ai=x;bi=y;reused=shared;
        };
        for(auto& m:materials)if(m.atlas.p&&m.layout==shape)consider(m);
        if(!group&&slot)consider(fresh);
        if(!group)return {};
        auto& m=*group;
        builtThisFrame=true;
        if(!m.atlas.p&&FAILED(d->CreateTexture(shape.width*NorthlightParticleAtlas::TileWidthScale*shape.capacity,
           shape.height,shape.levels,0,shape.format,D3DPOOL_MANAGED,m.atlas.out(),nullptr)))return {};
        const auto fill=[&](unsigned i,IDirect3DTexture9* texture){
            const auto revision=stamps.get(texture);
            auto& tile=m.tiles[i];
            if(tile.source==texture&&tile.revision==revision)return true;
            tile={};
            if(!writeTile(m.atlas.p,shape,i,texture))return false;
            tile={texture,revision};
            return true;
        };
        if(!fill(ai,a)||!fill(bi,b))return {};
        ++built;
        if(group==&fresh){
            std::swap(slot->atlas.p,fresh.atlas.p);
            slot->layout=shape;slot->tiles=fresh.tiles;
            group=slot;
        }
        return select(*group,ai,bi);
    }
    bool copyVertices(IDirect3DVertexBuffer9* source,std::uint64_t offset,unsigned count,unsigned stride,
                      unsigned uv,unsigned tile,unsigned widthScale,unsigned begin,const Transform& t){
        if(!copySource(source,offset,count,stride,begin))return false;
        const double origin=double(NorthlightParticleAtlas::origin(tile,1))/widthScale;
        for(unsigned i=0;i<count;++i){
            auto* at=scratch.data()+begin+i*stride+uv;
            float xy[2];std::memcpy(xy,at,sizeof xy);
            const double u=t.a*xy[0]+t.b*xy[1]+t.x,v=t.c*xy[0]+t.d*xy[1]+t.y;
            if(!(u>=-1&&u<=2)||!std::isfinite(v))return false;
            const double mapped=u/widthScale+origin,delta=mapped-u;
            xy[0]=float(xy[0]+t.invU*delta);
            xy[1]=float(xy[1]+t.invV*delta);
            if(!(std::abs(t.a*xy[0]+t.b*xy[1]+t.x-mapped)<2e-6&&
                 std::abs(t.c*xy[0]+t.d*xy[1]+t.y-v)<2e-6))return false;
            std::memcpy(at,xy,sizeof xy);
        }
        return true;
    }
    bool copyTexturePairVertices(IDirect3DVertexBuffer9* source,std::uint64_t offset,unsigned count,unsigned stride,
                                 unsigned begin,float selector){
        const unsigned packedStride=stride+sizeof selector;
        if(!prepareScratch(begin,count,packedStride))return false;
        auto* data=scratch.data()+begin;
        const auto pack=[&](const unsigned char* input){
            // Walk backwards so the fallback can expand its copy in place.
            for(unsigned i=count;i-->0;){
                auto* vertex=data+i*packedStride;
                std::memmove(vertex,input+i*stride,stride);
                std::memcpy(vertex+stride,&selector,sizeof selector);
            }
        };
        const unsigned bytes=count*stride;
        if(NorthlightParticleUploads::atlasUploads().readRange(source,offset,bytes,pack))return true;
        if(!NorthlightParticleUploads::cache().read(source,offset,bytes,data))return false;
        pack(data);
        return true;
    }
    IDirect3DVertexBuffer9* upload(UINT& offset){
        if(!vertices.p&&FAILED(d->CreateVertexBuffer(VertexBytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,vertices.out(),nullptr)))return nullptr;
        const unsigned bytes=scratchBytes;
        const bool discard=cursor>VertexBytes-bytes;
        offset=discard?0:cursor;
        void* target=nullptr;
        if(FAILED(vertices->Lock(offset,bytes,&target,discard?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE)))return nullptr;
        if(target)std::memcpy(target,scratch.data(),bytes);
        if(FAILED(vertices->Unlock())||!target)return nullptr;
        cursor=(offset+bytes+15)&~15u;
        return vertices.p;
    }
};
