// Runs src/water/water_shader_patch.h over a batch of programs for
// test_water_shader_patch.py (cross-check with Python patch()) and
// test_water_shaders.py (the 36 client originals). Input and output are the same
// little-endian stream: per program a u32 word count, then the words; a rejected
// program is written with count 0. No D3D, game or Wine.
// argv[3] "mrt" (0.3.214, water MRT): patchMrt() instead of patch(), and its result must also pass
// verifyMrt() against patch() of the same program (a failed check aborts). "resolve" as argv[1]:
// writes resolveShader() to argv[2] as bare words.
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "water_shader_patch.h"

int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="resolve"){
        const auto words=NorthlightWaterShaderPatch::resolveShader();FILE* out=std::fopen(argv[2],"wb");if(!out)return 3;
        std::fwrite(words.data(),4,words.size(),out);std::fclose(out);std::printf("water resolve shader: %zu words\n",words.size());return 0;
    }
    if(argc!=3&&argc!=4)return 2;
    const bool mrt=argc==4&&std::string(argv[3])=="mrt";
    FILE* in=std::fopen(argv[1],"rb");FILE* out=std::fopen(argv[2],"wb");if(!in||!out)return 3;
    unsigned programs=0,accepted=0;std::uint32_t count=0;
    while(std::fread(&count,4,1,in)==1){
        std::vector<std::uint32_t> words(count),patched;
        if(count&&std::fread(words.data(),4,count,in)!=count)return 4;
        const char* reason=mrt?NorthlightWaterShaderPatch::patchMrt(words.data(),words.size(),patched):NorthlightWaterShaderPatch::patch(words.data(),words.size(),patched);
        if(mrt&&!reason){ // the runtime identity check: the MRT variant is the mask variant plus a def and the other suffix
            std::vector<std::uint32_t> mask;
            if(NorthlightWaterShaderPatch::patch(words.data(),words.size(),mask)||!NorthlightWaterShaderPatch::verifyMrt(mask,patched)){std::fprintf(stderr,"verifyMrt failed\n");return 5;}
        }
        const std::uint32_t n=reason?0:std::uint32_t(patched.size());
        std::fwrite(&n,4,1,out);if(n)std::fwrite(patched.data(),4,n,out);
        ++programs;if(!reason)++accepted;
    }
    std::fclose(in);std::fclose(out);
    std::printf("water_shader_patch: %u programs, %u accepted\n",programs,accepted);
    return 0;
}
