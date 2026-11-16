#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Values owned by one menu draw domain. Addresses are identity keys only after
// capture; the replay thread never follows a menu model or scene pointer.
namespace NorthlightMenu {
struct Snapshot {
    std::uint32_t scene=0,model=0,data=0,created=0,time=0,valid=0;
    char name[128]{};
    float view[16]{},inverseView[16]{};
    void clear(){*this={};}
    void identity(char (&out)[64])const{
        std::snprintf(out,sizeof out,"Menu:%08x:%08x:%08x:%08x",scene,model,data,created);
    }
};
static_assert(sizeof(Snapshot)==280,"menu snapshot archive layout");
inline bool pointer(std::uint32_t p){return p>=0x10000&&p<0xffff0000&&!(p&3);}
inline bool backdrop(const char* name){return (name[0]=='U'||name[0]=='u')&&(name[1]=='I'||name[1]=='i')&&name[2]=='_';}
inline bool wellFormed(const Snapshot& s){
    if(s.valid>1)return false;
    if(!s.valid)return !s.scene&&!s.model&&!s.data;
    if(!pointer(s.scene)||!pointer(s.model)||!pointer(s.data)||!std::memchr(s.name,0,sizeof s.name)||!backdrop(s.name))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(s.view[i])||!std::isfinite(s.inverseView[i]))return false;
    for(unsigned i=0;i<4;++i){
        if(std::fabs(s.view[i*4+3]-(i==3?1.f:0.f))>.001f||std::fabs(s.inverseView[i*4+3]-(i==3?1.f:0.f))>.001f)return false;
        for(unsigned j=0;j<4;++j){float product=0;for(unsigned k=0;k<4;++k)product+=s.view[i*4+k]*s.inverseView[k*4+j];
            if(std::fabs(product-(i==j?1.f:0.f))>.01f)return false;}
        if(i<3){float length=0;for(unsigned j=0;j<3;++j)length+=s.view[i*4+j]*s.view[i*4+j];if(std::fabs(length-1.f)>.004f)return false;}
    }
    for(unsigned i=12;i<15;++i)if(std::fabs(s.inverseView[i])>100000)return false;
    return true;
}
// Filled by the audited SetupLighting thunk. A sequence protects readers from
// observing the scene/model pair halfway through publication.
struct Observation {std::uint32_t sequence=0,scene=0,model=0;};
inline Observation observation;
template<class Read> bool capture(Read read,Snapshot& out,unsigned* diagnostic=nullptr){
    if(diagnostic)*diagnostic=0;
    const auto reject=[&](unsigned reason){if(diagnostic)*diagnostic=reason;return false;};
    out.clear();Observation first{},last{};Snapshot next;std::uint32_t world=0,parent=0,flags=0,shared=0,name[2]{},device=0,type=0,slot=0;
    const auto address=reinterpret_cast<std::uintptr_t>(&observation);
    if(!read(address,&first,sizeof first)||(first.sequence&1)||!pointer(first.scene)||!pointer(first.model)||
       !read(0xcd754c,&world,4)||first.scene==world)return reject(1);
    next.scene=first.scene;next.model=first.model;
    if(!read(next.model+0x28,&parent,4)||parent!=next.scene||!read(next.model+0x10,&flags,4)||!(flags&1)||
       !read(next.model+0x2c,&shared,4)||!pointer(shared)||!read(shared+0x150,&next.data,4)||!pointer(next.data))return reject(2);
    if(!read(next.model+0x74,&next.created,4)||!read(next.data+8,name,sizeof name)||name[0]<4||name[0]>sizeof next.name||!pointer(name[1])||
       !read(name[1],next.name,name[0])||next.name[name[0]-1]||!backdrop(next.name))return reject(3);
    if(!read(next.scene+12,&next.time,4)||!read(next.scene+0x84,next.view,64)||!read(next.scene+0xc4,next.inverseView,64))return reject(4);
    // M2 Update (0x821a4d) stores the camera-relative view and its inverse here.
    // M2 Render (0x8236c1..0x823743) then sets the Gx view to identity: its
    // vertices/bones already contain that view. Comparing these two matrices
    // rejects every non-identity menu camera. The caller also proves the full
    // backbuffer/model draw domain; the observer proves the loaded UI scene.
    float view[16]{};
    if(!read(0xc5df88,&device,4)||!pointer(device)||!read(device,&type,4)||type!=0xa2e718)return reject(5);
    if(!read(device+0x1af8,&slot,4)||slot>=4||!read(device+0x1b00+64*slot,view,sizeof view))return reject(6);
    for(unsigned i=0;i<16;++i)if(!std::isfinite(view[i])||std::fabs(view[i]-(i%5==0?1.f:0.f))>.002f)return reject(7);
    if(!read(address,&last,sizeof last)||std::memcmp(&first,&last,sizeof first))return reject(8);
    next.valid=1;if(!wellFormed(next))return reject(9);out=next;return true;
}
}
