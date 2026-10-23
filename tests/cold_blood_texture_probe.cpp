// SPDX-License-Identifier: MIT
// Offline fixture decoder; uses exactly the runtime codec, no D3D or game process.
#include "actor_texture.h"
#include <fstream>
#include <iterator>
#include <string>

int main(int argc,char** argv){
    if(argc!=7)return 2;
    std::ifstream source(argv[1],std::ios::binary);
    if(!source)return 3;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(source)),{}),rgba;
    const std::string name=argv[6];
    using namespace NorthlightActorTexture;
    if(name!="DXT1"&&name!="DXT3"&&name!="DXT5")return 4;
    const auto format=name=="DXT1"?Format::BC1:name=="DXT3"?Format::BC2:Format::BC3;
    if(!decode(bytes.data(),bytes.size(),std::stoul(argv[3]),std::stoul(argv[4]),std::stoul(argv[5]),format,rgba))return 5;
    std::ofstream target(argv[2],std::ios::binary);
    target.write(reinterpret_cast<const char*>(rgba.data()),rgba.size());
    return target?0:6;
}
