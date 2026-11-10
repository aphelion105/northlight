// Compare the production merged pass with independent lighting + GI.
#define main compositeFixtureMain
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <chrono>
static IDirect3DPixelShader9* shader(IDirect3DDevice9* d,const std::string& path){auto b=read(path);IDirect3DPixelShader9* p=nullptr;ok(d->CreatePixelShader((const DWORD*)b.data(),&p));return p;}
template<class Pixel>static IDirect3DTexture9* pixels(IDirect3DDevice9* d,unsigned w,unsigned h,Pixel pixel){Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(size_t(w)*h*16)};for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto v=pixel(x,y);std::memcpy(im.pixels.data()+(size_t(y)*w+x)*16,v.data(),16);}return texture(d,im);}
int main(int argc,char** argv){
 assert(argc==5);const std::string root=argv[2],proto=argv[3];const bool timing=std::atoi(argv[4]);const unsigned W=timing?1920:97,H=timing?1080:65;
 auto window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SEPARATEALPHABLENDENABLE})ok(d->SetRenderState(s,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
 std::vector<IDirect3DTexture9*> owned;
 for(unsigned i=0;i<16;++i){auto t=pixels(d,1,1,[&](unsigned,unsigned){return std::array<float,4>{1,1,1,1};});owned.push_back(t);ok(d->SetTexture(i,t));for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(i,s,D3DTEXF_POINT));ok(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto s:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(i,s,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
 auto bind=[&](unsigned slot,unsigned w,unsigned h,auto pixel){auto t=pixels(d,w,h,pixel);owned.push_back(t);ok(d->SetTexture(slot,t));};
 bind(1,W,H,[&](unsigned x,unsigned y){float z=4+8.f*x/W;float depth=1000.f/999.8f*(1-.2f/z);if(!timing&&y>H-7)depth=1;return std::array<float,4>{depth,0,0,0};});
 bind(14,W,H,[&](unsigned x,unsigned){return std::array<float,4>{0,0,-1,x%3?.8f:0.f};});
 bind(11,1,1,[&](unsigned,unsigned){return std::array<float,4>{6.5f,1,0,0};});
 bind(10,16,4,[&](unsigned x,unsigned y){const int a=int(x%4),b=int(y),c=int(x/4);return std::array<float,4>{float(a<2?a:a-4),float(b<2?b:b-4),float(c<2?c:c-4),1};});
 bind(7,16,24,[&](unsigned,unsigned){return std::array<float,4>{100,10000,1,5.f};});
 for(unsigned i=4;i<7;++i)bind(i,16,4,[&](unsigned x,unsigned){return std::array<float,4>{.3f+float(x%3)*.5f,0,0,0};});
 bind(8,48,4,[&](unsigned,unsigned){return std::array<float,4>{.2f,0,0,0};});
 float c[128][4]{};c[0][0]=.5f/W;c[0][1]=.5f/H;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=1;c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;for(unsigned i=0;i<4;++i)c[3+i][i]=1;for(unsigned i=0;i<4;++i)c[7+i][i]=c[11+i][i]=.01f;c[16][2]=-1;c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;c[19][3]=16;c[20][0]=4;c[20][1]=.7f;c[20][2]=.8f;c[20][3]=1;c[24][3]=5;c[27][1]=1;c[28][2]=-1;c[29][0]=.8f;c[29][1]=.6f;c[29][2]=.4f;c[33][0]=W*2;c[33][1]=H*2;c[33][2]=W;c[33][3]=H;c[73][2]=1;
 IDirect3DSurface9 *light=nullptr,*base=nullptr,*back=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&light,nullptr));ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&base,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&back,nullptr));
 auto gi=shader(d,root+"/WorldGI.bin");IDirect3DQuery9 *a=nullptr,*b=nullptr,*hz=nullptr;for(auto p:{&a,&b})ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,p));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));const auto get=[&](IDirect3DQuery9* q){UINT64 v=0;HRESULT hr;while((hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);return v;};
 auto quad=[&]{const Vertex v[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof v[0]));};
 auto image=[&](IDirect3DSurface9* s){ok(d->GetRenderTargetData(s,back));D3DLOCKED_RECT lock{};ok(back->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<uint16_t> out(size_t(W)*H*4);for(unsigned y=0;y<H;++y)std::memcpy(out.data()+size_t(y)*W*4,(char*)lock.pBits+y*lock.Pitch,W*8);ok(back->UnlockRect());return out;};
 unsigned cases=0;float maximum=0;
 {const unsigned soft=0;const std::string name="WorldLightingUnshadowed";auto old=shader(d,root+"/"+name+".bin"),fused=shader(d,proto+"/"+name+"GI.bin");
 for(unsigned test=0;test<(timing?2u:16u);++test){const unsigned sources=1+test%2;c[20][1]=(1+test%4)*.35f;c[18][3]=test%3*.2f;c[24][2]=test%5==2?2.f:0.f;c[20][3]=test%6!=3;c[24][3]=timing?6.f:5.f+float(test%4)*.15f;c[30][0]=!timing&&test%7==4?1.f:0.f;ok(d->SetPixelShaderConstantF(0,c[0],128));std::vector<uint16_t> reference,baseline;
 for(unsigned pass=0;pass<(timing?12u:2u);++pass){const bool merge=timing?(pass%4==1||pass%4==2):pass!=0;
 ok(hz->Issue(D3DISSUE_END));ok(a->Issue(D3DISSUE_END));const auto start=std::chrono::steady_clock::now();ok(d->BeginScene());ok(d->SetRenderTarget(0,light));D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
 for(unsigned source=0;source<sources;++source){float color[4]={source?.1f:.8f,.6f,.4f,1};ok(d->SetPixelShaderConstantF(17,color,1));ok(d->SetRenderTarget(1,source?nullptr:base));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,source!=0));ok(d->SetPixelShader(merge&&source+1==sources?fused:old));quad();}
 ok(d->SetRenderTarget(1,nullptr));if(!merge){ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));ok(d->SetPixelShader(gi));quad();}ok(d->EndScene());const auto end=std::chrono::steady_clock::now();ok(b->Issue(D3DISSUE_END));const double gpu=double(get(b)-get(a))*1000/get(hz);
 if(timing){printf("FUSED_GI_TIMING soft=%u sources=%u pass=%u merged=%u cpuMs=%.6f gpuMs=%.6f\n",soft,sources,pass,unsigned(merge),std::chrono::duration<double,std::milli>(end-start).count(),gpu);fflush(stdout);}else{auto out=image(light),baseOut=image(base);if(!pass){reference=std::move(out);baseline=std::move(baseOut);}else{assert(baseOut==baseline);for(size_t i=0;i<out.size();++i){if(i%4==3){assert(out[i]==reference[i]);continue;}const float a=half(reference[i]),b=half(out[i]),error=std::fabs(a-b);maximum=std::max(maximum,error);assert(std::isfinite(a)&&std::isfinite(b)&&error<=.002f+.002f*std::max(std::fabs(a),std::fabs(b)));}++cases;}}
 }
 }old->Release();fused->Release();}
 printf("FUSED_GI_PASS cases=%u maxAbs=%.9f baselineAlphaExact=1\n",cases,maximum);return 0;
}

