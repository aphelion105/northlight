// Production init-only point prewarm: exact state/faults and cold 42-face oracle.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include "native_thread_probe.h"
#include <cassert>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PointTerrainCoverageTest {
 struct Capture {
  std::array<float,1024> vf;std::array<float,896> pf;std::array<int,64> vi,pi;std::array<BOOL,16> vb,pb;
  std::vector<DWORD> values;std::vector<IUnknown*> objects;
  explicit Capture(IDirect3DDevice9* d){
   must(d->GetVertexShaderConstantF(0,vf.data(),256));must(d->GetPixelShaderConstantF(0,pf.data(),224));must(d->GetVertexShaderConstantI(0,vi.data(),16));must(d->GetPixelShaderConstantI(0,pi.data(),16));must(d->GetVertexShaderConstantB(0,vb.data(),16));must(d->GetPixelShaderConstantB(0,pb.data(),16));
   for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_FILLMODE,D3DRS_MULTISAMPLEMASK}){DWORD v=0;must(d->GetRenderState(key,&v));values.push_back(v);}
   for(unsigned sampler=0;sampler<2;++sampler)for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE}){DWORD v=0;must(d->GetSamplerState(sampler,key,&v));values.push_back(v);}
   for(auto key:{D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS}){DWORD v=0;must(d->GetTextureStageState(0,key,&v));values.push_back(v);}
   D3DVIEWPORT9 viewport{};must(d->GetViewport(&viewport));const auto n=values.size();values.resize(n+6);std::memcpy(values.data()+n,&viewport,sizeof viewport);
   for(unsigned i=0;i<4;++i){IDirect3DSurface9* p=nullptr;d->GetRenderTarget(i,&p);objects.push_back(p);}
   IDirect3DSurface9* depth=nullptr;d->GetDepthStencilSurface(&depth);objects.push_back(depth);
   for(unsigned i=0;i<14;++i){IDirect3DBaseTexture9* p=nullptr;must(d->GetTexture(i,&p));objects.push_back(p);}
   IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
   must(d->GetVertexShader(&vs));must(d->GetPixelShader(&ps));must(d->GetVertexDeclaration(&decl));must(d->GetIndices(&ib));objects.insert(objects.end(),{vs,ps,decl,ib});
   DWORD fvf=0;must(d->GetFVF(&fvf));values.push_back(fvf);
   for(unsigned i=0;i<4;++i){IDirect3DVertexBuffer9* p=nullptr;UINT offset=0,stride=0,freq=0;must(d->GetStreamSource(i,&p,&offset,&stride));must(d->GetStreamSourceFreq(i,&freq));objects.push_back(p);values.insert(values.end(),{offset,stride,freq});}
  }
  ~Capture(){for(auto* p:objects)if(p)p->Release();}
  bool same(const Capture& o)const{return vf==o.vf&&pf==o.pf&&vi==o.vi&&pi==o.pi&&vb==o.vb&&pb==o.pb&&values==o.values&&objects==o.objects;}
 };
 struct FaultDevice:ForwardIDirect3DDevice9 {
  using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned fault=0,calls=0,begin=0,end=0,closed=0,draws=0,queries=0,polls=0;bool fired=false;
  bool fail(unsigned key){if(fault==key&&!fired){fired=true;return true;}return false;}
  HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** p)override{++calls;if(fail(1)){*p=nullptr;return E_FAIL;}return real->CreateVertexShader(code,p);}
  struct Block:IDirect3DStateBlock9 {
   IDirect3DStateBlock9* block;FaultDevice* owner;ULONG refs=1;Block(IDirect3DStateBlock9* b,FaultDevice* d):block(b),owner(d){}
   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p)override{*p=this;AddRef();return S_OK;}ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){block->Release();delete this;}return n;}
   HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** p)override{return block->GetDevice(p);}HRESULT STDMETHODCALLTYPE Capture()override{return block->Capture();}
   HRESULT STDMETHODCALLTYPE Apply()override{if(owner->fault==14){owner->fired=true;return E_FAIL;}if(owner->fail(13))return E_FAIL;return block->Apply();}
  };
  HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** p)override{++calls;if(fail(2)){*p=nullptr;return E_FAIL;}IDirect3DStateBlock9* b=nullptr;const auto hr=real->CreateStateBlock(type,&b);if(SUCCEEDED(hr))*p=new Block(b,this);return hr;}
  HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD i,IDirect3DSurface9** p)override{if(!i&&fail(10)){*p=nullptr;return E_FAIL;}return real->GetRenderTarget(i,p);}
  HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** p)override{if(fault==11){fired=true;*p=nullptr;return E_FAIL;}return real->GetDepthStencilSurface(p);}
  HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD i,IDirect3DSurface9* p)override{if(!i&&end&&fail(12))return E_FAIL;return real->SetRenderTarget(i,p);}
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* values,UINT count)override{if(fail(15))return E_FAIL;return real->GetVertexShaderConstantF(first,values,count);}
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* values,UINT count)override{if(end&&count==256&&fail(16))return E_FAIL;return real->SetVertexShaderConstantF(first,values,count);}
  HRESULT STDMETHODCALLTYPE BeginScene()override{++calls;if(fail(3))return E_FAIL;++begin;return real->BeginScene();}
  HRESULT STDMETHODCALLTYPE EndScene()override{++end;if(fault==18){fired=true;return E_FAIL;}if(fail(17))return E_FAIL;const auto hr=real->EndScene();if(SUCCEEDED(hr))++closed;return fail(6)?E_FAIL:hr;}
  HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* p)override{if(fail(4))return E_FAIL;return real->SetTexture(stage,p);}
  HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* p,UINT stride)override{++draws;if(fail(5))return E_FAIL;return real->DrawPrimitiveUP(type,n,p,stride);}
  struct Query:IDirect3DQuery9 {
   IDirect3DQuery9* q;FaultDevice* owner;ULONG refs=1;Query(IDirect3DQuery9* p,FaultDevice* d):q(p),owner(d){}
   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p)override{*p=this;AddRef();return S_OK;}
   ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n){q->Release();delete this;}return n;}
   HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** p)override{return q->GetDevice(p);}D3DQUERYTYPE STDMETHODCALLTYPE GetType()override{return q->GetType();}DWORD STDMETHODCALLTYPE GetDataSize()override{return q->GetDataSize();}
   HRESULT STDMETHODCALLTYPE Issue(DWORD flags)override{if(owner->fail(8))return E_FAIL;return q->Issue(flags);}
   HRESULT STDMETHODCALLTYPE GetData(void* p,DWORD n,DWORD flags)override{++owner->polls;if(owner->fail(9))return E_FAIL;return q->GetData(p,n,flags);}
  };
  HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type,IDirect3DQuery9** p)override{++queries;if(fail(7)){*p=nullptr;return E_FAIL;}IDirect3DQuery9* q=nullptr;const auto hr=real->CreateQuery(type,&q);if(SUCCEEDED(hr))*p=new Query(q,this);return hr;}
 };
 static void stateAndFaults(IDirect3DDevice9* raw){
  DeviceMirror mirror;mirror.gate.ownerTid=MirrorGuard::threadId();ExtensionDevice extension(raw,&mirror);
  std::array<float,1024> f;for(unsigned i=0;i<f.size();++i)f[i]=float(i%97)/97;std::array<int,64> ints;for(unsigned i=0;i<ints.size();++i)ints[i]=int(i);std::array<BOOL,16> bits;for(unsigned i=0;i<bits.size();++i)bits[i]=BOOL(i%2);
  must(extension.SetVertexShaderConstantF(0,f.data(),256));must(extension.SetPixelShaderConstantF(0,f.data(),224));must(extension.SetVertexShaderConstantI(0,ints.data(),16));must(extension.SetPixelShaderConstantI(0,ints.data(),16));must(extension.SetVertexShaderConstantB(0,bits.data(),16));must(extension.SetPixelShaderConstantB(0,bits.data(),16));
  D3DVIEWPORT9 viewport{1,2,32,29,.1f,.8f};must(extension.SetViewport(&viewport));must(extension.SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));must(extension.SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(extension.SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
  IDirect3DVertexBuffer9* vb=nullptr;must(raw->CreateVertexBuffer(96,0,0,D3DPOOL_MANAGED,&vb,nullptr));must(extension.SetStreamSource(0,vb,0,32));must(extension.SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
  for(unsigned fault=0;fault<=18;++fault){FaultDevice d(&extension);WorldRenderer w(&d);d.fault=fault;
   w.pipelineActive=true;w.effects.shadows=true;w.quality.localLights=w.quality.pointShadows=1;w.quality.localLightLimit=64;w.quality.pointShadowLimit=7;
   IDirect3DStateBlock9* rescue=nullptr;must(raw->CreateStateBlock(D3DSBT_ALL,&rescue));Capture before(raw);const auto hr=w.warmPointPipelinesAtDeviceInit();
   if(fault==14||fault==6||fault==18){assert(FAILED(hr)&&!d.polls);if(fault==18)must(raw->EndScene());must(rescue->Apply());mirror.invalidate();}else assert(SUCCEEDED(hr));
   rescue->Release();Capture after(raw);assert(before.same(after));assert((d.begin==d.closed||fault==18)&&d.polls<=1);
   must(raw->BeginScene());must(raw->EndScene()); // No published-success case may leave a scene open.
   if(fault)assert(d.fired);else assert(d.draws==3&&d.queries==1&&d.polls==1);
   assert(!w.failed&&!w.pointFailures);for(const auto& p:w.pointSlots)assert(!p.ready&&!p.cacheValid&&!p.schedule.usable&&!p.retryAt);
  }
  for(unsigned off=0;off<6;++off){FaultDevice d(&extension);WorldRenderer w(&d);w.pipelineActive=true;w.effects.shadows=true;w.quality.localLights=w.quality.pointShadows=1;w.quality.localLightLimit=64;w.quality.pointShadowLimit=7;
   switch(off){case 0:w.pipelineActive=false;break;case 1:w.effects.shadows=false;break;case 2:w.quality.localLights=0;break;case 3:w.quality.pointShadows=0;break;case 4:w.quality.localLightLimit=0;break;case 5:w.quality.pointShadowLimit=0;break;}
   const auto calls=d.calls;w.warmPointPipelinesAtDeviceInit();assert(d.calls==calls&&!d.draws&&!d.queries&&!w.pointScratch&&!w.pointSlots[0].cube);
  }
  must(extension.SetStreamSource(0,nullptr,0,0));vb->Release();
  puts("POINT_INIT_STATE_FAULTS_PASS all_float_bool_int_constants binding_raster_sampler_RT_viewport_exact mirror_restore balanced_scene create_begin_state_draw_end_query_issue_poll_failures disabled_zero_work");
 }
 static void run(IDirect3DDevice9* d,unsigned warm,const char* path){


  {WorldRenderer initializing(d);initializing.pipelineActive=true;initializing.effects.shadows=true;initializing.quality.localLights=initializing.quality.pointShadows=1;initializing.quality.localLightLimit=64;initializing.quality.pointShadowLimit=7;
   ThreadProbe probe;probe.begin();const auto at=NorthlightStream::nowNs();
   if(warm)must(initializing.warmPointPipelinesAtDeviceInit());else{for(unsigned i=0;i<3;++i)initializing.prewarmPointShaders();must(d->CreatePixelShader(kShadowUnionShader,&initializing.unionPS));IDirect3DQuery9* q=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&q));must(q->Issue(D3DISSUE_END));BOOL v=FALSE;const auto hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH);assert(hr==S_FALSE||hr==S_OK);q->Release();}
   printf("PRODUCTION_INIT mode=%u apiMs=%.4f noPresent=1 noWait=1\n",warm,double(NorthlightStream::nowNs()-at)/1e6);Sleep(200);probe.finish("production-init-lead",warm);
   for(const auto& p:initializing.pointSlots)assert(!p.ready&&!p.cacheValid&&!p.schedule.usable);
   initializing.releaseGPU(true); // Exact first createResources lifecycle.
  }
  WorldRenderer w(d);w.valid=true;w.frames=1;w.quality.pointShadowLimit=64;w.quality.pointShadowFacesPerFrame=6;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="point-stutter-proof";w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.uploadedMap=w.active->map;w.meshGeneration=1;
  NorthlightGI::WorldVertex vertices[]={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{-1,1,0},{0,0,1},0,1},{{1,1,8},{0,0,-1},1,1},{{-1,1,8},{0,0,-1},0,1},{{1,-1,8},{0,0,-1},1,0}};
  constexpr unsigned triangles=64;std::vector<WORD> indices;
  for(unsigned i=0;i<triangles;++i)for(WORD index:{WORD(i%2?3:0),WORD(i%2?4:1),WORD(i%2?5:2)})indices.push_back(index);
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices.data(),indices.size()*2)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(d->CreateIndexBuffer(UINT(indices.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));
  void* data=nullptr;must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,0,&data,0));std::memcpy(data,indices.data(),indices.size()*2);must(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&w.shadowDecl));must(d->CreatePixelShader(kShadowUnionShader,&w.unionPS));
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);
  for(unsigned i=0;i<triangles;++i){WorldRenderer::Batch b{};b.page=0;b.start=3*i;b.count=1;b.minVertex=i%2?3:0;b.vertexCount=3;b.boundsLow={-1,-1,i%2?8.f:0.f};b.boundsHigh={1,1,i%2?8.f:0.f};w.batches.push_back(b);}
  const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_FILLMODE,D3DFILL_SOLID}};
  for(const auto& state:states)must(d->SetRenderState(state.first,state.second));
  D3DCAPS9 caps{};must(d->GetDeviceCaps(&caps));assert(caps.MaxPrimitiveCount>=triangles);
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
  IDirect3DQuery9* fence=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));
  auto drain=[&](){must(fence->Issue(D3DISSUE_END));const DWORD started=GetTickCount();BOOL value=FALSE;HRESULT hr;while((hr=fence->GetData(&value,sizeof value,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-started)<10000);SwitchToThread();}must(hr);assert(value==TRUE);};
  auto image=[&](){std::vector<float> result(6*256*256);for(unsigned f=0;f<6;++f){must(d->GetRenderTargetData(w.pointSlot().faces[f],readback));D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y)std::memcpy(result.data()+f*256*256+y*256,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());}return result;};
  NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};

  for(unsigned slot=0;slot<7;++slot){w.pointIndex=slot;assert(w.pointResources());}w.pointIndex=0;
  w.materials.push_back(w.pointEmpty);w.pointEmpty->AddRef();w.uploadedAlphaCutoffs.push_back(.5f);for(unsigned i=0;i<w.batches.size();++i)w.batches[i].material=i%2;
  const double submit=0,presentMs=0;
  std::vector<float> reference;
  for(unsigned pass=0;pass<2;++pass){
    for(auto& p:w.pointSlots){p.cacheValid=false;p.schedule.invalidate();}
    ThreadProbe first;first.begin();const auto start=NorthlightStream::nowNs();must(d->BeginScene());w.pointWork={};
    for(unsigned slot=0;slot<7;++slot){w.pointIndex=slot;light.sourceId=100+slot;assert(w.renderPointShadow(light,true,false));}must(d->EndScene());const double apiMs=double(NorthlightStream::nowNs()-start)/1e6;drain();const double completion=double(NorthlightStream::nowNs()-start)/1e6;first.finish(pass?"repeat42faces":"first42faces",pass);
    // Clock ended before probe closure and every image readback below.
    assert(w.pointWork.rebuilt==42);std::vector<float> output;
    for(unsigned slot=0;slot<7;++slot){w.pointIndex=slot;auto face=image();output.insert(output.end(),face.begin(),face.end());}
    if(!pass){reference=output;FILE* f=fopen(path,"wb");assert(f);assert(fwrite(output.data(),sizeof(float),output.size(),f)==output.size());fclose(f);}else assert(output==reference);
    printf("POINT_PIPELINE_PROTOTYPE warm=%u pass=%u warmSubmitMs=%.4f presentMs=%.4f submitMs=%.4f completeMs=%.4f rebuiltFaces=%u pixels=%zu unpublished=1\n",warm,pass,submit,presentMs,apiMs,completion,w.pointWork.rebuilt,output.size());
  }
  readback->Release();fence->Release();
 }
};
int main(int argc,char** argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 assert(argc==5);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);std::wstring path(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointStutter";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,0x56,&pp,&d));if(std::atoi(argv[3])==9)PointTerrainCoverageTest::stateAndFaults(d);else PointTerrainCoverageTest::run(d,unsigned(std::atoi(argv[3])),argv[4]);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
