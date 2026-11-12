#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_quad.h"
static Image rgba(unsigned w,unsigned h,float r,float g,float b,float a){Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(w*h*16)};float c[]={r,g,b,a};for(unsigned i=0;i<w*h;++i)std::memcpy(im.pixels.data()+i*16,c,16);return im;}
static Image red(unsigned w,unsigned h,float v){Image im{w,h,D3DFMT_R32F,4,std::vector<unsigned char>(w*h*4)};for(unsigned i=0;i<w*h;++i)std::memcpy(im.pixels.data()+i*4,&v,4);return im;}
int main(int argc,char**argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 assert(argc==4||argc==5);const bool actorCase=argc==5,transitionCase=actorCase&&std::atoi(argv[4])>=2,actorEnabled=actorCase&&std::atoi(argv[4])!=3;const unsigned W=128,H=96;
 HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,256,256,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
 auto library=LoadLibraryA(argv[3]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=256;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9*d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto code=read(argv[i+1]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
 IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
 unsigned cases=0;double minClear=1,maxActorResidual=0,maxBlocked=0,maxSunError=0,maxTemporalStep=0,maxAlphaError=0;double previous[2]{},slitEnergy[17]{},maxFloorStep=0,maxReferenceError=0;std::vector<std::vector<float>> flatReference(17);
 for(unsigned mode=0;mode<22;++mode)for(unsigned frame=0;frame<(mode>=20?65u:17u);++frame){
  if((mode==14||mode==15)&&!actorCase)continue;if(mode==16&&!transitionCase)continue;
  float c[108][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=c[1][1]=1;c[1][2]=1;c[2][0]=1;c[2][2]=192;c[2][3]=1.f/1024;
  for(unsigned i=0;i<4;++i)c[3+i][i]=1;
  const float cameraX=mode>=20?(int(frame)-32)*.25f:(int(frame)-8)*.125f;c[6][0]=c[15][0]=cameraX;c[6][2]=c[15][2]=4;
  c[11][0]=c[12][1]=1.f/192;c[13][2]=-1.f/192;c[14][2]=.5f;c[14][3]=1;
  c[16][2]=1;c[16][3]=mode==4||mode==12?0:1;c[17][0]=.5f;c[17][1]=.35f;c[17][2]=.8f;
  c[21][0]=c[21][1]=1;c[21][2]=.4f;c[21][3]=128;c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=.0066f;
  c[24][0]=.00001f;c[27][0]=1;c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[31][3]=1;c[32][0]=.0144f;c[32][1]=.01075f;c[32][2]=64;c[32][3]=48;
  c[33][0]=W*2;c[33][1]=H*2;c[33][2]=W;c[33][3]=H;c[58][0]=2;c[58][1]=1.f/8;c[59][3]=.0017f;
  float depth=1000.f/(1000-.1f)*(1-.1f/(mode==13?6:12));auto field=rgba(64,64,mode>=5?24.f:0.f,0,0,mode==3?0:1.15625f);auto roof=red(1024,1024,.5f-24.f/192);
  for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x){float worldX=((x+.5f)/1024-.5f)*384;float z=mode==0||mode>=20?1.f:mode==1||mode==7?.5f-24.f/192:(std::fmod(worldX+1000.f,8.f)<3.f?1.f:.5f-24.f/192);if(mode==13)z=.5f-12.f/192;std::memcpy(roof.pixels.data()+4*(y*1024+x),&z,4);}
  std::vector<IDirect3DTexture9*> textures;auto bind=[&](unsigned slot,const Image&im){auto*t=texture(d,im);textures.push_back(t);ok(d->SetTexture(slot,t));};
  c[72][1]=mode>=6&&mode!=8?1.f:0.f;c[72][2]=mode!=11?1.f:0.f;c[72][3]=mode!=10?1.f:0.f;
  if(mode==9)c[14][0]=3;
  if(actorCase){
   for(unsigned r=0;r<4;++r){for(unsigned k=0;k<4;++k)c[68+r][k]=c[84+r][k]=c[11+r][k];c[94+r][r]=c[101+r][r]=1;}
   c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;c[72][0]=actorEnabled&&(mode==14||mode==15)?1.f:0.f;c[89][1]=.5f;c[100][1]=c[24][0];
   if(mode==16)c[104][0]=3;
  }
  ok(NorthlightHdr::quadState(d,target,W,H,16));float unknown=mode==17?1e20f:0.f;auto floorMap=rgba(64,64,unknown,unknown,unknown,unknown);
  if(mode==18)for(unsigned fy=0;fy<64;++fy)for(unsigned fx=0;fx<64;++fx){float v=fx%2?1e20f:0.f;for(unsigned k=0;k<4;++k)std::memcpy(floorMap.pixels.data()+16*(fy*64+fx)+k*4,&v,4);}
  if(mode>=20)for(unsigned fy=0;fy<64;++fy)for(unsigned fx=0;fx<64;++fx)for(unsigned k=0;k<4;++k){float x=-256.f+8*(fx+(k%2));float y=-256.f+8*(fy+(k/2));float v=mode==20?2+.1f*x+.025f*y:(x<0?0.f:4.f);std::memcpy(floorMap.pixels.data()+16*(fy*64+fx)+k*4,&v,4);}
  bind(0,floorMap);if(mode==19){c[65][0]=0;c[65][1]=72;c[65][2]=c[65][3]=1;bind(12,rgba(64,64,24,24,1.15625f,1));}if(actorCase){bind(4,red(1024,1024,mode==14?0.f:1.f));bind(5,red(64,64,mode==14?0.f:1.f));bind(7,roof);bind(9,red(1024,1024,1));}bind(1,red(W,H,depth));bind(3,roof);bind(11,rgba(1,1,0,0,0,0));bind(13,field);ok(d->SetPixelShaderConstantF(0,c[0],108));
  std::vector<float> output[2];double energy[2]{};
  for(unsigned v=0;v<2;++v){
   if(mode>=20){auto referenceField=field;if(v==0)for(unsigned fy=0;fy<64;++fy)for(unsigned fx=0;fx<64;++fx){float x=-256.f+8*fx,y=-256.f+8*fy;float value=mode==20?2+.1f*x+.025f*y:(x<0?0.f:4.f);std::memcpy(referenceField.pixels.data()+16*(fy*64+fx),&value,4);}bind(13,referenceField);}
   ok(d->SetPixelShader(shaders[v]));ok(d->BeginScene());ok(NorthlightHdr::drawQuad(d,W,H));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[v].resize(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(output[v].data()+y*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*16);ok(staging->UnlockRect());
   for(unsigned i=0;i<W*H;++i){for(unsigned k=0;k<4;++k)assert(std::isfinite(output[v][i*4+k]));energy[v]+=output[v][i*4];if(mode==14&&actorEnabled)maxActorResidual=std::max(maxActorResidual,double(output[v][i*4]));if(mode==1||mode==3||mode==5||mode==7||(mode>=8&&mode<=13)||mode==16||mode==17)maxBlocked=std::max(maxBlocked,double(output[v][i*4]));}
   if(frame==8){std::string file="moon-"+std::to_string(mode)+"-"+std::to_string(v)+".f32";auto*f=std::fopen(file.c_str(),"wb");assert(f);std::fwrite(output[v].data(),4,output[v].size(),f);std::fclose(f);}
  }
  for(unsigned i=0;i<W*H;++i){if(mode<20)maxAlphaError=std::max(maxAlphaError,double(std::abs(output[0][i*4+3]-output[1][i*4+3])));if(mode==4)for(unsigned k=0;k<4;++k)maxSunError=std::max(maxSunError,double(std::abs(output[0][i*4+k]-output[1][i*4+k])));}
  if(mode==2)flatReference[frame]=output[0];
  if(mode==6||mode>=20){const auto& reference=mode==6?flatReference[frame]:output[0];for(unsigned i=0;i<W*H;++i)for(unsigned k=0;k<3;++k)maxReferenceError=std::max(maxReferenceError,double(std::abs(reference[i*4+k]-output[1][i*4+k])));}
  if(mode==0)minClear=std::min(minClear,energy[1]);if((mode==2||mode==6)&&frame)for(unsigned v=0;v<2;++v)maxTemporalStep=std::max(maxTemporalStep,std::abs(energy[v]-previous[v])/std::max(previous[v],.00001));if(mode>=20&&frame)maxFloorStep=std::max(maxFloorStep,std::abs(energy[1]-previous[1])/std::max(previous[1],.00001));previous[0]=energy[0];previous[1]=energy[1];
  if(frame==8)std::printf("MOON_SCENE mode=%u oldEnergy=%g newEnergy=%g\n",mode,energy[0],energy[1]);if(mode==6||mode==15||mode==19){assert(energy[0]==0&&energy[1]>0);}if(mode>=20)assert(energy[0]>0&&energy[1]>0);if(mode==6)slitEnergy[frame]=energy[1];if(mode==18)assert(energy[0]==0&&energy[1]>0&&energy[1]<slitEnergy[frame]);
  for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);for(auto*t:textures)t->Release();++cases;
 }
 std::printf("FLOOR_MOTION slopeAndStep65FramesEach maxEnergyStep=%g\n",maxFloorStep);
 std::printf("FLOOR_REFERENCE flatSlopedSteppedPerPixelMotion maxRgbError=%g\n",maxReferenceError);
 assert(maxReferenceError<1e-6);
 assert(maxFloorStep<.03);
 std::printf("ACTOR_RESIDUAL max=%g\n",maxActorResidual);
 std::printf("PRECHECK clear=%g blocked=%g alpha=%g sun=%g movement=%g\n",minClear,maxBlocked,maxAlphaError,maxSunError,maxTemporalStep);
 assert(minClear>0&&maxBlocked==0&&maxAlphaError==0&&maxSunError==0&&maxTemporalStep<.03&&maxActorResidual<1e-8);
 std::printf("MOON_PASS cases=%u blocked=%g alphaError=%g sunError=%g movingEnergyStep=%g timings=none\n",cases,maxBlocked,maxAlphaError,maxSunError,maxTemporalStep);
 d->SetPixelShader(nullptr);for(auto*s:shaders)s->Release();target->Release();staging->Release();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
