#define NOMINMAX
#include "renderer.cpp"
#include <cassert>
#include <fstream>
#include <chrono>
static void benchOk(HRESULT h){if(FAILED(h)){std::printf("BENCH_HRESULT %08lx\n",(unsigned long)h);std::abort();}}
struct BenchMeter:ForwardIDirect3DDevice9 {
 using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned draws=0,copies=0,targets=0;
 HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* p,UINT s)override{++draws;return real->DrawPrimitiveUP(t,n,p,s);}
 HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c)override{++draws;return real->DrawIndexedPrimitive(t,b,m,n,s,c);}
 HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE f)override{++copies;return real->StretchRect(a,ar,b,br,f);}
 HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* s)override{++targets;return real->SetRenderTarget(n,s);}
};
struct PipelineCompareTest {
 static void setup(WorldRenderer& w,IDirect3DDevice9* d,const float* c,unsigned mode){
  w.valid=true;w.lastRequest.map="offline-common-pipeline";
#if NORTHLIGHT_COMPARE_PR
  w.pipelineActive=true;w.quality.localLights=mode==2;w.quality.pointShadowLimit=mode==2?1:0;w.quality.softShadows=0;
#endif
  w.quality.renderProfile=0;w.quality.replayJobs=0;w.quality.actorShadows=0;w.quality.giDynamicProbes=0;
  w.quality.equipmentLights=w.quality.weather=w.quality.fogClouds=0;
  w.quality.localLightLimit=mode==2?8:0;
  w.quality.pointShadowRefreshMs=0;w.quality.pointShadowFacesPerFrame=6;
  w.effects={mode==2,mode==2,mode!=0};
  std::memcpy(w.context.inverseView,c+12,64);std::memcpy(w.context.camera,c+60,12);
  std::memcpy(w.context.lightDirection,c+112,12);std::memcpy(w.context.direct,c+116,12);std::memcpy(w.context.ambient,c+72,12);
  for(unsigned i=0;i<16;++i)w.context.view[i]=i%5==0?1.f:0.f;
  for(unsigned r=0;r<3;++r){for(unsigned k=0;k<3;++k)w.context.view[4*r+k]=w.context.inverseView[4*k+r];
   w.context.view[12+r]=-(w.context.camera[0]*w.context.view[r]+w.context.camera[1]*w.context.view[4+r]+w.context.camera[2]*w.context.view[8+r]);}
  std::copy(c+4,c+7,w.projection);
  w.sourceDirections[0]={c[112],c[113],c[114]};w.sourceDirections[1]={-c[112],-c[113],-c[114]};
  w.sourceColors[0]=w.sourceGlowColors[0]={c[116],c[117],c[118]};w.sourceColors[1]=w.sourceGlowColors[1]={0,0,0};w.sourceWeights[0]=1;w.sourceWeights[1]=0;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;w.active->bvh=std::make_shared<NorthlightGI::BVH>();
  w.active->serial=1;w.active->atlas.resize(NorthlightGI::probeLayout().atlasSize());auto& entry=w.active->atlas[0];entry.occupied=true;entry.probe.valid=true;entry.probe.sh[0]={.3f,.4f,.5f};
  w.legacyFogKnown=true;std::copy_n(c+100,4,w.legacyFog.parameters);std::copy_n(c+104,4,w.legacyFog.color);
  if(mode){auto field=std::make_shared<NorthlightRegionalFog::Field>();field->originX=w.context.camera[0]-256;field->originY=w.context.camera[1]-256;field->fogCells=field->airCells=4096;
   for(auto& t:field->texels){t.ground=w.context.camera[2]-8;t.day=.004f;t.nightExtra=.002f;t.height=24;}w.active->fogField=field;}
  w.uploaded=w.active->bvh;w.uploadedMap=w.active->map;w.meshGeneration=1;
  const auto p=w.context.camera;
  const NorthlightGI::WorldVertex vertices[]={{{p[0]-8,p[1]-8,p[2]-4},{0,0,1},0,0},{{p[0]+8,p[1]-8,p[2]-4},{0,0,1},1,0},{{p[0]-8,p[1]+8,p[2]-4},{0,0,1},0,1},{{p[0]+8,p[1]+8,p[2]-4},{0,0,1},1,1}};const WORD indices[]={0,1,2,2,1,3};
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices,sizeof indices)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  benchOk(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));benchOk(d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* bytes=nullptr;
  benchOk(page.vb->Lock(0,sizeof vertices,&bytes,0));std::memcpy(bytes,vertices,sizeof vertices);benchOk(page.vb->Unlock());benchOk(page.ib->Lock(0,sizeof indices,&bytes,0));std::memcpy(bytes,indices,sizeof indices);benchOk(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  WorldRenderer::Batch batch;batch.page=0;batch.count=2;batch.minVertex=0;batch.vertexCount=4;batch.material=0;batch.terrain=true;batch.chunkX=7;batch.chunkY=9;batch.boundsLow={p[0]-8,p[1]-8,p[2]-4};batch.boundsHigh={p[0]+8,p[1]+8,p[2]-4};w.batches.push_back(batch);
  if(mode==2){batch.terrain=false;w.batches.push_back(batch);}
  IDirect3DTexture9* white=nullptr;benchOk(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT lock{};benchOk(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0xffffffff;benchOk(white->UnlockRect(0));w.materials.push_back(white);w.uploadedAlphaCutoffs.push_back(-1);
  if(mode==2)for(unsigned i=0;i<8;++i){NorthlightLocalLights::Light light{{p[0]+float(i%4)*4-6,p[1]+float(i/4)*5,p[2]+1},{.1f,.08f,.06f},1,18,100+i,3,
#if NORTHLIGHT_COMPARE_PR
   NorthlightLocalLights::CastShadow
#else
   0
#endif
  };w.active->localLights.push_back(light);}
 }
 static void frame(WorldRenderer& w){w.valid=true;w.captureMode=WorldRenderer::CaptureFresh;
  LARGE_INTEGER tick{};QueryPerformanceCounter(&tick);w.localLightQpc=tick.QuadPart-w.captureFrequency.QuadPart/60;
#if NORTHLIGHT_COMPARE_PR
  for(auto& p:w.pointSlots)p.schedule.updatedAt=GetTickCount()-1000;
#else
  w.pointSchedule.updatedAt=GetTickCount()-1000;
#endif
 }
};
struct BenchImage {unsigned h[5];std::vector<unsigned char> data;};
static BenchImage benchRead(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);BenchImage im{};f.read((char*)im.h,20);assert(im.h[0]==0x31524746);im.data.resize(size_t(im.h[1])*im.h[2]*im.h[4]);assert(f.read((char*)im.data.data(),im.data.size()));return im;}
static IDirect3DTexture9* benchUpload(IDirect3DDevice9* d,const BenchImage& im){
 IDirect3DTexture9* t=nullptr;IDirect3DSurface9 *s=nullptr,*to=nullptr;benchOk(d->CreateTexture(im.h[1],im.h[2],1,D3DUSAGE_RENDERTARGET,D3DFORMAT(im.h[3]),D3DPOOL_DEFAULT,&t,nullptr));benchOk(t->GetSurfaceLevel(0,&to));benchOk(d->CreateOffscreenPlainSurface(im.h[1],im.h[2],D3DFORMAT(im.h[3]),D3DPOOL_SYSTEMMEM,&s,nullptr));D3DLOCKED_RECT l{};benchOk(s->LockRect(&l,nullptr,0));for(unsigned y=0;y<im.h[2];++y)std::memcpy((char*)l.pBits+y*l.Pitch,im.data.data()+size_t(y)*im.h[1]*im.h[4],size_t(im.h[1])*im.h[4]);benchOk(s->UnlockRect());benchOk(d->UpdateSurface(s,nullptr,to,nullptr));s->Release();to->Release();return t;
}
static IDirect3DPixelShader9* benchShader(IDirect3DDevice9* d,const std::string& path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);const auto n=size_t(f.tellg());std::vector<DWORD> v(n/4);f.seekg(0);assert(f.read((char*)v.data(),n));IDirect3DPixelShader9* p=nullptr;benchOk(d->CreatePixelShader(v.data(),&p));return p;}
int main(int argc,char** argv){
 assert(argc==6);std::setvbuf(stdout,nullptr,_IONBF,0);logFile=nullptr;
 const std::string out=argv[2],capture=argv[3],compiled=argv[4];const unsigned mode=unsigned(std::atoi(argv[5]));assert(mode<3);
 assert(MultiByteToWideChar(CP_UTF8,0,(out+"/").c_str(),-1,rootPath,MAX_PATH));
 auto sceneImage=benchRead(capture+"/capture-1-scene.fgr"),depthImage=benchRead(capture+"/capture-1-depth.fgr");const unsigned W=sceneImage.h[1],H=sceneImage.h[2];float c[79][4];std::ifstream f(capture+"/capture-1-constants.f32",std::ios::binary);assert(f.read((char*)c,sizeof c));
 auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* d=nullptr;benchOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 BenchMeter meter(d);d=&meter;auto scene=benchUpload(d,sceneImage),depth=benchUpload(d,depthImage);IDirect3DSurface9* target=nullptr;benchOk(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
 IDirect3DTexture9 *rawAO=nullptr,*ao=nullptr;IDirect3DSurface9 *rawSurface=nullptr,*aoSurface=nullptr;benchOk(d->CreateTexture(W/2,H/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&rawAO,nullptr));benchOk(rawAO->GetSurfaceLevel(0,&rawSurface));benchOk(d->CreateTexture(W/2,H/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&ao,nullptr));benchOk(ao->GetSurfaceLevel(0,&aoSurface));
 auto aoPS=benchShader(d,compiled+"/AOContactBloom.bin"),blurPS=benchShader(d,compiled+"/AOBlur.bin");
 const auto quad=[&](unsigned w,unsigned h){struct V{float x,y,z,rhw,u,v;};const float rw=1.f;V v[]={{-.5f,-.5f,0,rw,0,0},{float(w)-.5f,-.5f,0,rw,1,0},{-.5f,float(h)-.5f,0,rw,0,1},{float(w)-.5f,float(h)-.5f,0,rw,1,1}};D3DVIEWPORT9 vp{0,0,w,h,0,1};benchOk(d->SetViewport(&vp));benchOk(d->SetVertexShader(nullptr));benchOk(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));benchOk(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));};
 IDirect3DQuery9 *a=nullptr,*b=nullptr,*hz=nullptr;benchOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&a));benchOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&b));benchOk(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));const auto get=[](IDirect3DQuery9* q){UINT64 value=0;HRESULT hr;while((hr=q->GetData(&value,sizeof value,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();benchOk(hr);return value;};
 auto world=std::make_unique<WorldRenderer>(d);PipelineCompareTest::setup(*world,d,c[0],mode);NorthlightStateBlockPool caller(d);
 for(unsigned frame=0;frame<48;++frame){
  PipelineCompareTest::frame(*world);benchOk(d->SetDepthStencilSurface(nullptr));benchOk(d->SetRenderTarget(0,target));for(unsigned i=1;i<4;++i)benchOk(d->SetRenderTarget(i,nullptr));benchOk(d->BeginScene());benchOk(hz->Issue(D3DISSUE_END));benchOk(a->Issue(D3DISSUE_END));meter.draws=meter.copies=meter.targets=0;const auto start=std::chrono::steady_clock::now();
  {SavedState outer(d,&caller);assert(outer.ok);for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})benchOk(d->SetRenderState(s,FALSE));benchOk(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
   for(unsigned i=0;i<16;++i){benchOk(d->SetTexture(i,nullptr));benchOk(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));benchOk(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));benchOk(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));benchOk(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));benchOk(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));benchOk(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
   float ac[7][4]={{1.f/W,1.f/H,c[0][2],c[0][3]},{c[1][0],c[1][1],1,0},{.08f,2,0,0},{c[1][3],c[2][0],c[1][3]+1/c[2][0],0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
   std::copy_n(c[25],4,ac[5]);std::copy_n(c[26],4,ac[6]);
   benchOk(d->SetPixelShaderConstantF(0,ac[0],7));benchOk(d->SetTexture(0,scene));benchOk(d->SetTexture(1,depth));benchOk(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));benchOk(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));benchOk(d->SetRenderTarget(0,rawSurface));benchOk(d->SetPixelShader(aoPS));quad(W/2,H/2);
   benchOk(d->SetTexture(0,rawAO));benchOk(d->SetTexture(2,rawAO));benchOk(d->SetRenderTarget(0,aoSurface));benchOk(d->SetPixelShader(blurPS));quad(W/2,H/2);benchOk(d->SetTexture(2,nullptr));benchOk(d->SetTexture(0,nullptr));
   const bool ok=world->render(target,depth,W,H,D3DFMT_A8R8G8B8,c[0][2],c[0][3],c[1][3],c[1][3]+1/c[2][0],0,nullptr,nullptr,scene,ao
#if NORTHLIGHT_COMPARE_PR
    ,frame,nullptr,false,true,false,false,&outer
#endif
   );if(!ok||!world->composited){std::printf("BENCH_WORLD_FAIL stage=%s\n",world->lastSkipReason());std::abort();}}
  const auto finish=std::chrono::steady_clock::now();benchOk(b->Issue(D3DISSUE_END));benchOk(d->EndScene());const auto ticks=get(b)-get(a),rate=get(hz);
  std::printf("COMMON_FRAME frame=%u mode=%u width=%u height=%u warm=%u cpuMs=%.9f gpuMs=%.9f draws=%u copies=%u targets=%u\n",frame,mode,W,H,unsigned(frame<16),std::chrono::duration<double,std::milli>(finish-start).count(),double(ticks)*1000/rate,meter.draws,meter.copies,meter.targets);
 }
 IDirect3DSurface9* readback=nullptr;benchOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));benchOk(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT l{};benchOk(readback->LockRect(&l,nullptr,D3DLOCK_READONLY));std::ofstream image(out+"/output.bgra",std::ios::binary);for(unsigned y=0;y<H;++y)image.write((char*)l.pBits+y*l.Pitch,W*4);benchOk(readback->UnlockRect());readback->Release();
 world.reset();aoPS->Release();blurPS->Release();a->Release();b->Release();hz->Release();aoSurface->Release();rawSurface->Release();ao->Release();rawAO->Release();target->Release();scene->Release();depth->Release();std::puts("COMMON_PIPELINE_PASS");return 0;
}
