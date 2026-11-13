#pragma once
#include "wmo_context.h"
#include "celestial_context.h"

// The sky's origin belongs to its source snapshot, not to the receiver camera.
// A continuous walk into a WMO may stop moving that origin. Only a source
// previously witnessed on this map may outlive the camera-distance check.
struct NorthlightWorldEnvironment {
    std::string map;
    float camera[3]={},origin[3]={};
    bool observed=false,proven=false,current=false,hasCelestial=false;
    uint32_t observedAt=0;static constexpr uint32_t MaxGapMs=500;
    NorthlightCelestial::Context celestial;
    static bool closeTo(const float* a,const float* b){
        double distance=0;
        for(unsigned i=0;i<3;++i){
            if(!std::isfinite(a[i])||!std::isfinite(b[i])||std::fabs(a[i])>100000||std::fabs(b[i])>100000)return false;
            const double delta=double(a[i])-b[i];distance+=delta*delta;
        }
        return distance<=double(NorthlightWmoContext::SkyCameraTolerance)*NorthlightWmoContext::SkyCameraTolerance;
    }
    template<class Read> bool read(Read reader,const char* name,const float* eye,NorthlightWmoContext::Lighting& lighting,uint32_t now){
        current=hasCelestial=false;
        if(!name||!*name||!eye||!closeTo(eye,eye)){observed=proven=false;return false;}
        if(!observed||map!=name||uint32_t(now-observedAt)>MaxGapMs||!closeTo(camera,eye))proven=false;
        map=name;std::copy_n(eye,3,camera);observed=true;observedAt=now;
        unsigned char first[0x388],second[sizeof first];
        if(!reader(0xd38b00,first,sizeof first)||!reader(0xd38b00,second,sizeof second)||std::memcmp(first,second,sizeof first))return false;
        NorthlightCelestial::Snapshot snapshot;NorthlightCelestial::unpackSnapshot(first,snapshot);
        NorthlightWmoContext::Lighting candidate;
        if(!NorthlightWmoContext::decodeGlobalLighting(first,sizeof first,snapshot.skyCamera,candidate))return false;
        if(!closeTo(eye,snapshot.skyCamera)&&!(proven&&closeTo(origin,snapshot.skyCamera)))return false;
        std::copy_n(snapshot.skyCamera,3,origin);proven=current=true;lighting=candidate;
        hasCelestial=NorthlightCelestial::decode(snapshot,snapshot.skyCamera,candidate.direct,celestial);
        return true;
    }
};
