#include "renderer_assets.h"
#include "renderer_asset_manifest.h"
#include "backend_loader.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include "resource_array_checks.h"

static std::string hash(const std::vector<unsigned char>& bytes){
    NorthlightBackendLoader::Sha256 h;h.update(bytes.data(),bytes.size());return h.hex();
}
int main(){
    using namespace NorthlightAssets;
    std::ifstream file("northlight-assets.bin",std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    assert(bytes.size()==PackBytes);
    std::vector<std::uint32_t> decoded;std::string error;
    assert(decode(bytes.data(),bytes.size(),hash(bytes).c_str(),RawBytes,decoded,error));
    std::ofstream("decoded.bin",std::ios::binary).write(reinterpret_cast<const char*>(decoded.data()),RawBytes);
    auto reject=[&](std::vector<unsigned char> bad,std::size_t rawSize,const char* expected=nullptr){
        std::vector<std::uint32_t> kept{0x12345678};
        assert(!decode(bad.data(),bad.size(),expected?expected:hash(bad).c_str(),rawSize,kept,error));
        assert(!error.empty()&&kept==std::vector<std::uint32_t>{0x12345678});
    };
    reject(bytes,RawBytes,"wrong hash");reject(bytes,RawBytes+4);reject(bytes,32u<<20);
    reject({},RawBytes);reject(std::vector<unsigned char>(bytes.begin(),bytes.begin()+8),RawBytes);
    auto bad=bytes;bad.pop_back();reject(bad,RawBytes);
    bad=bytes;bad.push_back(0);reject(bad,RawBytes);
    bad=bytes;bad[8]^=255;reject(bad,RawBytes);
    bad=bytes;bad.back()^=1;reject(bad,RawBytes);
    assert(!initialize("missing-assets.bin",error)&&!data);
    assert(initialize("northlight-assets.bin",error)&&data);
    checkArrays(reinterpret_cast<const unsigned char*>(decoded.data()));
    std::puts("PASS production decoder: exact arrays, malformed inputs, bounded output, missing-file failure");
}
