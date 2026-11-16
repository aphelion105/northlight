#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <cstring>
#include <vector>
#include "forwarders.h"
static void logf(const char*,...){}
#include "tracked_buffers.h"
#include "position_bounds.h"
#include "terrain_capture_bounds.h"

template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
constexpr unsigned Groups=96,Vertices=768,Stride=48,Block=Vertices*Stride,IndexBlock=4096,Frames=64;
struct Fixture {
 IDirect3DDevice9* d;
 IDirect3DVertexBuffer9 *rawVB=nullptr,*vb=nullptr;
 IDirect3DIndexBuffer9 *rawIB=nullptr,*ib=nullptr;
 IDirect3DVertexDeclaration9* declaration=nullptr;
 IDirect3DSurface9 *target=nullptr,*readback=nullptr;
 std::vector<unsigned char> vertices=std::vector<unsigned char>(Groups*Block+4096);
 std::vector<unsigned char> indices=std::vector<unsigned char>((Groups+1)*IndexBlock);
 explicit Fixture(IDirect3DDevice9* device):d(device){
  for(unsigned group=0;group<Groups;++group)for(unsigned i=0;i<Vertices;++i){
   const float xyz[3]={i%3==0?-.8f:i%3==1?.8f:0.f,i%3==2?.8f:-.8f,.3f};
   auto* p=vertices.data()+group*Block+i*Stride;std::memcpy(p,xyz,12);
   const DWORD color=0xff000000u|((group*913+i*77)&0xffffff);std::memcpy(p+12,&color,4);
   const uint16_t index=uint16_t(i);std::memcpy(indices.data()+group*IndexBlock+i*2,&index,2);
  }
  ok(d->CreateVertexBuffer(UINT(vertices.size()),D3DUSAGE_DYNAMIC,0,D3DPOOL_DEFAULT,&rawVB,nullptr));
  ok(d->CreateIndexBuffer(UINT(indices.size()),D3DUSAGE_DYNAMIC,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&rawIB,nullptr));
  vb=rawVB;ib=rawIB;
  NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,d,false);
  NorthlightTrackedBuffers::wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(&ib,d,true);
  assert(vb!=rawVB&&ib!=rawIB);
  write(vb,0,vertices.data(),UINT(vertices.size()),D3DLOCK_DISCARD);
  write(ib,0,indices.data(),UINT(indices.size()),D3DLOCK_DISCARD);
  const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
  ok(d->CreateVertexDeclaration(elements,&declaration));
  ok(d->CreateRenderTarget(96,96,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
  ok(d->CreateOffscreenPlainSurface(96,96,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
 }
 ~Fixture(){d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);d->SetVertexDeclaration(nullptr);drop(declaration);drop(vb);drop(ib);drop(target);drop(readback);}
 template<class Buffer> static void write(Buffer* buffer,UINT begin,const void* bytes,UINT count,DWORD flags){void* ptr=nullptr;ok(buffer->Lock(begin,count,&ptr,flags));assert(ptr);std::memcpy(ptr,bytes,count);ok(buffer->Unlock());}
 void bind(){ok(d->SetStreamSource(0,rawVB,0,Stride));ok(d->SetStreamSourceFreq(0,1));ok(d->SetIndices(rawIB));ok(d->SetVertexDeclaration(declaration));}
 void update(unsigned frame,const char* pattern){
  if(!std::strcmp(pattern,"immutable"))return;
  const bool discard=!std::strcmp(pattern,"discard");
  const DWORD tail=frame+101;if(!discard){write(vb,Groups*Block,&tail,4,D3DLOCK_NOOVERWRITE);write(ib,Groups*IndexBlock,&tail,4,D3DLOCK_NOOVERWRITE);}
  // Two visible mesh ranges change; all other declared ranges retain bytes.
  const unsigned changed=!std::strcmp(pattern,"dense")?Groups:2;
  for(unsigned j=0;j<changed;++j){const unsigned group=(frame*7+j*13)%Groups;const float x=1.f+float(frame%7)*.1f;
   std::memcpy(vertices.data()+group*Block,&x,4);if(!discard)write(vb,group*Block,&x,4,D3DLOCK_NOOVERWRITE);
   const uint16_t index=uint16_t((frame+j)%Vertices);std::memcpy(indices.data()+group*IndexBlock,&index,2);if(!discard)write(ib,group*IndexBlock,&index,2,D3DLOCK_NOOVERWRITE);}
  if(discard){write(vb,0,vertices.data(),UINT(vertices.size()),D3DLOCK_DISCARD);write(ib,0,indices.data(),UINT(indices.size()),D3DLOCK_DISCARD);}
 }
 NorthlightDrawSnapshot::Draw draw(unsigned group)const{return {D3DPT_TRIANGLELIST,INT(group*Vertices),0,Vertices,group*(IndexBlock/2),Vertices/3,true};}
 void verify(const NorthlightDrawSnapshot::Mesh& mesh,unsigned group){
  assert(mesh.vertexCount<=Vertices&&mesh.streams[0].stride==Stride&&mesh.streams[0].bytes.size()==size_t(mesh.vertexCount)*Stride&&mesh.indices.size()==Vertices);
  // Compare effective indexed inputs, allowing production's dense compaction
  // to omit unreferenced vertices and remap their indices.
  for(unsigned i=0;i<Vertices;++i){uint16_t index;std::memcpy(&index,indices.data()+group*IndexBlock+i*2,2);assert(mesh.indices[i]<mesh.vertexCount);
   assert(!std::memcmp(mesh.streams[0].bytes.data()+mesh.indices[i]*Stride,vertices.data()+group*Block+index*Stride,Stride));}
 }
 std::vector<uint32_t> pixels(const NorthlightDrawSnapshot::Mesh* mesh,unsigned group){
  ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 viewport{0,0,96,96,0,1};ok(d->SetViewport(&viewport));
  ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetTexture(0,nullptr));
  ok(d->SetRenderState(D3DRS_LIGHTING,FALSE));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
  ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
  ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
  ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE));D3DMATRIX identity{};for(unsigned i=0;i<4;++i)identity.m[i][i]=1;
  for(auto state:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION})ok(d->SetTransform(state,&identity));
  ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff010203,1,0));ok(d->BeginScene());
  if(mesh)ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,mesh->vertexCount,Vertices/3,mesh->indices.data(),D3DFMT_INDEX32,mesh->streams[0].bytes.data(),Stride));
  else{ok(d->SetStreamSource(0,rawVB,0,Stride));ok(d->SetIndices(rawIB));const auto args=draw(group);ok(d->DrawIndexedPrimitive(args.topology,args.base,args.minimum,args.vertices,args.start,args.primitives));}
  ok(d->EndScene());ok(d->GetRenderTargetData(target,readback));D3DLOCKED_RECT locked{};ok(readback->LockRect(&locked,nullptr,D3DLOCK_READONLY));
  std::vector<uint32_t> result(96*96);for(unsigned y=0;y<96;++y)std::memcpy(result.data()+y*96,static_cast<char*>(locked.pBits)+y*locked.Pitch,96*4);ok(readback->UnlockRect());return result;
 }
};

