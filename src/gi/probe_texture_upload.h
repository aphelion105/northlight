#pragma once
#include "probe_activation.h"
#include "probe_blend.h"
#include "probe_local_visibility.h"
#include <cstddef>
#include <cstring>
#include <limits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightProbeUpload {
// Retain the immutable payload identity until the GPU has its replacement.
// Light-only publications skip both LockRect and the managed texture upload.
class LocalVisibilityMirror {
    using Payload=std::shared_ptr<const NorthlightGI::ProbeLocalVisibility>;
    std::vector<Payload> values_;
    bool ready_=false;
    static const Payload& payload(const NorthlightGI::ProbeAtlasEntry& e){static const Payload none;return e.occupied&&e.probe.valid?e.probe.localVisibility:none;}
public:
    struct Dirty {unsigned left=0,top=0,right=0,bottom=0;};
    void reset(){values_.clear();ready_=false;}
    bool prepare(size_t count){
        if(values_.size()==count)return true;
        try{values_.assign(count,nullptr);ready_=false;return true;}catch(const std::bad_alloc&){return false;}
    }
    bool changed(const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas)const{
        if(!ready_||values_.size()!=atlas.size())return true;
        for(size_t i=0;i<atlas.size();++i)if(values_[i]!=payload(atlas[i]))return true;
        return false;
    }
    Dirty write(void* bits,size_t pitch,unsigned n,const std::vector<NorthlightGI::ProbeAtlasEntry>& entries){
        const auto layout=NorthlightGI::LocalProbeVisibility::atlas(n);
        Dirty dirty{layout.width,layout.height,0,0};
        const std::array<uint16_t,4> missing={0,0,0,0xbc00}; // w=-1: fail closed
        if(!ready_){
            for(unsigned y=0;y<layout.height;++y)for(unsigned x=0;x<layout.width;++x)
                std::memcpy(static_cast<char*>(bits)+y*pitch+x*8,missing.data(),8);
            dirty={0,0,layout.width,layout.height};
        }
        for(size_t i=0;i<entries.size();++i){auto p=payload(entries[i]);
            if((ready_&&p==values_[i])||(!ready_&&!p))continue;
            const unsigned x=unsigned(i%layout.columns)*9,y=unsigned(i/layout.columns)*9;
            for(unsigned row=0;row<9;++row){auto* dst=static_cast<char*>(bits)+(y+row)*pitch+x*8;
                if(p)std::memcpy(dst,p->texels.data()+row*9,9*8);
                else for(unsigned col=0;col<9;++col)std::memcpy(dst+col*8,missing.data(),8);}
            dirty.left=std::min(dirty.left,x);dirty.top=std::min(dirty.top,y);
            dirty.right=std::max(dirty.right,x+9);dirty.bottom=std::max(dirty.bottom,y+9);values_[i]=std::move(p);
        }
        ready_=true;return dirty;
    }
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
        memory.vector(NorthlightMemoryFootprint::Probes,values_);
        for(const auto& p:values_)if(p)memory.allocation(NorthlightMemoryFootprint::Probes,p.get(),sizeof(NorthlightGI::ProbeLocalVisibility));
    }
};
// Match the shader's x+z*n, y atlas layout. Specialize outside the texel loop
// so every texel writes just its own channel, with no runtime channel tests.
template<unsigned Channel,bool KnownValid=false,int PreviousMode=-1,bool BlendValid=false,bool Contiguous=false>
void write(void* bits,std::size_t pitch,unsigned n,
           const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas,
           const NorthlightProbeBlend::Mirror& blend,NorthlightProbeActivation& activation,
           float now,bool hasPrevious){
    static_assert(Channel<5);
    // Independent SH targets and the validated activation path read atlas entries contiguously.
    // The checked fallback keeps its original order if it throws partway through.
    for(unsigned major=0;major<n;++major)for(unsigned minor=0;minor<n;++minor){
        const unsigned y=((Channel<3&&Contiguous)||(Channel==4&&KnownValid))?minor:major;
        const unsigned z=((Channel<3&&Contiguous)||(Channel==4&&KnownValid))?major:minor;
        auto* row=reinterpret_cast<float*>(static_cast<char*>(bits)+y*pitch)+z*n*4;
        const std::size_t base=(std::size_t(z)*n+y)*n;
        for(unsigned x=0;x<n;++x){
            const auto& entry=atlas[base+x];const auto& p=entry.probe;float* dst=row+x*4;
            if constexpr(Channel<3){
#if defined(__SSE2__)
                // The independent target cannot alias the atlas: load the complete texel before one unaligned store.
                if constexpr(Contiguous){
                    if constexpr(Channel==0)_mm_storeu_ps(dst,_mm_set_ps(p.sh[3].x,p.sh[2].x,p.sh[1].x,p.sh[0].x));
                    else if constexpr(Channel==1)_mm_storeu_ps(dst,_mm_set_ps(p.sh[3].y,p.sh[2].y,p.sh[1].y,p.sh[0].y));
                    else _mm_storeu_ps(dst,_mm_set_ps(p.sh[3].z,p.sh[2].z,p.sh[1].z,p.sh[0].z));
                }else
#endif
                for(unsigned k=0;k<4;++k){
                    if constexpr(Channel==0)dst[k]=p.sh[k].x;
                    else if constexpr(Channel==1)dst[k]=p.sh[k].y;
                    else dst[k]=p.sh[k].z;
                }
            }else if constexpr(Channel==3){
                const float valid=entry.occupied&&p.valid?1.f:0.f;
                const float start=(PreviousMode<0?hasPrevious:PreviousMode!=0)?(BlendValid?blend.startKnownValid(base+x):blend.start(base+x)):NorthlightProbeBlend::None;
                for(unsigned axis=0;axis<6;++axis){
                    float* moment=reinterpret_cast<float*>(reinterpret_cast<char*>(dst)+axis*n*pitch);
#if defined(__SSE2__)
                    if constexpr(Contiguous)_mm_storeu_ps(moment,_mm_set_ps(start,valid,p.moments[axis].meanSquare,p.moments[axis].mean));else
#endif
                    {moment[0]=p.moments[axis].mean;moment[1]=p.moments[axis].meanSquare;moment[2]=valid;moment[3]=start;}
                }
            }else{
                dst[0]=float(entry.key.x);dst[1]=float(entry.key.y);dst[2]=float(entry.key.z);
                dst[3]=KnownValid?activation.updateKnownValid(base+x,entry,now):activation.update(base+x,entry,now);
            }
        }
    }
}
inline void write(unsigned channel,void* bits,std::size_t pitch,unsigned n,
                  const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas,
                  const NorthlightProbeBlend::Mirror& blend,NorthlightProbeActivation& activation,
                  float now,bool hasPrevious,bool independentTarget=false){
    switch(channel){
    case 0:if(independentTarget)write<0,false,-1,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<0>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 1:if(independentTarget)write<1,false,-1,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<1>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 2:if(independentTarget)write<2,false,-1,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<2>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 3:
        if(hasPrevious&&n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&blend.covers(std::size_t(n)*n*n)){
            if(independentTarget)write<3,false,1,true,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
            else write<3,false,1,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        }else if(hasPrevious)write<3,false,1>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        else if(independentTarget)write<3,false,0,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        else write<3,false,0>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        break;
    case 4:if(n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&activation.covers(std::size_t(n)*n*n))write<4,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<4,false>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    }
}
template<bool KnownValid> void previousImpl(void* bits,std::size_t pitch,unsigned n,const NorthlightProbeBlend::Mirror& blend){
    for(unsigned y=0;y<n;++y)for(unsigned z=0;z<n;++z){
        auto* row=reinterpret_cast<float*>(static_cast<char*>(bits)+y*pitch)+z*n*4;
        const std::size_t base=(std::size_t(z)*n+y)*n;
        for(unsigned x=0;x<n;++x){
            const float* source=KnownValid?blend.previousKnownValid(base+x):blend.previous(base+x);
            for(unsigned c=0;c<3;++c)std::memcpy(row+x*4+c*n*n*4,source+c*4,4*sizeof(float));
        }
    }
}
inline void previous(void* bits,std::size_t pitch,unsigned n,const NorthlightProbeBlend::Mirror& blend){
    if(n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&blend.covers(std::size_t(n)*n*n))previousImpl<true>(bits,pitch,n,blend);
    else previousImpl<false>(bits,pitch,n,blend);
}
} // namespace NorthlightProbeUpload
