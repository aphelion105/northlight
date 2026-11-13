#include "world_environment.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

int main(){
    std::array<unsigned char,0x388> bytes{};
    auto scalar=[&](size_t at,float value){std::memcpy(bytes.data()+at,&value,4);};
    auto packed=[&](size_t at,uint32_t value){std::memcpy(bytes.data()+at,&value,4);};
    scalar(4,.5f);scalar(0x19c,-.6f);scalar(0x1a4,-.8f);
    packed(0x1a8,0xff996633);packed(0x1ac,0xff202030);
    NorthlightCelestial::Snapshot sky;sky.dayFraction=.5f;
    for(unsigned i=0;i<3;++i){auto& body=sky.bodies[i];body.center[0]=7.2f;body.center[2]=i?-9.6f:9.6f;body.color=0xffffffff;body.texture=1;body.size=1;}
    std::memcpy(bytes.data()+0x328,sky.bodies,sizeof sky.bodies);
    unsigned copies=0;bool torn=false,unreadable=false;
    auto read=[&](uintptr_t at,void* out,size_t size){
        assert(at==0xd38b00&&size==bytes.size());if(unreadable)return false;
        std::memcpy(out,bytes.data(),size);if(torn&&++copies%2==0)static_cast<unsigned char*>(out)[4]^=1;return true;
    };
    NorthlightWorldEnvironment environment;NorthlightWmoContext::Lighting light;
    uint32_t now=1000;float eye[3]={};assert(environment.read(read,"Azeroth",eye,light,now)&&environment.hasCelestial);
    const auto sun=environment.celestial.sun;
    // The sky origin stops at the door; the camera walks 600 units inside.
    // Current source colours/time are still sampled, never a frozen light hold.
    for(unsigned frame=1;frame<=600;++frame){
        now+=16;eye[0]=float(frame);scalar(4,.5f+frame*.000001f);packed(0x1a8,frame&1?0xff336699:0xff996633);
        assert(environment.read(read,"Azeroth",eye,light,now)&&environment.hasCelestial);
        assert(light.dayFraction==.5f+frame*.000001f);
        assert(light.packedDirect==(frame&1?0xff336699u:0xff996633u));
        for(unsigned i=0;i<3;++i)assert(environment.celestial.sun.direction[i]==sun.direction[i]);
    }
    // Strict general-purpose decoder keeps rejecting unrelated old cameras.
    NorthlightWmoContext::Lighting strict;assert(!NorthlightWmoContext::decodeGlobalLighting(bytes.data(),bytes.size(),eye,strict));
    now+=NorthlightWorldEnvironment::MaxGapMs+1;assert(!environment.read(read,"Azeroth",eye,light,now));
    eye[0]=0;assert(environment.read(read,"Azeroth",eye,light,now));
    const auto saved=light;torn=true;assert(!environment.read(read,"Azeroth",eye,light,now)&&!environment.current);assert(light.packedDirect==saved.packedDirect);torn=false;
    unreadable=true;assert(!environment.read(read,"Azeroth",eye,light,now));unreadable=false;
    assert(environment.read(read,"Azeroth",eye,light,now));
    // A same-map teleport, map switch and invalid camera revoke source proof.
    eye[0]+=100;assert(!environment.read(read,"Azeroth",eye,light,now)&&!environment.proven);
    eye[0]=0;assert(environment.read(read,"Azeroth",eye,light,now));
    eye[0]=50;assert(!environment.read(read,"Kalimdor",eye,light,now));
    assert(!environment.read(read,"Azeroth",eye,light,now));
    eye[0]=0;assert(environment.read(read,"Azeroth",eye,light,now));
    eye[0]=std::numeric_limits<float>::quiet_NaN();assert(!environment.read(read,"Azeroth",eye,light,now));
    eye[0]=60;assert(!environment.read(read,"Azeroth",eye,light,now));
    eye[0]=0;assert(environment.read(read,"Azeroth",eye,light,now));
    // Bad celestial artwork cannot invalidate an otherwise sound environment;
    // it also cannot manufacture a directional source.
    packed(0x328+16,0);assert(environment.read(read,"Azeroth",eye,light,now)&&!environment.hasCelestial);
    scalar(0x19c,std::numeric_limits<float>::quiet_NaN());assert(!environment.read(read,"Azeroth",eye,light,now));
    std::puts("WORLD_ENVIRONMENT_PASS indoorWalk=600 liveColours=1 skyOriginIndependent=1 strictDecoderRetained=1 teleportMapTornInvalidRejected=1");
}
