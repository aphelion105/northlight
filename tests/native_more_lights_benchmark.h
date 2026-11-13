#pragma once
#include <chrono>

// Invoked by the same fixture/image oracles as the correctness test. Timestamps
// cover submission plus GPU execution; waits/readback are outside CPU samples.
template<class Draw> static void benchmarkMoreLights(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::vector<DWORD>& source,
        IDirect3DVertexShader9* original,IDirect3DVertexShader9* eight,bool terrain,const Vertex* quad,Draw smallDraw){
    constexpr unsigned Grid=16,Draws=64,Samples=12;
    std::vector<Vertex> mesh;
    const auto vertex=[&](unsigned x,unsigned y){Vertex v;const float u=float(x)/Grid,t=float(y)/Grid;
        for(unsigned i=0;i<16;++i)for(unsigned k=0;k<4;++k)v.inputs[i][k]=quad[0].inputs[i][k]+u*(quad[2].inputs[i][k]-quad[0].inputs[i][k])+t*(quad[1].inputs[i][k]-quad[0].inputs[i][k]);return v;};
    for(unsigned y=0;y<Grid;++y)for(unsigned x=0;x<Grid;++x)for(const auto& v:{vertex(x,y),vertex(x,y+1),vertex(x+1,y),vertex(x+1,y),vertex(x,y+1),vertex(x+1,y+1)})mesh.push_back(v);
    IDirect3DVertexBuffer9* buffer=nullptr;must(d->CreateVertexBuffer(UINT(mesh.size()*sizeof(Vertex)),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&buffer,nullptr));
    void* mapped=nullptr;must(buffer->Lock(0,0,&mapped,0));std::memcpy(mapped,mesh.data(),mesh.size()*sizeof(Vertex));must(buffer->Unlock());
    const auto denseDraw=[&]{return d->DrawPrimitive(D3DPT_TRIANGLELIST,0,UINT(mesh.size()/3));};
    IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;
    must(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));
    must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    const auto drain=[&]{must(event->Issue(D3DISSUE_END));HRESULT hr;const DWORD start=GetTickCount();while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-start)<10000);SwitchToThread();}must(hr);};
    const auto micros=[](auto start){return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();};
    const auto median=[](std::vector<double> samples){std::sort(samples.begin(),samples.end());return (samples[(samples.size()-1)/2]+samples[samples.size()/2])*.5;};
    for(unsigned mode=0;mode<6;++mode)for(bool cached:{false,true})for(bool dense:{false,true}){
        const char* names[]={"full","base-limit","empty","handoff-all","handoff-one","already-eight"};
        const unsigned base=terrain?3:4,limit=mode==1?base:8,slots=terrain?5:4;const bool own=mode==3||mode==4;
        float values[256][4];constants(values,terrain,0,base);if(mode==2)for(unsigned s=0;s<slots;++s)std::fill_n(values[236]+(terrain?12*s+4:4*s),3,0.f);
        auto* bound=mode==5?eight:original;const auto& words=source;
        FaultDevice counted(d);NorthlightNativeMoreLightRenderer renderer(&counted);const auto coldStart=std::chrono::steady_clock::now();
        if(mode==5)renderer.registerShader(bound,nullptr,0,terrain);else renderer.registerShader(bound,words.data(),words.size(),terrain);
        const double coldUs=micros(coldStart);renderer.limits(terrain?8:limit,terrain?limit:8);assert(renderer.kind(bound)!=(Kind::None));
        Mirror mirror;mirror.enabled=cached;std::memcpy(mirror.vsFloat,values,sizeof values);std::fill_n(mirror.vsFloatKnown,256,true);
        const auto setup=[&]{must(d->SetVertexShader(bound));must(d->SetVertexShaderConstantF(0,values[0],256));if(dense)must(d->SetStreamSource(0,buffer,0,sizeof(Vertex)));};
        const auto submit=[&]{return dense?denseDraw():smallDraw();};
        const auto handoff=[&](bool t,float* bank){if(own)for(unsigned s=0;s<(mode==3?slots:1u);++s)std::fill_n(bank+(t?12*s+4:4*s),3,0.f);};
        const auto managed=[&]{
#if defined(NORTHLIGHT_MORE_LIGHTS_BASELINE_HEADER) || !defined(NORTHLIGHT_MORE_LIGHTS_OPTIMIZED)
            return renderer.draw(renderer.kind(bound),&mirror,handoff,submit);
#else
            return renderer.draw(renderer.kind(bound),&mirror,handoff,submit,own);
#endif
        };
        setup();must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));must(d->BeginScene());must(managed());must(d->EndScene());const auto managedImage=image(d,target);
        float reference[256][4];std::memcpy(reference,values,sizeof values);
        for(unsigned s=0;s<slots;++s)if(mode==1||mode==3||(mode==4&&s==0))std::fill_n(reference[236]+(terrain?12*s+4:4*s),3,0.f);
        must(d->SetVertexShader(eight));must(d->SetVertexShaderConstantF(0,reference[0],256));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));must(d->BeginScene());must(submit());must(d->EndScene());
        const float imageError=compare(managedImage,image(d,target));float positive=0;for(unsigned k=0;k<managedImage.size();++k)if(k%4!=3)positive=std::max(positive,managedImage[k]);assert(positive>.001f);
        setup();counted.shaderReads=counted.constantReads=counted.shaderWrites=counted.constantWrites=0;const unsigned creations=counted.creates;
        must(d->BeginScene());must(managed());must(d->EndScene());drain();
        const unsigned gets=counted.shaderReads,constGets=counted.constantReads,sets=counted.shaderWrites,constSets=counted.constantWrites;
        std::vector<double> cpu,gpu;
        for(unsigned sample=0;sample<Samples+2;++sample){setup();drain();must(disjoint->Issue(D3DISSUE_BEGIN));must(begin->Issue(D3DISSUE_END));must(d->BeginScene());const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<Draws;++i)must(managed());const double cpuUs=micros(start)/Draws;must(d->EndScene());must(end->Issue(D3DISSUE_END));must(freq->Issue(D3DISSUE_END));must(disjoint->Issue(D3DISSUE_END));drain();
            UINT64 a=0,b=0,hz=0;BOOL bad=FALSE;must(begin->GetData(&a,sizeof a,0));must(end->GetData(&b,sizeof b,0));must(freq->GetData(&hz,sizeof hz,0));must(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);
            if(sample>=2){cpu.push_back(cpuUs);gpu.push_back(double(b-a)*1e6/double(hz)/Draws);}}
        assert(!renderer.restoreFailed&&creations==counted.creates);
        std::printf("MORE_LIGHTS_BENCH {\"terrain\":%s,\"case\":\"%s\",\"cached\":%s,\"vertices\":%u,\"draws\":%u,\"samples\":%u,\"cpuUs\":%.9g,\"gpuUs\":%.9g,\"coldRegisterUs\":%.9g,\"getVS\":%u,\"getConstants\":%u,\"setVS\":%u,\"setConstants\":%u,\"imageError\":%.9g,\"warmCreates\":0,\"cpuSamples\":[",terrain?"true":"false",names[mode],cached?"true":"false",dense?unsigned(mesh.size()):4u,Draws,Samples,median(cpu),median(gpu),coldUs,gets,constGets,sets,constSets,imageError);
        for(unsigned k=0;k<cpu.size();++k)std::printf("%s%.9g",k?",":"",cpu[k]);std::printf("],\"gpuSamples\":[");for(unsigned k=0;k<gpu.size();++k)std::printf("%s%.9g",k?",":"",gpu[k]);std::printf("]}\n");
    }
    {FaultDevice counted(d);NorthlightNativeMoreLightRenderer renderer(&counted);renderer.registerShader(original,source.data(),source.size(),terrain);Mirror mirror;
     const DWORD unlit[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000000,0xe00f0000,0x02000001,0xe00f0000,0x90e40000,0xffff};
     std::vector<IDirect3DVertexShader9*> shaders;for(unsigned k=0;k<520;++k){IDirect3DVertexShader9* shader=nullptr;must(d->CreateVertexShader(unlit,&shader));shaders.push_back(shader);renderer.registerShader(shader,unlit,std::size(unlit),false);}
     float values[256][4];constants(values,terrain,0,terrain?3:4);must(d->SetVertexShader(original));must(d->SetVertexShaderConstantF(0,values[0],256));const unsigned before=counted.creates;
     must(d->BeginScene());const auto start=std::chrono::steady_clock::now();must(renderer.draw(renderer.kind(original),&mirror,[](bool,float*){},smallDraw));const double us=micros(start);must(d->EndScene());drain();
     std::printf("MORE_LIGHTS_CACHE {\"terrain\":%s,\"unrelated\":520,\"rebuilds\":%u,\"firstDrawUs\":%.9g}\n",terrain?"true":"false",counted.creates-before,us);
     for(auto* shader:shaders)shader->Release();}
    for(auto* q:{event,begin,end,freq,disjoint})q->Release();buffer->Release();
}
