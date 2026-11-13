#include "renderer_assets.h"
#include "renderer_asset_manifest.h"
#include "backend_loader.h"
#include <cstdio>
#include <cstring>
#include <memory>

// Only the bounded zlib decoder is compiled; no archive, compressor or zlib API.
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC
#define MINIZ_LITTLE_ENDIAN 1
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_HAS_64BIT_REGISTERS 0
#include "miniz_tinfl.c"

namespace NorthlightAssets {
const unsigned char* data=nullptr;
static std::vector<std::uint32_t> storage;
const char* identity(){return PackIdentity;}

bool decode(const unsigned char* bytes,std::size_t size,const char* sha256,std::size_t rawSize,
            std::vector<std::uint32_t>& result,std::string& error){
    error.clear();
    if(!bytes||size<8||size>(16u<<20)||!rawSize||rawSize>(16u<<20)||rawSize%4||
       std::memcmp(bytes,"NLA1",4)!=0){error="Invalid resource header";return false;}
    const std::uint32_t declared=std::uint32_t(bytes[4])|(std::uint32_t(bytes[5])<<8)|
        (std::uint32_t(bytes[6])<<16)|(std::uint32_t(bytes[7])<<24);
    if(declared!=rawSize){error="Resource size differs from DLL";return false;}
    NorthlightBackendLoader::Sha256 hash;hash.update(bytes,size);
    if(hash.hex()!=sha256){error="Resource SHA-256 differs from DLL";return false;}
    std::vector<std::uint32_t> decoded(rawSize/4);
    tinfl_decompressor state;tinfl_init(&state);
    std::size_t input=size-8,output=rawSize;
    auto* destination=reinterpret_cast<unsigned char*>(decoded.data());
    const auto status=tinfl_decompress(&state,bytes+8,&input,destination,destination,&output,
        TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if(status!=TINFL_STATUS_DONE||input!=size-8||output!=rawSize){error="Invalid compressed resources";return false;}
    result.swap(decoded);return true;
}

bool initialize(const std::filesystem::path& path,std::string& error){
    try{
#ifdef _WIN32
        FILE* opened=_wfopen(path.c_str(),L"rb");
#else
        FILE* opened=std::fopen(path.c_str(),"rb");
#endif
        std::unique_ptr<FILE,decltype(&std::fclose)> file(opened,&std::fclose);
        if(!file){error="Resource file is missing or unreadable";return false;}
        if(std::fseek(file.get(),0,SEEK_END)||std::ftell(file.get())!=long(PackBytes)||std::fseek(file.get(),0,SEEK_SET)){
            error="Resource file size differs from DLL";return false;}
        std::vector<unsigned char> bytes(PackBytes);
        if(std::fread(bytes.data(),1,bytes.size(),file.get())!=bytes.size()){
            error="Cannot read complete resource file";return false;}
        if(!decode(bytes.data(),bytes.size(),PackIdentity+sizeof("NORTHLIGHT_ASSETS_V1:")-1,RawBytes,storage,error))return false;
        data=reinterpret_cast<const unsigned char*>(storage.data());return true;
    }catch(const std::exception&){error="Cannot allocate renderer resources";return false;}
}
}
