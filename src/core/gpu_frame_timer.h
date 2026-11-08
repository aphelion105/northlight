#pragma once
#include <d3d9.h>
#include <cstdint>
#include <array>
#include <limits>

// 0.3.200 (gpu budget): Northlight's own GPU time on every frame (GpuBudgetMs > 0), for gpu_budget.h. Two timestamps around the effects in a
// small ring; read back several frames later. Include after the renderer's logf(const char*, ...). Render thread only. Like
// NorthlightGpuProfile it never flushes or waits: every GetData uses flags=0 and is tried once per poll; a full ring skips the frame's
// measurement, a disjoint, zero-frequency or nonmonotonic result is dropped. Any device failure disables the timer (the level then stays where
// it is; the image is never affected). The device is borrowed: reset() before the device is Reset, destroy before it is released.
class NorthlightGpuFrameTimer {
    static constexpr unsigned RingSize=4;
    static constexpr uint64_t NoFrame=std::numeric_limits<uint64_t>::max();
public:
    enum class SampleState : unsigned char {Unavailable,Pending,Ready,Dropped,Disjoint,Invalid,Disabled};
    struct Sample {uint64_t frame=NoFrame;double ms=-1;SampleState state=SampleState::Unavailable;};
    // FRAME slow retains 120 frames. 64 entries would lose frame 628 before
    // its report at 720; 128 also leaves room for the four pending GPU slots.
    static constexpr unsigned HistorySize=128;
    static const char* stateName(SampleState state){switch(state){
        case SampleState::Pending:return "pending";case SampleState::Ready:return "ready";case SampleState::Dropped:return "ring-full";
        case SampleState::Disjoint:return "disjoint";case SampleState::Invalid:return "invalid";case SampleState::Disabled:return "disabled";
        default:return "unavailable";}}
private:
    struct Slot {IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;uint64_t serial=0,frequencyValue=0,startValue=0,frame=NoFrame;unsigned readStage=0;bool pending=false;};
    IDirect3DDevice9* device_=nullptr;
    Slot slots_[RingSize];
    int active_=-1;
    uint64_t serial_=0;
    bool disabled_=false;
    std::array<Sample,HistorySize> history_;
    void remember(uint64_t frame,SampleState state,double ms=-1){
        if(frame==NoFrame)return;
        auto& sample=history_[frame%HistorySize];
        // A very late GPU result must not replace a newer frame in this slot.
        if(sample.frame!=NoFrame&&sample.frame>frame)return;
        sample={frame,ms,state};
    }
    static void release(IDirect3DQuery9*& q){if(q){q->Release();q=nullptr;}}
    void releaseAll(){active_=-1;for(auto& s:slots_){release(s.start);release(s.end);release(s.frequency);release(s.disjoint);s.pending=false;}}
    void disable(const char* operation,HRESULT hr){
        if(!disabled_)logf("GPUBUDGET timer disabled: %s HRESULT=0x%08lx (rendering unaffected)",operation,static_cast<unsigned long>(static_cast<uint32_t>(hr)));
        for(unsigned i=0;i<RingSize;++i)if(slots_[i].pending||active_==int(i))remember(slots_[i].frame,SampleState::Disabled);
        disabled_=true;releaseAll();}
    bool create(D3DQUERYTYPE type,IDirect3DQuery9** out){
        HRESULT hr=device_->CreateQuery(type,out);
        if(FAILED(hr)||!*out){*out=nullptr;disable("query creation",FAILED(hr)?hr:E_FAIL);return false;}
        return true;}
    bool prepare(Slot& s){
        if(s.disjoint)return true;
        return create(D3DQUERYTYPE_TIMESTAMP,&s.start)&&create(D3DQUERYTYPE_TIMESTAMP,&s.end)&&create(D3DQUERYTYPE_TIMESTAMPFREQ,&s.frequency)&&create(D3DQUERYTYPE_TIMESTAMPDISJOINT,&s.disjoint);}
    bool issue(IDirect3DQuery9* q,DWORD flags,const char* operation){HRESULT hr=q->Issue(flags);if(FAILED(hr)){disable(operation,hr);return false;}return true;}
public:
    explicit NorthlightGpuFrameTimer(IDirect3DDevice9* device):device_(device),disabled_(!device){}
    NorthlightGpuFrameTimer(const NorthlightGpuFrameTimer&)=delete;
    NorthlightGpuFrameTimer& operator=(const NorthlightGpuFrameTimer&)=delete;
    ~NorthlightGpuFrameTimer(){releaseAll();}
    bool disabled()const{return disabled_;}
    Sample sample(uint64_t frame)const{const auto& found=history_[frame%HistorySize];return found.frame==frame?found:Sample{};}
    // At the start of the effects. false: nothing was opened (disabled, or every slot still waits for its result).
    bool begin(uint64_t frame=NoFrame){
        if(disabled_){remember(frame,SampleState::Disabled);return false;}
        if(active_>=0)end();
        for(unsigned i=0;i<RingSize;++i){Slot& s=slots_[i];
            if(s.pending)continue;
            if(!prepare(s)){remember(frame,SampleState::Disabled);return false;}
            if(!issue(s.disjoint,D3DISSUE_BEGIN,"disjoint begin")||!issue(s.start,D3DISSUE_END,"start timestamp")){remember(frame,SampleState::Disabled);return false;}
            s.serial=++serial_;s.frame=frame;active_=int(i);remember(frame,SampleState::Pending);return true;}
        remember(frame,SampleState::Dropped);
        return false;
    }
    // At the end of the effects (every return path: the caller holds a scope guard). No-op without an open begin.
    void end(){
        if(active_<0||disabled_)return;
        Slot& s=slots_[active_];active_=-1;
        if(!issue(s.end,D3DISSUE_END,"end timestamp")||!issue(s.frequency,D3DISSUE_END,"frequency end")||!issue(s.disjoint,D3DISSUE_END,"disjoint end")){remember(s.frame,SampleState::Disabled);return;}
        s.readStage=0;s.pending=true;
    }
    // Once per frame. Tries each pending slot once (flags 0, never D3DGETDATA_FLUSH, no loop until ready); true with the newest completed
    // valid interval in ms. A slot whose result is not there yet stays pending for a later frame.
    bool poll(double& ms){
        if(disabled_)return false;
        bool found=false;uint64_t newest=0;
        for(auto& s:slots_){
            if(!s.pending)continue;
            HRESULT hr;
            if(s.readStage==0){BOOL disjoint=FALSE;hr=s.disjoint->GetData(&disjoint,sizeof disjoint,0);
                if(hr==S_FALSE)continue;
                if(FAILED(hr)){disable("disjoint read",hr);return false;}
                if(disjoint){remember(s.frame,SampleState::Disjoint);s.pending=false;continue;}s.readStage=1;}
            if(s.readStage==1){hr=s.frequency->GetData(&s.frequencyValue,sizeof s.frequencyValue,0);
                if(hr==S_FALSE)continue;
                if(FAILED(hr)){disable("frequency read",hr);return false;}s.readStage=2;}
            if(s.readStage==2){hr=s.start->GetData(&s.startValue,sizeof s.startValue,0);
                if(hr==S_FALSE)continue;
                if(FAILED(hr)){disable("timestamp read",hr);return false;}s.readStage=3;}
            uint64_t t1=0;
            hr=s.end->GetData(&t1,sizeof t1,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("timestamp read",hr);return false;}
            s.pending=false;
            if(!s.frequencyValue||t1<s.startValue){remember(s.frame,SampleState::Invalid);continue;}
            const double interval=double(t1-s.startValue)*1000.0/double(s.frequencyValue);
            remember(s.frame,SampleState::Ready,interval);
            if(!found||s.serial>newest){newest=s.serial;ms=interval;found=true;}
        }
        return found;
    }
    // Before a device Reset: releases the queries; the next begin recreates them (a timer disabled by a failure may retry).
    void reset(){releaseAll();history_={};disabled_=!device_;}
};
