#!/usr/bin/env python3
# northlight-test: requires=cxx
"""task 34: the generated M2 shader family table (src/gamedata/m2_shader_families.h) is sorted by hash, free of
duplicates, every entry resolves to its own family through m2ShaderFamily() and unknown hashes give 0 (native
build); the table's key is the hash Device::CreateVertexShader computes (NorthlightShaderTags::fnv1a over the
bytecode bytes) equal to the extractor's fnv1a64. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import struct,subprocess,tempfile
from extract_signatures import fnv1a64
HERE=Path(__file__).resolve().parent
CPP=r'''
#include <cstdio>
#include "shader_tags.h"
#include "m2_shader_families.h"
int main(){
    int bad=0;
    for(std::size_t i=0;i<kM2ShaderFamilyCount;++i){
        const auto& e=kM2ShaderFamilies[i];
        if(i&&!(kM2ShaderFamilies[i-1].hash<e.hash)){std::printf("not strictly sorted at %zu\n",i);++bad;}
        if(e.family<1||e.family>5){std::printf("bad family at %zu\n",i);++bad;}
        if(m2ShaderFamily(e.hash)!=e.family){std::printf("lookup failed at %zu\n",i);++bad;}
        if(m2ShaderFamily(e.hash+1)!=0&&(i+1>=kM2ShaderFamilyCount||kM2ShaderFamilies[i+1].hash!=e.hash+1)){std::printf("neighbour matched at %zu\n",i);++bad;}
        if(m2ShaderFamily(e.hash-1)!=0&&(i==0||kM2ShaderFamilies[i-1].hash!=e.hash-1)){std::printf("neighbour matched at %zu\n",i);++bad;}
    }
    if(kM2ShaderFamilyCount<100){std::printf("table too small\n");++bad;}
    if(m2ShaderFamily(UINT64_C(0x2b989ec35d3738f5))!=NorthlightM2Family::kRipples){std::printf("WaterRipples vs_3_0 not family 5\n");++bad;}
    if(m2ShaderFamily(0)!=0||m2ShaderFamily(~0ULL)!=0||m2ShaderFamily(0x0123456789abcdefULL)!=0){std::printf("unknown hash matched\n");++bad;}
    unsigned counts[6]={};for(std::size_t i=0;i<kM2ShaderFamilyCount;++i)++counts[kM2ShaderFamilies[i].family];
    for(unsigned f=1;f<=5;++f)if(!counts[f]){std::printf("empty family %u\n",f);++bad;}
    static_assert(m2ShaderFamily(kM2ShaderFamilies[0].hash)==kM2ShaderFamilies[0].family,"constexpr lookup");
    const unsigned words[]={0xfffe0300u,0x0200001fu,0x80000000u,0x900f0000u,0x0000ffffu};
    std::printf("fnv %016llx\n",(unsigned long long)NorthlightShaderTags::fnv1a(words,sizeof words));
    std::printf("entries %zu bad %d\n",kM2ShaderFamilyCount,bad);
    return bad?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-m2families-') as tmp:
    p=Path(tmp);(p/'t.cpp').write_text(CPP)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-I',str(fp.src('shader_tags.h').parent),'-I',str(fp.GAMEDATA),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
    out=subprocess.run([str(p/'t')],capture_output=True,text=True)
    print(out.stdout,end='')
    assert out.returncode==0,out.stdout
    words=struct.pack('<5I',0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0000ffff)
    assert f'fnv {fnv1a64(words):016x}' in out.stdout.splitlines(),'extractor and runtime hash differ'
print('ok')
