// Replay actual exported point-pass shader, constants, textures and visibility.
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include "local_light_compiled_shaders.h"
static void ok(HRESULT h){assert(SUCCEEDED(h));}
static std::vector<char> read(const std::string& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static IDirect3DTexture9* texture(IDirect3DDevice9* d,const std::string& p){auto b=read(p);unsigned h[5];std::memcpy(h,b.data(),20);assert(h[0]==0x31524746&&b.size()==20+size_t(h[1])*h[2]*h[4]);IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(h[1],h[2],1,0,D3DFORMAT(h[3]),D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l;ok(t->LockRect(0,&l,nullptr,0));for(unsigned y=0;y<h[2];++y)std::memcpy((char*)l.pBits+y*l.Pitch,b.data()+20+size_t(y)*h[1]*h[4],h[1]*h[4]);ok(t->UnlockRect(0));return t;}
static IDirect3DCubeTexture9* cube(IDirect3DDevice9* d,const std::string& prefix,unsigned mode){
 auto first=read(prefix+"-face-0.fgr");unsigned h[5];std::memcpy(h,first.data(),20);assert(h[0]==0x31524746&&h[1]==h[2]&&h[3]==D3DFMT_R32F&&h[4]==4);
 IDirect3DCubeTexture9* t=nullptr;ok(d->CreateCubeTexture(h[1],1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&t,nullptr));
 for(unsigned face=0;face<6;++face){auto b=read(prefix+"-face-"+std::to_string(face)+".fgr");assert(b.size()==20+size_t(h[1])*h[1]*4&&!std::memcmp(b.data(),h,20));
  D3DLOCKED_RECT l;ok(t->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<h[1];++y){auto* row=(float*)((char*)l.pBits+y*l.Pitch);
   if(mode)std::fill_n(row,h[1],mode==1?1.f:0.f);else std::memcpy(row,b.data()+20+size_t(y)*h[1]*4,h[1]*4);}ok(t->UnlockRect(D3DCUBEMAP_FACES(face),0));}
 return t;
}
int main(int argc,char** argv){assert(argc==4||argc==7);const bool cubes=argc==7;const unsigned firstPass=cubes?std::atoi(argv[4]):7,count=cubes?std::atoi(argv[5]):7,shaderIndex=cubes?std::atoi(argv[6]):2;assert(count&&count<=128);
 const std::string prefix=argv[2],out=argv[3];auto dimensions=read(prefix+(cubes?"lighting-input-g4-s3.fgr":"point-0-resolved.fgr"));unsigned h[5];std::memcpy(h,dimensions.data(),20);const UINT w=h[1],height=h[2];
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CapturedPoint";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 std::vector<IDirect3DTexture9*> owned;auto bind=[&](unsigned s,const std::string& name){
  if(!std::ifstream(prefix+name+".fgr",std::ios::binary).good()){ok(d->SetTexture(s,nullptr));return;}
  auto* t=texture(d,prefix+name+".fgr");owned.push_back(t);ok(d->SetTexture(s,t));};
 for(unsigned s:{0u,2u,3u,4u,5u})bind(s,"lighting-input-g4-s"+std::to_string(s));
 for(unsigned s=0;s<6;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
 ok(d->SetSamplerState(1,D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP));
 auto shader=read(prefix+"lighting-shader-"+std::to_string(shaderIndex)+".bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader((DWORD*)shader.data(),&ps));ok(d->SetPixelShader(ps));
 IDirect3DPixelShader9* blockedPS=nullptr;IDirect3DTexture9* blockedVisibility=nullptr;
 if(cubes){ok(d->CreatePixelShader(kLocalLightingSoftShader,&blockedPS));ok(d->CreateTexture(1,1,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&blockedVisibility,nullptr));
  D3DLOCKED_RECT l;ok(blockedVisibility->LockRect(0,&l,nullptr,0));*(float*)l.pBits=0;ok(blockedVisibility->UnlockRect(0));}
 IDirect3DSurface9 *target=nullptr,*copy=nullptr;ok(d->CreateRenderTarget(w,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(w,height,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));
 for(auto s:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(s,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
 struct V{float x,y,z,w,u,v;};const V q[]={{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{w-.5f,height-.5f,0,1,1,1}};ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));D3DVIEWPORT9 vp{0,0,w,height,0,1};ok(d->SetViewport(&vp));
 for(unsigned source=0;source<count;++source)for(unsigned mode=0;mode<(cubes?3u:1u);++mode){
  // A zero resolved visibility is the exact fully blocked energy reference.
  // A near-plane cube is not: PCSS changes its footprint with blocker distance.
  ok(d->SetPixelShader(mode==2?blockedPS:ps));
  if(cubes&&mode==2)ok(d->SetTexture(1,blockedVisibility));
  else if(cubes){auto* t=cube(d,prefix+"point-"+std::to_string(source),mode);ok(d->SetTexture(1,t));t->Release();}
  else bind(1,"point-"+std::to_string(source)+"-resolved");
  auto bank=read(prefix+"lighting-pass-"+std::to_string(firstPass+source)+"-constants.f32");assert(bank.size()==116*16);ok(d->SetPixelShaderConstantF(0,(float*)bank.data(),116));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)));ok(d->EndScene());ok(d->GetRenderTargetData(target,copy));D3DLOCKED_RECT l;ok(copy->LockRect(&l,nullptr,D3DLOCK_READONLY));std::ofstream f(out+"/point-"+std::to_string(source)+(cubes?mode==1?"-clear":mode==2?"-blocked":"-captured":"")+".fgr",std::ios::binary);unsigned header[]={0x31524746,w,height,D3DFMT_A16B16G16R16F,8};f.write((char*)header,20);for(unsigned y=0;y<height;++y)f.write((char*)l.pBits+y*l.Pitch,w*8);ok(copy->UnlockRect());}
 std::puts("CAPTURED_POINT_PASS_OK");return 0;
}
