#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>
#include "patch_shadow_shader.h"

// Water mask variant of an original liquid shader, built at shader creation from
// the bytecode the game supplies (0.3.161; the DLL carries no shader bytes).
// A word-for-word port of patch() in renderer/water_shader_patch.py, which
// stays the reference oracle: the position output (VS) or oC0 (PS) is renamed
// to a spare temporary, two MOVs are appended before END and one DCL follows
// the last original DCL. Same rejection set; tests/test_water_shader_patch.py
// cross-checks both. Pure: no D3D, no logging. The caller must also match the
// result against the generated patched hash (water_shader_identities.h).
// 0.3.214 (water MRT): patchMrt() is the pixel variant of the game's OWN water draw (the mask goes
// out through render target 1, no second draw): the same analysis and renaming, then oC0 = the
// original colour and oC1 = (cover*distance, cover, cover, cover), cover = alpha > .003. Its
// safety rests on the mask variant's recorded hash: see verifyMrt(). resolveShader() is the
// ps_2_0 that decodes the multisample-averaged oC1 data back into the mask the consumers read.
namespace NorthlightWaterShaderPatch {
using Word=std::uint32_t;
using NorthlightShadowShader::regType;
using NorthlightShadowShader::regIndex;
using NorthlightShadowShader::replaceReg;
inline int arity(unsigned op,unsigned major) {
    if(op==37)return major==3?2:4;
    switch(op) {
    case 0:case 29:return 0;
    case 65:return 1;
    case 1:case 6:case 7:case 14:case 15:case 16:case 19:case 27:case 35:case 36:case 46:case 78:case 79:case 91:case 92:return 2;
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:case 13:case 17:case 20:case 21:case 22:case 23:case 24:
    case 32:case 33:case 66:case 89:case 94:case 95:return 3;
    case 4:case 18:case 34:case 88:case 90:return 4;
    case 93:return 5;
    default:return -1;
    }
}
inline Word dst(unsigned type,unsigned index,unsigned mask=15) {return replaceReg(0x80000000u|(mask<<16),type,index);}
inline Word src(unsigned type,unsigned index,unsigned swizzle=0xe4) {return replaceReg(0x80000000u|(swizzle<<16),type,index);}
inline std::uint64_t fnv1a64(const Word* words,std::size_t count) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(words);
    std::uint64_t h=14695981039346656037ULL;
    for(std::size_t i=0;i<count*4;++i)h=(h^bytes[i])*1099511628211ULL;
    return h;
}
// What patch() and patchMrt() share: the validated program with the position/oC0 writes renamed to
// the spare temporary, and where things go. Python: _analyse() in water_shader_patch.py.
struct Analysis {
    std::vector<Word> out;std::size_t end=0,declEnd=1,first=1;unsigned major=0,posType=0,posIndex=0,spare=0,kind=0,tex=0,writes=0;bool vertex=false,relative=false;
    std::uint64_t used[32]={};
    bool isUsed(unsigned type,unsigned index)const{return index<64&&(used[type]>>index&1);}
};
// Returns nullptr and fills a on success, else the Python ValueError text.
inline const char* analyse(const Word* w,std::size_t count,Analysis& a) {
    if(!w||!count)return "empty";
    if(w[0]!=0xfffe0200u&&w[0]!=0xfffe0300u&&w[0]!=0xffff0200u&&w[0]!=0xffff0300u)return "unsupported shader model";
    struct Op {std::size_t p;unsigned op;std::size_t n;};
    std::vector<Op> ops;std::size_t end=0;
    for(std::size_t p=1;;) {
        if(p>=count)return "missing END";
        const unsigned op=w[p]&65535;
        if(op==65535) {
            if(w[p]!=65535||p+1!=count)return "bad END";
            end=p;break;
        }
        const std::size_t n=op==65534?(w[p]>>16)&32767:(w[p]>>24)&15;
        if(p+n>=count)return "truncated";
        ops.push_back({p,op,n});p+=n+1;
    }
    const unsigned major=(w[0]>>8)&255;const bool vertex=(w[0]>>16)==65534;
    // Only indices below 32 decide the spare temporary and varying.
    std::uint64_t* used=a.used;
    auto use=[&](unsigned type,unsigned index){if(index<64)used[type]|=std::uint64_t(1)<<index;};
    bool relative=false;
    bool havePos=vertex?major==2:true;
    unsigned posType=vertex?4:8,posIndex=0;
    for(const Op& o:ops)
        if(o.op==31&&vertex&&major==3) {
            if(o.n<1)return "bad DCL";
            if((w[o.p+1]&15)==0) {
                if(o.n<2)return "bad DCL";
                posType=regType(w[o.p+2]);posIndex=regIndex(w[o.p+2]);havePos=true;
            }
        }
    if(!havePos)return "no position";
    std::vector<std::size_t> params;
    int depth=0;unsigned mask=0,writes=0;std::size_t declEnd=1;
    for(const Op& o:ops) {
        const std::size_t p=o.p;const Word* args=w+p+1;
        if(o.op==65534)continue;
        if(w[p]&0xf0000000u)return "predicated/coissued";
        if(o.op==31) {
            if(o.n!=2)return "bad DCL";
            if((args[0]&15)==5&&((args[0]>>16)&15)==7)return "TEXCOORD7 occupied";
            use(regType(args[1]),regIndex(args[1]));declEnd=p+3;continue;
        }
        if(o.op==81||o.op==48||o.op==47) {
            if(o.op==81&&o.n)use(regType(args[0]),regIndex(args[0])); // a def'd constant: not free for patchMrt()
            continue;
        }
        const int formal=arity(o.op,major);
        if(formal<0)return "unsupported control/opcode";
        if(o.op==27)++depth;
        if(o.op==29&&--depth<0)return "unbalanced LOOP";
        std::size_t q=0;
        for(int operand=0;operand<formal;++operand) {
            if(q>=o.n)return "operand count";
            const Word t=args[q];const unsigned k=regType(t),i=regIndex(t);
            if(!(t&0x80000000u))return "parameter marker";
            use(k,i);params.push_back(p+1+q);
            if(operand==0&&o.op!=27&&o.op!=65) {
                if(k==posType&&i==posIndex) {
                    if(depth)return "conditional output";
                    mask|=(t>>16)&15;++writes;
                } else if(!vertex&&(k==8||k==9))return "MRT/depth write";
                if(t&0x2000)return "relative dest";
            }
            if(operand==2&&o.op>=20&&o.op<=24) {
                const unsigned rows=o.op==20||o.op==22?4:o.op==24?2:3;
                for(unsigned r=0;r<rows;++r)use(k,i+r);
            }
            ++q;
            if(t&0x2000) {
                relative=true;
                if(q>=o.n)return "bad relative";
                const unsigned rk=regType(args[q]);
                if(rk!=3&&rk!=15)return "bad relative";
                use(rk,regIndex(args[q]));++q;
            }
        }
        if(q!=o.n)return "extra operands";
    }
    if(depth||mask!=15)return "incomplete output";
    const unsigned limit=major==3?32:12;
    unsigned spare=0;while(spare<limit&&a.isUsed(0,spare))++spare;
    if(spare==limit)return "no free temporary";
    unsigned kind=6,tex=7;
    if(vertex) {
        if(major!=2) {tex=0;while(tex<12&&a.isUsed(6,tex))++tex;if(tex==12)return "no free varying";}
    } else {
        kind=major==2?3:1;
        if(major!=2) {tex=0;while(tex<10&&a.isUsed(1,tex))++tex;if(tex==10)return "no free varying";}
    }
    if(a.isUsed(kind,tex))return "no free varying";
    a.out.assign(w,w+count);
    for(std::size_t p:params)
        if(regType(a.out[p])==posType&&regIndex(a.out[p])==posIndex)a.out[p]=replaceReg(a.out[p],0,spare);
    std::size_t first=1; // after the version and the leading comments/defs: where a def goes
    while(first<count) {
        const unsigned op=w[first]&65535;
        if(op==65534)first+=1+((w[first]>>16)&32767);
        else if(op==81||op==48||op==47)first+=1+((w[first]>>24)&15);
        else break;
    }
    a.end=end;a.declEnd=declEnd;a.first=first;a.major=major;a.vertex=vertex;a.posType=posType;a.posIndex=posIndex;a.spare=spare;a.kind=kind;a.tex=tex;a.writes=writes;a.relative=relative;
    return nullptr;
}
// Returns nullptr and fills output on success, else the Python ValueError text.
inline const char* patch(const Word* w,std::size_t count,std::vector<Word>& output) {
    Analysis a;if(const char* why=analyse(w,count,a))return why;
    const bool vertex=a.vertex;
    const Word suffix[]={0x02000001,dst(a.posType,a.posIndex),src(0,a.spare,vertex?0xe4:0xff),0x02000001,
        vertex?dst(a.kind,a.tex):dst(8,0,1),vertex?src(0,a.spare):src(a.kind,a.tex,0xff),0x0000ffff};
    std::vector<Word>& out=a.out;
    out.erase(out.begin()+std::ptrdiff_t(a.end));
    out.insert(out.end(),suffix,suffix+7);
    if(a.major==3||!vertex) {
        const Word decl[]={0x0200001f,a.major==3?0x80070005u:0x80000000u,dst(a.kind,a.tex)};
        out.insert(out.begin()+std::ptrdiff_t(a.declEnd),decl,decl+3);
    }
    output.swap(out);return nullptr;
}
constexpr float kMrtCover=0.003f; // the consumers' coverage threshold: they test mask.g > .003
// Words patchMrt() adds to patch()'s result for a pixel shader: 6 (def) - 7 (the mask suffix) + 18 (the MRT suffix).
constexpr std::size_t kMrtExtraWords=17;
inline Word floatBits(float f){Word b;std::memcpy(&b,&f,4);return b;}
// The pixel variant for the game's own water draw. Same rejections as patch(), plus: pixel shaders only,
// no relative addressing (a def'd constant could be read through it), a free constant register below
// c64 (c32 for ps_2_0) and at least one DCL. Python: patch_mrt().
inline const char* patchMrt(const Word* w,std::size_t count,std::vector<Word>& output) {
    Analysis a;if(const char* why=analyse(w,count,a))return why;
    if(a.vertex)return "vertex shaders keep the mask variant";
    if(a.relative)return "relative addressing";
    if(a.declEnd==1)return "no declarations";
    unsigned constant=0;const unsigned limit=a.major==3?64:32;
    while(constant<limit&&a.isUsed(2,constant))++constant;
    if(constant==limit)return "no free constant";
    const unsigned spare=a.spare,kind=a.kind,tex=a.tex;
    // K = (.003, 0, 1, 0). spare.x = .003 - alpha is negative exactly when alpha > .003; cmp keeps its second
    // operand where the first is >= 0 (no cover) and its third where it is negative.
    const Word suffix[]={0x02000001,dst(8,0),src(0,spare),                                                // mov oC0, spare
        0x03000003,dst(0,spare,1),src(2,constant,0x00),src(0,spare,0xff),                                 // sub spare.x, K.xxxx, spare.wwww
        0x04000058,dst(8,1,1),src(0,spare,0x00),src(2,constant,0x55),src(kind,tex,0xff),                  // cmp oC1.x, spare.xxxx, K.yyyy, varying.wwww
        0x04000058,dst(8,1,14),src(0,spare,0x00),src(2,constant,0x55),src(2,constant,0xaa),               // cmp oC1.yzw, spare.xxxx, K.yyyy, K.zzzz
        0x0000ffff};
    std::vector<Word>& out=a.out;
    out.erase(out.begin()+std::ptrdiff_t(a.end));
    out.insert(out.end(),suffix,suffix+std::size(suffix));
    const Word decl[]={0x0200001f,a.major==3?0x80070005u:0x80000000u,dst(kind,tex)};
    out.insert(out.begin()+std::ptrdiff_t(a.declEnd),decl,decl+3);
    const Word def[]={0x05000051,dst(2,constant),floatBits(kMrtCover),floatBits(0.f),floatBits(1.f),floatBits(0.f)};
    out.insert(out.begin()+std::ptrdiff_t(a.first),def,def+6);
    output.swap(out);return nullptr;
}
// The runtime identity check of an MRT variant (see the scheme in water_renderer.h): `mask` is patch() of
// the same original, already proven against its recorded hash, so the original bytes are the known shader.
// The MRT variant must be exactly that proven program with (1) one def inserted after the leading comments
// and defs, and (2) the two-MOV mask suffix replaced by the MRT suffix. Anything else is a transform bug.
inline bool verifyMrt(const std::vector<Word>& mask,const std::vector<Word>& mrt) {
    if(mrt.size()!=mask.size()+kMrtExtraWords||mask.size()<8||mrt.size()<8)return false;
    if(mrt.back()!=0x0000ffff||mask.back()!=0x0000ffff||mrt[0]!=mask[0])return false;
    std::size_t first=1; // the def goes where the mask variant's leading comments/defs end
    while(first<mask.size()) {
        const unsigned op=mask[first]&65535;
        if(op==65534)first+=1+((mask[first]>>16)&32767);
        else if(op==81||op==48||op==47)first+=1+((mask[first]>>24)&15);
        else break;
    }
    if(first+6>mrt.size()||mrt[first]!=0x05000051)return false;
    const std::size_t bodyEnd=mask.size()-7; // the mask suffix (two MOVs and END) is 7 words
    // mask[0..first) | def (6) | mask[first..bodyEnd) | MRT suffix (18)
    for(std::size_t i=0;i<first;++i)if(mrt[i]!=mask[i])return false;
    for(std::size_t i=first;i<bodyEnd;++i)if(mrt[i+6]!=mask[i])return false;
    return mrt.size()==bodyEnd+6+18;
}
// ps_2_0 that decodes the multisample-resolved render target 1 back into the mask; Python: resolve_shader().
// s0 = the resolved data (point), r0.x = mean(cover*distance), r0.y = mean cover. Less than half of the samples
// covered: discard (the mask keeps its cleared 0); else out = (r0.x/r0.y, 1).
inline std::vector<Word> resolveShader() {
    return {0xffff0200,
        0x05000051,dst(2,0),floatBits(.5f),floatBits(1.f),floatBits(0.f),floatBits(0.f),   // def c0, .5, 1, 0, 0
        0x0200001f,0x80000000,dst(3,0,3),                                                  // dcl t0
        0x0200001f,0x90000000,dst(10,0),                                                   // dcl_2d s0
        0x03000042,dst(0,0),src(3,0,0x04),src(10,0),                                       // texld r0, t0, s0
        0x03000002,dst(0,1),src(0,0,0x55),src(2,0,0x00)|0x01000000u,                       // add r1, r0.yyyy, -c0.xxxx
        0x01000041,dst(0,1),                                                               // texkill r1
        0x02000006,dst(0,2,1),src(0,0,0x55),                                               // rcp r2.x, r0.y
        0x03000005,dst(0,0,1),src(0,0,0x00),src(0,2,0x00),                                 // mul r0.x, r0.x, r2.x
        0x02000001,dst(0,0,2),src(2,0,0x55),                                               // mov r0.y, c0.y
        0x02000001,dst(8,0),src(0,0),                                                      // mov oC0, r0
        0x0000ffff};
}
} // namespace NorthlightWaterShaderPatch
