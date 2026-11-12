#pragma once
#include "equipment_lights.h"
#include <cstring>
#include "northlight_mem.h"

// Authored lights can prove presence in the receiver's actual slots even when
// the scene inventory is incomplete. Ambiguous sources keep the original draw.
namespace NorthlightNativeDirect {
inline bool close(float a,float b,float tolerance){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=tolerance*std::max({1.f,std::fabs(a),std::fabs(b)});}
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
