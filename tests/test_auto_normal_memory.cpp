#define NOMINMAX
#include <d3d9.h>
#include "auto_normal_memory.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
static std::uint64_t oracle(unsigned width,unsigned height,unsigned levels,unsigned block,unsigned texel){
    if(!width||!height)return 0;
    std::uint64_t bytes=0;unsigned n=0;
    do{
        bytes+=block?((std::uint64_t(width)+3)/4)*((std::uint64_t(height)+3)/4)*block:std::uint64_t(width)*height*texel;
        if((levels&&++n>=levels)||(width==1&&height==1))return bytes;
        width=width>1?width/2:1;height=height>1?height/2:1;
    }while(true);
}
int main(){
    using NorthlightAutoNormalMemory::mipBytes;
    using NorthlightAutoNormalMemory::budget;
    assert(budget(715ull<<20,96ull<<20)==(32ull<<20));
    assert(budget(895ull<<20,96ull<<20)==(32ull<<20));
    assert(budget(950ull<<20,32ull<<20)==(32ull<<20));
    assert(budget(950ull<<20,96ull<<20)==(96ull<<20));
    assert(budget(1024ull<<20,32ull<<20)==(96ull<<20));
    assert(mipBytes(D3DFMT_DXT1,1,1,1)==8);
    assert(mipBytes(D3DFMT_DXT5,5,7,1)==64);
    assert(mipBytes(D3DFMT_DXT1,4,4,0)==24);
    assert(mipBytes(D3DFMT_DXT5,512,512,0)==349552);
    assert(mipBytes(D3DFMT_A8R8G8B8,512,512,0)==1398100);
    assert(mipBytes(D3DFMT_A8R8G8B8,512,512,1)==1048576);
    assert(mipBytes(D3DFMT_A8R8G8B8,3,5,0)==72);
    assert(mipBytes(D3DFMT_A8R8G8B8,0,1,0)==0);
    assert(mipBytes(D3DFMT_A32B32G32R32F,2,2,1)==64);
    struct Format{D3DFORMAT value;unsigned block,texel;};
    const Format formats[]={{D3DFMT_DXT1,8,0},{D3DFMT_DXT2,16,0},{D3DFMT_DXT3,16,0},{D3DFMT_DXT4,16,0},{D3DFMT_DXT5,16,0},
        {D3DFMT_A8,0,1},{D3DFMT_L8,0,1},{D3DFMT_P8,0,1},{D3DFMT_R5G6B5,0,2},{D3DFMT_X1R5G5B5,0,2},{D3DFMT_A1R5G5B5,0,2},
        {D3DFMT_A4R4G4B4,0,2},{D3DFMT_A8L8,0,2},{D3DFMT_V8U8,0,2},{D3DFMT_L16,0,2},{D3DFMT_R8G8B8,0,3},
        {D3DFMT_A8R8G8B8,0,4},{D3DFMT_X8R8G8B8,0,4},{D3DFMT_A8B8G8R8,0,4},{D3DFMT_X8B8G8R8,0,4},{D3DFMT_G16R16,0,4},{D3DFMT_R32F,0,4},
        {D3DFMT_A16B16G16R16,0,8},{D3DFMT_A16B16G16R16F,0,8},{D3DFMT_G32R32F,0,8},{D3DFMT_A32B32G32R32F,0,16},{D3DFMT_UNKNOWN,0,16}};
    unsigned cases=0;
    for(const auto& f:formats)for(unsigned width=1;width<=32768;width*=2)for(unsigned levels=0;levels<=20;++levels){
        assert(mipBytes(f.value,width,width,levels)==oracle(width,width,levels,f.block,f.texel));++cases;
    }
    for(const auto& f:formats)for(unsigned width:{0u,1u,3u,5u,127u,512u,1023u,16385u})for(unsigned height:{0u,1u,2u,7u,64u,513u})for(unsigned levels:{0u,1u,2u,5u,20u}){
        assert(mipBytes(f.value,width,height,levels)==oracle(width,height,levels,f.block,f.texel));++cases;
    }
    const auto dxt1=mipBytes(D3DFMT_DXT1,512,512,0)+mipBytes(D3DFMT_A8R8G8B8,512,512,0);
    const auto dxt5=mipBytes(D3DFMT_DXT5,512,512,0)+mipBytes(D3DFMT_A8R8G8B8,512,512,0);
    assert((32u<<20)/dxt1==21&&(32u<<20)/dxt5==19);
    // Full-size RGBA sources still fit exactly the original conservative capacity.
    assert((32u<<20)/(2*mipBytes(D3DFMT_A8R8G8B8,512,512,0))==12);
    std::printf("AUTO_NORMAL_MEMORY_PASS cases=%u compressed_mips block_rounding partial_chains bounded_capacity\n",cases);
}
