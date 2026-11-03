// Paired shader replay: recorded receiver buffers, fixed camera and lighting.
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "world_compiled_shaders.h"
static void must(HRESULT h){assert(SUCCEEDED(h));}
static std::vector<unsigned char> read(const std::string& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static IDirect3DTexture9* texture(IDirect3DDevice9* d,const std::string& p){auto b=read(p);unsigned h[5];std::memcpy(h,b.data(),20);assert(h[0]==0x31524746&&b.size()==20+size_t(h[1])*h[2]*h[4]);IDirect3DTexture9* t=nullptr;must(d->CreateTexture(h[1],h[2],1,0,D3DFORMAT(h[3]),D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l;must(t->LockRect(0,&l,nullptr,0));for(unsigned y=0;y<h[2];++y)std::memcpy((char*)l.pBits+y*l.Pitch,b.data()+20+size_t(y)*h[1]*h[4],h[1]*h[4]);must(t->UnlockRect(0));return t;}
int main(int argc,char** argv){assert(argc==5);const std::string prefix=argv[2],out=argv[3];const bool material=std::string(argv[4])=="material";
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LampComposite";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 auto bank=read(prefix+"lighting-pass-23-constants.f32");assert(bank.size()==116*16);float constants[224][4]{};std::memcpy(constants,bank.data(),bank.size());must(d->SetPixelShaderConstantF(0,constants[0],224));
 std::vector<IDirect3DTexture9*> owned;auto bind=[&](unsigned slot,const std::string& path,bool linear=false){auto* t=texture(d,path);owned.push_back(t);must(d->SetTexture(slot,t));for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(slot,s,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));must(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));must(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));must(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));};
 bind(1,prefix+"depth.fgr");bind(8,prefix+"lighting-input-g3-s8.fgr");bind(10,prefix+"lighting-input-g3-s10.fgr",true);bind(12,prefix+"lighting-input-g0-s12.fgr",true);bind(14,prefix+"normals.fgr");bind(15,prefix+"distance.fgr");
 if(material){bind(4,prefix+"lighting-input-g3-s4.fgr",true);bind(5,prefix+"lighting-input-g3-s5.fgr",true);bind(6,prefix+"lighting-input-g3-s6.fgr");bind(9,prefix+"lighting-input-g0-s9.fgr");}
 else{bind(0,prefix+"lighting-input-g3-s5.fgr",true);bind(9,prefix+"lighting-input-g3-s4.fgr",true);}
 IDirect3DPixelShader9* ps=nullptr;if(material){auto code=read(prefix+"lighting-shader-4.bin");must(d->CreatePixelShader((DWORD*)code.data(),&ps));}else must(d->CreatePixelShader(kWorldCompositeShader,&ps));must(d->SetPixelShader(ps));
 IDirect3DSurface9 *target=nullptr,*copy=nullptr;must(d->CreateRenderTarget(2560,1440,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));must(d->CreateOffscreenPlainSurface(2560,1440,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));
 for(const auto& s:std::initializer_list<std::pair<D3DRENDERSTATETYPE,DWORD>>{{D3DRS_ZENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15}})must(d->SetRenderState(s.first,s.second));
 struct V{float x,y,z,w,u,v;};const V quad[]={{-.5f,-.5f,0,1,0,0},{2559.5f,-.5f,0,1,1,0},{-.5f,1439.5f,0,1,0,1},{2559.5f,1439.5f,0,1,1,1}};must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));D3DVIEWPORT9 vp{0,0,2560,1440,0,1};must(d->SetViewport(&vp));
 for(unsigned mode=0;mode<2;++mode){
  if(material)bind(0,mode?out+"/material-input-fixed.fgr":prefix+"lighting-input-g3-s0.fgr",true);
  else bind(3,mode?out+"/current-local-fixed.fgr":prefix+"current-local-light.fgr");
  must(d->BeginScene());must(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));must(d->EndScene());must(d->GetRenderTargetData(target,copy));D3DLOCKED_RECT l;must(copy->LockRect(&l,nullptr,D3DLOCK_READONLY));
  std::ofstream f(out+(material?"/material-":"/world-")+(mode?"after.fgr":"before.fgr"),std::ios::binary);unsigned h[]={0x31524746,2560,1440,D3DFMT_A16B16G16R16F,8};f.write((const char*)h,20);for(unsigned y=0;y<1440;++y)f.write((char*)l.pBits+y*l.Pitch,2560*8);assert(f);must(copy->UnlockRect());
 }
 for(auto* t:owned)t->Release();ps->Release();target->Release();copy->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
