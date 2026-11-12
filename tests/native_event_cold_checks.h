#pragma once
// Test-only phase attribution; production uses its existing optional cold sink.
struct NativeEventColdDevice:NativeSamplerReadDevice {
    using NativeSamplerReadDevice::NativeSamplerReadDevice;
    using Clock=std::chrono::steady_clock;
    bool measuring=false;unsigned vsCreates=0,psCreates=0,psWords=0;
    double vsMs=0,psMs=0,factoryMs=0,createFenceMs=0;std::uint64_t psHash=0;
    Clock::time_point started;NorthlightNativeLocalLighting::ColdTrace trace;
    static double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
    static double drain(IDirect3DDevice9* d){
        IDirect3DQuery9* q=nullptr;assert(SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_EVENT,&q)));const auto start=Clock::now();assert(SUCCEEDED(q->Issue(D3DISSUE_END)));
        HRESULT hr;while((hr=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(ms(start,Clock::now())<10000);SwitchToThread();}
        assert(hr==S_OK);const auto elapsed=ms(start,Clock::now());q->Release();return elapsed;
    }
    static void cold(void* context,const NorthlightNativeLocalLighting::ColdTrace& value)noexcept{
        auto& self=*static_cast<NativeEventColdDevice*>(context);self.trace=value;self.factoryMs=ms(self.started,Clock::now());
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{
        const auto start=measuring?Clock::now():Clock::time_point{};const auto hr=real->CreateVertexShader(code,out);
        if(measuring){++vsCreates;vsMs+=ms(start,Clock::now());}return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        const auto start=measuring?Clock::now():Clock::time_point{};const auto hr=real->CreatePixelShader(code,out);
        if(measuring){++psCreates;psMs+=ms(start,Clock::now());unsigned at=1;
            while(at<16384&&(code[at]&65535)!=65535){const unsigned op=code[at]&65535;at+=op==65534?1+((code[at]>>16)&32767):1+((code[at]>>24)&15);}assert(at<16384);psWords=at+1;
            psHash=NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const std::uint32_t*>(code),psWords);
            if(std::getenv("NORTHLIGHT_TEST_COLD_CREATE_FENCE"))createFenceMs+=drain(real);
        }return hr;
    }
    void resetMeter(){vsCreates=psCreates=psWords=0;vsMs=psMs=factoryMs=createFenceMs=0;psHash=0;trace={};started=Clock::now();}
};

template<class Submit,class Read> void nativeEventCold(NativeEventColdDevice& d,NorthlightNativeLocalLighting& renderer,
    NorthlightNativeLocalDraw light,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,Submit submit,Read read,unsigned firstFrame=1,bool donor=false){
    using Meter=NativeEventColdDevice;using Clock=Meter::Clock;
    const auto begin=[&]{assert(SUCCEEDED(d.Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0)));assert(SUCCEEDED(d.BeginScene()));};
    begin();assert(SUCCEEDED(submit()));assert(SUCCEEDED(d.EndScene()));Meter::drain(&d);const auto native=read();
    renderer.setColdTraceSink(Meter::cold,&d);light.rendererFrame=1;
    auto second=light;second.slot=1;second.position[0]=-2;second.position[1]=2;second.position[2]=5;
    for(unsigned count:{1u,2u}){std::vector<float> cold;
        for(unsigned warm=0;warm<2;++warm){light.rendererFrame=firstFrame+count-1;begin();d.resetMeter();d.measuring=true;
            double submitMs=0,beforeSubmitMs=0;
            const auto draw=[&]{const auto start=Clock::now();beforeSubmitMs=Meter::ms(d.started,start);const auto hr=submit();submitMs=Meter::ms(start,Clock::now());return hr;};
            const auto hr=renderer.draw(light,vs,ps,draw,count==2?&second:nullptr);const auto returned=Clock::now();d.measuring=false;assert(SUCCEEDED(hr));
            const auto endStart=Clock::now();assert(SUCCEEDED(d.EndScene()));const auto ended=Clock::now();const auto waitMs=Meter::drain(&d);const auto actual=read();
            if(!warm){cold=actual;assert(d.trace.success&&d.trace.count==count&&d.psCreates==1);if(donor)assert(d.trace.vertexReused&&!d.vsCreates);}
            else assert(actual==cold&&!d.psCreates&&!d.vsCreates);
            float error=0;assert(actual.size()==native.size());for(size_t i=0;i<actual.size();++i){assert(std::isfinite(actual[i]));error=std::max(error,std::fabs(actual[i]-native[i]));}assert(error<.003f);
            std::printf("NATIVE_EVENT_COLD count=%u warm=%u donor=%u factoryMs=%.6f createVsMs=%.6f createPsMs=%.6f beforeSubmitMs=%.6f submitMs=%.6f drawCallMs=%.6f endSceneMs=%.6f eventWaitMs=%.6f createFenceMs=%.6f vsCreates=%u psCreates=%u psWords=%u psHash=%016llx sourcePsHash=%016llx imageMaxError=%g\n",
                count,warm,unsigned(donor),d.factoryMs,d.vsMs,d.psMs,beforeSubmitMs,submitMs,Meter::ms(d.started,returned),Meter::ms(endStart,ended),waitMs,d.createFenceMs,d.vsCreates,d.psCreates,d.psWords,(unsigned long long)d.psHash,(unsigned long long)d.trace.sourcePSHash,error);
        }
    }
    renderer.setColdTraceSink(nullptr);std::puts("NATIVE_EVENT_COLD_PASS coldThenWarm=exact sourceCounts=1,2 realEvent=1 creationFence=diagnosticOnly driverCache=preserved");
}
