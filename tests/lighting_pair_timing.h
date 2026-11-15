#pragma once
#include <chrono>

// Warm ABBA pairs. Readback and setup are outside both timestamp intervals;
// CPU submission and GPU execution are reported separately, never added.
template<class Setup,class Draw> void lightingPairTiming(IDirect3DDevice9* d,const char* name,unsigned draws,Setup setup,Draw draw){
    const auto check=[](HRESULT hr){assert(SUCCEEDED(hr));};
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
    check(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));check(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));
    check(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));check(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    check(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    const auto drain=[&]{check(event->Issue(D3DISSUE_END));HRESULT hr;const DWORD at=GetTickCount();
        while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-at)<15000);SwitchToThread();}check(hr);};
    const auto batch=[&](unsigned mode){check(d->BeginScene());for(unsigned i=0;i<draws;++i)check(draw(mode));check(d->EndScene());};
    for(unsigned block=0;block<24;++block){const unsigned mode=unsigned(block%4==1||block%4==2);
        setup(mode);batch(mode);drain();check(disjoint->Issue(D3DISSUE_BEGIN));check(start->Issue(D3DISSUE_END));
        const auto at=std::chrono::steady_clock::now();batch(mode);
        const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-at).count()/draws;
        check(end->Issue(D3DISSUE_END));check(frequency->Issue(D3DISSUE_END));check(disjoint->Issue(D3DISSUE_END));drain();
        UINT64 a=0,b=0,hz=0;BOOL bad=FALSE;check(start->GetData(&a,sizeof a,0));check(end->GetData(&b,sizeof b,0));
        check(frequency->GetData(&hz,sizeof hz,0));check(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);
        if(block>=4)std::printf("LIGHTING_COST {\"case\":\"%s\",\"block\":%u,\"variant\":%u,\"draws\":%u,\"gpu_ms\":%.9g,\"cpu_ms\":%.9g}\n",name,block,mode,draws,double(b-a)*1000/double(hz)/draws,cpu);
    }
    for(auto* q:{event,start,end,frequency,disjoint})q->Release();
}
