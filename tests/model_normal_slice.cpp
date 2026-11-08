#undef NDEBUG
#include "model_normal_patch.h"
#include "model_normal_slice.h"
#include "shader_read_set.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <cstdlib>
static int failAllocation=-1;
void* operator new(std::size_t n){if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}if(failAllocation>0)--failAllocation;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
using namespace NorthlightWaterShaderPatch;
namespace S=NorthlightModelNormalSlice;
static void op(std::vector<Word>& code,unsigned instruction,std::initializer_list<Word> args){code.push_back(instruction|(unsigned(args.size())<<24));code.insert(code.end(),args);}
static std::vector<Word> vertex(){
    std::vector<Word> v{0xfffe0300u};
    op(v,31,{0x80000000,dst(1,0)});op(v,31,{0x80000003,dst(1,1,7)});op(v,31,{0x8000000a,dst(1,2)});
    op(v,31,{0x80000000,dst(6,0)});op(v,31,{0x80050005,dst(6,1,7)});op(v,31,{0x80070005,dst(6,2)});
    op(v,31,{0x8000000a,dst(6,3)});op(v,31,{0x80000005,dst(6,4,3)});
    op(v,1,{dst(0,0),src(1,0)});op(v,36,{dst(0,1,7),src(1,1)});op(v,1,{dst(6,0),src(0,0)});
    op(v,1,{dst(6,1,7),src(0,1)});op(v,1,{dst(6,2),src(0,0)});
    op(v,5,{dst(0,2),src(1,2),src(2,24)});op(v,8,{dst(0,3),src(0,1),src(2,12)});
    op(v,4,{dst(6,3),src(0,2),src(0,3),src(2,11)});op(v,1,{dst(6,4,3),src(1,0)});v.push_back(65535);return v;
}
static std::vector<Word> pixel(){
    std::vector<Word> p{0xffff0300u};op(p,31,{0x80000005,dst(1,0,3)});op(p,31,{0x80050005,dst(1,1,7)});
    op(p,31,{0x80070005,dst(1,2)});op(p,31,{0x8000000a,dst(1,3)});
    op(p,8,{dst(0,0,1),src(1,1),src(1,1)});op(p,7,{dst(0,0,1),src(0,0,0)});
    op(p,5,{dst(8,0,7),src(1,1),src(0,0,0)});op(p,1,{dst(8,0,8),src(1,2,0xff)});p.push_back(65535);return p;
}
static std::vector<Word> load(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f&&f.tellg()%4==0);std::vector<Word> v(size_t(f.tellg())/4);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),v.size()*4);return v;}
int main(int argc,char** argv){
    auto v=vertex(),p=pixel();std::vector<Word> lean,small;assert(S::vertex(v.data(),v.size(),lean)&&S::pixel(p.data(),p.size(),small));
    assert(lean.size()<v.size()&&small.size()<p.size());std::vector<Word> again;assert(S::vertex(lean.data(),lean.size(),again)&&again==lean);
    const auto depthMask=[](const std::vector<Word>& code){for(size_t at=1;at<code.size()-1;){unsigned n=(code[at]>>24)&15;
        if((code[at]&65535)==31&&(code[at+1]&0xfffff)==0x70005)return (code[at+2]>>16)&15;at+=n+1;}return 0u;};
    assert(depthMask(lean)==15&&depthMask(small)==15); // Preserve depth W on DXVK2.
    std::vector<Word> shadow;assert(S::shadowVertex(v.data(),v.size(),shadow)&&shadow.size()<v.size());
    assert(S::shadowVertex(shadow.data(),shadow.size(),again)&&again==shadow&&depthMask(shadow)==15);
    bool alphaDeclared=false,alphaWritten=false;
    for(size_t at=1;at<shadow.size()-1;){const unsigned instruction=shadow[at]&65535,n=(shadow[at]>>24)&15;
        if(instruction==31&&(shadow[at+1]&0xfffff)==10&&regType(shadow[at+2])==6){assert(((shadow[at+2]>>16)&15)==15);alphaDeclared=true;}
        if(S::arithmetic(instruction)&&regType(shadow[at+1])==6&&regIndex(shadow[at+1])==3){assert(((shadow[at+1]>>16)&15)==8);alphaWritten=true;}at+=n+1;}
    assert(alphaDeclared&&alphaWritten);
    auto reads=NorthlightShaderReads::scan(lean.data(),lean.size());assert(reads.exact&&!reads.f[0]);
    // Relative palette operands retain their exact address producer.
    auto relative=v;const size_t mov=1+3*8;relative.insert(relative.begin()+mov,{0x0200002eu,dst(3,0,1),src(1,0,0)});
    relative[mov+3]=0x03000001;relative[mov+5]=src(2,31)|0x2000u;relative.insert(relative.begin()+mov+6,src(3,0,0));
    assert(S::vertex(relative.data(),relative.size(),again));reads=NorthlightShaderReads::scan(again.data(),again.size());assert(reads.exact&&(reads.f[0]&(1ull<<31))&&reads.f[3]);
    const auto rejected=[&](const std::vector<Word>& code,bool isVertex){std::vector<Word> out{0xdeadbeef};assert(!(isVertex?S::vertex(code.data(),code.size(),out):S::pixel(code.data(),code.size(),out)));assert(out==std::vector<Word>{0xdeadbeef});};
    auto coverage=p;coverage.insert(coverage.end()-1,{0x01000041,src(1,0,0)});rejected(coverage,false);
    auto branch=v;branch.insert(branch.end()-1,0x0000002a);rejected(branch,true);
    auto predicated=v;predicated[mov]|=0x10000000u;rejected(predicated,true);
    auto texture=v;texture[mov]=(texture[mov]&~65535u)|95;rejected(texture,true);
    const auto shadowRejected=[](const std::vector<Word>& code){std::vector<Word> out{0xdeadbeef};assert(!S::shadowVertex(code.data(),code.size(),out)&&out==std::vector<Word>{0xdeadbeef});};
    for(const auto& bad:{branch,predicated,texture})shadowRejected(bad);
    for(unsigned semantic:{10u,5u,0x70005u}){auto bad=v;for(size_t at=1;at<bad.size()-1;){const unsigned n=(bad[at]>>24)&15;if((bad[at]&65535)==31&&(bad[at+1]&0xfffff)==semantic)bad[at+1]^=0x10000;at+=n+1;}shadowRejected(bad);}
    for(size_t n=0;n<v.size();++n){auto truncated=std::vector<Word>(v.begin(),v.begin()+n);rejected(truncated,true);}
    for(size_t n=0;n<v.size();++n)shadowRejected(std::vector<Word>(v.begin(),v.begin()+n));
    unsigned accepted=0;uint32_t random=41;
    for(unsigned trial=0;trial<20000;++trial){auto code=trial%2?v:p;random=random*1664525u+1013904223u;const auto index=random%code.size();random=random*1664525u+1013904223u;code[index]^=1u<<(random%32);std::vector<Word> out{0xdeadbeef};
        const bool ok=trial%2?S::vertex(code.data(),code.size(),out):S::pixel(code.data(),code.size(),out);if(!ok)assert(out==std::vector<Word>{0xdeadbeef});else ++accepted;
    }
    for(unsigned fail=0;fail<40;++fail)for(bool isVertex:{false,true}){std::vector<Word> out{0xdeadbeef};auto& code=isVertex?v:p;failAllocation=int(fail);const bool ok=isVertex?S::vertex(code.data(),code.size(),out):S::pixel(code.data(),code.size(),out);failAllocation=-1;if(!ok)assert(out==std::vector<Word>{0xdeadbeef});}
    for(unsigned trial=0;trial<20000;++trial){auto code=v;random=random*1664525u+1013904223u;const auto index=random%code.size();random=random*1664525u+1013904223u;code[index]^=1u<<(random%32);std::vector<Word> out{0xdeadbeef};if(!S::shadowVertex(code.data(),code.size(),out))assert(out==std::vector<Word>{0xdeadbeef});}
    for(unsigned fail=0;fail<40;++fail){std::vector<Word> out{0xdeadbeef};failAllocation=int(fail);const bool ok=S::shadowVertex(v.data(),v.size(),out);failAllocation=-1;if(!ok)assert(out==std::vector<Word>{0xdeadbeef});}
    std::puts("SHADOW_SLICE_SAFETY_PASS fuzz=20000 allocationFailures=40 requiredSemantics=checked declarationRGBA=preserved outputAlphaOnly=1 idempotent=1");
    unsigned real=0,shadowReal=0;for(int i=1;i<argc;++i){auto code=load(argv[i]);std::vector<Word> full,sliced;
        if(NorthlightShadowShader::patch(code.data(),code.size(),full)&&S::shadowVertex(full.data(),full.size(),sliced)){
            assert(sliced.size()<=full.size());++shadowReal;std::printf("SHADOW_SLICE_CLIENT fullWords=%zu slicedWords=%zu\n",full.size(),sliced.size());}
        if(!NorthlightModelNormal::vertex(code.data(),code.size(),full))continue;
        if(S::vertex(full.data(),full.size(),sliced)){assert(sliced.size()<=full.size());++real;std::printf("MODEL_SLICE_CLIENT fullWords=%zu slicedWords=%zu\n",full.size(),sliced.size());}}
    assert(real>=3&&shadowReal>=3);std::printf("MODEL_SLICE_PASS fuzz=20000 accepted=%u allocationFailures=80 relativePalette=preserved realPrograms=%u shadowPrograms=%u rejectionOutput=unchanged\n",accepted,real,shadowReal);
}
