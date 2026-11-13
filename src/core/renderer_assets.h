#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace NorthlightAssets {
// Published once, before Factory is constructed. Immutable until DLL unload.
extern const unsigned char* data;
bool initialize(const std::filesystem::path& path,std::string& error);
const char* identity();
bool decode(const unsigned char* bytes,std::size_t size,const char* sha256,std::size_t rawSize,
            std::vector<std::uint32_t>& result,std::string& error);
template<class T,std::size_t N> inline const T (&array(std::size_t offset))[N]{
    static_assert(alignof(T)<=4);
    if(!data)throw std::logic_error("Northlight resources were not initialized");
    return *reinterpret_cast<const T(*)[N]>(data+offset);
}
}
