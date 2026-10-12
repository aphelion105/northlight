#pragma once
#include <d3d9.h>
#include <cstdint>
#include <cstdio>
// 0.3.212 (task 32): GPU time of the name mask's duplicate draws. They sit between the game's draws, before the effect interval NorthlightGpuProfile measures, so this brackets each one with its own
// timestamp pair (start before the duplicate's clear/draw, stop after) on sample frames only, inside one disjoint interval, and logs the sum when the results are ready. Include after the renderer's
// logf definition. Render thread only; the device is borrowed. Never waits: GetData with flags 0, a full ring drops the sample, any failure disables it (rendering unaffected).
class NorthlightGpuPairTimer {
    static constexpr unsigned RingSize=3,MaxPairs=48;
    struct Slot {IDirect3DQuery9* stamps[2*MaxPairs]={};IDirect3DQuery9* frequency=nullptr;IDirect3DQuery9* disjoint=nullptr;uint64_t frame=0;unsigned pairs=0;bool pending=false,open=false;};
    IDirect3DDevice9* device_=nullptr;Slot slots_[RingSize];int active_=-1;unsigned next_=0;bool disabled_=false;uint64_t lastFrame_=~0ull;
    static void release(IDirect3DQuery9*& q){if(q){q->Release();q=nullptr;}}
    void releaseAll(){active_=-1;for(auto& s:slots_){for(auto& q:s.stamps)release(q);release(s.frequency);release(s.disjoint);s.pending=s.open=false;s.pairs=0;}}
    void disable(const char* operation,HRESULT hr){if(!disabled_)logf("NAMEMASK GPU timer disabled: %s HRESULT=0x%08lx (rendering unaffected)",operation,static_cast<unsigned long>(static_cast<uint32_t>(hr)));disabled_=true;releaseAll();}
    bool create(D3DQUERYTYPE type,IDirect3DQuery9** out){const HRESULT hr=device_->CreateQuery(type,out);if(FAILED(hr)||!*out){disable("query creation",FAILED(hr)?hr:E_FAIL);return false;}return true;}
    bool prepare(Slot& s){
        if(s.disjoint)return true;
        for(auto& q:s.stamps)if(!create(D3DQUERYTYPE_TIMESTAMP,&q))return false;
        return create(D3DQUERYTYPE_TIMESTAMPFREQ,&s.frequency)&&create(D3DQUERYTYPE_TIMESTAMPDISJOINT,&s.disjoint);
    }
    bool issue(IDirect3DQuery9* q,DWORD flags,const char* operation){const HRESULT hr=q->Issue(flags);if(FAILED(hr)){disable(operation,hr);return false;}return true;}
public:
    explicit NorthlightGpuPairTimer(IDirect3DDevice9* device):device_(device),disabled_(!device){}
    NorthlightGpuPairTimer(const NorthlightGpuPairTimer&)=delete;NorthlightGpuPairTimer& operator=(const NorthlightGpuPairTimer&)=delete;
    ~NorthlightGpuPairTimer(){releaseAll();}
    // Start of one bracketed duplicate on a sample frame (the first one opens the frame's interval). False: not measured; then stop() must not be called.
    bool start(uint64_t frame){
        if(disabled_)return false;
        if(active_<0){
            if(frame==lastFrame_)return false;
            for(unsigned o=0;o<RingSize;++o){const unsigned i=(next_+o)%RingSize;Slot& s=slots_[i];if(s.pending)continue;
                if(!prepare(s)||!issue(s.disjoint,D3DISSUE_BEGIN,"disjoint begin"))return false;
                s.frame=frame;s.pairs=0;s.open=false;active_=int(i);next_=(i+1)%RingSize;lastFrame_=frame;break;}
            if(active_<0)return false;
        }
        Slot& s=slots_[active_];if(s.pairs>=MaxPairs||s.open)return false;
        if(!issue(s.stamps[2*s.pairs],D3DISSUE_END,"start timestamp"))return false;
        s.open=true;return true;
    }
    void stop(){
        if(active_<0||disabled_)return;Slot& s=slots_[active_];if(!s.open)return;
        s.open=false;if(issue(s.stamps[2*s.pairs+1],D3DISSUE_END,"stop timestamp"))++s.pairs;
    }
    void endFrame(){
        if(active_<0||disabled_)return;Slot& s=slots_[active_];
        if(issue(s.frequency,D3DISSUE_END,"frequency end")&&issue(s.disjoint,D3DISSUE_END,"disjoint end")){s.pending=true;active_=-1;}
    }
    void poll(){
        if(disabled_)return;
        for(auto& s:slots_){
            if(!s.pending)continue;
            BOOL disjoint=FALSE;HRESULT hr=s.disjoint->GetData(&disjoint,sizeof disjoint,0);
            if(hr==S_FALSE)continue;if(FAILED(hr)){disable("disjoint read",hr);return;}
            if(disjoint){s.pending=false;continue;}
            uint64_t frequency=0;hr=s.frequency->GetData(&frequency,sizeof frequency,0);
            if(hr==S_FALSE)continue;if(FAILED(hr)){disable("frequency read",hr);return;}
            if(!frequency){s.pending=false;continue;}
            uint64_t ticks=0,worst=0;bool complete=true;
            for(unsigned i=0;i<s.pairs&&complete;++i){
                uint64_t a=0,b=0;
                hr=s.stamps[2*i]->GetData(&a,sizeof a,0);if(hr==S_FALSE){complete=false;break;}if(FAILED(hr)){disable("timestamp read",hr);return;}
                hr=s.stamps[2*i+1]->GetData(&b,sizeof b,0);if(hr==S_FALSE){complete=false;break;}if(FAILED(hr)){disable("timestamp read",hr);return;}
                if(b<a)continue;ticks+=b-a;if(b-a>worst)worst=b-a;
            }
            if(!complete)continue;
            logf("NAMEMASK GPU frame=%llu pairs=%u gpuUs=%.1f maxUs=%.1f",static_cast<unsigned long long>(s.frame),s.pairs,double(ticks)*1e6/double(frequency),double(worst)*1e6/double(frequency));
            s.pending=false;
        }
    }
    void reset(){releaseAll();disabled_=!device_;next_=0;lastFrame_=~0ull;}
};
