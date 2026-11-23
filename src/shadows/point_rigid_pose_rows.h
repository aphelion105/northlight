#pragma once
#include "shader_read_set.h"
#include <array>
#include <cstring>

namespace NorthlightPointShadow {

// Exact float reads of the patched vertex program. Cube projection kind 2
// overwrites c2..c5. Unsupported programs retain full pose comparison.
class RigidPoseRows {
    std::array<std::uint8_t,32> registers_{};
    unsigned count_=0;
    bool valid_=false;

public:
    static RigidPoseRows fromProgram(const std::uint32_t* words,std::size_t size){
        auto reads=NorthlightShaderReads::scan(words,size);
        if(!reads.exact||!reads.vertex||reads.i||reads.b||reads.sampledSamplers)return {};
        reads.f[0]&=~(15ull<<2);
        RigidPoseRows result;
        for(unsigned bank=0;bank<4;++bank){
            auto mask=reads.f[bank];
            while(mask){
                if(result.count_==result.registers_.size())return {};
                result.registers_[result.count_++]=static_cast<std::uint8_t>(bank*64+__builtin_ctzll(mask));
                mask&=mask-1;
            }
        }
        result.valid_=true;
        return result;
    }

    template<class Replay> bool sameBanks(const Replay& first,const Replay& second)const{
        if(!valid_)return false;
        const auto a=first.constantUsage.floats,b=second.constantUsage.floats;
        if(a.first>2||b.first>2||a.count<6-a.first||b.count<6-b.first||
           a.count>256-a.first||b.count>256-b.first)return false;
        for(unsigned i=0;i<count_;++i){
            const unsigned reg=registers_[i];
            if(reg<a.first||reg>=a.first+a.count||reg<b.first||reg>=b.first+b.count||
               std::memcmp(first.constants+4*reg,second.constants+4*reg,4*sizeof(float)))return false;
        }
        return true;
    }

    template<class Replay,class Visit> bool visitBank(const Replay& replay,const Visit& visit)const{
        const auto range=replay.constantUsage.floats;
        if(!valid_||range.first>2||range.count<6-range.first||range.count>256-range.first)return false;
        for(unsigned i=0;i<count_;++i){
            const unsigned reg=registers_[i];
            if(reg<range.first||reg>=range.first+range.count||!visit(replay.constants+4*reg))return false;
        }
        return true;
    }
};

} // namespace NorthlightPointShadow
