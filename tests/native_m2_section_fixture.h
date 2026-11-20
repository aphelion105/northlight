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
    struct Bank{float v[256*4]{};float* at(unsigned r){return v+4*r;}const float* at(unsigned r)const{return v+4*r;}bool allKnown(unsigned r,unsigned n)const{return r==31&&n==6;}} bank;
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
