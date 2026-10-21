#include "client_latency_diagnostics.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
int main(){
    using namespace NorthlightClientLatency;
    std::map<std::uintptr_t,std::vector<unsigned char>> memory;
    memory[0x7696a0]={Signature,Signature+sizeof Signature};memory[0xcabcde]={1};
    auto put=[&](std::uintptr_t at,const auto& value){auto* p=reinterpret_cast<const unsigned char*>(&value);memory[at]={p,p+sizeof value};};
    std::uint32_t pointer=0x20000;put(0xcabac0,pointer);int configured=0;put(pointer+0x30,configured);
    unsigned copies=0;auto copy=[&](std::uintptr_t at,void* out,std::size_t bytes){++copies;auto it=memory.find(at);if(it==memory.end()||it->second.size()!=bytes)return false;std::memcpy(out,it->second.data(),bytes);return true;};
    auto state=read(0x400000,copy);assert(state.verified&&state.runtime==1&&state.configured==0);
    memory[0xcabcde][0]=0;configured=1;put(pointer+0x30,configured);state=read(0x400000,copy);assert(state.verified&&state.runtime==0&&state.configured==1);
    memory[0xcabcde][0]=2;configured=42;put(pointer+0x30,configured);state=read(0x400000,copy);assert(state.verified&&state.runtime==-1&&state.configured==-1);
    for(std::uint32_t invalid:{0u,0xffffu,0xfffffff0u}){put(0xcabac0,invalid);const auto before=copies;state=read(0x400000,copy);assert(state.verified&&state.configured==-1&&copies-before==3);}
    put(0xcabac0,pointer);memory.erase(pointer+0x30);state=read(0x400000,copy);assert(state.verified&&state.configured==-1);
    const auto count=copies;state=read(0x500000,copy);assert(!state.verified&&state.runtime==-1&&state.configured==-1&&copies==count);
    for(std::size_t i=0;i<sizeof Signature;++i){memory[0x7696a0][i]^=1;state=read(0x400000,copy);assert(!state.verified&&state.runtime==-1&&state.configured==-1);memory[0x7696a0][i]^=1;}
    memory.erase(0x7696a0);state=read(0x400000,copy);assert(!state.verified);
    std::puts("PASS latency read: exact signature/base gates, configured/runtime mismatch, missing reads, invalid pointers and invalid values");
}
