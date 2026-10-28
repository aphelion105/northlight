#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <random>
#include <vector>
int main(){
    using namespace NorthlightFogClouds;
    std::mt19937 rng(2841);
    for(unsigned v=0;v<12;++v){
        std::vector<uint8_t> data(N*N*N);
        for(auto& b:data)b=v==0?0:v==1?255:uint8_t(rng());
        for(unsigned i=0;i<8192;++i){
            const float x=detail::unit(detail::hash(i,1,0,Seed)),y=detail::unit(detail::hash(i,2,0,Seed)),z=detail::unit(detail::hash(i,3,0,Seed));
            for(unsigned scale=0;scale<2;++scale){
                const float a=scale?x*4+.37f:x,b=scale?y*4+.61f:y,c=scale?z*4+.13f:z;
                const float old=OldFogClouds::sample(data.data(),a,b,c),now=sample<true>(data.data(),a,b,c);
                assert(!std::memcmp(&old,&now,sizeof old));
            }
        }
        const auto old=OldFogClouds::quantiles(data.data());const auto now=quantiles(data.data());
        assert(!std::memcmp(&old,&now,sizeof old));
    }
}
