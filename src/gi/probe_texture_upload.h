#pragma once
#include "probe_activation.h"
#include "probe_blend.h"
#include <cstddef>
#include <cstring>
#include <limits>

namespace NorthlightProbeUpload {
// Match the shader's x+z*n, y atlas layout. Specialize outside the texel loop
// so every texel writes just its own channel, with no runtime channel tests.
template<unsigned Channel,bool KnownValid=false>
void write(void* bits,std::size_t pitch,unsigned n,
           const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas,
           const NorthlightProbeBlend::Mirror& blend,NorthlightProbeActivation& activation,
           float now,bool hasPrevious){
    static_assert(Channel<5);
    // The validated activation path reads atlas entries in their contiguous order.
    // The checked fallback keeps its original order if it throws partway through.
    for(unsigned major=0;major<n;++major)for(unsigned minor=0;minor<n;++minor){
        const unsigned y=Channel==4&&KnownValid?minor:major;
        const unsigned z=Channel==4&&KnownValid?major:minor;
        auto* row=reinterpret_cast<float*>(static_cast<char*>(bits)+y*pitch)+z*n*4;
        const std::size_t base=(std::size_t(z)*n+y)*n;
        for(unsigned x=0;x<n;++x){
            const auto& entry=atlas[base+x];const auto& p=entry.probe;float* dst=row+x*4;
            if constexpr(Channel<3){
                for(unsigned k=0;k<4;++k){
                    if constexpr(Channel==0)dst[k]=p.sh[k].x;
                    else if constexpr(Channel==1)dst[k]=p.sh[k].y;
                    else dst[k]=p.sh[k].z;
                }
            }else if constexpr(Channel==3){
                const float valid=entry.occupied&&p.valid?1.f:0.f;
                const float start=hasPrevious?blend.start(base+x):NorthlightProbeBlend::None;
                for(unsigned axis=0;axis<6;++axis){
                    float* moment=reinterpret_cast<float*>(reinterpret_cast<char*>(dst)+axis*n*pitch);
                    moment[0]=p.moments[axis].mean;moment[1]=p.moments[axis].meanSquare;
                    moment[2]=valid;moment[3]=start;
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
                  float now,bool hasPrevious){
    switch(channel){
    case 0:write<0>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 1:write<1>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 2:write<2>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 3:write<3>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 4:if(n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&activation.covers(std::size_t(n)*n*n))write<4,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<4,false>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    }
}
inline void previous(void* bits,std::size_t pitch,unsigned n,const NorthlightProbeBlend::Mirror& blend){
    for(unsigned y=0;y<n;++y)for(unsigned z=0;z<n;++z){
        auto* row=reinterpret_cast<float*>(static_cast<char*>(bits)+y*pitch)+z*n*4;
        const std::size_t base=(std::size_t(z)*n+y)*n;
        for(unsigned x=0;x<n;++x){
            const float* source=blend.previous(base+x);
            for(unsigned c=0;c<3;++c)std::memcpy(row+x*4+c*n*n*4,source+c*4,4*sizeof(float));
        }
    }
}
} // namespace NorthlightProbeUpload
