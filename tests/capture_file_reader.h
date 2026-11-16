#pragma once
#include "backend_loader.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

// Read both formats in GPU oracles; Python independently checks the writer's
// hashes and complete old-capture archive roundtrips.
inline std::vector<unsigned char> captureFileBytes(const std::string& path){
    const auto raw=[](const std::filesystem::path& file){std::ifstream f(file,std::ios::binary);assert(f);return std::vector<unsigned char>{std::istreambuf_iterator<char>(f),{}};};
    auto bytes=raw(std::filesystem::u8path(path));
    if(bytes.size()>=7&&!std::memcmp(bytes.data(),"NLCREF1\n",7)){
        const std::string text(bytes.begin(),bytes.end());char digest[65]={};unsigned long long size=0;
        assert(std::sscanf(text.c_str(),"NLCREF1\n%64[0-9a-f] %llu",digest,&size)==2&&std::strlen(digest)==64);
        bytes=raw(std::filesystem::u8path(path).parent_path()/".capture-blobs"/digest);
        NorthlightBackendLoader::Sha256 hash;hash.update(bytes.data(),bytes.size());assert(bytes.size()==size&&hash.hex()==digest);
    }
    return bytes;
}