static void terrainContracts(Fixture& fixture,const char* pattern){
 NorthlightTerrainCapture::FrameCache terrain;
 terrain.setIdentityProvider(NorthlightTrackedBuffers::identity);terrain.setVersionProvider(NorthlightTrackedBuffers::version);terrain.setMetadataProvider(NorthlightTrackedBuffers::captureMetadata);
 // Move the authored clip-space triangles inside one ADT chunk. Terrain
 // deliberately rejects triangles straddling the chunk boundary at world 0.
 for(unsigned group=0;group<Groups;++group)for(unsigned i=0;i<Vertices;++i){auto* p=fixture.vertices.data()+group*Block+i*Stride;float xy[2];std::memcpy(xy,p,8);xy[0]+=1010;xy[1]+=1010;std::memcpy(p,xy,8);}
 Fixture::write(fixture.vb,0,fixture.vertices.data(),UINT(fixture.vertices.size()),D3DLOCK_DISCARD);
 unsigned hits=0;
 for(unsigned frame=0;frame<8;++frame){const unsigned serial=frame+100;fixture.update(serial,pattern);
  if(std::strcmp(pattern,"immutable")){const unsigned changed=!std::strcmp(pattern,"dense")?Groups:2;
   for(unsigned j=0;j<changed;++j){const unsigned group=(serial*7+j*13)%Groups;float x;std::memcpy(&x,fixture.vertices.data()+group*Block,4);x+=1010;std::memcpy(fixture.vertices.data()+group*Block,&x,4);Fixture::write(fixture.vb,group*Block,&x,4,D3DLOCK_NOOVERWRITE);}}
  fixture.bind();terrain.clearFrame();
  for(unsigned group=0;group<Groups;++group){const auto args=fixture.draw(group);NorthlightTerrainCapture::Diagnostics details;
   std::shared_ptr<const NorthlightTerrainCapture::MeshSnapshot> mesh;
   if(!terrain.readMeshShared(fixture.d,args.topology,args.base,args.minimum,args.vertices,args.start,args.primitives,nullptr,mesh,&details)||!mesh){std::fprintf(stderr,"TERRAIN capture failed frame=%u group=%u reason=%s\n",frame,group,NorthlightTerrainCapture::rejectName(details.reason));assert(false);}
   hits+=details.trackedCacheHit;size_t at=0;
   for(unsigned i=0;i<Vertices;i+=3){uint16_t sources[3];std::memcpy(sources,fixture.indices.data()+group*IndexBlock+i*2,6);
    if(sources[0]==sources[1]||sources[1]==sources[2]||sources[2]==sources[0])continue;
    for(auto source:sources){assert(at<mesh->indices.size());const auto actual=mesh->indices[at++];assert(actual<mesh->positions.size());
     assert(!std::memcmp(&mesh->positions[actual],fixture.vertices.data()+group*Block+source*Stride,12));}}
   assert(at==mesh->indices.size());
  }
 }
#ifndef NORTHLIGHT_RANGE_BASELINE
 if(!std::strcmp(pattern,"sparse")||!std::strcmp(pattern,"immutable"))assert(hits>600);else assert(!hits);
#endif
 std::printf("RANGE_TERRAIN_PASS pattern=%s draws=768 trackedHits=%u indexed_positions=bitexact\n",pattern,hits);
}

