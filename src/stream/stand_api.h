#pragma once
#ifndef NORTHLIGHT_REPLAY_STAND
#error Stand exports are restricted to the dedicated offline build.
#endif
#include "stand_inputs.h"
#include "snapshot_archive.h"

// Private, same-process host protocol. The caller supplies actual immutable
// file/archive buffers, not addresses inside the recorded client. Open and the
// first snapshot must precede Direct3DCreate9 and all signature-cache readers.
namespace NorthlightStandApi {
struct State {
    std::vector<unsigned char> image;NorthlightStream::ClientImage::View view;
    NorthlightStream::GameSnapshot snapshot;
    std::unique_ptr<NorthlightStream::StandInputs::Session> inputs;
    std::unique_ptr<NorthlightStream::StandInputs::Scope> scope;
    DWORD owner=0;bool apiStarted=false;std::uint64_t snapshots=0;
};
inline State& state(){static State value;return value;}
inline bool owner(const State& s){return s.owner&&s.owner==GetCurrentThreadId();}
inline bool beforeApi(){
    auto& s=state();s.apiStarted=true;
    return owner(s)&&s.inputs&&s.inputs->valid()&&s.scope&&s.scope->entered()&&s.snapshots;
}
}
extern "C" __declspec(dllexport) int __cdecl NorthlightStandOpen(const void* image,unsigned bytes){
    auto& s=NorthlightStandApi::state();
    if(!image||bytes<64||bytes>64u*1024u*1024u||s.apiStarted||s.inputs)return 0;
    try{
        const auto* first=static_cast<const unsigned char*>(image);s.image.assign(first,first+bytes);
        NorthlightStream::ClientImage::Error error;
        if(!s.view.open(s.image.data(),s.image.size(),error)){s.image.clear();return 0;}
        s.inputs=std::make_unique<NorthlightStream::StandInputs::Session>(s.view);
        s.scope=std::make_unique<NorthlightStream::StandInputs::Scope>(*s.inputs);
        if(!s.scope->entered()){s.inputs->reject();return 0;}
        s.owner=GetCurrentThreadId();return 1;
    }catch(...){if(s.inputs)s.inputs->reject();return 0;}
}
extern "C" __declspec(dllexport) int __cdecl NorthlightStandSnapshot(const void* packet,unsigned bytes,std::uint64_t draw,unsigned trigger){
    auto& s=NorthlightStandApi::state();
    if(!NorthlightStandApi::owner(s)||!s.inputs||!s.inputs->valid())return 0;
    NorthlightStream::SnapshotArchive::Error error;
    if(!NorthlightStream::SnapshotArchive::decode(static_cast<const unsigned char*>(packet),bytes,s.snapshot,error)||
       !s.inputs->bind(s.snapshot,draw,trigger)){s.inputs->reject();return 0;}
    ++s.snapshots;return 1;
}
extern "C" __declspec(dllexport) int __cdecl NorthlightStandStatus(std::uint64_t* output,unsigned words){
    auto& s=NorthlightStandApi::state();
    if(!output||words!=8||!NorthlightStandApi::owner(s)||!s.inputs)return 0;
    const std::uint64_t values[8]={1,s.inputs->valid(),s.snapshots,s.inputs->staticReads,s.inputs->dynamicReads,
        s.inputs->recordedFailures,s.inputs->missingReads,s.apiStarted};
    std::memcpy(output,values,sizeof values);return 1;
}
