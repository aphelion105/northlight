// GPU oracle for additive lamp scattering and its batch-independent bound.
#include "lighting_gpu_support.h"
int main(int argc,char** argv){
 assert(argc==3);const unsigned W=97,H=65;
 auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 auto module=LoadLibraryA(argv[1]);assert(window&&module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
 for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,state,D3DTADDRESS_CLAMP));}
 auto depth=pixels(d,1,1,[](unsigned,unsigned){return std::array<float,4>{1000.f/999.8f*(1-.2f/100),0,0,0};});ok(d->SetTexture(1,depth));
 auto fog=shader(d,std::string(argv[2])+"/LocalFog.bin"),combine=shader(d,std::string(argv[2])+"/LocalFogCombine.bin");
 IDirect3DTexture9* accumulator=nullptr;IDirect3DSurface9 *acc=nullptr,*target=nullptr,*back=nullptr;
 ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&accumulator,nullptr));ok(accumulator->GetSurfaceLevel(0,&acc));
 for(auto ptr:{&target})ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,ptr,nullptr));
 ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&back,nullptr));
 float c[128][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;
 for(unsigned i=0;i<4;++i)c[3+i][i]=1;c[21][3]=100;c[33][0]=W;c[33][1]=H;c[58][0]=3.5f;c[58][1]=1.f/12;c[58][2]=.25f*.9f;c[58][3]=.12f*.9f;
 auto quad=[&]{const Vertex v[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof v[0]));};
 auto render=[&](unsigned count,unsigned batch,float sigma){
  ok(d->SetTexture(9,nullptr));ok(d->SetRenderTarget(0,acc));D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
  ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetPixelShader(fog));ok(d->BeginScene());
  for(unsigned first=0;first<count;first+=batch){for(unsigned i=0;i<4;++i){c[36+i][0]=c[36+i][1]=0;c[36+i][2]=30;c[36+i][3]=12;
    c[44+i][0]=1;c[44+i][1]=.6f;c[44+i][2]=.3f;c[59+i][0]=i<batch&&first+i<count?sigma:0;}
   ok(d->SetPixelShaderConstantF(0,c[0],128));quad();}
  ok(d->SetRenderTarget(0,target));ok(d->SetTexture(9,accumulator));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShader(combine));ok(d->SetPixelShaderConstantF(58,c[58],1));quad();ok(d->EndScene());
  ok(d->GetRenderTargetData(target,back));D3DLOCKED_RECT lock{};ok(back->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(size_t(W)*H*4);
  for(unsigned y=0;y<H;++y)std::memcpy(out.data()+size_t(y)*W*4,(char*)lock.pBits+y*lock.Pitch,W*16);ok(back->UnlockRect());return out;
 };
 unsigned cases=0;float error=0;const size_t center=(size_t(H/2)*W+W/2)*4;
 auto atanFast=[](float x){return x<=1?x*(.785398f+.273f*(1-x)):1.570796f-(1/x)*(.785398f+.273f*(1-1/x));};
 // viewPositionDistance uses D3D9 integer raster centers, including at odd sizes.
 const float rz=1/std::sqrt(1+1.f/(W*W)+1.f/(H*H)),h2=900*(1-rz*rz);
 const float root=std::sqrt(144-h2),core=std::sqrt(h2+.75f*.75f);
 const float integral=2*atanFast(root/core)/core-2*root/(144+.75f*.75f);
 for(float sigma:{0.f,.000001f,.0055f,.1f})for(unsigned count:{1u,2u,4u,8u,16u,64u}){
  const auto reference=render(count,4,sigma);const float raw=integral*sigma*count*c[58][2],expected=raw*c[58][3]/(raw+c[58][3]);
  if(std::fabs(reference[center]-expected)>=.00001f){printf("center sigma=%g count=%u actual=%g expected=%g\n",sigma,count,reference[center],expected);fflush(stdout);}assert(std::fabs(reference[center]-expected)<.00001f);
  for(unsigned batch:{1u,2u,3u}){const auto out=render(count,batch,sigma);++cases;
   for(size_t i=0;i<out.size();++i){assert(std::isfinite(out[i])&&out[i]>=0&&out[i]<=c[58][3]);error=std::max(error,std::fabs(out[i]-reference[i]));assert(std::fabs(out[i]-reference[i])<.000001f);if(!sigma||i%4==3)assert(out[i]==0);}
  }
 }
 printf("LOCAL_FOG_GPU_PASS cases=%u maxBatchError=%.9g analyticalCenter=1 finiteBound=1 zeroExact=1\n",cases,error);return 0;
}
