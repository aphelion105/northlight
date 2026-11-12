#pragma once
#ifndef NORTHLIGHT_NATIVE_DUPLICATE_BASELINE
// Actual native host, with counted source bytecode reads and device creations.
static bool (*nativeVertexCallbackTarget)(const DWORD*,size_t,std::vector<uint32_t>&)=nullptr;
static unsigned nativeVertexCallbackCalls=0;
static bool nativeVertexCallback(const DWORD* p,size_t n,std::vector<uint32_t>& out){++nativeVertexCallbackCalls;return nativeVertexCallbackTarget(p,n,out);}
static bool nativeVertexCallbackOther(const DWORD* p,size_t n,std::vector<uint32_t>& out){++nativeVertexCallbackCalls;return nativeVertexCallbackTarget(p,n,out);}
static bool nativeVertexCallbackFailure(const DWORD*,size_t,std::vector<uint32_t>&){++nativeVertexCallbackCalls;return false;}
static void nativeLocalVertexReuse(IDirect3DDevice9* d,NorthlightNativeLocalDraw light,IDirect3DVertexShader9* boundVS,IDirect3DPixelShader9* boundPS,const NorthlightNativePOM* pom=nullptr,const NorthlightNativeDisplacement* displacement=nullptr){
    const char* alternatePath=std::getenv("NORTHLIGHT_TEST_VERTEX_ALTERNATE_PS");assert(alternatePath);auto alternate=words(alternatePath);
    struct Source:IDirect3DVertexShader9 {
        IDirect3DVertexShader9* real;ULONG refs=1;unsigned reads=0;explicit Source(IDirect3DVertexShader9* p):real(p){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p)override{*p=nullptr;return E_NOINTERFACE;}
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
        HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** p)override{return real->GetDevice(p);}
        HRESULT STDMETHODCALLTYPE GetFunction(void* p,UINT* n)override{++reads;return real->GetFunction(p,n);}
        ~Source(){assert(refs==1);}
    } source(pom?pom->originalVS:displacement?displacement->originalVS:boundVS),changedSource(source.real);
    struct Recorder:ForwardIDirect3DDevice9 {
        Source *source,*changed;unsigned vsCalls=0,psCalls=0;bool failVS=false,failPS=false;std::vector<std::vector<uint32_t>> vertices,pixels;
        Recorder(IDirect3DDevice9* d,Source* s,Source* c):ForwardIDirect3DDevice9(d),source(s),changed(c){}
        HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{return real->SetVertexShader(p==source?source->real:p==changed?changed->real:p);}
        HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** p)override{++vsCalls;vertices.push_back(nativeLocalWords(code));if(failVS){*p=nullptr;return E_FAIL;}return real->CreateVertexShader(code,p);}
        HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** p)override{++psCalls;pixels.push_back(nativeLocalWords(code));if(failPS){*p=nullptr;return E_FAIL;}return real->CreatePixelShader(code,p);}
    } recorder(d,&source,&changedSource);
    auto* hostVS=pom||displacement?boundVS:static_cast<IDirect3DVertexShader9*>(&source);
    NorthlightNativePOM primaryPom;if(pom){primaryPom=*pom;primaryPom.originalVS=&source;if(pom->shared){nativeVertexCallbackTarget=pom->sharedVertex;primaryPom.sharedVertex=&nativeVertexCallback;}}
    NorthlightNativeDisplacement primaryDisplacement;if(displacement){primaryDisplacement=*displacement;primaryDisplacement.originalVS=&source;}
    auto* primaryP=pom?&primaryPom:nullptr;auto* primaryD=displacement?&primaryDisplacement:nullptr;
    IDirect3DPixelShader9* alternateOriginal=nullptr,*alternateBound=nullptr;must(d->CreatePixelShader(alternate.data(),&alternateOriginal));NorthlightNativePOM alternatePom=primaryPom;
    if(pom){std::vector<uint32_t> material;unsigned control=0;assert(NorthlightParallaxPatch::pixel(reinterpret_cast<const uint32_t*>(alternate.data()),alternate.size(),pom->dual,material,&control,pom->shared,nullptr,512));must(d->CreatePixelShader(reinterpret_cast<const DWORD*>(material.data()),&alternateBound));alternatePom.originalPS=alternateOriginal;alternatePom.ps=alternateBound;alternatePom.sourceControl=control;}
    else {alternateBound=alternateOriginal;alternateBound->AddRef();}
    std::vector<IDirect3DPixelShader9*> clones;
    auto clone=[&](IDirect3DPixelShader9* p){UINT bytes=0;must(p->GetFunction(nullptr,&bytes));std::vector<DWORD> code(bytes/4);must(p->GetFunction(code.data(),&bytes));IDirect3DPixelShader9* q=nullptr;must(d->CreatePixelShader(code.data(),&q));clones.push_back(q);return q;};
    NorthlightNativeLocalLighting renderer(&recorder);NorthlightNativeLocalDraw second=light;second.slot=1;light.rendererFrame=500;unsigned submits=0;nativeVertexCallbackCalls=0;
    auto call=[&](NorthlightNativeLocalLighting& cache,IDirect3DVertexShader9* v,IDirect3DPixelShader9* p,const NorthlightNativePOM* pc,const NorthlightNativeDisplacement* dc,bool dual=false){
        must(d->SetVertexShader(v==&source?source.real:v==&changedSource?changedSource.real:v));must(d->SetPixelShader(p));++light.rendererFrame;
        must(cache.draw(light,v,p,[&]{++submits;return S_OK;},dual?&second:nullptr,pc,dc));IDirect3DVertexShader9* restoredV=nullptr;IDirect3DPixelShader9* restoredP=nullptr;must(d->GetVertexShader(&restoredV));must(d->GetPixelShader(&restoredP));assert(restoredV==(v==&source?source.real:v==&changedSource?changedSource.real:v)&&restoredP==p);restoredV->Release();restoredP->Release();
    };
    for(unsigned pair=0;pair<2;++pair)for(unsigned dual=0;dual<2;++dual)call(renderer,hostVS,pair?alternateBound:boundPS,pom?(pair?&alternatePom:&primaryPom):nullptr,primaryD,dual!=0);
    assert(renderer.draws==4&&renderer.multiDraws==2&&renderer.coldPairs==4&&recorder.vsCalls==2&&recorder.psCalls==4&&source.reads==4);
    assert(recorder.vertices[0]!=recorder.vertices[1]&&recorder.pixels[0]!=recorder.pixels[2]&&recorder.pixels[1]!=recorder.pixels[3]);if(pom&&pom->shared)assert(nativeVertexCallbackCalls==2);
    // Different source ownership cannot alias even with the same bound VS and bytes.
    auto* changedPS=clone(boundPS);auto changedPom=primaryPom;changedPom.ps=changedPS;changedPom.originalVS=&changedSource;auto changedD=primaryDisplacement;changedD.originalVS=&changedSource;
    call(renderer,pom||displacement?hostVS:static_cast<IDirect3DVertexShader9*>(&changedSource),changedPS,pom?&changedPom:nullptr,displacement?&changedD:nullptr);assert(recorder.vsCalls==3&&changedSource.reads==2);
    // Selecting another native slot changes the vertex program.
    light.slot=1;auto slotPom=primaryPom;slotPom.ps=changedPS;call(renderer,hostVS,changedPS,pom?&slotPom:nullptr,primaryD);light.slot=0;assert(recorder.vsCalls==4&&source.reads==6);
    if(pom&&pom->shared){auto callbackPom=primaryPom;callbackPom.ps=clone(boundPS);callbackPom.sharedVertex=&nativeVertexCallbackOther;call(renderer,hostVS,callbackPom.ps,&callbackPom,nullptr);assert(recorder.vsCalls==5&&source.reads==8&&nativeVertexCallbackCalls==5);
        callbackPom.ps=clone(boundPS);callbackPom.sharedVertex=&nativeVertexCallbackFailure;const auto draws=renderer.draws;call(renderer,hostVS,callbackPom.ps,&callbackPom,nullptr);assert(renderer.draws==draws&&recorder.vsCalls==5&&source.reads==10&&nativeVertexCallbackCalls==6);}
    // A metadata mode change cannot reuse a different vertex composition.
    if(pom){auto mode=primaryPom;mode.ps=clone(boundPS);mode.shared=!mode.shared;mode.count=mode.shared?3:2;nativeVertexCallbackTarget=pom->sharedVertex;mode.sharedVertex=&nativeVertexCallback;
        const unsigned created=recorder.vsCalls,reads=source.reads;call(renderer,hostVS,mode.ps,&mode,nullptr);assert(recorder.vsCalls==created+1&&source.reads==reads+2);}
    if(displacement){auto mode=primaryDisplacement;mode.gpu=!mode.gpu;mode.count=mode.gpu?2:1;mode.sampler=0;
        const unsigned created=recorder.vsCalls,reads=source.reads;call(renderer,hostVS,clone(boundPS),nullptr,&mode);assert(recorder.vsCalls==created+1&&source.reads==reads+2);}
    if(displacement&&displacement->gpu){auto bad=primaryDisplacement;++bad.sampler;auto* p=clone(boundPS);const unsigned before=recorder.vsCalls,reads=source.reads;const auto draws=renderer.draws;call(renderer,hostVS,p,nullptr,&bad);assert(renderer.draws==draws&&recorder.vsCalls==before&&source.reads==reads+2);}
    if(displacement){auto bad=primaryDisplacement;++bad.count;const auto before=renderer.coldPairs;call(renderer,hostVS,clone(boundPS),nullptr,&bad);assert(renderer.coldPairs==before);}
    // Device PS failure on a reused VS must release only the new entry's ref.
    auto rejectedPom=primaryPom;rejectedPom.ps=clone(boundPS);if(pom)rejectedPom.originalPS=clone(pom->originalPS);const unsigned beforeVS=recorder.vsCalls,beforeReads=source.reads;recorder.failPS=true;const auto beforeDraws=renderer.draws;call(renderer,hostVS,rejectedPom.ps,pom?&rejectedPom:nullptr,primaryD);recorder.failPS=false;assert(renderer.draws==beforeDraws&&recorder.vsCalls==beforeVS&&source.reads==beforeReads);
    call(renderer,hostVS,boundPS,primaryP,primaryD);assert(renderer.draws==beforeDraws+1&&recorder.vsCalls==beforeVS);
    renderer.reset();assert(source.refs==1&&changedSource.refs==1);
    // Failed entries are never donors; a later valid pair still builds its VS.
    for(unsigned failure=0;failure<2;++failure){NorthlightNativeLocalLighting failed(&recorder);auto fp=primaryPom;fp.ps=clone(boundPS);const unsigned startVS=recorder.vsCalls,startReads=source.reads;recorder.failVS=failure==0;recorder.failPS=failure==1;call(failed,hostVS,fp.ps,pom?&fp:nullptr,primaryD);recorder.failVS=recorder.failPS=false;assert(!failed.draws);call(failed,hostVS,boundPS,primaryP,primaryD);assert(failed.draws==1&&recorder.vsCalls==startVS+2&&source.reads==startReads+4);failed.reset();assert(source.refs==1);}
    // Reset releases shared VS references; rebuilding is cold, never stale.
    const unsigned resetVS=recorder.vsCalls;call(renderer,hostVS,boundPS,primaryP,primaryD);assert(recorder.vsCalls==resetVS+1);renderer.reset();assert(source.refs==1&&changedSource.refs==1);
    must(d->SetVertexShader(boundVS));must(d->SetPixelShader(boundPS));for(auto* p:clones)p->Release();alternateBound->Release();alternateOriginal->Release();
    printf("NATIVE_VERTEX_REUSE_PASS pom=%u shared=%u displacement=%u gpu=%u pairs=4 vertexCreates=2 sourceGetFunctionCalls=4 previousCreates=4 previousReads=8 pixelCreates=4 sourceIdentity=isolated slots=isolated callback=isolated failedDonors=excluded restore=exact resetRefs=exact\n",unsigned(pom!=nullptr),pom?unsigned(pom->shared):0,unsigned(displacement!=nullptr),displacement?unsigned(displacement->gpu):0);
}
#endif
