#pragma once
#include "world_batch_12340.h"

static void nativeWorldBatches(){
    using namespace NorthlightWorldBatch12340;
    constexpr unsigned frame=0x1000,gx=0x1100,submit=0x1200,top=0x1400;
    constexpr unsigned group=0x2000,record=0x3030,records=0x3000,groupSlot=0x4000,chunk=0x50000;
    constexpr unsigned device=0x6000,vb=0xa000,ib=0xb000;
    constexpr unsigned nearBatch=submit-0x78,depthBatch=submit-0x38;
    std::unordered_map<unsigned,std::uint32_t> words{
        {frame,gx},{frame+4,0x6a36d1},{gx,submit},{gx+4,0x7a9bb3},{gx-4,record},
        {submit+4,0x7aca21},{submit+8,group},{group+0xf8,records},{group+0x16c,3},
        {record+12,11},{nearBatch,3},{nearBatch+4,11},{nearBatch+8,39},
        {depthBatch,3},{depthBatch+4,11},{depthBatch+8,39},{0xc5df88,device},
        {device+0x28bc,ib},{device+0x224,0},{device+0x2870,vb},{ib+24,48},{vb+12,20},{vb+24,100}};
    std::unordered_map<unsigned,std::uint16_t> halves{
        {record+16,39},{record+18,2},{record+20,27},
        {nearBatch+12,2},{nearBatch+14,27},{depthBatch+12,2},{depthBatch+14,27}};
    const auto writeFloat=[&](unsigned where,float value){
        std::uint32_t bits;
        std::memcpy(&bits,&value,sizeof bits);
        words[where]=bits;
    };
    for(unsigned d=0;d<3;++d){
        writeFloat(group+0x34+4*d,-float(d+1));
        writeFloat(group+0x40+4*d,float(d+1)*4);
        writeFloat(chunk+0x18+4*d,680.5f+d);
    }
    const Range actual{5,2,26,35,13};
    const auto audit=[&](const Range& range,unsigned tags,bool bindings=true,bool validWmo=true,bool validAdt=true){
        return decode(frame,top,[&](unsigned at){return words.at(at);},[&](unsigned at){return halves.at(at);},
                      range,tags,bindings,validWmo,validAdt);
    };
    const unsigned wmoTag=NorthlightShaderTags::kWmo,adtTag=NorthlightShaderTags::kTerrain;
    auto result=audit(actual,wmoTag);
    assert(result.kind==Kind::Wmo&&result.owner==group&&result.record==record&&result.spatial);
    assert(result.minimum[1]==-2&&result.maximum[2]==12);
    assert(!audit(actual,wmoTag,false).owner);
    assert(audit(actual,wmoTag,true,false).kind==Kind::None);
    assert(audit(actual,adtTag).kind==Kind::None);
    assert(!audit({5,2,26,34,13},wmoTag).record);
    words[group+0x16c]=2;assert(!audit(actual,wmoTag).record);words[group+0x16c]=3;
    const unsigned sharedTailBatch=submit-0x58;
    words[sharedTailBatch]=3;words[sharedTailBatch+4]=11;words[sharedTailBatch+8]=36;
    halves[sharedTailBatch+12]=2;halves[sharedTailBatch+14]=27;
    words[nearBatch+8]=36;assert(!audit(actual,wmoTag).record);
    words[sharedTailBatch+8]=39;assert(audit(actual,wmoTag).record==record);
    words[nearBatch+8]=39;
    writeFloat(group+0x40,-2);assert(!audit(actual,wmoTag).spatial);writeFloat(group+0x40,4);
    words[gx+4]=0x7a96d4;assert(audit(actual,wmoTag).record==record);

    words[submit+4]=0x7bc368;words[submit+12]=groupSlot;words[groupSlot]=group;
    words[gx+4]=0x7ab9d5;words[gx-12]=record+16;
    words[submit-0x2c]=3;words[submit-0x28]=11;words[submit-0x24]=39;
    halves[submit-0x20]=2;halves[submit-0x1e]=27;
    assert(audit(actual,wmoTag).record==record);

    words[gx+4]=0x7aba91;words[gx-4]=group;
    words[group+0x19c]=11;words[group+0x1a0]=49;halves[group+0x1a4]=2;halves[group+0x1a6]=27;
    words[submit-0x3c]=3;words[submit-0x38]=11;words[submit-0x34]=39;
    halves[submit-0x30]=2;halves[submit-0x2e]=27;
    assert(audit(actual,wmoTag).record==group+0x19c);

    words[gx+4]=0x7d2f82;words[gx-4]=chunk;words[submit+4]=0x798aa4;
    words[chunk+0x90]=3;words[chunk+0x94]=11;words[chunk+0x98]=39;
    halves[chunk+0x9c]=2;halves[chunk+0x9e]=27;
    result=audit(actual,adtTag);
    assert(result.kind==Kind::Adt&&result.owner==chunk&&result.record==chunk+0x90&&result.spatial);
    assert(result.minimum[2]==682.5f);
    assert(audit(actual,adtTag,true,true,false).kind==Kind::None);
    constexpr unsigned first=0x51000,second=0x52000,heights=0x53001;
    words[chunk+0x10]=first;words[chunk+0x14]=second;
    for(unsigned part=0;part<2;++part){
        const unsigned source=part?second:first, data=heights+part*0x400;
        words[source+0x11c]=data+8;
        words[data]=0x4d435654;words[data+4]=145*sizeof(float);
        writeFloat(data+8,-2);writeFloat(data+8+144*4,5);
        for(unsigned axis=0;axis<3;++axis){
            const float origin=680.5f+axis+part;
            writeFloat(source+0x7c+axis*4,origin);
            writeFloat(source+0x4c+axis*4,origin-2);
            writeFloat(source+0x58+axis*4,origin+5);
        }
    }
    const auto read=[&](std::uintptr_t at,void* destination,std::size_t size){
        for(std::size_t i=0;i<size;i+=4){
            auto entry=words.find(unsigned(at+i));
            const auto bits=entry==words.end()?0u:entry->second;
            std::memcpy(static_cast<unsigned char*>(destination)+i,&bits,sizeof bits);
        }
        return true;
    };
    NorthlightAdtBounds::Evidence bounds;
    assert(terrainBounds(result,read,bounds)==AdtRejection::None&&bounds.low[2]==680.5f&&bounds.high[2]==688.5f);
    const float low[]={679,680,681},high[]={686,687,688.5f},outside[]={686,687,688.51f};
    assert(bounds.contains(low,high)&&!bounds.contains(low,outside));
    const float below=std::nextafter(682.5f,0.f),above=std::nextafter(682.5f,1000.f);
    assert(heightMatches(682.5f,.00001f,682.5f)&&heightMatches(above,.00001f,682.5f));
    assert(!heightMatches(below,.00001f,682.5f)&&!heightMatches(above,0,682.5f));
    words[heights+4]=144*sizeof(float);assert(terrainBounds(result,read,bounds)==AdtRejection::Mcvt);
    words[heights+4]=145*sizeof(float);
    writeFloat(first+0x60,689);assert(terrainBounds(result,read,bounds)==AdtRejection::None&&bounds.high[2]==689);
    writeFloat(first+0x60,687);assert(terrainBounds(result,read,bounds)==AdtRejection::Height);
    writeFloat(first+0x60,687.5f);
    words[chunk+0x14]=0;assert(terrainBounds(result,read,bounds)==AdtRejection::None&&bounds.high[2]==687.5f);
    assert(terrainBounds(audit({5,2,26,34,13},adtTag),read,bounds)==AdtRejection::Draw);
    words[chunk+0x18]=0x7f800000;assert(!audit(actual,adtTag).spatial);
    words[submit+4]=0x798aa6;assert(audit(actual,adtTag).kind==Kind::None);
    std::puts("PASS native WMO and ADT batch owners, indexed pool offsets, spatial metadata and rejection");
}
