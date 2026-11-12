// Actual client three-light endpoints and physical-slot permutation.
// No game execution or timing measurements.
#include <windows.h>
#include <d3d9.h>
#include "native_local_light_renderer.h"
#include "native_direct_ownership.h"
#include "forwarders.h"
#include "native_local_sampler_read_checks.h"
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
    NativeSamplerReadDevice samplerDevice(d);d=&samplerDevice;
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
    NorthlightNativeLocalLighting renderer(d);samplerDevice.attach(renderer);IDirect3DSurface9* resolved=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));

    must(d->SetRenderTarget(0,resolved));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};must(d->SetViewport(&viewport));must(d->SetVertexDeclaration(declaration));must(d->SetVertexShader(vs));must(d->SetPixelShader(ps));
    for(auto state:{D3DRS_ZENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE})must(d->SetRenderState(state,FALSE));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    must(d->SetTexture(0,texture));must(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));must(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    float c[256][4]{};c[2][0]=c[3][1]=1;c[4][2]=64.f/63.9f;c[4][3]=-.1f*c[4][2];c[5][2]=1;c[6][0]=c[7][1]=1;
    for(unsigned k=0;k<3;++k){c[10][k]=.2f;c[11][k]=.1f;c[28][k]=1;c[21+k][2]=4;c[25][k]=1;c[26][k]=.2f;c[27][k]=.01f;}c[12][2]=1;c[28][3]=.7f;c[30][1]=c[30][2]=1;c[31][0]=c[32][1]=c[33][2]=1;
    float pc[224][4]{};for(unsigned k=0;k<4;++k)pc[0][k]=pc[1][k]=1;must(d->SetPixelShaderConstantF(0,pc[0],224));
    if(gpuDisplacement){c[displacement.control][0]=1; c[displacement.control+1][0]=.5f;}
    NorthlightNativeLocalDraw other=light;other.slot=1;
    NorthlightNativeLocalDraw third=light;third.slot=2;
    auto exact=[&](auto submit){third.position[0]=c[23][0];return renderer.draw(light,vs,ps,submit,&other,nullptr,gpuDisplacement?&displacement:nullptr,&third);};
    std::vector<float> images[2][2][2];
    for(unsigned clear=0;clear<2;++clear){fillCube(clear?0:1);for(unsigned permutation=0;permutation<2;++permutation){
      for(unsigned slot=0;slot<3;++slot){std::fill_n(c[17+slot],4,0.f);const unsigned source=permutation?2-slot:slot;c[17+slot][source]=.3f;c[21+slot][0]=float(source)-1;}light.position[0]=c[21][0];other.position[0]=c[22][0];must(d->SetVertexShaderConstantF(0,c[0],256));
      for(unsigned patched=0;patched<2;++patched){
        for(unsigned warm=0;warm<3;++warm){++light.rendererFrame;must(exact([]{return S_OK;}));}

        must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};must(patched?exact(submit):submit());must(d->EndScene());images[clear][permutation][patched]=pixels(d,resolved,Size);
      }
    }}
    float nativeDelta=0,shadowDelta=0,clearError=0;unsigned changed=0;
    for(unsigned i=0;i<images[0][0][0].size();++i){nativeDelta=std::max(nativeDelta,std::fabs(images[0][0][0][i]-images[0][1][0][i]));const float diff=std::fabs(images[0][0][1][i]-images[0][1][1][i]);shadowDelta=std::max(shadowDelta,diff);changed+=diff>.005f;for(unsigned p=0;p<2;++p)clearError=std::max(clearError,std::fabs(images[1][p][0][i]-images[1][p][1][i]));}
    printf("THREE_SOURCE_PERMUTATION lights=%u nativeDelta=%g shadowDelta=%g clearError=%g changedComponents=%u dualDraws=%llu\n",NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(v.data()),v.size()),nativeDelta,shadowDelta,clearError,changed,(unsigned long long)renderer.multiDraws);
    assert(nativeDelta<=.001f&&clearError==0&&shadowDelta<=.001f&&changed==0&&renderer.tripleDraws);
    for(unsigned i=0;i<3;++i)std::fill_n(c[17+i],3,0.f);must(d->SetVertexShaderConstantF(0,c[0],256));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());must(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex)));must(d->EndScene());auto zero=pixels(d,resolved,Size);float zeroError=0,removed=0;for(unsigned i=0;i<zero.size();++i){if((i/4)%Size>=80&&(i/4)%Size<176&&(i/4)/Size>=80&&(i/4)/Size<176)zeroError=std::max(zeroError,std::fabs(zero[i]-images[0][0][1][i]));removed=std::max(removed,std::fabs(images[0][0][0][i]-zero[i]));}printf("THREE_ENDPOINT zeroError=%g removed=%g\n",zeroError,removed);assert(zeroError<=.001f&&removed>.005f);

    // Every exact visibility endpoint uses three distinct physical cube objects.
    IDirect3DCubeTexture9* cubes[3]{cube,nullptr,nullptr};for(unsigned i=1;i<3;++i)must(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,cubes+i,nullptr));
    auto cubeValue=[&](unsigned source,float value){for(unsigned face=0;face<6;++face){D3DLOCKED_RECT locked;must(cubes[source]->LockRect(D3DCUBEMAP_FACES(face),0,&locked,nullptr,0));for(unsigned y=0;y<256;++y)std::fill_n(reinterpret_cast<float*>(static_cast<char*>(locked.pBits)+y*locked.Pitch),256,value);must(cubes[source]->UnlockRect(D3DCUBEMAP_FACES(face),0));}};
    unsigned endpointCases=0;float endpointWorst=0;std::vector<float> reference[8];
    for(unsigned ms:{0u,2u}){IDirect3DSurface9* target=resolved;if(ms){if(FAILED(d->CreateRenderTarget(Size,Size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(ms),0,FALSE,&target,nullptr)))continue;}must(d->SetRenderTarget(0,target));
    for(float strength:{.3f,12.f})for(float fog:{1.f,.35f})for(unsigned permutation=0;permutation<2;++permutation)for(unsigned mask=0;mask<8;++mask){
      for(unsigned i=0;i<3;++i)cubeValue(i,mask&(1u<<i)?0.f:1.f);
      const auto coldBefore=renderer.coldPairs;
      std::vector<float> result[3];for(unsigned patched=0;patched<3;++patched){
        for(unsigned slot=0;slot<3;++slot){unsigned source=permutation?2-slot:slot;std::fill_n(c[17+slot],4,0.f);c[17+slot][source]=!patched&&(mask&(1u<<source))?0:strength;c[21+slot][0]=float(source)-1;}
        c[30][1]=fog;must(d->SetVertexShaderConstantF(0,c[0],256));light.position[0]=c[21][0];other.position[0]=c[22][0];third.position[0]=c[23][0];light.cube=cubes[permutation?2:0];other.cube=cubes[1];third.cube=cubes[permutation?0:2];
        light.cubeIsWhite=patched==2&&!(mask&(1u<<(permutation?2:0)));other.cubeIsWhite=patched==2&&!(mask&2);third.cubeIsWhite=patched==2&&!(mask&(1u<<(permutation?0:2)));
        float saved[224][4],savedVS[256][4];must(d->GetPixelShaderConstantF(0,saved[0],224));must(d->GetVertexShaderConstantF(0,savedVS[0],256));IDirect3DBaseTexture9* textures[3]{};for(unsigned i=0;i<3;++i)must(d->GetTexture(1+i,textures+i));
        must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());auto submit=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));};must(patched?exact(submit):submit());must(d->EndScene());if(ms)must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));result[patched]=pixels(d,resolved,Size);
        float restored[224][4];must(d->GetPixelShaderConstantF(0,restored[0],224));assert(!std::memcmp(saved,restored,sizeof saved));float restoredVS[256][4];must(d->GetVertexShaderConstantF(0,restoredVS[0],256));assert(!std::memcmp(savedVS,restoredVS,sizeof savedVS));for(unsigned i=0;i<3;++i){IDirect3DBaseTexture9* textureNow=nullptr;must(d->GetTexture(1+i,&textureNow));assert(textureNow==textures[i]);if(textureNow)textureNow->Release();if(textures[i])textures[i]->Release();}
      }
      assert(result[2]==result[1]&&renderer.coldPairs==coldBefore);light.cubeIsWhite=other.cubeIsWhite=third.cubeIsWhite=false;
      if(!ms&&strength==12.f&&fog==1.f&&!permutation)reference[mask]=result[0];
      if(!mask)assert(result[0]==result[1]);for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned k=0;k<4;++k){unsigned i=(y*Size+x)*4+k;endpointWorst=std::max(endpointWorst,std::fabs(result[0][i]-result[1][i]));}++endpointCases;
    }if(ms)target->Release();}
    assert(endpointWorst<=.001f&&renderer.whiteCubeBypasses);printf("THREE_ALL_ENDPOINTS cases=%u maxError=%g clearExact=1 saturation=1 fog=1 msaa=1 constantsTexturesRestore=1 whiteProofExact=1 noNewVariants=1\n",endpointCases,endpointWorst);

    must(d->SetRenderTarget(0,resolved));float fractionalError=0,edgePermutationError=0;unsigned fractionalCases=0,pcfIntermediate=0;
    for(unsigned edge=0;edge<2;++edge){for(unsigned source=0;source<3;++source){if(!edge){cubeValue(source,0);continue;}for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l;must(cubes[source]->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(l.pBits)+y*l.Pitch)[x]=source==0?(x<128?1.f:0.f):source==1?(y<128?1.f:0.f):float((x+y)%2);must(cubes[source]->UnlockRect(D3DCUBEMAP_FACES(face),0));}}
      for(float fraction:{0.f,.25f,.5f,.75f,1.f}){const float gains[]={.3f,.7f,fraction};std::vector<float> permutations[2];
        for(unsigned permutation=0;permutation<2;++permutation){for(unsigned slot=0;slot<3;++slot){unsigned source=permutation?2-slot:slot;std::fill_n(c[17+slot],4,0.f);c[17+slot][source]=12;c[21+slot][0]=float(source)-1;}c[30][1]=1;must(d->SetVertexShaderConstantF(0,c[0],256));light.position[0]=c[21][0];other.position[0]=c[22][0];third.position[0]=c[23][0];light.cube=cubes[permutation?2:0];other.cube=cubes[1];third.cube=cubes[permutation?0:2];light.gain=gains[permutation?2:0];other.gain=gains[1];third.gain=gains[permutation?0:2];
          must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());must(exact([&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,plane,sizeof(Vertex));}));must(d->EndScene());permutations[permutation]=pixels(d,resolved,Size);
        }
        for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned k=0;k<3;++k){unsigned i=(y*Size+x)*4+k;if(!edge){double expected=0;for(unsigned mask=0;mask<8;++mask){double weight=1;for(unsigned source=0;source<3;++source)weight*=mask&(1u<<source)?gains[source]:1-gains[source];expected+=weight*reference[mask][i];}for(unsigned p=0;p<2;++p)fractionalError=std::max(fractionalError,float(std::fabs(permutations[p][i]-expected)));}else {edgePermutationError=std::max(edgePermutationError,std::fabs(permutations[0][i]-permutations[1][i]));if(k==2&&fraction==1&&permutations[0][i]>reference[7][i]+.001f&&permutations[0][i]<reference[0][i]-.001f)++pcfIntermediate;}}
        ++fractionalCases;
      }
    }
    assert(fractionalError<=.001f&&edgePermutationError<=.001f&&pcfIntermediate>100);printf("THREE_FRACTIONAL cases=%u trilinearError=%g pcfPermutationError=%g intermediatePCFComponents=%u\n",fractionalCases,fractionalError,edgePermutationError,pcfIntermediate);light.gain=other.gain=third.gain=1;
    // Cache reset keeps the established one-cold-attempt budget and rewarms
    // all three recipes without claiming a third correction before it exists.
    renderer.reset();++light.rendererFrame;auto triples=renderer.tripleDraws;must(exact([]{return S_OK;}));assert(renderer.tripleDraws==triples);auto cold=renderer.coldPairs;must(exact([]{return S_OK;}));assert(renderer.coldPairs==cold);++light.rendererFrame;must(exact([]{return S_OK;}));assert(renderer.tripleDraws==triples);++light.rendererFrame;must(exact([]{return S_OK;}));assert(renderer.tripleDraws==triples+1);

    // POM reserves TEXCOORD1/6/7: never steal them to force a third source.
    {unsigned slots3[]={0,1,2};std::vector<uint32_t> rejected;NorthlightNativeLocalPatch::VertexInfo info;assert(!NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(v.data()),v.size(),slots3,3,rejected,info,0xc2u));}
    // A valid two-source material remains available when three endpoints exceed512.
    {auto padded=p;for(unsigned i=0;i<30;++i)padded.insert(padded.end()-1,{0x02000001u,0x800f000fu,0x90e40000u});IDirect3DPixelShader9* over=nullptr;must(d->CreatePixelShader(padded.data(),&over));must(d->SetPixelShader(over));NorthlightNativeLocalLighting limited(d);unsigned submits=0;
     for(unsigned i=0;i<4;++i){++light.rendererFrame;must(limited.draw(light,vs,over,[&]{++submits;return S_OK;},&other,nullptr,gpuDisplacement?&displacement:nullptr,&third));}assert(submits==4&&limited.multiDraws&&!limited.tripleDraws&&limited.coldPairs==3);must(d->SetPixelShader(ps));over->Release();}
    // All setters on the three-source binding/restore path may fail once.
    {FaultDevice fault(d);fault.originalVS=vs;fault.originalPS=ps;NorthlightNativeLocalLighting checked(&fault);
     for(unsigned i=0;i<3;++i){++light.rendererFrame;must(checked.draw(light,vs,ps,[]{return S_OK;},&other,nullptr,gpuDisplacement?&displacement:nullptr,&third));}assert(checked.tripleDraws);
     const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
     float saved[224][4];must(d->GetPixelShaderConstantF(0,saved[0],224));DWORD states[3][7];IDirect3DBaseTexture9* textures[3]{};for(unsigned i=0;i<3;++i){must(d->GetTexture(i+1,textures+i));for(unsigned k=0;k<7;++k)must(d->GetSamplerState(i+1,keys[k],&states[i][k]));}
     auto state=[&]{float actual[224][4];must(d->GetPixelShaderConstantF(0,actual[0],224));assert(!std::memcmp(saved,actual,sizeof saved));IDirect3DVertexShader9* vertex=nullptr;IDirect3DPixelShader9* pixel=nullptr;must(d->GetVertexShader(&vertex));must(d->GetPixelShader(&pixel));assert(vertex==vs&&pixel==ps);vertex->Release();pixel->Release();for(unsigned i=0;i<3;++i){IDirect3DBaseTexture9* t=nullptr;must(d->GetTexture(i+1,&t));assert(t==textures[i]);if(t)t->Release();for(unsigned k=0;k<7;++k){DWORD value;must(d->GetSamplerState(i+1,keys[k],&value));assert(value==states[i][k]);}}};
     fault.sets=0;must(checked.draw(light,vs,ps,[]{return S_OK;},&other,nullptr,gpuDisplacement?&displacement:nullptr,&third));const unsigned setters=fault.sets;
     for(unsigned i=1;i<=setters;++i){fault.sets=0;fault.failAt=i;unsigned submits=0;must(checked.draw(light,vs,ps,[&]{++submits;return S_OK;},&other,nullptr,gpuDisplacement?&displacement:nullptr,&third));assert(submits==1);fault.failAt=0;state();}
     for(auto* t:textures)if(t)t->Release();printf("THREE_FAILURES setters=%u exactlyOneSubmit=1 fullStateRestored=1 unsupportedThreeKeepsTwo=1 reservedPOMRejected=1\n",setters);}
    if(std::getenv("NORTHLIGHT_TEST_SAMPLER_BULK_MODE"))samplerDevice.verify(light,vs,ps,3,nullptr,gpuDisplacement?&displacement:nullptr);
    for(unsigned i=1;i<3;++i)cubes[i]->Release();
    printf("THREE_COMPOSITION gpuDisplacement=%u vertexConstantsRestored=1\n",unsigned(gpuDisplacement));
    return 0;
}
