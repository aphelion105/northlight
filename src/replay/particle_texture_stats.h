#pragma once
#include "actor_texture.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace NorthlightParticleTextures {
inline std::atomic<bool> enabled{false}; // Set by the device's EquipmentLights quality setting.
// Replay-thread only. Views borrow a complete CPU-uploaded mip until the next
// cache mutation; no driver lock, readback, COM reference or game-side shadow.
struct View {
    const std::uint8_t* rgba=nullptr;
    unsigned width=0,height=0,mip=0,format=0;
    std::uint64_t lifetime=0,revision=0,rgbaHash=0;
    bool sample(float u,float v,bool srgb,unsigned addressU,unsigned addressV,float out[4],bool linear=true)const{
        if(!rgba||!width||!height||!out||!std::isfinite(u)||!std::isfinite(v)||addressU<1||addressU>3||addressV<1||addressV>3)return false;
        auto coord=[](float f,unsigned mode){
            if(mode==3)return std::max(0.f,std::min(1.f,f));
            if(mode==2){f=std::fmod(f,2.f);if(f<0)f+=2;return f<=1?f:2-f;}
            return f-std::floor(f);
        };
        const float x=coord(u,addressU)*width-.5f,y=coord(v,addressV)*height-.5f;
        const int ix=int(std::floor(x)),iy=int(std::floor(y));
        auto texel=[](int i,unsigned n,unsigned mode){
            if(mode==3)return unsigned(std::max(0,std::min(int(n)-1,i)));
            if(mode==2){const int period=int(n)*2;i%=period;if(i<0)i+=period;return unsigned(i<int(n)?i:period-1-i);}
            i%=int(n);if(i<0)i+=int(n);return unsigned(i);
        };
        static const std::array<float,256> linearRGB=[](){std::array<float,256> a{};for(unsigned i=0;i<256;++i){const float s=float(i)/255;a[i]=s<=.04045f?s/12.92f:std::pow((s+.055f)/1.055f,2.4f);}return a;}();
        for(unsigned c=0;c<4;++c){
            auto read=[&](int a,int b){const auto value=rgba[(std::size_t(texel(b,height,addressV))*width+texel(a,width,addressU))*4+c];return srgb&&c<3?linearRGB[value]:float(value)/255;};
            if(!linear)out[c]=read(int(std::floor(x+.5f)),int(std::floor(y+.5f)));
            else{const float fx=x-ix,fy=y-iy;out[c]=(read(ix,iy)*(1-fx)+read(ix+1,iy)*fx)*(1-fy)+(read(ix,iy+1)*(1-fx)+read(ix+1,iy+1)*fx)*fy;}
        }
        return true;
    }
};
struct Counters {
    std::uint64_t uploads=0,retained=0,decoded=0,hits=0,misses=0,partial=0,failed=0,unsupported=0,large=0,invalidations=0,evictions=0,releases=0;
};
class Cache {
public:
    static constexpr unsigned MaxDimension=64,MaxEntries=128;
    static constexpr std::size_t PixelBudget=768u<<10;
private:
    struct Entry {
        const void* raw=nullptr;
        const void* owner=nullptr;
        std::uint64_t lifetime=0,revision=0,used=0,rgbaHash=0;
        unsigned width=0,height=0,mip=0,format=0;
        std::vector<std::uint8_t> pixels;
        // The upload's own bytes until a particle draw first asks for them:
        // most small mips are never sampled, so decoding waits for lookup().
        std::vector<std::uint8_t> source;std::size_t pitch=0;
        bool held()const{return !pixels.empty()||!source.empty();}
    };
    std::array<Entry,MaxEntries> entries_{};
    std::size_t bytes_=0;
    std::uint64_t serial_=0,clock_=0;
    Entry* find(const void* raw){if(raw)for(auto& e:entries_)if(e.raw==raw)return &e;return nullptr;}
    void drop(Entry& e){bytes_-=e.pixels.size()+e.source.size();std::vector<std::uint8_t>().swap(e.pixels);std::vector<std::uint8_t>().swap(e.source);e.width=e.height=0;}
    Entry* oldest(const Entry* except=nullptr){Entry* result=nullptr;for(auto& e:entries_)if(e.raw && &e!=except&&(!result||e.used<result->used))result=&e;return result;}
    Entry& slot(const void* raw,const void* owner){
        if(auto* e=find(raw)){if(e->owner==owner)return *e;drop(*e);*e={};}
        Entry* selected=nullptr;for(auto& e:entries_)if(!e.raw){selected=&e;break;}
        if(!selected){selected=oldest();drop(*selected);++counts.evictions;}
        *selected={};selected->raw=raw;selected->owner=owner;selected->lifetime=++serial_;selected->used=++clock_;return *selected;
    }
    static bool format(unsigned d3d,NorthlightActorTexture::Format& out){
        using F=NorthlightActorTexture::Format;
        switch(d3d){case 21:out=F::BGRA8;break;case 22:out=F::BGRX8;break;case 23:out=F::RGB565;break;
        case 24:out=F::XRGB1555;break;case 25:out=F::ARGB1555;break;case 26:out=F::ARGB4444;break;
        case 0x31545844:out=F::BC1;break;case 0x33545844:out=F::BC2;break;case 0x35545844:out=F::BC3;break;default:return false;}return true;
    }
public:
    Counters counts;
    std::size_t bytes()const{return bytes_;}
    unsigned size()const{unsigned n=0;for(const auto& e:entries_)n+=e.held();return n;}
    void clear(){for(auto& e:entries_){drop(e);e={};}}
    void erase(const void* raw,const void* owner=nullptr){if(auto* e=find(raw))if(!owner||e->owner==owner){drop(*e);*e={};++counts.releases;}}
    void invalidate(const void* raw){if(auto* e=find(raw)){drop(*e);++e->revision;++counts.invalidations;}}
    // Other full mips do not change this mip. Partial/failed writes invalidate
    // the texture conservatively. Only a later complete CPU upload can restore it.
    void upload(const void* raw,const void* owner,unsigned width,unsigned height,unsigned mip,unsigned d3dFormat,
                const void* source,std::size_t sourceBytes,std::size_t pitch,bool whole,bool success)noexcept{
        if(!enabled.load(std::memory_order_relaxed)){invalidate(raw);return;}
        ++counts.uploads;
        try{
        if(!success){++counts.failed;invalidate(raw);return;}
        if(!whole){++counts.partial;invalidate(raw);return;}
        if(auto* old=find(raw))if(old->owner!=owner)erase(raw);
        NorthlightActorTexture::Format decodedFormat;
        if(!raw||!owner||!format(d3dFormat,decodedFormat)){++counts.unsupported;invalidate(raw);return;}
        if(!width||!height||width>MaxDimension||height>MaxDimension){++counts.large;return;}
        auto* previous=find(raw);
        if(previous&&previous->owner==owner&&previous->held()&&mip>previous->mip){++previous->revision;return;}
        if(!source||!sourceBytes||sourceBytes>std::size_t(MaxDimension)*MaxDimension*16){++counts.failed;invalidate(raw);return;}
        Entry& e=slot(raw,owner);drop(e);
        while(bytes_+sourceBytes>PixelBudget){auto* victim=oldest(&e);if(!victim)break;drop(*victim);*victim={};++counts.evictions;}
        e.source.assign(static_cast<const std::uint8_t*>(source),static_cast<const std::uint8_t*>(source)+sourceBytes);e.pitch=pitch;
        e.width=width;e.height=height;e.mip=mip;e.format=d3dFormat;++e.revision;e.used=++clock_;bytes_+=e.source.size();++counts.retained;
        }catch(...){++counts.failed;invalidate(raw);}
    }
    bool lookup(const void* raw,View& out){
        out={};if(!enabled.load(std::memory_order_relaxed))return false;auto* e=find(raw);if(!e||!e->held()){++counts.misses;return false;}
        if(e->pixels.empty()){
            NorthlightActorTexture::Format decodedFormat;std::vector<std::uint8_t> decoded;
            try{if(!format(e->format,decodedFormat)||!NorthlightActorTexture::decode(e->source.data(),e->source.size(),e->width,e->height,e->pitch,decodedFormat,decoded))decoded.clear();}
            catch(...){decoded.clear();}
            if(decoded.empty()){++counts.failed;++counts.misses;invalidate(raw);return false;}
            bytes_-=e->source.size();std::vector<std::uint8_t>().swap(e->source);
            while(bytes_+decoded.size()>PixelBudget){auto* victim=oldest(e);if(!victim)break;drop(*victim);*victim={};++counts.evictions;}
            e->rgbaHash=14695981039346656037ull;for(auto byte:decoded){e->rgbaHash^=byte;e->rgbaHash*=1099511628211ull;}
            e->pixels=std::move(decoded);bytes_+=e->pixels.size();++counts.decoded;
        }
        e->used=++clock_;out={e->pixels.data(),e->width,e->height,e->mip,e->format,e->lifetime,e->revision,e->rgbaHash};++counts.hits;return true;
    }
};
static_assert(sizeof(Cache)+Cache::PixelBudget+Cache::MaxDimension*Cache::MaxDimension*4<(1u<<20),"particle texture cache plus decode scratch stays below 1 MiB");
inline Cache& cache(){static thread_local Cache value;return value;}
} // namespace NorthlightParticleTextures
