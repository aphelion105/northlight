#pragma once
#include <cstdint>
#include <cstring>

// Read-only evidence for the fixed-base 3.3.5 client; unknown builds stay unknown.
namespace NorthlightClientLatency {
struct State {bool verified=false;int runtime=-1,configured=-1;};
inline constexpr unsigned char Signature[]={
    0x55,0x8b,0xec,0x8b,0x45,0x10,0x50,0xe8,0x24,0x5a,0,0,0x85,0xc0,0x0f,0x95,0xc1,
    0x6a,0,0x68,0xe0,0x88,0xa3,0,0x88,0x0d,0xde,0xbc,0xca,0,0xe8,0xad,0xbb,0xff,0xff,
    0x83,0xc4,8,0xb0,1,0x5d,0xc3};
template<class Read> State read(std::uintptr_t imageBase,Read&& copy){
    State out;unsigned char actual[sizeof Signature];
    if(imageBase!=0x400000||!copy(0x7696a0,actual,sizeof actual)||std::memcmp(actual,Signature,sizeof actual))return out;
    out.verified=true;unsigned char enabled=2;
    if(copy(0xcabcde,&enabled,sizeof enabled)&&enabled<=1)out.runtime=enabled;
    std::uint32_t cvar=0;int value=-1;
    if(copy(0xcabac0,&cvar,sizeof cvar)&&cvar>=0x10000&&cvar<=0xffffffcf&&
       copy(std::uintptr_t(cvar)+0x30,&value,sizeof value)&&(value==0||value==1))out.configured=value;
    return out;
}
}
