#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace StaticShadow {

struct InstanceSpan {
    size_t first=0,count=0;

    template<class Instance>
    size_t store(std::vector<Instance>& chunk,const std::vector<Instance>& values,bool share){
        if(share && count && count==values.size() &&
           std::memcmp(chunk.data()+first,values.data(),count*sizeof(Instance))==0)
            return first;
        first=chunk.size();
        count=values.size();
        chunk.insert(chunk.end(),values.begin(),values.end());
        return first;
    }
};

// Commands retain draw order. Multiple sections of a model may reference the
// same transform range, so its physical offset can move backwards.
struct InstanceUploadWindow {
    size_t first=0,limit=0,endCommand=0;

    template<class Commands>
    void select(const Commands& commands,size_t start,size_t capacity,bool streamOffsets){
        const auto& head=commands[start];
        first=head.packedFirst;
        limit=first+head.count;
        endCommand=start+1;
        size_t drawn=head.count;
        while(streamOffsets && endCommand<commands.size()){
            const auto& next=commands[endCommand];
            const size_t low=std::min(first,next.packedFirst);
            const size_t high=std::max(limit,next.packedFirst+next.count);
            drawn+=next.count;
            if(high-low>capacity || high-low>drawn)break;
            first=low;
            limit=high;
            ++endCommand;
        }
    }

    template<class Commands,class Instance>
    void copy(const Commands& commands,size_t start,Instance* destination)const{
        while(start<endCommand){
            const auto& head=commands[start++];
            size_t low=head.packedFirst,high=low+head.count;
            while(start<endCommand && commands[start].resource==head.resource){
                const auto& next=commands[start++];
                low=std::min(low,next.packedFirst);
                high=std::max(high,next.packedFirst+next.count);
            }
            // Each model owns one contiguous chunk, including any intervening spans.
            std::memcpy(destination+(low-first),head.data-(head.packedFirst-low),
                        (high-low)*sizeof(Instance));
        }
    }
};

}
