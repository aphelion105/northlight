#pragma once
#include "tideglass_x64_inputs.h"
#include "world_context_x64.h"

namespace NorthlightTideglassX64 {
using NorthlightX64World::CodeProof;
using NorthlightX64World::read;
using NorthlightX64World::verifyCode;

// WowClient64.exe layout witnessed by World::SceneCamLiquidStatus, the WXL
// WaterHost type lookup, MovementMoveTransports and ISetTexture.
inline constexpr uintptr_t CameraType=0x1b746d8, CameraDepth=0x1b6a840;
inline constexpr uintptr_t LiquidTypes=0x193b750, LiquidSettings=0x1bd9da8;
inline constexpr uintptr_t TransportList=0x1950ef0, ShipVtable=0xadf1b8;

struct Rows {
    uintptr_t records[512]={};
    unsigned minimum=0,count=0;
    WaterHostLiquidKind kind(unsigned id) const {
        if(id<minimum||id-minimum>=count)return WaterHostLiquidUnknown;
        const auto row=records[id-minimum];
        struct {unsigned id;std::byte gap[16];unsigned soundBank;} data{};
        if(!row||!read(row,data)||data.id!=id)return WaterHostLiquidUnknown;
        return liquidKind(data.soundBank);
    }
};

inline Rows liquidTypes(uintptr_t base){
    Rows result;
    struct {unsigned max,min;std::byte gap[32];uintptr_t rows;} table{};
    if(!read(base+LiquidTypes,table)||table.max<table.min||table.max-table.min>=std::size(result.records))return result;
    const unsigned count=table.max-table.min+1;
    if(table.rows&&read(table.rows,result.records,count*sizeof(uintptr_t))){result.minimum=table.min;result.count=count;}
    return result;
}

inline void readShips(uintptr_t base,Inputs& out){
    // TSExplicitList stores its member offset, then previous/next for the
    // sentinel. A set low bit on next marks the end of the list.
    struct {uintptr_t linkOffset,previous,next;} list{};
    if(!read(base+TransportList,list)||list.linkOffset<8||list.linkOffset>0x1000||list.linkOffset%8)return;
    auto object=list.next;
    for(unsigned scanned=0;scanned<128&&object>=0x10000&&!(object&7)&&out.shipCount<out.ships.size();++scanned){
        if(object>UINTPTR_MAX-0x1008)break;
        uintptr_t typeData=0,vtable=0;
        if(read(object+0x2e0,typeData)&&typeData&&typeData<=UINTPTR_MAX-0x48&&read(typeData,vtable)&&vtable==base+ShipVtable){
            struct {float position[3],speed;} state{};
            if(read(typeData+0x38,state)&&std::isfinite(state.speed)&&state.speed>=0&&state.speed<=200)
                out.ships[out.shipCount++]={object,{state.position[0],state.position[1],state.position[2]},state.speed};
        }
        uintptr_t next=0;
        if(!read(object+list.linkOffset+8,next)||next==object)break;
        object=next;
    }
}

inline uintptr_t textureIdentity(uintptr_t texture){
    if(!texture||texture>UINTPTR_MAX-0xa8)return 0;
    unsigned flags=0;uintptr_t gx=0;
    if(!read(texture+0x48,flags))return 0;
    if(flags&4){
        uintptr_t atlas=0;
        if(!read(texture+0xa0,atlas)||!atlas||atlas>UINTPTR_MAX-0x20||!read(atlas+0x18,gx))return 0;
    }else if(!read(texture+0x80,gx))return 0;
    uintptr_t exposed=0;
    return gx&&gx<=UINTPTR_MAX-0x48&&read(gx+0x40,exposed)?exposed:0;
}

template<class ToRaw> void readMaterials(uintptr_t base,const Rows& rows,Inputs& out,ToRaw toRaw){
    struct {unsigned capacity,count;uintptr_t entries;} bank{};
    uintptr_t types[512]={};
    if(!read(base+LiquidSettings,bank)||bank.count>std::size(types)||bank.count>bank.capacity||!bank.entries||
       !read(bank.entries,types,bank.count*sizeof(uintptr_t)))return;
    for(unsigned id=0;id<bank.count;++id){
        if(!types[id]||types[id]>UINTPTR_MAX-0x428)continue;
        const auto kind=rows.kind(id);
        if(kind!=WaterHostLiquidMagma&&kind!=WaterHostLiquidSlime)continue;
        for(unsigned slot:{0x380u,0x410u}){
            decltype(bank) list{};
            uintptr_t textures[64]={};
            if(!read(types[id]+slot,list)||!list.count||list.count>list.capacity||list.count>std::size(textures)||
               !list.entries||!read(list.entries,textures,list.count*sizeof(uintptr_t)))continue;
            for(unsigned i=0;i<list.count;++i){
                const auto texture=toRaw(textureIdentity(textures[i]));
                if(texture){
                    if(out.materialCount==out.materials.size()){out.materialCount=0;return;}
                    out.materials[out.materialCount++]={texture,kind,kind==WaterHostLiquidMagma&&id==15?-2:-1};
                }
            }
        }
    }
}

template<class ToRaw> void capture(Inputs& out,ToRaw toRaw){
    static const auto base=NorthlightX64World::imageBase();
    static const bool liquid=verifyCode(base, std::array<CodeProof,4>{{
        {0x6b4290,24,UINT64_C(0xdd78121cbd0d23ff)}, {0x21e8a0,29,UINT64_C(0x8bd45f292248e2b1)},
        {0x21e6ae,18,UINT64_C(0x6baee63f673df882)}, {0x6c304b,12,UINT64_C(0x76e05ea6021c5cdf)}}});
    static const bool ships=verifyCode(base, std::array<CodeProof,6>{{
        {0x438e93,18,UINT64_C(0x28a80ee0934573c9)}, {0x438eb0,36,UINT64_C(0xe932e51cc0a4c029)},
        {0x4390f0,16,UINT64_C(0xe9ef128da580c639)}, {0x438a00,19,UINT64_C(0x5ebd42d72515b0cc)},
        {0x416890,19,UINT64_C(0xc6abf7a83245bc38)}, {0x416960,6,UINT64_C(0x0a3ab67fe29aa361)}}});
    static const bool materials=verifyCode(base, std::array<CodeProof,5>{{
        {0x21e5d6,23,UINT64_C(0x7d7befe98a6e488c)}, {0x73294c,39,UINT64_C(0xae0fa628af236e26)},
        {0x24a64f,38,UINT64_C(0x15192fd2e3dd7d6d)}, {0x24a6ba,7,UINT64_C(0x110822045d7bdc05)},
        {0x118edc,14,UINT64_C(0xb79a3daf162dd6a1)}}});
    unsigned loaded=0;
    if(!base||!read(base+NorthlightX64World::MapLoaded,loaded)||loaded!=1)return;
    if(liquid){
        const auto rows=liquidTypes(base);
        if(read(base+CameraType,out.cameraId)&&out.cameraId&&read(base+CameraDepth,out.depth)&&
           std::isfinite(out.depth)&&out.depth>0&&out.depth<=1024.f)out.camera=rows.kind(out.cameraId);
        if(materials)readMaterials(base,rows,out,toRaw);
    }
    if(ships)readShips(base,out);
}
}
