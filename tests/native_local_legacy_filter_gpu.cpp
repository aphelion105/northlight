// Fixed-world native receiver stability with an actual client shader pair.
// No game execution or timing measurements.
#include <windows.h>
#include <d3d9.h>
#include "native_local_light_renderer.h"
#include "native_direct_ownership.h"
#include "forwarders.h"
#include <cassert>
#include <cstdio>
#include <fstream>
static void must(HRESULT hr){if(FAILED(hr)){std::printf("HRESULT %08lx\n",(unsigned long)hr);std::abort();}}
static std::vector<DWORD> words(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f&&f.tellg()%4==0);std::vector<DWORD> out(size_t(f.tellg())/4);f.seekg(0);f.read(reinterpret_cast<char*>(out.data()),out.size()*4);assert(f);return out;}
static std::vector<float> pixels(IDirect3DDevice9* d,IDirect3DSurface9* rt,unsigned size){IDirect3DSurface9* copy=nullptr;must(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));must(d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock;must(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(size*size*4);for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size*4;++x)out[y*size*4+x]=NorthlightActorDeformation::half(reinterpret_cast<const uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x]);must(copy->UnlockRect());copy->Release();return out;}
struct FaultDevice:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned sets=0,failAt=0;bool failPrivatePS=false,failOriginalVS=false;IDirect3DVertexShader9* originalVS=nullptr;IDirect3DPixelShader9* originalPS=nullptr;
    bool fail(){return ++sets==failAt;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* p)override{if(fail()||(failOriginalVS&&p==originalVS))return E_FAIL;return real->SetVertexShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* p)override{if(fail())return E_FAIL;if(failPrivatePS&&p!=originalPS){failPrivatePS=false;return E_FAIL;}return real->SetPixelShader(p);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* p,UINT n)override{if(fail())return E_FAIL;return real->SetPixelShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* p)override{if(fail())return E_FAIL;return real->SetTexture(stage,p);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage,D3DSAMPLERSTATETYPE key,DWORD value)override{if(fail())return E_FAIL;return real->SetSamplerState(stage,key,value);}
};
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==7);constexpr unsigned Size=256;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightNativeLocalTest";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,Size,Size,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=Size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto v=words(argv[2]),p=words(argv[3]);
    NorthlightNativeLocalPatch::VertexInfo singleInfo;std::vector<uint32_t> singleVS;unsigned singleSlot=0;assert(NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),&singleSlot,1,singleVS,singleInfo));
    auto originalPixel=p;unsigned insertAt=1;while(insertAt<p.size()-1){unsigned op=p[insertAt]&65535,n=op==65534?(p[insertAt]>>16)&32767:(p[insertAt]>>24)&15;if(op!=31&&op!=81&&op!=65534)break;insertAt+=n+1;}
    unsigned padding=0;for(unsigned n=1;n<400;++n){p=originalPixel;std::vector<DWORD> paddingWords;for(unsigned i=0;i<n;++i)paddingWords.insert(paddingWords.end(),{0x02000001,0x800f0001,0xa0e40000});p.insert(p.begin()+insertAt,paddingWords.begin(),paddingWords.end());NorthlightNativeLocalPatch::PixelInfo pi;std::vector<uint32_t> result;
        bool stable=NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),singleInfo,reinterpret_cast<const uint32_t*>(kNativeLocalLighting1Shader),sizeof(kNativeLocalLighting1Shader)/4,result,pi,true)||NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),singleInfo,reinterpret_cast<const uint32_t*>(kNativeLocalLighting1Shader),sizeof(kNativeLocalLighting1Shader)/4,result,pi,false);
        bool legacy=NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(p.data()),p.size(),singleInfo,reinterpret_cast<const uint32_t*>(kNativeLocalLightingLegacy1Shader),sizeof(kNativeLocalLightingLegacy1Shader)/4,result,pi,true);
        if((std::getenv("NORTHLIGHT_PAD_SLOTS")&&n==unsigned(std::atoi(std::getenv("NORTHLIGHT_PAD_SLOTS"))))||(!std::getenv("NORTHLIGHT_PAD_SLOTS")&&legacy&&!stable)){padding=n;break;}}
    assert(padding);printf("LEGACY_PADDING %u\n",padding);
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;must(d->CreateVertexShader(v.data(),&vs));must(d->CreatePixelShader(p.data(),&ps));
    unsigned slots[]={0,1};std::vector<uint32_t> negative;NorthlightNativeLocalPatch::VertexInfo vi;const bool two=NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(v.data()),v.size())>=2;assert(NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),slots,2,negative,vi)==two);
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,28,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,7},{0,52,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,6},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));
    struct Vertex {float x,y,z;DWORD index;float nx,ny,nz;DWORD color;float u,v;float dx,dy,dz,hu,hv;};Vertex plane[]={{-8,8,8,0,0,0,-1,0xb3ffffff,0,0},{8,8,8,0,0,0,-1,0xb3ffffff,1,0},{-8,-8,8,0,0,0,-1,0xb3ffffff,0,1},{8,-8,8,0,0,0,-1,0xb3ffffff,1,1}};

    NorthlightNativeDisplacement displacement{};const bool gpuDisplacement=std::getenv("NORTHLIGHT_THREE_DISPLACEMENT")!=nullptr;
    if(gpuDisplacement){std::vector<uint32_t> displaced;NorthlightDisplacementPatch::Binding binding;assert(NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(v.data()),v.size(),displaced,binding,true,true));IDirect3DVertexShader9* bound=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(displaced.data()),&bound));displacement={vs,bound,binding.constant,binding.count,binding.sampler,true};vs=bound;
      for(auto& vertex:plane){vertex.dz=1.5f;vertex.hu=vertex.u;vertex.hv=vertex.v;}
      IDirect3DTexture9* height=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&height,nullptr));D3DLOCKED_RECT l;must(height->LockRect(0,&l,nullptr,0));for(unsigned y=0;y<8;++y)std::fill_n(reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch),8,.75f);must(height->UnlockRect(0));must(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+binding.sampler,height));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0+binding.sampler,key,D3DTEXF_POINT));height->Release();}
    IDirect3DTexture9* texture=nullptr;must(d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));D3DLOCKED_RECT lock;must(texture->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=((x+y)%2?0xff000000:0x00000000)|0x00b39980;must(texture->UnlockRect(0));
    IDirect3DCubeTexture9* cube=nullptr;must(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
    auto fillCube=[&](unsigned mode){for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l;must(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)[x]=mode==0?1.f:mode==1?0.f:x<128?1.f:0.f;must(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}};
    NorthlightNativeLocalDraw light;light.cube=cube;light.position[2]=4;light.range=12;for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;
    NorthlightNativeLocalLighting renderer(d);renderer.setColdTraceSink([](void*,const NorthlightNativeLocalLighting::ColdTrace& t)noexcept{if(t.success){printf("LEGACY_TRACE %u\n",unsigned(t.legacyFilter));if(std::getenv("NORTHLIGHT_EXPECT_LEGACY"))assert(t.legacyFilter);}});IDirect3DSurface9* resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));

    must(d->SetRenderTarget(0,resolved));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));must(d->SetVertexDeclaration(declaration));must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));
    for(auto state:{D3DRS_ZENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE})must(d->SetRenderState(state,FALSE));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    must(d->SetTexture(0,texture));must(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));must(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    float c[256][4]{};c[2][0]=c[3][1]=1;c[4][2]=64.f/63.9f;c[4][3]=-.1f*c[4][2];c[5][2]=1;c[6][0]=c[7][1]=1;
    for(unsigned k=0;k<3;++k){c[10][k]=.2f;c[11][k]=.1f;c[28][k]=1;c[21+k][2]=4;c[25][k]=1;c[26][k]=.2f;c[27][k]=.01f;}c[12][2]=1;c[28][3]=.7f;c[30][1]=c[30][2]=1;c[31][0]=c[32][1]=c[33][2]=1;
    float pc[224][4]{};for(unsigned k=0;k<4;++k)pc[0][k]=pc[1][k]=1;must(d->SetPixelShaderConstantF(0,pc[0],224));
    if(gpuDisplacement){c[displacement.control][0]=1; c[displacement.control+1][0]=.5f;}

    for(auto& vertex:plane){vertex.nx=1;vertex.ny=vertex.nz=0;}fillCube(1);unsigned wrong=0;
    for(unsigned scene=0;scene<6;++scene)for(float sourceZ:{4.f,7.9f,7.975f,8.f,8.025f,8.1f,9.f}){light.position[0]=4;light.position[2]=sourceZ;c[21][0]=4;c[21][2]=sourceZ;float values[3]{};std::vector<float> frames[3];
      if(scene<2)fillCube(scene?0:1);else {
        static const float forward[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}},right[6][3]={{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}},up[6][3]={{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
        for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l;must(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){float ray[3];for(unsigned k=0;k<3;++k)ray[k]=forward[face][k]+right[face][k]*(2*(x+.5f)/256-1)+up[face][k]*(1-2*(y+.5f)/256);float depth=1;
          auto hit=[&](float t,bool receiver){if(!(t>=.1f))return;float point[3];float distance2=0;for(unsigned k=0;k<3;++k){point[k]=light.position[k]+ray[k]*t;distance2+=ray[k]*t*ray[k]*t;}if(distance2>144)return;if(receiver){if(std::fabs(point[0])>8||std::fabs(point[1])>8)return;}else{if(std::fabs(point[1])>8||point[2]<-8||point[2]>16)return;if(scene==4&&(point[1]<0||point[1]>=.016f))return;}const float a=12.f/11.9f;depth=std::min(depth,a-.1f*a/t);};
          if(ray[2]!=0)hit((8-sourceZ)/ray[2],true);if(scene>=3&&ray[0]!=0)hit(((scene==5?.05f:2.f)-4)/ray[0],false);
          reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)[x]=depth;}must(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
      }

      for(unsigned mode=0;mode<3;++mode){c[17][0]=mode==1?0:1;must(d->SetVertexShaderConstantF(0,c[0],256));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};++light.rendererFrame;must(mode==2?renderer.draw(light,vs,ps,submit):submit());must(d->EndScene());auto image=pixels(d,resolved,Size);values[mode]=image[(128*Size+128)*4];uint64_t digest=14695981039346656037ull;for(float value:image){uint32_t bits;std::memcpy(&bits,&value,4);for(unsigned k=0;k<4;++k)digest=(digest^((bits>>(8*k))&255))*1099511628211ull;}printf("IMAGE %u %g %u %016llx\n",scene,sourceZ,mode,(unsigned long long)digest);frames[mode]=std::move(image);}
      const float contribution=values[0]-values[1],remaining=values[2]-values[1];assert(contribution>.005f);unsigned worstIndex=0;float selfError=0;for(unsigned i=0;i<frames[0].size();++i){if((scene==1||scene==2)&&std::fabs(frames[0][i]-frames[2][i])>selfError){selfError=std::fabs(frames[0][i]-frames[2][i]);worstIndex=i;}if(i%4==3)assert(frames[0][i]==frames[2][i]);}
      if(std::getenv("NORTHLIGHT_EXPECT_FIXED")){if(scene==0||scene==3)assert(std::fabs(remaining)<.001f);if(scene==1||scene==2)assert(selfError<=.001f);if(scene==4)assert(std::fabs(remaining-.5f*contribution)<.001f);if(scene==5)assert(remaining<contribution*.9f);}
      wrong+=(scene==0||scene==3)&&remaining>.005f;printf("SMOOTH_NORMAL scene=%u sourceZ=%g native=%g zero=%g blocked=%g contribution=%g remaining=%g selfFrameError=%g worstXY=%u,%u\n",scene,sourceZ,values[0],values[1],values[2],contribution,remaining,selfError,(worstIndex/4)%Size,(worstIndex/4)/Size);
    }
    if(std::getenv("NORTHLIGHT_EXPECT_LEGACY")){
        bool donorSeen=false;const auto before=renderer.legacyByCount[0];renderer.setColdTraceSink([](void* context,const NorthlightNativeLocalLighting::ColdTrace& t)noexcept{assert(t.success&&t.legacyFilter&&t.pixelReused&&t.pixelCreates==0);*static_cast<bool*>(context)=true;},&donorSeen);
        light.slot=1;++light.rendererFrame;must(renderer.draw(light,vs,ps,[]{return S_OK;}));assert(donorSeen&&renderer.legacyByCount[0]==before+1);printf("LEGACY_DONOR_PASS flag=1 newPS=0 histogram=exact\n");
    }
    printf("SMOOTH_NORMAL_SUMMARY nonzeroNativeBlockedButKept=%u\n",wrong);return 0;
}
