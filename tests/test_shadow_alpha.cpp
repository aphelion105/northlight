#include "shadow_alpha.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <random>
using namespace NorthlightShadowAlpha;
using V=std::array<float,4>;
static void outputTests(){
    for(unsigned major:{2u,3u}){
        std::vector<uint32_t> w={0xffff0000u|(major<<8)};
        if(major==3)w.insert(w.end(),{0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x80000005,0x900f0001});
        const auto sampler=w.size();w.insert(w.end(),{0x0200001f,0x90000000,0xa00f0800});
        const auto sample=w.size();w.insert(w.end(),{0x03000042,0x800f0000,major==3?0x90e40001u:0xb0e40000u,0xa0e40800});
        const auto output=w.size();w.insert(w.end(),{0x03000005,0x80080800,0x80ff0000,0x90ff0000,0x0000ffff});
        auto info=analyze(w.data(),w.size());assert(info.outputKnown&&info.outputMode==TextureColor&&!info.discard);
        for(unsigned format:{21u,22u,113u,116u})for(unsigned ref:{0u,1u,127u,255u})for(bool equal:{false,true}){
            const auto mode=fixedTestMode(info,true,ref,equal,format);
            assert(fixed(mode)&&recipe(mode)==TextureColor&&reference(mode)==ref&&sourceFormat(mode)==format&&bool(mode&GreaterEqual)==equal);
            assert(!fixed(fixedTestMode(info,false,ref,equal,format)));
        }
        assert(!fixed(fixedTestMode(info,true,128,false,0))&&!fixed(fixedTestMode(info,true,128,false,256)));
        for(unsigned changed:{0u,1u,2u,3u,4u,5u,6u}){
            auto bad=w;
            if(changed==0)bad[sample+2]^=0x00060000; // UV .xy is required.
            if(changed==1)bad[sample+3]^=1;          // Only the captured sampler.
            if(changed==2)bad[sample+3]^=0x00ff0000; // No sampler result swizzle.
            if(changed==3)bad[output+1]|=0x00100000; // Saturated output is another recipe.
            if(changed==4)bad.insert(bad.end()-1,{0x01000041,0x800f0000});
            if(changed==5)bad[sampler+1]=0x98000000; // Cube sampling is not sampler2D.
            if(changed==6)bad[sampler+1]=0xa0000000; // Nor is volume sampling.
            assert(!analyze(bad.data(),bad.size()).outputKnown);
        }
        for(auto recipe_:{Texture,Color}){
            auto single=w;single.erase(single.begin()+output,single.end());
            single.insert(single.end(),{0x02000001,0x80080800,recipe_==Texture?0x80ff0000u:0x90ff0000u,0x0000ffff});
            info=analyze(single.data(),single.size());assert(info.outputKnown&&info.outputMode==recipe_);
        }
    }
    std::puts("PASS fixed output proof: PS2/PS3, native recipe/ref/function/source-format descriptor; unknown UV/sampler/swizzle/saturation/discard rejected");
}
static bool nativeKill(const std::vector<uint32_t>& w,float color,float tex,float ref){
    V temp[32]={},constants[224]={},inputs[16]={};for(auto& c:constants)c.fill(ref);
    for(auto& v:inputs)v={.3f,.5f,.7f,color};
    auto read=[&](uint32_t token){V out{};unsigned r=token&2047,k=type(token);V value=k==0?temp[r]:k==2?constants[r]:inputs[r];
        for(unsigned c=0;c<4;++c){out[c]=value[(token>>(16+c*2))&3];if(((token>>24)&15)==1)out[c]=-out[c];}return out;};
    for(size_t p=1;p<w.size();){unsigned op=w[p]&65535;if(op==65535)break;if(op==65534){p+=1+((w[p]>>16)&32767);continue;}
        unsigned n=(w[p]>>24)&15;const auto* a=w.data()+p+1;p+=1+n;
        if(op==31)continue;if(op==81){for(unsigned c=0;c<4;++c){union {uint32_t u;float f;} x{a[c+1]};constants[a[0]&2047][c]=x.f;}continue;}
        if(op==65){const auto v=read((a[0]&~0x00ff0000u)|0x00e40000u);return v[0]<0||v[1]<0||v[2]<0;}
        V out{};
        if(op==66)out={.2f,.4f,.6f,tex};
        else if(op==1||op==2||op==3||op==4||op==5){
            V x=read(a[1]),y=n>=3?read(a[2]):V{},z=n>=4?read(a[3]):V{};
            for(unsigned c=0;c<4;++c)out[c]=op==1?x[c]:op==2?x[c]+y[c]:op==3?x[c]-y[c]:op==5?x[c]*y[c]:x[c]*y[c]+z[c];}
        else out.fill(NAN);
        assert(type(a[0])==0);for(unsigned c=0;c<4;++c)if(a[0]&(1u<<(16+c)))temp[a[0]&2047][c]=out[c];
    }assert(false);return false;
}
int main(int argc,char** argv){
    outputTests();
    assert(argc==2);FILE* f=std::fopen(argv[1],"rb");assert(f);unsigned total=0,discard=0,known=0,product=0,color=0;uint32_t bytes;std::mt19937 rng(71);
    while(std::fread(&bytes,4,1,f)==1){std::vector<uint32_t>w(bytes/4);assert(std::fread(w.data(),1,bytes,f)==bytes);auto info=analyze(w.data(),w.size());++total;discard+=info.discard;
        if(!info.known)continue;++known;product+=info.mode==TextureColor;color+=info.mode==Color;
        for(unsigned i=0;i<200;++i){float c=float(rng()%1001)/1000,t=float(rng()%1001)/1000,r=float(rng()%1001)/1000;
            assert(nativeKill(w,c,t,r)==((info.mode==TextureColor?c*t:c)-r<0));}
        auto broken=w; // A second discard or a different sampler is never silently accepted.
        broken.insert(broken.end()-1,{0x01000041u,0x800f0000u});assert(!analyze(broken.data(),broken.size()).known);
        if(info.mode==TextureColor){for(size_t p=1;p+3<broken.size();){unsigned op=broken[p]&65535;if(op==65534){p+=1+((broken[p]>>16)&32767);continue;}unsigned n=(broken[p]>>24)&15;if(op==66){broken[p+3]=(broken[p+3]&~2047u)|1;break;}p+=1+n;}assert(!analyze(broken.data(),broken.size()).known);}
    }std::fclose(f);std::fprintf(stderr,"census %u %u %u %u\n",total,known,product,color);assert(total>100&&known>100&&product>20&&color>20);
    std::printf("PASS shader corpus: %u shaders, %u discard, %u proved (%u texture*color, %u color); 200 randomized mask checks each; unknown/multiple/sampler changes rejected\n",total,discard,known,product,color);
}
