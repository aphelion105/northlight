#pragma once
// Task 34 SpellFog: a registration-time bytecode proof for the vertex shader, the counterpart of NorthlightLegacyFog::ps3FogColorRegister. The spell layer replaces c30 of a replayed
// draw (the fog ramp, see spell_fog.h) only when this proves the shader's fog output is exactly the ramp of that constant:
//   vs_3_0:  dp4 rZ.k, c33, X ; mad rM.m, rZ.k, c30.x, c30.y ; pow rP.n, rM.m, c30.z ; min o<fog>.x, rP.n, cOne.x      (o<fog> declared with the FOG usage, cOne.x a def of 1.0)
//   vs_2_0:  dp4 rZ.k, c33, X ; mad rM.xy, rZ.k, c30.x, c30.y ; mov rM.zw, c30.z ; lit rM, rM ; min oFog, rM.z, cOne.x
// (fog = min(pow(max(viewZ * c30.x + c30.y, 0), c30.z), 1)), and c30 is read nowhere else: no other operand of it, no relative-addressed or matrix read that can cover it. The vs_1_1
// linear form is not accepted (the layer defers only draws with a ps_2_0 or newer pixel shader). Fail closed: any control flow or predicated instruction, a def of c30, another
// writer of the fog output, an unrecognised shape, a truncated stream. A relative read is taken to start at its base register (a0 is an index >= 0 for bone matrices); the
// view depth may come from a bone matrix row, c33[a0.x + 0] (skinned models). The power may be taken of the bare ramp or of max(ramp, 0) (both seen in the client).
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace NorthlightSpellFog {
inline bool vsFogProven(const uint32_t* code,size_t count,bool* bare=nullptr){
    if(!code||count<4||code[count-1]!=0xffff)return false;
    const bool vs3=code[0]==0xfffe0300,vs2=code[0]==0xfffe0200;
    if(!vs3&&!vs2)return false;
    enum:uint8_t{None,ViewZ,Ramp,RampMax,Power,PowBare,PowMax,LitFog};
    uint8_t fact[32][4]={};bool row33[32]={};float cval[256][4]={};bool cdef[256]={};
    int fogOut=-1;unsigned fogMin=0,c30Ok=0,c30All=0;bool fogBare=false;
    const auto type=[](uint32_t t){return ((t>>28)&7)|((t>>8)&24);};
    const auto num=[](uint32_t t){return t&2047;};
    const auto swz=[](uint32_t t){return (t>>16)&255;};
    const auto plain=[&](uint32_t t,unsigned ty,unsigned n,unsigned sw){return type(t)==ty&&num(t)==n&&swz(t)==sw&&((t>>24)&15)==0&&!(t&0x2000);};
    const auto rep=[&](uint32_t t)->int{ // a replicated swizzle: its component, else -1
        const unsigned s=swz(t);for(unsigned c=0;c<4;++c)if(s==c*0x55u)return int(c);return -1;};
    for(size_t at=1;at<count;){
        const uint32_t token=code[at];const unsigned op=token&65535;
        if(op==65535){if(!(at==count-1&&fogMin==1&&c30Ok==3&&c30All==3&&(vs2||fogOut>=0)))return false;if(bare)*bare=fogBare;return true;}
        if(op==65534){const size_t words=1+((token>>16)&32767);if(words>count-at)return false;at+=words;continue;}
        if(op==0){++at;continue;}
        if(token&0x10000000)return false;                                   // predicated
        if(op==25||op==26||op==27||op==28||op==29||op==30||op==38||op==39||op==40||op==41||op==42||op==43||op==44||op==45||op==94||op==96)return false;
        const unsigned size=(token>>24)&15;if(!size||size+1>count-at)return false;
        const uint32_t* a=code+at+1;at+=1+size;
        if(op==31){ // dcl
            if(vs3&&size==2&&type(a[1])==6&&(a[0]&31)==11&&((a[0]>>16)&15)==0){if(fogOut>=0)return false;fogOut=int(num(a[1]));}
            continue;
        }
        if(op==81){ // def
            if(size!=5)return false;
            if(type(a[0])==2){const unsigned i=num(a[0]);if(i>=256||i==30)return false;cdef[i]=true;for(unsigned c=0;c<4;++c)std::memcpy(&cval[i][c],&a[1+c],4);}
            continue;
        }
        if(op==82||op==83)continue;                                         // defi, defb
        // operands: destination (with its relative token), then sources (each with its relative token)
        unsigned i=0;const uint32_t d=a[i++];if(d&0x2000)++i;
        uint32_t src[4];unsigned ns=0;
        while(i<size&&ns<4){const uint32_t s=a[i++];if(s&0x2000)++i;src[ns++]=s;}
        if(i!=size)return false;
        for(unsigned k=0;k<ns;++k){ // every read of c30 is counted; a relative or matrix read that can reach it refuses the shader
            const uint32_t s=src[k];if(type(s)!=2)continue;
            const unsigned n=num(s);
            if(s&0x2000){if(n<=30)return false;continue;}
            if(op>=20&&op<=24&&k==1){const unsigned rows=op==20?4:op==21?3:op==22?4:op==23?3:2;if(n<=30&&30<n+rows)return false;}
            if(n==30)++c30All;
        }
        const unsigned dt=type(d),dn=num(d),mask=(d>>16)&15;
        const bool fogDest=vs3?(dt==6&&int(dn)==fogOut):(dt==4&&dn==1);
        uint8_t made[4]={0,0,0,0};bool tempWrite=dt==0&&dn<32;
        if(op==9&&ns==2&&tempWrite&&(mask==1||mask==2||mask==4||mask==8)&&swz(src[0])==0xE4&&((src[0]>>24)&15)==0
           &&((type(src[0])==2&&num(src[0])==33)||(type(src[0])==0&&num(src[0])<32&&!(src[0]&0x2000)&&row33[num(src[0])]))   // the view-depth row c33 (or c33[a0.x + 0], or a temp built from those rows: a skinned blend)
           &&(type(src[1])==0||type(src[1])==1)&&!(src[1]&0x2000)&&((src[1]>>24)&15)==0){
            for(unsigned c=0;c<4;++c)if(mask>>c&1)made[c]=ViewZ;
        }else if(op==4&&ns==3&&tempWrite&&type(src[0])==0&&((src[0]>>24)&15)==0&&!(src[0]&0x2000)&&rep(src[0])>=0&&num(src[0])<32&&fact[num(src[0])][rep(src[0])]==ViewZ
                 &&plain(src[1],2,30,0x00)&&plain(src[2],2,30,0x55)){
            for(unsigned c=0;c<4;++c)if(mask>>c&1)made[c]=Ramp;c30Ok+=2;
        }else if(op==1&&vs2&&ns==1&&tempWrite&&plain(src[0],2,30,0xAA)){
            for(unsigned c=0;c<4;++c)if(mask>>c&1)made[c]=Power;c30Ok+=1;
        }else if(op==11&&ns==2&&tempWrite){ // max(ramp, 0): the 0 a def'd constant component (either operand order)
            for(unsigned o=0;o<2;++o){
                const uint32_t t=src[o],z=src[1-o];const int c=rep(t),k=rep(z);
                if(type(t)==0&&num(t)<32&&!(t&0x2000)&&((t>>24)&15)==0&&c>=0&&fact[num(t)][c]==Ramp&&type(z)==2&&!(z&0x2000)&&((z>>24)&15)==0&&k>=0&&num(z)<256&&cdef[num(z)]&&cval[num(z)][k]==0.0f){
                    for(unsigned m=0;m<4;++m)if(mask>>m&1)made[m]=RampMax;break;}
            }
        }else if(op==32&&ns==2&&tempWrite&&type(src[0])==0&&((src[0]>>24)&15)==0&&!(src[0]&0x2000)&&rep(src[0])>=0&&num(src[0])<32
                 &&(fact[num(src[0])][rep(src[0])]==Ramp||fact[num(src[0])][rep(src[0])]==RampMax)&&plain(src[1],2,30,0xAA)){
            const uint8_t f=fact[num(src[0])][rep(src[0])]==RampMax?PowMax:PowBare;
            for(unsigned c=0;c<4;++c)if(mask>>c&1)made[c]=f;c30Ok+=1;
        }else if(op==16&&vs2&&ns==1&&tempWrite&&type(src[0])==0&&num(src[0])<32&&swz(src[0])==0xE4&&((src[0]>>24)&15)==0&&!(src[0]&0x2000)
                 &&fact[num(src[0])][0]==Ramp&&fact[num(src[0])][1]==Ramp&&fact[num(src[0])][3]==Power){
            if(mask&4)made[2]=LitFog;
        }else if(op==10&&ns==2&&fogDest){
            const int c=rep(src[0]);const int k=rep(src[1]);
            if(!(vs3?mask==1:(mask==15||mask==1)))return false;
            if(type(src[0])!=0||num(src[0])>=32||((src[0]>>24)&15)||(src[0]&0x2000)||c<0)return false;
            {const uint8_t f=fact[num(src[0])][c];if(f!=PowMax&&f!=PowBare&&f!=LitFog)return false;fogBare=f==PowBare;}
            if(type(src[1])!=2||(src[1]&0x2000)||((src[1]>>24)&15)||k<0||num(src[1])>=256||!cdef[num(src[1])]||cval[num(src[1])][k]!=1.0f)return false;
            ++fogMin;continue;
        }else if(fogDest){
            return false;                                                    // any other write to the fog output
        }
        if(tempWrite){
            for(unsigned c=0;c<4;++c)if(mask>>c&1)fact[dn][c]=made[c];
            bool rowSrc=false; // a blend of bone-matrix z rows: mul/mad/add/mov with a c33 operand or a temp that is one
            if(op==1||op==2||op==4||op==5)for(unsigned k=0;k<ns;++k)if((type(src[k])==2&&num(src[k])==33)||(type(src[k])==0&&num(src[k])<32&&!(src[k]&0x2000)&&row33[num(src[k])]))rowSrc=true;
            row33[dn]=mask==15&&rowSrc;
        }
    }
    return false;                                                            // no end token
}
}
