#pragma once
#include "m2_draw_12340.h"

static void nativeM2Section(){
    using namespace NorthlightM2Draw;
    // Model two suballocations and two selected bones in reverse order. Using
    // at() means an unexpected read, including one past the stack, fails.
    constexpr unsigned frame=0x1000,gx=0x1040,batch=0x10a0,ctx=0x1100,top=0x1200;
    constexpr unsigned section=0x2000,model=0x3000,material=0x4000,gxDevice=0x5000;
    constexpr unsigned vb=0x9000,ib=0xa000,data=0xb000,selection=0xc000,palette=0xd000;
    std::unordered_map<unsigned,uint32_t> words{
        {frame,gx},{frame+4,0x6a36d1},{gx,batch},{gx+4,0x820610},{batch+4,0x823a88},{batch-20,ctx},
        {ctx+0x90,section},{ctx+0x60,model},{ctx+0x98,material},{ctx+0x4c,0},{0xc5df88,gxDevice},
        {gxDevice+0x28bc,ib},{ib+24,100},{gxDevice+0x224,0},{gxDevice+0x2870,vb},{vb+12,32},{vb+24,128},
        {batch-16,3},{batch-12,7},{batch-8,12},{ctx+0x48,data},{model+0x98,palette},
        {data+0x7c,selection},{data+0x78,5},{data+0x2c,2}};
    std::unordered_map<unsigned,uint16_t> halves{
        {material+2,0},{section+4,3},{section+6,9},{section+8,7},{section+10,12},
        {section+12,2},{section+14,3},{selection+6,1},{selection+8,0}};
    struct Bank{
        float v[256*4]{};
        float* at(unsigned r){return v+4*r;}
        const float* at(unsigned r)const{return v+4*r;}
        bool allKnown(unsigned r,unsigned n)const{return r==31&&n==6;}
    } bank;
    for(unsigned b=0;b<2;++b)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col){
        float value=float(b*16+row*4+col);uint32_t bits;std::memcpy(&bits,&value,4);
        bank.at(31+b*3+row)[col]=value;words[palette+64*(1-b)+16*col+4*row]=bits;
    }
    ProducerStats stats;Section s{};
    auto match=[&](int base=4,unsigned start=57,unsigned count=4){
        return decodeSection(frame,top,[&](unsigned at){return words.at(at);},[&](unsigned at){return halves.at(at);},
                             base,3,9,start,count,bank,true,s,stats);
    };
    assert(match()&&s.model==model&&s.material==material&&s.section==section&&s.bones==2&&s.combo==3&&stats.paletteChecks==1);
    const Range submitted{4,3,9,57,4},stale{4,3,9,58,4};
    assert(s.range==submitted&&s.paletteAudited);
    s.producerFrame=127;
    ConsumerStats consumer;
    assert(!consumer.admit(s,&stale,true)&&!consumer.admit(s,&submitted,false)&&consumer.admit(s,&submitted,true));
    consumer.packet(s,true,2,35,bank.at(0));
    assert(consumer.admit(s,&submitted,true));
    consumer.packet(s,false,31,5,bank.at(0));
    bank.at(34)[1]+=1;
    assert(consumer.admit(s,&submitted,true));
    consumer.packet(s,true,31,6,bank.at(0));
    bank.at(34)[1]-=1;
    assert(consumer.producerFrame==127&&consumer.seen==5&&consumer.eligible==3&&consumer.rangeMismatch==1&&consumer.coverageMismatch==1);
    assert(consumer.queued==3&&consumer.immutable==2&&consumer.paletteChecks==2&&consumer.paletteMismatch==1&&consumer.paletteUnavailable==1);
    assert(!match(0)&&!match(4,7)&&!match(4,57,3));
    words[gxDevice+0x224]=1;assert(match(0));words[gxDevice+0x224]=0;
    bank.at(34)[1]+=1;assert(!match()&&stats.paletteMismatch==1);bank.at(34)[1]-=1;
    halves[material+2]=1;assert(!match());halves[material+2]=0;
    words[ctx+0x4c]=1;assert(!match());words[ctx+0x4c]=0;
    words[batch+4]=0x823a8c;assert(!match());words[batch+4]=0x823a88;
    words[batch-20]=top-4;assert(!match());words[batch-20]=ctx;
    halves[section+12]=76;assert(!match());halves[section+12]=2;
    assert(match());
    std::puts("PASS native M2 stack, pooled ranges, material, transposed bone selection and fallback");
}

