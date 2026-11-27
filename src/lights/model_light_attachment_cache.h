#pragma once
#include <filesystem>
#include "world_local_lights.h"
#include <array>
#include <memory>

namespace NorthlightLightAttachments {
inline constexpr uint32_t NativeSource=1;
struct Attachment {
    uint64_t sourceId=0;uint32_t bone=0,reference=0;
    std::array<float,9> witness{};
    float point[3]{},offset[3]{},placement[12]{};uint32_t flags=0;
};
static_assert(sizeof(Attachment)==128,"FGA1 record layout");
using Records=std::vector<Attachment>;
inline bool readFile(const std::string& path,Records& output){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);if(!file)return false;
    const auto length=file.tellg();if(length<16)return false;file.seekg(0);
    char magic[4];uint32_t header[3];file.read(magic,4);file.read(reinterpret_cast<char*>(header),12);
    if(!file||std::memcmp(magic,"FGA1",4)||(header[0]!=1&&header[0]!=2)||header[1]!=sizeof(Attachment)||header[2]>1000000||
       uint64_t(length)!=16+uint64_t(header[2])*sizeof(Attachment))return false;
    Records records(header[2]);if(!records.empty())file.read(reinterpret_cast<char*>(records.data()),records.size()*sizeof(Attachment));
    if(!file)return false;
    for(size_t i=0;i<records.size();++i){const auto& a=records[i];if((a.flags&~NativeSource)||(header[0]==1&&a.flags)||
            (a.flags&NativeSource?(a.bone>=64||a.reference):a.bone>=75||a.reference>=75))return false;
        for(float v:a.witness)if(!std::isfinite(v)||std::fabs(v)>=100000)return false;
        for(const auto* values:{a.point,a.offset,a.placement}){const unsigned count=values==a.placement?12:3;
            for(unsigned k=0;k<count;++k)if(!std::isfinite(values[k])||std::fabs(values[k])>=100000)return false;}
        if(i&&a.witness<records[i-1].witness)return false;
    }
    output.swap(records);return true;
}
class Cache {
    std::string root_,map_;std::shared_ptr<const Records> records_;
public:
    explicit Cache(std::string root):root_(std::move(root)){}
    std::shared_ptr<const Records> load(const std::string& map){
        if(!NorthlightLocalLights::safeMap(map))return {};
        if(map!=map_){Records candidate;readFile(root_+"/"+map+".fga",candidate);
            records_=std::make_shared<const Records>(std::move(candidate));map_=map;}
        return records_;
    }
};
} // namespace NorthlightLightAttachments
