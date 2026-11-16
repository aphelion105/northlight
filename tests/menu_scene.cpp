#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <fstream>
#include "menu_scene.h"
#include "menu_environment.h"
#include "snapshot_archive.h"
#include "world_draw_domain.h"

struct Memory {
    std::map<std::uintptr_t,std::vector<unsigned char>> bytes;
    template<class T> void put(std::uintptr_t address,const T& value){auto& b=bytes[address];b.resize(sizeof value);std::memcpy(b.data(),&value,sizeof value);}
    bool read(std::uintptr_t address,void* destination,std::size_t count){auto i=bytes.find(address);if(i==bytes.end()||i->second.size()!=count)return false;std::memcpy(destination,i->second.data(),count);return true;}
};
int main(int argc,char** argv){
    using namespace NorthlightMenu;
    Memory m;Observation seen{2,0x20000,0x30000};m.put(reinterpret_cast<std::uintptr_t>(&observation),seen);
    const std::uint32_t world=0x80000,parent=seen.scene,loaded=1,shared=0x40000,data=0x50000,created=123,time=678,device=0x70000,type=0xa2e718,slot=1;
    m.put(0xcd754c,world);m.put(seen.model+0x28,parent);m.put(seen.model+0x10,loaded);m.put(seen.model+0x2c,shared);m.put(shared+0x150,data);m.put(seen.model+0x74,created);m.put(seen.scene+12,time);
    const char name[]="UI_Scourge";std::uint32_t names[2]={sizeof name,0x60000};m.put(data+8,names);m.put(names[1],name);
    // The native M2 draw has an identity Gx view, while the scene retains the
    // rotated/translated camera used to prepare its view-space bone matrices.
    float view[16]={0,1,0,0,-1,0,0,0,0,0,1,0,-3,-2,-6,1},inverse[16]={0,-1,0,0,1,0,0,0,0,0,1,0,2,-3,6,1};
    const float deviceView[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    m.put(seen.scene+0x84,view);m.put(seen.scene+0xc4,inverse);m.put(0xc5df88,device);m.put(device,type);m.put(device+0x1af8,slot);m.put(device+0x1b00+slot*64,deviceView);
    Snapshot captured;auto reader=[&](std::uintptr_t a,void* d,std::size_t n){return m.read(a,d,n);};
    assert(capture(reader,captured)&&wellFormed(captured)&&captured.inverseView[14]==6);
    char identity[64],changed[64];captured.identity(identity);assert(!std::strncmp(identity,"Menu:",5));
    auto copy=captured;copy.created++;copy.identity(changed);assert(std::strcmp(identity,changed));
    for(const auto& entry:m.bytes){auto bad=m;bad.bytes.erase(entry.first);Snapshot result=captured;
        assert(!capture([&](auto a,void* d,auto n){return bad.read(a,d,n);},result)&&!result.valid);}
    for(unsigned reason=0;reason<8;++reason){auto bad=m;
        if(reason==0)bad.put(0xcd754c,seen.scene);
        if(reason==1){auto value=seen;value.sequence|=1;bad.put(reinterpret_cast<std::uintptr_t>(&observation),value);}
        if(reason==2)bad.put(seen.model+0x28,world);
        if(reason==3)bad.put(seen.model+0x10,std::uint32_t(0));
        if(reason==4)bad.put(device+0x1af8,std::uint32_t(4));
        if(reason==5)bad.put(device+0x1b00+slot*64,view); // Scene view has not been replaced for the native draw yet.
        if(reason==6){float other[16];std::copy_n(inverse,16,other);other[0]=2;bad.put(seen.scene+0xc4,other);}
        if(reason==7){char other[sizeof name]="UI_Scourge";other[0]='X';bad.put(names[1],other);}
        Snapshot result;assert(!capture([&](auto a,void* d,auto n){return bad.read(a,d,n);},result));
    }
    unsigned reads=0;Snapshot result;
    assert(!capture([&](std::uintptr_t a,void* d,std::size_t n){bool ok=m.read(a,d,n);if(a==reinterpret_cast<std::uintptr_t>(&observation)&&++reads==2)static_cast<Observation*>(d)->sequence+=2;return ok;},result));
    using namespace NorthlightStream;
    auto source=std::make_unique<GameSnapshot>(),decoded=std::make_unique<GameSnapshot>();
    source->record(0x123400,&world,4,true);source->triggerKind=4;source->menu=captured;
    source->nativeLights.valid=1;source->nativeLights.scene=captured.scene;std::strcpy(source->nativeLights.map,identity);source->nativeLights.count=1;
    auto& p=source->nativeLights.lights[0];p.id=1;p.flags=NorthlightNativeLights::RootAnchor|NorthlightNativeLights::MenuFixture;p.color[0]=1;p.attenuation[0]=1;
    std::vector<unsigned char> bytes(SnapshotArchive::MaxBytes);std::size_t size=0;SnapshotArchive::Error error;
    assert(SnapshotArchive::encode(*source,bytes.data(),bytes.size(),size,error));assert(SnapshotArchive::u32(bytes.data()+8)==5);
    assert(SnapshotArchive::decode(bytes.data(),size,*decoded,error));assert(!std::memcmp(&captured,&decoded->menu,sizeof captured));
    if(argc==2){std::ofstream file(argv[1],std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),size);assert(file.good());}
    source->nativeLights.clear();assert(SnapshotArchive::encode(*source,bytes.data(),bytes.size(),size,error));
    assert(SnapshotArchive::decode(bytes.data(),size,*decoded,error)&&decoded->menu.valid&&!decoded->nativeLights.valid);
    if(argc==2){std::ofstream file(std::string(argv[1])+".empty",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),size);assert(file.good());}
    source->menu.clear();assert(decoded->menu.valid&&!decoded->nativeLights.valid);
    decoded->clear();assert(!decoded->menu.valid&&!decoded->nativeLights.valid);
    const float ambient[3]={.1f,.2f,.3f},direct[3]={.6f,.5f,.4f};auto night=environment(captured,ambient,direct);
    assert(night.atmosphere.valid&&night.atmosphere.skyValid&&night.celestial.dayFraction<.1f);
    std::strcpy(copy.name,"UI_Dwarf");auto snow=environment(copy,ambient,direct);assert(snow.weather.kind==NorthlightWeather::Kind::Snow&&snow.weather.intensity>0);
    assert(night.weather.kind==NorthlightWeather::Kind::None&&night.atmosphere.ambient[1]==ambient[1]);
    assert(NorthlightWorldDrawDomain::menuModelDraw(1,4,0,1,true));
    assert(!NorthlightWorldDrawDomain::menuModelDraw(1,4,0,.94f,true));assert(!NorthlightWorldDrawDomain::menuModelDraw(1,4,0,1,false));
    std::puts("PASS menu identity, camera coherence, rejected memory, archive isolation and atmosphere");
}