static void nativeM2Grouped(){
    using namespace NorthlightM2Draw;
    constexpr unsigned frame=0x1000,gx=0x1040,batch=0x10a0,ctx=0x1100,top=0x1200;
    constexpr unsigned group=0x2000,section=0x2100,shared=0x2200,pool=0x2400;
    constexpr unsigned data=0x3000,selection=0x3100,elements=0x4000,order=0x5000;
    constexpr unsigned modelA=0x6000,modelB=0x6200,paletteA=0x6800,paletteB=0x7000;
    constexpr unsigned device=0x8000,vb=0xc000,ib=0xd000;
    std::unordered_map<unsigned,std::uint32_t> words{
        {frame,gx},{frame+4,0x6a36d1},{gx,batch},{gx+4,0x820d50},{gx-4,ctx},{batch+4,0x823a9f},
        {batch-4,1},{batch-8,2},{batch-0x14,3},{batch-0x30,3},{batch-0x2c,7},
        {batch-0x28,24},{batch-0x24,17u<<16},{batch+8,elements},{batch+12,order},
        {ctx+0x50,group},{ctx+0x90,section},{ctx+0x68,shared},{ctx+0x48,data},
        {group,2},{group+0x2c,section},{group+0x1c,3},{shared+0x170,pool},
        {0xc5df88,device},{device+0x28bc,ib},{device+0x224,0},{device+0x2870,vb},
        {ib+24,100},{vb+12,32},{vb+24,128},{data+0x7c,selection},{data+0x78,4},{data+0x2c,2},
        {order+4,3},{order+8,1},{elements+68*3+4,modelA},{elements+68+4,modelB},
        {modelA+0x98,paletteA},{modelB+0x98,paletteB}};
    std::unordered_map<unsigned,std::uint16_t> halves{
        {section+8,7},{section+10,12},{section+12,2},{section+14,2},{pool+4,9},
        {selection+4,1},{selection+6,0}};
    struct Bank{
        float values[12][4]{};
        bool known=true;
        const float* at(unsigned r)const{return values[r-PaletteFirst];}
        bool allKnown(unsigned r,unsigned n)const{return known&&r==PaletteFirst&&n==12;}
    } bank;
    for(unsigned instance=0;instance<2;++instance)for(unsigned bone=0;bone<2;++bone)
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col){
            const unsigned r=instance*6+bone*3+row;
            const float value=float(r*4+col+1);
            bank.values[r][col]=value;
            std::uint32_t bits;
            std::memcpy(&bits,&value,sizeof bits);
            const auto palette=instance==0?paletteA:paletteB;
            words[palette+64*(1-bone)+16*col+4*row]=bits;
        }
    const Range actual{4,0,18,57,8};
    const auto audit=[&](const Range& draw,bool bindings=true){
        GroupedStats stats;
        auditGrouped(frame,top,[&](unsigned at){return words.at(at);},[&](unsigned at){return halves.at(at);},
                     draw,bindings,true,bank,stats);
        return stats;
    };
    const auto valid=audit(actual);
    assert(valid.shaderDraws==1&&valid.alphaTested==1&&valid.matched==1&&valid.multiInstance==1&&valid.instances==2&&valid.triangles==8);
    assert(audit({4,0,18,58,8}).rangeMismatch==1);
    assert(audit(actual,false).rangeMismatch==1);
    words[batch-4]=2;assert(audit(actual).rangeMismatch==1);words[batch-4]=1;
    bank.known=false;assert(audit(actual).paletteUnavailable==1);bank.known=true;
    std::swap(words[order+4],words[order+8]);assert(audit(actual).paletteMismatch==1);
    std::swap(words[order+4],words[order+8]);
    halves[selection+6]=2;assert(audit(actual).paletteMismatch==1);halves[selection+6]=0;
    words[batch+4]=0x823a88;assert(!audit(actual).shaderDraws);words[batch+4]=0x823a9f;
    words[gx+4]=0x820f1b;assert(audit(actual).softwareDraws==1);words[gx+4]=0x820d50;
    assert(audit(actual).matched==1);
    std::puts("PASS native grouped M2 slice, pooled indices, ordered instance palettes and rejection");
}
