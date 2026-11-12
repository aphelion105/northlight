// Actual WorldRenderer optional moon-floor ownership and draw wiring, isolated from the game.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include <windows.h>
static bool denyMoonMemory=false;
static BOOL WINAPI moonMemoryStatus(LPMEMORYSTATUSEX info){return denyMoonMemory?FALSE:GlobalMemoryStatusEx(info);}
#define GlobalMemoryStatusEx moonMemoryStatus
#include "renderer.cpp"
#undef GlobalMemoryStatusEx
#include <cassert>
class MoonTexture:public IDirect3DTexture9 {
public:
    IDirect3DTexture9* real;
    unsigned failure=0;
    explicit MoonTexture(IDirect3DTexture9* p):real(p){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {return real->QueryInterface(riid, ppvObject);}
    ULONG STDMETHODCALLTYPE AddRef() override {return real->AddRef();}
    ULONG STDMETHODCALLTYPE Release() override {return real->Release();}
    HRESULT STDMETHODCALLTYPE GetDevice(struct IDirect3DDevice9** ppDevice) override {return real->GetDevice(ppDevice);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, const void *data, DWORD data_size, DWORD flags) override {return real->SetPrivateData(guid, data, data_size, flags);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) override {return real->GetPrivateData(refguid, pData, pSizeOfData);}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override {return real->FreePrivateData(refguid);}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override {return real->SetPriority(PriorityNew);}
    DWORD STDMETHODCALLTYPE GetPriority() override {return real->GetPriority();}
    void STDMETHODCALLTYPE PreLoad() override {return real->PreLoad();}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override {return real->GetType();}
    DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override {return real->SetLOD(LODNew);}
    DWORD STDMETHODCALLTYPE GetLOD() override {return real->GetLOD();}
    DWORD STDMETHODCALLTYPE GetLevelCount() override {return real->GetLevelCount();}
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override {return real->SetAutoGenFilterType(FilterType);}
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override {return real->GetAutoGenFilterType();}
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {return real->GenerateMipSubLevels();}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) override {return real->GetLevelDesc(Level, pDesc);}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) override {return real->GetSurfaceLevel(Level, ppSurfaceLevel);}
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags) override {if(failure==1)return E_FAIL;auto hr=real->LockRect(level, locked_rect, rect, flags);if(failure==2&&SUCCEEDED(hr))locked_rect->Pitch=1;return hr;}
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override {auto hr=real->UnlockRect(Level);return failure==3?E_FAIL:hr;}
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *dirty_rect) override {return real->AddDirtyRect(dirty_rect);}
};
struct MoonDevice:ForwardIDirect3DDevice9 {
 bool failTexture=false,failShader=false,failDraw=false,failBind=false;unsigned slots=65535,allocations=0,draws=0;float support[4]{},bounds[4]{};IDirect3DPixelShader9* drawn=nullptr;IDirect3DBaseTexture9* sampled=nullptr;DWORD filter=0;
 unsigned restoreKind=0,restoreFailures=0;IDirect3DTexture9* restoreScene=nullptr;IDirect3DPixelShader9* restoreShader=nullptr;
 bool restoreFault(unsigned kind){if(restoreKind!=kind||!restoreFailures)return false;if(restoreFailures!=UINT_MAX)--restoreFailures;return true;}
 HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD i,D3DSAMPLERSTATETYPE t,DWORD v)override{if(i==0&&v==D3DTEXF_LINEAR&&restoreFault(t==D3DSAMP_MINFILTER?2:t==D3DSAMP_MAGFILTER?3:0))return E_FAIL;return real->SetSamplerState(i,t,v);}
 HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT r,const float*c,UINT n)override{if(((r==72&&c[1]==0&&restoreFault(4))||(r==65&&c[0]==23.75f&&restoreFault(5))))return E_FAIL;return real->SetPixelShaderConstantF(r,c,n);}
 HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9*p)override{if(p==restoreShader&&restoreFault(6))return E_FAIL;return real->SetPixelShader(p);}
 explicit MoonDevice(IDirect3DDevice9*d):ForwardIDirect3DDevice9(d){}
 HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9*c)override{auto h=real->GetDeviceCaps(c);c->MaxPixelShader30InstructionSlots=std::min<DWORD>(c->MaxPixelShader30InstructionSlots,slots);return h;}
 HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9**t,HANDLE*x)override{++allocations;if(failTexture){*t=nullptr;return E_OUTOFMEMORY;}return real->CreateTexture(w,h,l,u,f,p,t,x);}
 HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*c,IDirect3DPixelShader9**s)override{if(failShader){*s=nullptr;return E_FAIL;}return real->CreatePixelShader(c,s);}
 HRESULT STDMETHODCALLTYPE SetTexture(DWORD i,IDirect3DBaseTexture9*t)override{if(i==0&&t==restoreScene&&restoreFault(1))return E_FAIL;if(failBind&&i==0){failBind=false;return E_FAIL;}return real->SetTexture(i,t);}
 HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void*v,UINT stride)override{
  ++draws;real->GetPixelShaderConstantF(72,support,1);real->GetPixelShaderConstantF(65,bounds,1);
  IDirect3DPixelShader9*p=nullptr;real->GetPixelShader(&p);drawn=p;if(p)p->Release();IDirect3DBaseTexture9*q=nullptr;real->GetTexture(0,&q);sampled=q;if(q)q->Release();real->GetSamplerState(0,D3DSAMP_MINFILTER,&filter);
  if(failDraw)return E_FAIL;return real->DrawPrimitiveUP(t,n,v,stride);
 }
};
struct WorldScreenEffectsTest {
 static void cityImage(WorldRenderer& w,MoonDevice& d,IDirect3DPixelShader9* baseline,IDirect3DTexture9* scene){
  using namespace NorthlightGI;NorthlightRegionalFog::Region region;
  for(int y=31;y<=33;++y)for(int x=31;x<=33;++x){NorthlightRegionalFog::Tile tile;tile.x=x;tile.y=y;tile.zones.fill(1637);region.tiles.push_back(tile);}
  WorldScene geometry;WorldMaterial ground;ground.terrain=true;geometry.materials.push_back(ground);WorldMaterial roof;roof.wmo=true;geometry.materials.push_back(roof);
  auto slab=[&](float x0,float x1,float z,unsigned material){unsigned a=geometry.vertices.size();geometry.vertices.push_back({{x0,-400,z},{0,0,1},0,0});geometry.vertices.push_back({{x1,-400,z},{0,0,1},1,0});geometry.vertices.push_back({{x1,400,z},{0,0,1},1,1});geometry.vertices.push_back({{x0,400,z},{0,0,1},0,1});geometry.triangles.push_back({a,a+1,a+2,material});geometry.triangles.push_back({a,a+2,a+3,material});};
  slab(-400,400,0,0);for(int x=-400;x<400;x+=8){slab(float(x),float(x+3),24,1);slab(float(x+5),float(x+8),24,1);}
  auto field=std::make_shared<NorthlightRegionalFog::Field>(NorthlightRegionalFog::buildField(geometry,region,0,0));assert(field->texels[2080].day==0&&field->texels[2080].nightExtra==0&&field->texels[2080].ground==24&&field->directFloor[2080]==0);
  w.moonFloorShadowCurrent=true;assert(w.uploadRegionalFog(field,"fixture"));assert(w.moonFloorSourceCurrent(1,true,true,"fixture"));
  constexpr unsigned W=128,H=96;IDirect3DSurface9 *target=nullptr,*readback=nullptr;assert(SUCCEEDED(d.real->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));assert(SUCCEEDED(d.real->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr)));
  auto texture=[&](unsigned width,unsigned height,float value){IDirect3DTexture9*t=nullptr;assert(SUCCEEDED(d.real->CreateTexture(width,height,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&t,nullptr)));D3DLOCKED_RECT l{};assert(SUCCEEDED(t->LockRect(0,&l,nullptr,0)));for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)memcpy(static_cast<char*>(l.pBits)+y*l.Pitch+x*4,&value,4);t->UnlockRect(0);return t;};
  auto* depth=texture(W,H,1000.f/(1000-.1f)*(1-.1f/12));auto* shadow=texture(1024,1024,1);drop(w.shadow[3]);w.shadow[3]=shadow;shadow->AddRef();auto* zero=texture(1,1,0);
  float c[108][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
  for(unsigned i=0;i<4;++i)c[3+i][i]=1;c[6][2]=c[15][2]=4;c[11][0]=c[12][1]=1.f/192;c[13][2]=-1.f/192;c[14][2]=.5f;c[14][3]=1;
  c[16][2]=c[16][3]=1;c[17][0]=.5f;c[17][1]=.35f;c[17][2]=.8f;c[21][0]=c[21][1]=1;c[21][2]=.4f;c[21][3]=128;c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=.0066f;c[24][0]=.00001f;c[27][0]=1;
  c[31][0]=field->originX;c[31][1]=field->originY;c[31][2]=1.f/512;c[31][3]=1;c[32][0]=.0144f;c[32][1]=.01075f;c[32][2]=64;c[32][3]=48;c[33][0]=W*2;c[33][1]=H*2;c[33][2]=W;c[33][3]=H;c[58][0]=2;c[58][1]=1.f/8;c[59][3]=.0017f;
  c[65][0]=w.fogGlobalHeights[0]-.25f;c[65][1]=w.fogGlobalHeights[1]+64;c[65][2]=c[65][3]=1;
  double energy[3]{};for(unsigned mode=0;mode<3;++mode){D3DLOCKED_RECT l{};shadow->LockRect(0,&l,nullptr,0);for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x){float worldX=((x+.5f)/1024-.5f)*384;float z=mode==2?.5f-24.f/192:((std::fmod(worldX+1000.f,8.f)>=3.f&&std::fmod(worldX+1000.f,8.f)<5.f)?1.f:.5f-24.f/192);memcpy(static_cast<char*>(l.pBits)+y*l.Pitch+x*4,&z,4);}shadow->UnlockRect(0);
   d.real->SetRenderTarget(0,target);d.real->SetTexture(0,scene);d.real->SetTexture(1,depth);d.real->SetTexture(3,shadow);d.real->SetTexture(11,zero);d.real->SetTexture(12,w.fogHeightTexture);d.real->SetTexture(13,w.regionalFogTexture);
   for(unsigned slot=0;slot<16;++slot){d.real->SetSamplerState(slot,D3DSAMP_MINFILTER,slot==0||slot==13?D3DTEXF_LINEAR:D3DTEXF_POINT);d.real->SetSamplerState(slot,D3DSAMP_MAGFILTER,slot==0||slot==13?D3DTEXF_LINEAR:D3DTEXF_POINT);d.real->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d.real->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
   d.real->SetPixelShaderConstantF(0,c[0],108);d.real->BeginScene();assert(SUCCEEDED(w.drawFogVolume(W,H,baseline,mode!=0&&w.moonFloorSourceCurrent(1,true,true,"fixture"),false,false,false,scene,c[65])));d.real->EndScene();assert(SUCCEEDED(d.real->GetRenderTargetData(target,readback)));readback->LockRect(&l,nullptr,D3DLOCK_READONLY);for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto* v=reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)+4*x;assert(std::isfinite(v[0])&&v[3]==1);energy[mode]+=v[0];}readback->UnlockRect();
  }
  assert(energy[0]==0&&energy[1]>0&&energy[2]==0);std::printf("MOON_HOST_CITY_IMAGE_PASS baseline=%g slit=%g sealed=%g cityDensityZero=1 ambientAlphaExact=1 actualBuildUploadSelectDraw=1\n",energy[0],energy[1],energy[2]);
  for(unsigned slot=0;slot<16;++slot)d.real->SetTexture(slot,nullptr);depth->Release();shadow->Release();zero->Release();target->Release();readback->Release();
 }
 static void run(MoonDevice& d){
  auto w=std::make_unique<WorldRenderer>(&d);auto field=std::make_shared<NorthlightRegionalFog::Field>();
  for(unsigned i=0;i<4096;++i){field->texels[i]={24,0,0,32};field->directFloor[i]=0;}
  w->uploadedFogField=field;w->uploadedFogMap=w->uploadedMap="fixture";w->active=std::make_shared<WorldRenderer::Snapshot>();w->active->map="fixture";
  w->active->bvh=std::make_shared<NorthlightGI::BVH>();w->uploaded=w->active->bvh;w->moonFloorShadowCurrent=true;
  w->effects.shadows=true;w->sourceColors[1]={1,1,1};w->shadowCacheKey[3].valid=true;w->shadowTransition[3].workingValid=true;
  assert(SUCCEEDED(d.real->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&w->shadow[3],nullptr)));
  assert(w->moonFloorSourceCurrent(1,true,true,"fixture"));assert(!w->moonFloorSourceCurrent(0,true,true,"fixture"));assert(!w->moonFloorSourceCurrent(1,false,true,"fixture"));assert(!w->moonFloorSourceCurrent(1,true,false,"fixture"));assert(!w->moonFloorSourceCurrent(1,true,true,"other"));
  w->neutralShadowMaps=true;assert(!w->moonFloorSourceCurrent(1,true,true,"fixture"));w->neutralShadowMaps=false;
  w->shadowTransition[3].workingValid=false;assert(!w->moonFloorSourceCurrent(1,true,true,"fixture"));w->shadowTransition[3].workingValid=true;
  using Action=NorthlightQuality::CascadeAction;
  NorthlightQuality::ShadowMapReuse reuse;reuse.valid=true;reuse.pass=0;
  auto deferred=NorthlightQuality::cascadeAction(reuse,4,8,false,false,false);assert(deferred==Action::Defer);
  w->moonFloorReuseProof(3,deferred,"local-content",false,false);assert(!w->moonFloorSourceCurrent(1,true,true,"fixture"));
  w->moonFloorReuseProof(3,Action::Reuse,nullptr,true,false);assert(!w->moonFloorShadowCurrent);
  w->moonFloorReuseProof(3,Action::Reuse,nullptr,false,true);assert(!w->moonFloorShadowCurrent);
  w->staticSlices[3].active=true;w->moonFloorReuseProof(3,Action::Reuse,nullptr,false,false);assert(!w->moonFloorShadowCurrent);w->staticSlices[3].active=false;
  w->moonFloorReuseProof(3,Action::Reuse,nullptr,false,false);w->shadowCacheKey[3].serial=1;w->meshGeneration=99;assert(w->moonFloorSourceCurrent(1,true,true,"fixture"));
  auto oldBvh=w->active->bvh;w->active->bvh=std::make_shared<NorthlightGI::BVH>();assert(!w->moonFloorSourceCurrent(1,true,true,"fixture"));w->active->bvh=oldBvh;
  w->context.camera[0]=97;assert(!w->moonFloorSourceCurrent(1,true,true,"fixture"));w->context.camera[0]=0;
  IDirect3DPixelShader9* baseline=nullptr;assert(SUCCEEDED(d.real->CreatePixelShader(kWorldFogShader,&baseline)));
  IDirect3DTexture9* scene=nullptr;assert(SUCCEEDED(d.real->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&scene,nullptr)));
  d.restoreScene=scene;d.restoreShader=baseline;
  const float bounds[]={23.75f,64,1,1},zero[4]={};d.real->SetPixelShaderConstantF(21,zero,1);
  auto draw=[&](bool eligible,bool actor=false,bool tiled=false,bool fading=false,unsigned expectedDraws=1,bool restored=true){
   d.real->SetTexture(0,scene);d.real->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d.real->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
   d.real->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d.real->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
   float info[4]={actor?1.f:0.f,0,0,0};d.real->SetPixelShaderConstantF(72,info,1);d.real->SetPixelShaderConstantF(65,bounds,1);
   const unsigned before=d.draws;auto result=w->drawFogVolume(32,32,baseline,eligible,actor,tiled,fading,scene,bounds);assert(d.draws==before+expectedDraws);if(!restored)return result;
   float after[4];d.real->GetPixelShaderConstantF(72,after,1);assert(!memcmp(after,info,16));d.real->GetPixelShaderConstantF(65,after,1);assert(!memcmp(after,bounds,16));
   IDirect3DBaseTexture9*t=nullptr;d.real->GetTexture(0,&t);assert(t==scene);if(t)t->Release();DWORD filter;d.real->GetSamplerState(0,D3DSAMP_MINFILTER,&filter);assert(filter==D3DTEXF_LINEAR);return result;
  };
  d.real->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d.real->SetVertexShader(nullptr);d.real->SetRenderState(D3DRS_ZENABLE,FALSE);d.real->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
  assert(SUCCEEDED(d.real->BeginScene()));
  assert(SUCCEEDED(draw(false))&&d.drawn==baseline&&!w->moonFloorTexture);
  d.slots=512;for(unsigned mode=0;mode<6;++mode)assert(SUCCEEDED(draw(true,mode%3!=0,mode%3==2,mode>=3))&&d.drawn==baseline&&!w->moonFloorTexture);d.slots=65535;w->releaseMoonFloor();
  d.failShader=true;assert(SUCCEEDED(draw(true))&&d.drawn==baseline);d.failShader=false;w->releaseMoonFloor();
  denyMoonMemory=true;assert(SUCCEEDED(draw(true))&&d.drawn==baseline&&!w->moonFloorTexture);denyMoonMemory=false;w->invalidateMoonFloor();
  d.failTexture=true;assert(SUCCEEDED(draw(true))&&d.drawn==baseline&&!w->moonFloorReady);d.failTexture=false;w->invalidateMoonFloor();
  for(unsigned mode=0;mode<6;++mode){assert(SUCCEEDED(draw(true,mode%3!=0,mode%3==2,mode>=3)));assert(d.drawn==w->moonFloorPS[mode]&&d.support[1]==1&&d.bounds[0]==-.25f&&d.sampled==w->moonFloorTexture&&d.filter==D3DTEXF_POINT);}
  auto* retained=w->moonFloorTexture;unsigned allocations=d.allocations;assert(w->ensureMoonFloor()&&d.allocations==allocations);
  D3DLOCKED_RECT lock{};assert(SUCCEEDED(retained->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));for(unsigned i=0;i<4;++i)assert(static_cast<float*>(lock.pBits)[i]==0);retained->UnlockRect(0);
  for(unsigned failure=1;failure<=3;++failure){MoonTexture fault(retained);fault.failure=failure;w->invalidateMoonFloor();w->moonFloorTexture=&fault;assert(!w->ensureMoonFloor()&&!w->moonFloorReady);w->moonFloorTexture=retained;w->invalidateMoonFloor();assert(w->ensureMoonFloor());}
  d.failDraw=true;assert(FAILED(draw(true)));d.failDraw=false;d.failBind=true;assert(SUCCEEDED(draw(true))&&d.drawn==baseline);
  for(unsigned kind=1;kind<=6;++kind){
   d.restoreKind=kind;d.restoreFailures=1;assert(SUCCEEDED(draw(true))&&d.restoreFailures==0);
   d.restoreFailures=UINT_MAX;assert(FAILED(draw(true,false,false,false,1,false)));
   d.failBind=true;assert(FAILED(draw(true,false,false,false,0,false)));
   d.restoreFailures=0;assert(SUCCEEDED(draw(false)));
  }
  d.restoreKind=0;
  auto unknown=std::make_shared<NorthlightRegionalFog::Field>();w->uploadedFogField=unknown;assert(SUCCEEDED(draw(true))&&d.drawn==baseline&&!w->moonFloorReady);assert(w->moonFloorTexture==retained);
  w->uploadedFogField=field;assert(SUCCEEDED(draw(true))&&w->moonFloorReady);assert(w->uploadRegionalFog(nullptr,"other")&&!w->moonFloorReady&&!w->moonFloorField);
  assert(SUCCEEDED(draw(true))&&d.drawn==baseline);w->releaseMoonFloor();assert(!w->moonFloorTexture&&!w->moonFloorReady);for(auto*p:w->moonFloorPS)assert(!p);
  d.real->EndScene();cityImage(*w,d,baseline,scene);d.real->SetTexture(0,nullptr);d.real->SetPixelShader(nullptr);scene->Release();baseline->Release();
  std::puts("MOON_HOST_PASS checkedRestoreRetry=1 persistentRestoreFailClosed=1 drawCountZeroOrOne=1 deferredContent=blocked slicedContent=blocked stagedOldMesh=blocked signatureReuse=preserved sourceIdentity=1 caps512Fallback=1 sixVariants=1 createFailure=1 admissionFailure=1 lockLayoutUnlockFailure=1 shaderFailure=1 bindFailure=1 drawFailureRestore=1 samplerRestore=1 constantsRestore=1 fieldReplacement=1 missingFloor=1 mapInvalidation=1 boundedAllocation=1 reset=1");
 }
};
int main(int argc,char**argv){assert(argc==3);logFile=stdout;std::string root=std::string(argv[2])+"/";MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MoonFloorHost";wc.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&wc);HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9*d=nullptr;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d)));{MoonDevice device(d);WorldScreenEffectsTest::run(device);}d->Release();api->Release();DestroyWindow(window);return 0;
}
