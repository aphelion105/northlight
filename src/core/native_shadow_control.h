#pragma once
#include <cstddef>
#include <cstdint>

// Build 12340's runtime quality is separate from its saved CVar. Zero bypasses
// the native shadow producer before caster preparation and map rendering.
namespace NorthlightNativeShadowControl {
constexpr std::uintptr_t Quality=0xd43154,Dirty=0xb1d51c,CVar=0xcd85dc;
struct State {bool readable=false;unsigned runtime=0,configured=0,pending=0;};
template<class Read> State sample(Read read){
    State out;std::uint32_t cvar=0;
    if(!read(CVar,&cvar,4)||cvar<0x10000||cvar>0xffffffcbu||(cvar&3)||
       !read(std::uintptr_t(cvar)+0x30,&out.configured,4)||out.configured>6||
       !read(Quality,&out.runtime,4)||out.runtime>6||!read(Dirty,&out.pending,4)||out.pending>1)return {};
    out.readable=true;return out;
}
template<class Snapshot,class Read> State sampleFrame(Snapshot* snapshot,bool streamed,Read live){
    return sample([&](std::uintptr_t address,void* out,std::size_t size){
        if(!streamed)return live(address,out,size);
        return snapshot&&snapshot->usable()&&snapshot->has(address,size)&&snapshot->serve(address,out,size)==Snapshot::Serve::Ok;
    });
}
class Controller {
    bool leased_=false,retry_=false;unsigned saved_=0,configured_=0;
public:
    bool leased()const{return leased_;}
    // Called on the game thread between frames. A failed transaction retains
    // the lease so an interrupted acquisition can still restore the original.
    template<class Write> bool update(const State& state,bool replace,Write write){
        if(!state.readable)return false;
        if(!leased_){if(!replace||state.pending)return true;saved_=state.runtime;configured_=state.configured;leased_=true;}
        if(configured_!=state.configured){saved_=state.configured;configured_=state.configured;}
        else if(replace&&state.runtime!=0&&!retry_)saved_=state.runtime;
        const unsigned target=replace?0:saved_;
        if(state.runtime!=target||retry_){
            retry_=true;
            if(!write(Quality,target))return false;
            if(!write(Dirty,1)){write(Quality,state.runtime);return false;}
            retry_=false;
        }
        if(!replace)leased_=false;
        return true;
    }
};
} // namespace NorthlightNativeShadowControl
