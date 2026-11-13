#include "game_snapshot.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
using namespace NorthlightStream;
using namespace NorthlightWorldDrawDomain;

struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    unsigned reads=0;
    template<class T> void put(std::uintptr_t address,const T& value){
        const auto* p=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof value;++i)bytes[address+i]=p[i];
    }
    bool read(std::uintptr_t address,void* output,std::size_t size){
        ++reads;
        for(std::size_t i=0;i<size;++i)if(!bytes.count(address+i))return false;
        for(std::size_t i=0;i<size;++i)static_cast<unsigned char*>(output)[i]=bytes.at(address+i);
        return true;
    }
};
int main(){
    constexpr unsigned tags=NorthlightShaderTags::kWorld|NorthlightShaderTags::kWmo;
    constexpr std::uint32_t frame=0x110000,cam=0x220000,dev=0x330000;
    Memory memory;
    memory.put(0xb7436c,frame);memory.put(frame+0x7e20,cam);memory.put(cam,std::uint32_t(0xa1ea54));
    memory.put(cam+0xa0,std::uint64_t(0));memory.put(0xc5df88,dev);memory.put(dev,std::uint32_t(0xa2e718));
    memory.put(dev+0x1af8,std::uint32_t(0));
    const float camera[12]={1550,-4325,24,0,0,1,1,0,0,0,1,0};memory.put(cam+8,camera);
    float view[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0}; // non-affine sky stack
    memory.put(dev+0x1b00,view);
    auto sky=std::make_unique<GameSnapshot>(),world=std::make_unique<GameSnapshot>();
    const CodeRange code;
    auto live=[&](std::uintptr_t a,void* o,std::size_t n){return memory.read(a,o,n);};
    auto record=[&](GameSnapshot& s){ScopedRecording scope(s);captureReaders([&](std::uintptr_t a,void* o,std::size_t n){return snapshotRead(s,a,o,n,live,code);});};
    TriggerPolicy policy;
    // The same coordinate-compatible VS can be used by the sky before WMO.
    assert(policy.onDraw(tags,mainWorldDraw(1,4,kSkyMinZ,1))==Trigger::FrameStart);
    record(*sky);assert(!policy.world);
    assert(policy.onDraw(tags,mainWorldDraw(1,4,0,1))==Trigger::None); // shadow / auxiliary target
    assert(policy.onDraw(tags,mainWorldDraw(1,4,kHorizonMinZ,kHorizonMaxZ))==Trigger::None);
    assert(policy.onDraw(tags,mainWorldDraw(0,4,0,kWorldMaxZ))==Trigger::None);
    assert(policy.onDraw(tags,mainWorldDraw(1,8,0,kWorldMaxZ))==Trigger::None);
    assert(!policy.world);
    view[15]=1;memory.put(dev+0x1b00,view);
    assert(policy.onDraw(tags,mainWorldDraw(1,4,0,kWorldMaxZ))==Trigger::World);
    record(*world);assert(policy.world);
    assert(policy.onDraw(tags,true)==Trigger::None);
    assert(policy.onDraw(NorthlightShaderTags::kUi,false)==Trigger::Ui);
    assert(policy.onDraw(NorthlightShaderTags::kUi,false)==Trigger::None);
    // Replay after the producer has moved on must use the world snapshot,
    // never the sky stack and never a late live-memory fallback.
    memory.bytes.clear();memory.reads=0;
    for(auto* s:{sky.get(),world.get()}){
        ScopedPlayback scope(*s);NorthlightWorldCamera::Camera result;NorthlightWorldCamera::Diagnostics why;
        const bool accepted=NorthlightWorldCamera::readCurrent([&](std::uintptr_t a,void* o,std::size_t n){return snapshotRead(*s,a,o,n,live,code);},1,result,&why);
        assert(accepted==(s==world.get()));
        if(accepted){assert(result.camera[0]==1550&&result.camera[1]==-4325&&result.camera[2]==24);}
        else assert(why.reason==NorthlightWorldCamera::Reject::Matrix);
        assert(!s->misses&&!memory.reads);
    }
    policy.onPresent();assert(policy.onDraw(tags,true)==Trigger::World);
    policy.onPresent();assert(policy.onDraw(NorthlightShaderTags::kUi,false)==Trigger::Ui);
    assert(policy.onDraw(tags,false)==Trigger::None&&!policy.world);
    assert(policy.onDraw(NorthlightShaderTags::kTerrain,true)==Trigger::World);
    std::puts("PASS world snapshot: sky/shadow/horizon/depth-off do not consume world; current camera survives asynchronous replay");
}