int main(int argc,char** argv){
 assert(argc>=2);const bool benchmark=argc>2&&!std::strcmp(argv[2],"benchmark");const char* pattern=argc>3?argv[3]:"sparse";std::setvbuf(stdout,nullptr,_IONBF,0);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightRangeCapture";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,96,96,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 const auto module=LoadLibraryA(argv[1]);assert(module);const auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=96;pp.BackBufferHeight=96;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 size_t misses=0,hits=0,scans=0,readBytes=0,images=0;uint64_t digest=0;double elapsed=0;
 {
  Fixture fixture(d);NorthlightDrawSnapshot::Frame frames[3];
  for(auto& frame:frames){frame.setIdentityProvider(NorthlightTrackedBuffers::identity);frame.setVersionProvider(NorthlightTrackedBuffers::version);frame.setMetadataProvider(NorthlightTrackedBuffers::captureMetadata);}
  for(unsigned i=1;i<3;++i)assert(frames[i].joinSnapshotPool(frames[0].sharedSources()));
  NorthlightPositionBounds::Cache bounds;bounds.tracking(NorthlightTrackedBuffers::identity,NorthlightTrackedBuffers::version,NorthlightTrackedBuffers::captureMetadata);
  for(unsigned frame=0;frame<Frames;++frame){
   const auto begin=std::chrono::steady_clock::now();fixture.update(frame,pattern);fixture.bind();
   for(auto& snapshots:frames)snapshots.clearFrame();bounds.beginFrame();
   for(unsigned group=0;group<Groups;++group){const auto args=fixture.draw(group);float low[3],high[3];assert(bounds.read(d,args,0,low,high));
    if(!benchmark){float expectedLow=100,expectedHigh=-100;for(unsigned i=0;i<Vertices;++i){float x;std::memcpy(&x,fixture.vertices.data()+group*Block+i*Stride,4);expectedLow=std::min(expectedLow,x);expectedHigh=std::max(expectedHigh,x);}assert(low[0]==expectedLow&&high[0]==expectedHigh);}
    for(auto& snapshots:frames){NorthlightDrawSnapshot::Mesh output;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared;
     assert(snapshots.read(d,fixture.declaration,args,output,nullptr,false,&shared)&&shared);if(!benchmark)fixture.verify(*shared,group);
     digest=digest*33+shared->vertexCount+shared->indices.front();
     if(!benchmark&&group==(frame*7)%Groups&& &snapshots==&frames[2]&&frame%8==0){const auto expected=fixture.pixels(nullptr,group),actual=fixture.pixels(shared.get(),group);assert(expected==actual&&std::count_if(actual.begin(),actual.end(),[](uint32_t value){return value!=0xff010203;})>1000);++images;fixture.bind();}
    }
   }
   if(frame)elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
   for(auto& snapshots:frames){misses+=snapshots.snapshotCacheMisses();hits+=snapshots.snapshotCacheHits();}
   scans+=bounds.scans();readBytes+=bounds.bytesRead();
  }
  if(!benchmark)terrainContracts(fixture,pattern);
 }
 assert(NorthlightTrackedBuffers::records.empty());
 assert(hits+misses==Frames*Groups*3&&misses<=Frames*Groups);
#ifndef NORTHLIGHT_RANGE_BASELINE
 if(!std::strcmp(pattern,"sparse")||!std::strcmp(pattern,"immutable"))assert(misses<Frames*Groups/8&&scans<Frames*Groups/8);
 else assert(misses==Frames*Groups&&scans==Frames*Groups);
#endif
 std::printf("RANGE_CAPTURE_PASS pattern=%s cpuMs=%.6f frames=%u consumers=3 draws=%u misses=%zu hits=%zu boundsScans=%zu positionBytes=%zu imagePairs=%zu digest=%llu\n",pattern,elapsed,Frames-1,Groups,misses,hits,scans,readBytes,images,(unsigned long long)digest);
 d->Release();api->Release();DestroyWindow(window);
}
