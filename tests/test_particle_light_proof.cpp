#include "particle_light_proof.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
using namespace NorthlightParticleLightProof;
static Word bits(float f){Word w;std::memcpy(&w,&f,4);return w;}
static std::vector<Word> vertex(bool white=false){
    std::vector<Word> w={0xfffe0300,0x05000051,0xa00f0000,bits(1),bits(0),bits(0),bits(0),
        0x0200001f,0x8000000a,0x900f0001,0x0200001f,0x80000005,0x900f0002,
        0x0200001f,0x8000000a,0xe00f0001,0x0200001f,0x80000005,0xe0030002,
        0x02000001,0xe00f0001,white?0xa0000000u:0x90e40001u,
        0x04000004,0xe0030002,0x90e40002,0xa0e40006,0xa0e40007,0xffff};
    return w;
}
static std::vector<Word> pixel(bool fog=false,bool opaque=false,bool twice=false){
    std::vector<Word> w={0xffff0300,0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x80000005,0x90030001,
        0x0200001f,0x90000000,0xa00f0800};
    if(fog)w.insert(w.end(),{0x0200001f,0x8000000b,0x90010002});
    w.insert(w.end(),{0x03000042,0x800f0000,0x90e40001,0xa0e40800});
    if(opaque)w.insert(w.end(),{0x02000001,0x80080800,0x90ff0000});
    else w.insert(w.end(),{0x03000005,0x80080800,0x80ff0000,0x90ff0000});
    if(twice)w.insert(w.end(),{0x03000002,0x80070000,0x80e40000,0x80e40000});
    if(fog)w.insert(w.end(),{0x04000004,0x80070000,0x80e40000,0x90e40000,0xa1e40002,
                           0x04000004,0x80070800,0x90000002,0x80e40000,0xa0e40002});
    else w.insert(w.end(),{0x03000005,0x80070800,0x80e40000,0x90e40000});
    w.push_back(0xffff);return w;
}
static bool close(float a,float b){return std::fabs(a-b)<1e-6f;}
static std::uint64_t hash(const std::vector<Word>& w){std::uint64_t h=14695981039346656037ull;for(size_t i=0;i<w.size()*4;++i)h=(h^reinterpret_cast<const unsigned char*>(w.data())[i])*1099511628211ull;return h;}
int main(int argc,char**argv){
    Four inputs[16]={};inputs[1]={.25f,.5f,.75f,.4f};inputs[2]={.3f,.7f,0,1};float constants[1024]={};
    constants[24]=.5f;constants[25]=.25f;constants[28]=.1f;constants[29]=.2f;
    for(bool white:{false,true}){
        auto code=vertex(white);Vertex v;
        assert(NorthlightActorDeformation::compile(code.data(),code.size(),v.color,true,2,10));
        assert(NorthlightActorDeformation::compile(code.data(),code.size(),v.uv,true)); // Old UV API/default.
        assert(v.color.outputMask==15&&v.uv.outputMask==3&&initialized(v.color)&&initialized(v.uv));v.valid=true;
        Four c;float uv[2];assert(evaluateVertex(v,inputs,constants,c,uv));assert(close(uv[0],.25f)&&close(uv[1],.375f));
        for(unsigned j=0;j<4;++j)assert(c[j]==(white?1.f:inputs[1][j]));
        Vertex unknown;assert(!compileVertex(code.data(),code.size(),1,unknown)); // Family enum alone is not identity proof.
        auto undefined=code;undefined[21]=0x80e40000; // Unwritten temp as COLOR source.
        NorthlightActorDeformation::Program bad;
        if(NorthlightActorDeformation::compile(undefined.data(),undefined.size(),bad,true,2,10))assert(!initialized(bad));
        if(!white){
            inputs[1][3]=NAN;assert(!evaluateVertex(v,inputs,constants,c,uv));inputs[1][3]=.4f;
            auto saturated=code;saturated[20]|=0x00100000u;Vertex checked;
            assert(NorthlightActorDeformation::compile(saturated.data(),saturated.size(),checked.color,true,2,10));
            checked.uv=v.uv;checked.valid=true;inputs[1][0]=NAN;
            assert(NorthlightActorDeformation::evaluateFast(checked.color,inputs,constants,c)); // Legacy behavior stays unchanged.
            assert(!evaluateVertex(checked,inputs,constants,c,uv));inputs[1][0]=.25f; // Strict proof never hides NaN under saturation.
        }
    }
    const Four tint{.25f,.5f,.75f,.4f},tex{.1f,.8f,.2f,.5f};
    for(bool fog:{false,true})for(bool opaque:{false,true})for(bool twice:{false,true}){
        auto code=pixel(fog,opaque,twice);Pixel p;assert(compilePixel(code.data(),code.size(),p)&&p.valid&&p.fog==fog&&p.textureAlpha!=opaque&&p.rgbScale==(twice?2:1));
        Four out;assert(shade(p,tint,tex,nullptr,out));for(unsigned j=0;j<3;++j)assert(close(out[j],tint[j]*tex[j]*(twice?2:1)));
        assert(close(out[3],tint[3]*(opaque?1:tex[3])));assert(shade(p,Four{1,1,1,1},tex,nullptr,out)&&out[0]==tex[0]*(twice?2:1));
        auto bad=code;bad.pop_back();assert(!compilePixel(bad.data(),bad.size(),p)&&!p.valid);
        bad=code;for(auto& x:bad)if(x==0x90e40000u)x=0x90c90000u;assert(!compilePixel(bad.data(),bad.size(),p)); // Crossed RGB lanes.
        bad=code;bad.insert(bad.end()-1,{0x03000042,0x800f0003,0x90e40001,0xa0e40800});assert(!compilePixel(bad.data(),bad.size(),p)); // Second sample.
        if(fog){bad=code;bad[bad.size()-2]=0xa0e40003;assert(!compilePixel(bad.data(),bad.size(),p));} // Mismatched fog constant.
    }
    auto bad=pixel();bad[3]=0x90070000;Pixel p;assert(!compilePixel(bad.data(),bad.size(),p)); // Undeclared input alpha.
    bad=pixel();bad[7]=0x03000028;assert(!compilePixel(bad.data(),bad.size(),p)); // Control flow.
    bad=pixel();bad.insert(bad.end()-1,{0x02000001,0x800f0801,0x80e40000});assert(!compilePixel(bad.data(),bad.size(),p)); // MRT.
    bad=pixel();bad[bad.size()-4]=0x80070001; // Hold the RGB product in r1.
    bad.insert(bad.begin()+1,{0x05000051,0xa00f0000,0,0,0,0});
    bad.insert(bad.end()-1,{0x03000005,0x800f0003,0xa0e40003,0xa0000000,
                          0x03000002,0x80070800,0x80e40001,0x80e40003});
    assert(!compilePixel(bad.data(),bad.size(),p)); // Unknown c3 * 0 is not a finite-value proof.
    bad=pixel();for(unsigned n=0;n<40;++n)bad.insert(bad.end()-5,{0x03000005,0x80070000,0x80e40000,0x80e40000});
    assert(!compilePixel(bad.data(),bad.size(),p)); // Shared expression DAG cannot trigger exponential proof work.
    assert(compilePixel(pixel().data(),pixel().size(),p));Four out;assert(!shade(p,tint,Four{1,NAN,1,1},nullptr,out));
    std::printf("PASS synthetic RGBA/COLOR0 constants, transformed UV0, exact intrinsic texture tint, fog exclusion and fail-closed negatives\n");
    if(argc==2){
        FILE* f=std::fopen(argv[1],"rb");assert(f);unsigned count=0;assert(std::fread(&count,4,1,f)==1);unsigned nativeVs=0,nativePs=0,white=0,vsEvaluated=0,positions=0;
        for(unsigned i=0;i<count;++i){
            unsigned h[3];assert(std::fread(h,4,3,f)==3);std::vector<Word> w(h[2]);assert(std::fread(w.data(),4,w.size(),f)==w.size());
            if(h[0]){
                const unsigned family=m2ShaderFamily(hash(w));Vertex v;assert(compileVertex(w.data(),w.size(),family,v));++nativeVs;
                Vertex no;assert(!compileVertex(w.data(),w.size(),family==1?2:1,no));auto changed=w;changed.back()^=1;assert(!compileVertex(changed.data(),changed.size(),family,no));
                NorthlightActorDeformation::Program position;assert(NorthlightActorDeformation::compile(w.data(),w.size(),position,false,2));
                Four positionInputs[16]={};for(const auto& input:position.inputs){
                    if(input.usage==0)positionInputs[input.reg]={.3f,.7f,.2f,1};
                    else if(input.usage==1)positionInputs[input.reg]={1,0,0,0};}
                float positionBank[1024]={};for(unsigned b=0;b<75;++b){unsigned r=31+3*b;positionBank[r*4]=positionBank[(r+1)*4+1]=positionBank[(r+2)*4+2]=1;}
                Four view;assert(NorthlightActorDeformation::evaluateFast(position,positionInputs,positionBank,view));
                assert(close(view[0],.3f)&&close(view[1],.7f)&&close(view[2],.2f)&&close(view[3],1.f));++positions;
                if(v.color.inputs.empty()){Four c;float uv[2];Four in[16]={};for(auto& x:in)x={.3f,.7f,0,1};
                    float bank[1024]={};bank[24]=1;bank[29]=1;
                    if(evaluateVertex(v,in,bank,c,uv)&&c[0]==1&&c[1]==1&&c[2]==1&&c[3]==1)++white;}
                if(family==1||family==2){
                    Four in[16]={};const auto fill=[&](const auto& program){for(const auto& input:program.inputs){
                        if(input.usage==10)in[input.reg]=tint;else if(input.usage==5)in[input.reg]={.3f,.7f,0,1};
                        else if(input.usage==0)in[input.reg]={0,0,0,1};else if(input.usage==3)in[input.reg]={0,0,1,0};
                        else if(input.usage==1)in[input.reg]={1,0,0,0};}};fill(v.color);fill(v.uv);
                    float bank[1024]={};bank[24]=.5f;bank[27]=.1f;bank[29]=.25f;bank[31]=.2f;
                    bank[44]=bank[45]=bank[46]=1;bank[50]=-1;
                    for(unsigned b=0;b<75;++b){unsigned r=31+3*b;bank[r*4]=bank[(r+1)*4+1]=bank[(r+2)*4+2]=1;}
                    for(unsigned light=0;light<4;++light){bank[(21+light)*4]=bank[(21+light)*4+1]=bank[(21+light)*4+2]=10;bank[100+light]=1;}
                    Four c;float uv[2];
                    if(!evaluateVertex(v,in,bank,c,uv)){std::fprintf(stderr,"native vertex evaluate failed row=%u family=%u hash=%llx\n",i,family,(unsigned long long)hash(w));assert(false);}
                    assert(close(uv[0],.25f)&&close(uv[1],.375f));++vsEvaluated;
                    Pixel shader;assert(compilePixel(pixel(true).data(),pixel(true).size(),shader));assert(shade(shader,c,tex,nullptr,out));
                    for(unsigned j=0;j<3;++j)assert(close(out[j],c[j]*tex[j]));
                }
            }else if(compilePixel(w.data(),w.size(),p)){assert(shade(p,tint,tex,nullptr,out));++nativePs;}
        }
        std::fclose(f);assert(nativeVs==540&&positions==540&&vsEvaluated==360&&white>0&&nativePs==12);
        std::printf("PASS native BLS: %u vertex variants, %u view positions, %u particle UV/RGBA pipelines, %u white COLOR0 permutations, %u proven pixel permutations\n",nativeVs,positions,vsEvaluated,white,nativePs);
    }
}
