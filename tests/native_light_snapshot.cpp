#include "equipment_lights.h"
#include "particle_lights.h"
#include "snapshot_archive.h"
#include "native_light_bridge.h"
#include <cassert>
#include <fstream>
#include <limits>
#include <memory>

using namespace NorthlightNativeLights;
using namespace NorthlightStream;
namespace A=NorthlightStream::SnapshotArchive;
static Snapshot scene(){
    Snapshot s;s.valid=1;s.scene=0x1234000;s.time=1000;s.count=1;std::strcpy(s.map,"Northrend");
    auto& p=s.lights[0];p.id=0xfea0000000000001ull;p.position[0]=4;p.position[2]=2;
    p.color[0]=.7f;p.color[1]=.4f;p.color[2]=.1f;p.attenuation[1]=.7f;p.attenuation[2]=.03f;return s;
}
static void publication(){
    auto s=scene();float camera[3]{};NorthlightEquipmentLights::Tracker native;
    NorthlightParticleLights::Frame particles;NorthlightLocalLightSelection::Tracker selected;
    // No observe(draw) call, even on discovery. Keep sampling longer than the
    // old native source's lifetime; camera orientation is deliberately absent.
    for(unsigned now=1000;now<=6000;now+=50){
        native.observeScene(s,now,{},camera);native.publish(now);
        particles.publish(native.output.data(),native.outputCount,{},camera,selected);
        const auto result=selected.update({},camera,32,.05f,false,nullptr,particles.output.data(),particles.outputCount);
        if(now>=1100){assert(result.count==1);assert(result.color[0][0]>.6f);}
    }
    const auto identity=native.output[0].sourceId;
    // Large placement and hue changes retain identity; a draw reporting the
    // new hue early cannot create a second source at the same place.
    s.lights[0].position[0]=12;s.lights[0].color[0]=.05f;s.lights[0].color[1]=.8f;
    NorthlightEquipmentLights::Native drawn;std::copy_n(s.lights[0].position,3,drawn.position);
    std::copy_n(s.lights[0].color,3,drawn.color);std::copy_n(s.lights[0].attenuation,3,drawn.attenuation);
    native.observe(drawn,6050,{},camera);native.observeScene(s,6050,{},camera);native.publish(6050);
    assert(native.outputCount==1&&native.output[0].sourceId==identity&&native.output[0].position[0]==12);
    // Authored disable and detachment expire the source in this publication,
    // including the shared selector's old fade tail.
    unsigned now=6100;
    for(bool removed:{false,true}){
        auto dark=s;if(removed)dark.count=0;else std::fill_n(dark.lights[0].color,3,0.f);
        native.observeScene(dark,now,{},camera);native.publish(now);
        assert(native.outputCount==0&&native.suppressedCount==1);
        auto ids=native.suppressed;std::sort(ids.begin(),ids.begin()+native.suppressedCount);
        selected.forgetSorted(ids.data(),native.suppressedCount);
        particles.publish(nullptr,0,{},camera,selected);
        assert(selected.update({},camera,32,.016f,true,nullptr,particles.output.data(),particles.outputCount).count==0);
        native.observeScene(s,now+100,{},camera);native.publish(now+100);native.observeScene(s,now+200,{},camera);native.publish(now+200);now+=300;
        assert(native.outputCount==1);
    }
    auto broken=s;broken.count=129;assert(!wellFormed(broken));broken=s;broken.lights[0].color[0]=std::numeric_limits<float>::quiet_NaN();assert(!wellFormed(broken));
    std::puts("NATIVE_LIGHT_PUBLICATION_PASS neverDrawn=1 longOffscreen=1 movement=1 hue=1 disable=1 detach=1");
}
int main(int argc,char** argv){
    assert(argc==2);publication();auto source=std::make_unique<GameSnapshot>(),target=std::make_unique<GameSnapshot>();
    unsigned value=42;assert(source->record(0x123456,&value,4,true));source->triggerKind=2;source->triggerDraw=123;
    source->nativeLights=scene();std::vector<unsigned char> bytes(A::MaxBytes);std::size_t written=0;A::Error error;
    assert(A::encode(*source,bytes.data(),bytes.size(),written,error));bytes.resize(written);assert(A::u32(bytes.data()+8)==2);
    assert(A::decode(bytes.data(),bytes.size(),*target,error));
    assert(target->nativeLights.count==1&&target->nativeLights.lights[0].id==source->nativeLights.lights[0].id);
    source->nativeLights.lights[0].position[0]=999;assert(target->nativeLights.lights[0].position[0]==4);
    {ScopedPlayback play(*target);assert(activeSnapshot->nativeLights.lights[0].color[0]==.7f);
#ifdef _WIN32
        Snapshot output;float camera[3]{};const auto reads=NorthlightWorldContext::SelfReadStats::calls.load();
        frame("Northrend",camera,output);assert(output.valid&&output.lights[0].position[0]==4);
        frame("Azeroth",camera,output);assert(!output.valid);
        assert(NorthlightWorldContext::SelfReadStats::calls.load()==reads);
#endif
    }
    std::ofstream file(argv[1],std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();assert(file.good());
    // Corrupt count/map/value with a valid checksum: strict import rejects it.
    const auto extension=A::HeaderBytes+A::RecordBytes+4;
    for(auto change:std::array<std::pair<std::size_t,std::uint32_t>,3>{{{extension+8,129},{extension+12,2},{extension+80+8,0x7fc00000}}}){
        auto bad=bytes;A::put32(bad.data()+change.first,change.second);A::put32(bad.data()+bad.size()-4,A::checksum(bad.data(),bad.size()-4));
        assert(!A::decode(bad.data(),bad.size(),*target,error)&&!target->nativeLights.valid);
    }
    target->clear();assert(!target->nativeLights.valid&&!target->nativeLights.count);
    source->nativeLights.clear();bytes.resize(A::MaxBytes);assert(A::encode(*source,bytes.data(),bytes.size(),written,error));
    assert(A::u32(bytes.data()+8)==1&&A::decode(bytes.data(),written,*target,error)&&!target->nativeLights.valid);
    std::puts("NATIVE_LIGHT_SNAPSHOT_PASS ownedValues=1 v1Compatible=1 v2RoundTrip=1 invalidRejected=1");
}
