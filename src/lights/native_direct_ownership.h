#pragma once
#include "equipment_lights.h"
#include <cstring>
#include "northlight_mem.h"

// Authored lights can prove presence in the receiver's actual slots even when
// the scene inventory is incomplete. Ambiguous sources keep the original draw.
namespace NorthlightNativeDirect {
// Terrain stores camera-relative world positions, RGB and denominator in
// interleaved triples. Normalize to the model bank before exact source matching.
inline bool terrainBank(const float* source,const float* view,float* bank){
    if(!source||!view||!bank)return false;std::fill_n(bank,44,0.f);
    for(unsigned i=0;i<12;++i)if(!std::isfinite(view[i]))return false;
    for(unsigned slot=0;slot<3;++slot)for(unsigned k=0;k<4;++k){
        bank[4*slot+k]=source[12*slot+4+k];
        if(k<3){for(unsigned j=0;j<3;++j)bank[4*(4+slot)+k]+=source[12*slot+j]*view[4*j+k];
            bank[4*(8+k)+slot]=source[12*slot+8+k];}
    }
    return true;
}
inline bool close(float a,float b,float tolerance){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=tolerance*std::max({1.f,std::fabs(a),std::fabs(b)});}
// Match the selected transport and fade. Identity and the actual native slot
// are checked by the caller; source classification grants no RGB/shadow link.
template<class Source> inline float ownershipGain(const NorthlightLocalLights::Light& selected,const Source& point,const float* resolved=nullptr){
    if(selected.kind!=2||(selected.flags&NorthlightLocalLights::Flash))return 0;
    NorthlightLocalLights::Light raw=selected;std::copy_n(point.position,3,raw.position);std::copy_n(point.color,3,raw.diffuse);
    const auto full=NorthlightLocalLights::nativeTransport(raw,point.attenuation);
    if(!NorthlightLocalLights::valid(selected)||!NorthlightLocalLights::valid(full))return 0;
    for(unsigned k=0;k<3;++k){const float position=resolved?resolved[k]:full.position[k];if(!std::isfinite(position)||std::fabs(selected.position[k]-position)>.005f)return 0;}
    if(!close(selected.attenuationStart,full.attenuationStart,.0001f)||!close(selected.attenuationEnd,full.attenuationEnd,.0001f))return 0;
    const unsigned peak=unsigned(std::max_element(full.diffuse,full.diffuse+3)-full.diffuse);
    const float gain=selected.diffuse[peak]/full.diffuse[peak];if(!std::isfinite(gain)||gain<=0||gain>1.0001f)return 0;
    for(unsigned k=0;k<3;++k)if(!close(selected.diffuse[k],full.diffuse[k]*gain,.0001f))return 0;
    return std::clamp(gain,0.f,1.f);
}
// Draw-derived sources already feed the renderer's selected local-light pass.
// They can hand off RGB without a scene identity; this grants no shadow intent.
inline float observedOwnershipGain(const NorthlightLocalLights::Light& selected,const NorthlightEquipmentLights::Tracker::Entry& entry,std::uint32_t now,const NorthlightNativeLights::Snapshot& scene){
    if((selected.sourceId>>52)!=0xfec||selected.sourceId!=(0xfec0000000000000ull|entry.id)||std::uint32_t(now-entry.seen)>=NorthlightEquipmentLights::FreshMilliseconds)return 0;
    // A current value for THIS identity supersedes a draw observation. An
    // unrelated source in a partial inventory cannot revoke RGB ownership.
    if(entry.nativeId&&scene.valid&&NorthlightNativeLights::wellFormed(scene))
        for(unsigned i=0;i<scene.count;++i)if(scene.lights[i].id==entry.nativeId)return 0;
    float resolved[3];for(unsigned k=0;k<3;++k)resolved[k]=entry.source.position[k]+entry.emitterOffset[k];
    return ownershipGain(selected,entry.source,resolved);
}
inline bool same(const NorthlightEquipmentLights::Native& a,const NorthlightNativeLights::Point& b){
    for(unsigned k=0;k<3;++k)if(std::fabs(a.position[k]-b.position[k])>.005f||!close(a.color[k],b.color[k],.0001f)||!close(a.attenuation[k],b.attenuation[k],.0001f))return false;
    return true;
}
inline bool authoredSlot(const NorthlightLocalLights::Light& light,const NorthlightEquipmentLights::Native& point){
    if(light.kind!=2||(light.flags&NorthlightLocalLights::Flash))return false;
    for(unsigned k=0;k<3;++k)if(!std::isfinite(light.position[k])||!std::isfinite(point.position[k])||std::fabs(light.position[k]-point.position[k])>.005f||!std::isfinite(point.color[k])||point.color[k]<0||!std::isfinite(point.attenuation[k])||point.attenuation[k]<0)return false;
    NorthlightLocalLights::Light native=light;std::copy_n(point.position,3,native.position);std::copy_n(point.color,3,native.diffuse);
    native=NorthlightLocalLights::nativeTransport(native,point.attenuation);
    for(unsigned k=0;k<3;++k)if(!close(light.diffuse[k],native.diffuse[k],.0001f))return false;
    return close(light.attenuationStart,native.attenuationStart,.0001f)&&close(light.attenuationEnd,native.attenuationEnd,.0001f);
}
inline bool sameAuthored(const NorthlightLocalLights::Light& a,const NorthlightLocalLights::Light& b){
    if(a.kind!=2||b.kind!=2||(a.flags&NorthlightLocalLights::Flash)||(b.flags&NorthlightLocalLights::Flash))return false;
    for(unsigned k=0;k<3;++k)if(!std::isfinite(a.position[k])||!std::isfinite(b.position[k])||std::fabs(a.position[k]-b.position[k])>.005f||!close(a.diffuse[k],b.diffuse[k],.0001f))return false;
    return close(a.attenuationStart,b.attenuationStart,.0001f)&&close(a.attenuationEnd,b.attenuationEnd,.0001f);
}
template<class Binding> inline void ownExtraBank(bool terrain,float* bank,const float* inverseView,const float* camera,const Binding* bindings,unsigned bindingCount){
    if(!bank||!inverseView||!camera||(!bindings&&bindingCount))return;
        for(unsigned slot=0;slot<(terrain?5u:4u);++slot){
            NorthlightEquipmentLights::Native value;bool valid=true;
            if(terrain){for(unsigned k=0;k<3;++k){value.position[k]=inverseView[12+k]+bank[12*slot+k];value.color[k]=bank[12*slot+4+k];value.attenuation[k]=bank[12*slot+8+k];
                    valid=valid&&std::isfinite(value.position[k])&&std::isfinite(value.color[k])&&value.color[k]>=0&&std::isfinite(value.attenuation[k])&&value.attenuation[k]>=0;}}
            else valid=NorthlightEquipmentLights::Detail::decodeSlot(bank,slot,inverseView,camera,value);
            if(!valid)continue;int match=-1;
            for(unsigned i=0;i<bindingCount;++i){const auto& binding=bindings[i];
                if(!(binding.ownerGain>0&&binding.ownerGain<=1))continue;
                if(binding.authored?!NorthlightNativeDirect::authoredSlot(*binding.authored,value):!binding.native||!NorthlightNativeDirect::same(value,*binding.native))continue;
                if(match>=0){match=-2;break;}match=int(i);}
            if(match>=0)for(unsigned k=0;k<3;++k)bank[(terrain?12*slot+4:4*slot)+k]*=1-bindings[unsigned(match)].ownerGain;
        }
}
inline bool uniqueAuthoredSlot(const NorthlightLocalLights::Light& light,const NorthlightEquipmentLights::Native* values,const bool* valid,unsigned count,unsigned slot){
    if(count>4||slot>=count||!valid[slot]||!authoredSlot(light,values[slot]))return false;
    for(unsigned i=0;i<count;++i)if(i!=slot&&valid[i]&&authoredSlot(light,values[i]))return false;
    return true;
}
// Pure counterpart of the receiver's slot-by-slot binding search. Indices
// keep ambiguity distinct from absence without retaining external pointers.
template<class Binding> inline std::array<int,4> matchDecoded(const NorthlightEquipmentLights::Native* values,const bool* valid,unsigned count,const Binding* bindings,unsigned bindingCount){
    std::array<int,4> result{{-1,-1,-1,-1}};
    if(!values||!valid||count>4||(!bindings&&bindingCount))return result;
    for(unsigned slot=0;slot<count;++slot)if(valid[slot])for(unsigned i=0;i<bindingCount;++i){const auto& candidate=bindings[i];
        if(candidate.authored?!uniqueAuthoredSlot(*candidate.authored,values,valid,count,slot):!candidate.native||!same(values[slot],*candidate.native))continue;
        if(result[slot]>=0){result[slot]=-2;break;}result[slot]=int(i);
    }
    return result;
}
struct BankMatch {
    NorthlightEquipmentLights::Native values[4]{};bool valid[4]{};
    std::array<int,4> bindings{{-1,-1,-1,-1}};
};
template<class Binding> inline unsigned ownedBank(const float* bank,unsigned count,const BankMatch& match,const Binding* bindings,float* rgb){
    std::copy_n(bank,16,rgb);unsigned mask=0;
    for(unsigned slot=0;slot<std::min(count,4u);++slot)if(match.valid[slot]&&match.bindings[slot]>=0){
        const float gain=bindings[unsigned(match.bindings[slot])].ownerGain;
        if(!std::isfinite(gain)||gain<=0||gain>1)continue;
        for(unsigned k=0;k<3;++k)rgb[4*slot+k]*=1-gain;
        mask|=1u<<slot;
    }
    return mask;
}
// One adjacent bank within the owner's binding generation. The owner resets
// before rebuilding/mutating bindings; live cube and fade checks stay outside.
class BankMemo {
    BankMatch result_;
    float bank_[44]{},view_[16]{},camera_[3]{};
    unsigned count_=0,bindingCount_=0;std::uintptr_t bindingIdentity_=0;bool cached_=false;
public:
    void reset(){cached_=false;}
    template<class Binding> const BankMatch& resolve(const float* bank,unsigned count,const float* view,const float* camera,const Binding* bindings,unsigned bindingCount,bool& hit){
        const auto identity=reinterpret_cast<std::uintptr_t>(bindings);
        hit=cached_&&bank&&view&&camera&&count==count_&&bindingCount==bindingCount_&&identity==bindingIdentity_&&
            !NorthlightMem::compare(camera_,camera,sizeof camera_)&&!NorthlightMem::compare(view_,view,sizeof view_)&&!NorthlightMem::compare(bank_,bank,sizeof bank_);
        if(hit)return result_;
        cached_=false;result_={};
        NorthlightEquipmentLights::decodeBank(bank,count,view,camera,result_.values,result_.valid);
        result_.bindings=matchDecoded(result_.values,result_.valid,count,bindings,bindingCount);
        if(bank&&view&&camera&&count<=4&&(bindings||!bindingCount)){
            std::memcpy(bank_,bank,sizeof bank_);std::memcpy(view_,view,sizeof view_);std::memcpy(camera_,camera,sizeof camera_);
            count_=count;bindingCount_=bindingCount;bindingIdentity_=identity;cached_=true;
        }
        return result_;
    }
};
inline const NorthlightNativeLights::Point* source(const NorthlightLocalLights::Light& light,const NorthlightNativeLights::Snapshot& scene){
    if(light.kind!=2||(light.flags&NorthlightLocalLights::Flash)||!scene.valid||scene.count>NorthlightNativeLights::Capacity)return nullptr;
    const NorthlightNativeLights::Point* match=nullptr;
    for(unsigned i=0;i<scene.count;++i){const auto& point=scene.lights[i];
        NorthlightEquipmentLights::Native value;std::copy_n(point.position,3,value.position);std::copy_n(point.color,3,value.color);std::copy_n(point.attenuation,3,value.attenuation);
        if(authoredSlot(light,value)){if(match)return nullptr;match=&point;}
    }
    return match;
}
inline const NorthlightNativeLights::Point* slot(const NorthlightEquipmentLights::Native& value,const NorthlightNativeLights::Snapshot& scene){
    const NorthlightNativeLights::Point* match=nullptr;if(!scene.valid||scene.count>NorthlightNativeLights::Capacity)return nullptr;
    for(unsigned i=0;i<scene.count;++i)if(same(value,scene.lights[i])){if(match)return nullptr;match=&scene.lights[i];}
    return match;
}
} // namespace NorthlightNativeDirect
