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
    auto v=words(argv[2]),p=words(argv[3]);IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;must(d->CreateVertexShader(v.data(),&vs));must(d->CreatePixelShader(p.data(),&ps));
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
    NorthlightNativeLocalLighting renderer(d);IDirect3DSurface9* resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));

    must(d->SetRenderTarget(0,resolved));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));must(d->SetVertexDeclaration(declaration));must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));
    for(auto state:{D3DRS_ZENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE})must(d->SetRenderState(state,FALSE));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    must(d->SetTexture(0,texture));must(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));must(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    float c[256][4]{};c[2][0]=c[3][1]=1;c[4][2]=64.f/63.9f;c[4][3]=-.1f*c[4][2];c[5][2]=1;c[6][0]=c[7][1]=1;
    for(unsigned k=0;k<3;++k){c[10][k]=.2f;c[11][k]=.1f;c[28][k]=1;c[21+k][2]=4;c[25][k]=1;c[26][k]=.2f;c[27][k]=.01f;}c[12][2]=1;c[28][3]=.7f;c[30][1]=c[30][2]=1;c[31][0]=c[32][1]=c[33][2]=1;
    float pc[224][4]{};for(unsigned k=0;k<4;++k)pc[0][k]=pc[1][k]=1;must(d->SetPixelShaderConstantF(0,pc[0],224));
    if(gpuDisplacement){c[displacement.control][0]=1; c[displacement.control+1][0]=.5f;}

    IDirect3DSurface9 *probeTarget=nullptr,*probeRead=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&probeTarget,nullptr));must(d->CreateOffscreenPlainSurface(Size,Size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&probeRead,nullptr));
    IDirect3DVertexShader9* probeVS=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(negative.data()),&probeVS));assert(vi.varying[0]==1);IDirect3DPixelShader9* probes[2]{};for(unsigned i=0;i<2;++i){auto code=words(i?"ProbeNormal.bin":"ProbeView.bin");must(d->CreatePixelShader(code.data(),probes+i));}
    fillCube(2);float globalSpan=0;unsigned unstable=0;const float pi=3.14159265358979323846f;
    for(unsigned large=0;large<2;++large)for(unsigned phase=0;phase<9;++phase){float minimum=1e9f,maximum=-1e9f,nativeMin=1e9f,nativeMax=-1e9f,viewError=0,normalError=0,worldError=0;const float origin[3]={large?-8565.f:0.f,large?1011.f:0.f,large?95.f:0.f};const float sourceX=-float(phase)*.00390625f;
      for(int step=-180;step<=180;++step){const float a=step*.4f*pi/180,sn=std::sin(a),cs=std::cos(a);std::fill_n(light.inverseView,16,0.f);light.inverseView[0]=cs;light.inverseView[2]=sn;light.inverseView[5]=1;light.inverseView[8]=-sn;light.inverseView[10]=cs;light.inverseView[12]=origin[0]+8*sn;light.inverseView[13]=origin[1];light.inverseView[14]=origin[2]+8-8*cs;light.inverseView[15]=1;
        light.position[0]=origin[0]+sourceX;light.position[1]=origin[1];light.position[2]=origin[2]+4;
        c[31][0]=cs;c[31][1]=0;c[31][2]=sn;c[31][3]=-8*sn;c[32][0]=0;c[32][1]=1;c[32][2]=c[32][3]=0;c[33][0]=-sn;c[33][1]=0;c[33][2]=cs;c[33][3]=8-8*cs;
        const float ex=light.inverseView[12]-origin[0],ez=light.inverseView[14]-origin[2];c[31][3]=-(cs*ex+sn*ez);c[33][3]=-(-sn*ex+cs*ez);const float targetX=8*sn+c[31][3],targetZ=8*cs+c[33][3];
        c[12][0]=sn;c[12][1]=0;c[12][2]=cs;c[2][2]=-targetX/targetZ;c[3][2]=0;c[21][0]=cs*sourceX+4*sn+c[31][3];c[21][1]=0;c[21][2]=-sn*sourceX+4*cs+c[33][3];c[17][0]=1;c[17][1]=c[17][2]=0;
        float values[3]{};for(unsigned mode=0;mode<3;++mode){c[17][0]=mode==1?0:1;must(d->SetVertexShaderConstantF(0,c[0],256));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};++light.rendererFrame;must(mode==2?renderer.draw(light,vs,ps,submit):submit());must(d->EndScene());auto image=pixels(d,resolved,Size);values[mode]=image[(128*Size+128)*4];}

        if(phase==4){must(d->SetRenderTarget(0,probeTarget));float saved[4][4];must(d->GetPixelShaderConstantF(3,saved[0],4));must(d->SetPixelShaderConstantF(3,light.inverseView,4));must(d->SetVertexShader(probeVS));
          for(unsigned probe=0;probe<2;++probe){must(d->SetPixelShader(probes[probe]));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());must(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex)));must(d->EndScene());must(d->GetRenderTargetData(probeTarget,probeRead));D3DLOCKED_RECT probeLock;must(probeRead->LockRect(&probeLock,nullptr,D3DLOCK_READONLY));float sample[4];std::memcpy(sample,reinterpret_cast<const float*>(static_cast<const char*>(probeLock.pBits)+128*probeLock.Pitch)+128*4,16);must(probeRead->UnlockRect());const float* pixel=sample;for(unsigned k=0;k<3;++k){if(probe)normalError=std::max(normalError,std::fabs(pixel[k]-(k==2?-1.f:0.f)));else{viewError=std::max(viewError,std::fabs(pixel[k]-(k==0?targetX:k==2?targetZ:0.f)));float world=pixel[0]*light.inverseView[k]+pixel[1]*light.inverseView[4+k]+pixel[2]*light.inverseView[8+k]+light.inverseView[12+k];worldError=std::max(worldError,std::fabs(world-origin[k]-(k==2?8.f:0.f)));}}}
          must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));must(d->SetPixelShaderConstantF(3,saved[0],4));must(d->SetRenderTarget(0,resolved));}
        if(values[0]-values[1]>.001f){float visibility=(values[2]-values[1])/(values[0]-values[1]);minimum=std::min(minimum,visibility);maximum=std::max(maximum,visibility);nativeMin=std::min(nativeMin,values[0]);nativeMax=std::max(nativeMax,values[0]);}
      }
      float span=maximum-minimum;globalSpan=std::max(globalSpan,span);unstable+=span>.1f;printf("CAMERA_ORBIT largeWorld=%u phase=%u visibilityMin=%g max=%g span=%g nativeSpan=%g viewError=%g normalError=%g worldError=%g\n",large,phase,minimum,maximum,span,nativeMax-nativeMin,viewError,normalError,worldError);
    }
    printf("CAMERA_ORBIT_SUMMARY maxSpan=%g unstable=%u\n",globalSpan,unstable);return 0;
}
