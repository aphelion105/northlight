// Exercise the production queue/MRT transaction, not an isolated shader path.
#define main legacyModelNormalMain
#include "model_normals_gpu.cpp"
#undef main
#include "auto_parallax_patch.h"
#include "native_local_light_renderer.h"
#include "displacement_shader_patch.h"
#include "hdr_sky_foreground.h"

struct ModelNormalPacketTestAccess {
    static unsigned committed(const NorthlightModelNormals& n){return n.nativeCommittedPackets;}
    static std::string stats(const NorthlightModelNormals& n){std::string text;n.nativeMrt.report(0,[&](const char* format,auto... args){char line[1024];std::snprintf(line,sizeof line,format,args...);text+=line;});return text;}
};
struct NativeCalls:CaptureCalls {
    unsigned vsSets=0,psSets=0,failVS=0,failPS=0,fillSuccess=0,fillFailure=0;bool after=false,denyFill=false,denyFormat=false;
    using CaptureCalls::CaptureCalls;
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* shader)override{if(++vsSets==failVS){if(after)ok(real->SetVertexShader(shader));return E_FAIL;}return real->SetVertexShader(shader);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{if(++psSets==failPS){if(after)ok(real->SetPixelShader(shader));return E_FAIL;}return real->SetPixelShader(shader);}
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* target,const RECT* rect,D3DCOLOR color)override{const auto hr=denyFill?D3DERR_INVALIDCALL:real->ColorFill(target,rect,color);++(SUCCEEDED(hr)?fillSuccess:fillFailure);return hr;}
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** api)override{if(denyFormat){*api=nullptr;return E_FAIL;}return real->GetDirect3D(api);}
};
static void dispose(Target& t){t.read->Release();t.s->Release();t.t->Release();}
static std::vector<unsigned char> canonicalNormals(std::vector<unsigned char> data){
    for(size_t at=0;at<data.size();at+=8){bool zero=true;for(unsigned c=0;c<6;++c)zero&=data[at+c]==0;
        if(zero)data[at+6]=data[at+7]=0;}return data;
}
int main(int argc,char** argv){
    assert(argc>=3&&argc<=6);std::setvbuf(stdout,nullptr,_IONBF,0);const std::string dir=argv[2];
    const unsigned timing=argc>=4?unsigned(std::atoi(argv[3])):0,N=timing?timing:3,copies=argc>=5?unsigned(std::atoi(argv[4])):1;assert(copies>=1&&copies<=32);
    const std::string timingMode=argc==6?argv[5]:"plain";
    const bool timingLocal=timingMode.find("local")!=std::string::npos;
    const bool timingBaseline=timingMode.find("baseline")!=std::string::npos;
    const bool timingBlend=timingMode.find("blend")!=std::string::npos;
    const bool timingSrgb=timingMode.find("srgb")!=std::string::npos;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightNativeNormalTransaction";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    NativeCalls calls(raw);DeviceMirror mirror;ExtensionDevice extension(&calls,&mirror);MirrorGuard held(mirror.gate);d=&extension;
    const unsigned W=timing?3840:256,H=timing?2160:256;IDirect3DSurface9* depth=nullptr;ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,48,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,52,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,7},{0,64,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,6},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));
    auto bytes=read(dir+"human-0.mesh");unsigned nv,ni;std::memcpy(&nv,bytes.data()+4,4);std::memcpy(&ni,bytes.data()+8,4);assert(bytes.size()==12+nv*48+ni*4);auto* originalVertices=bytes.data()+12;auto* indices=originalVertices+nv*48;
    constexpr unsigned Stride=72;std::vector<unsigned char> expanded(size_t(nv)*Stride);
    for(unsigned i=0;i<nv;++i){auto* v=expanded.data()+i*Stride;std::memcpy(v,originalVertices+i*48,48);std::memset(v+48,255,4);std::memcpy(v+64,v+32,8);}
    std::vector<uint32_t> copiedIndices;
    if(copies>1){assert(timing);const auto originalCount=nv;expanded.resize(size_t(nv)*Stride*copies);copiedIndices.resize(size_t(ni)*copies);
        for(unsigned c=0;c<copies;++c){if(c)std::memcpy(expanded.data()+size_t(c)*nv*Stride,expanded.data(),size_t(nv)*Stride);
            for(unsigned i=0;i<ni;++i){uint32_t index;std::memcpy(&index,indices+i*4,4);copiedIndices[c*ni+i]=index+c*originalCount;}}
        nv*=copies;ni*=copies;indices=reinterpret_cast<unsigned char*>(copiedIndices.data());}
    auto* vertices=expanded.data();
    auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->topology=D3DPT_TRIANGLELIST;m->primitiveCount=ni/3;m->vertexCount=nv;m->indexed=true;m->indices.assign(reinterpret_cast<const uint32_t*>(indices),reinterpret_cast<const uint32_t*>(indices)+ni);m->streams[0].stride=Stride;m->streams[0].bytes.assign(vertices,vertices+nv*Stride);std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh=m;
    const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,nv,0,ni/3,true};
    IDirect3DTexture9* checker=nullptr;ok(d->CreateTexture(16,16,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&checker,nullptr));D3DLOCKED_RECT lock{};ok(checker->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)reinterpret_cast<DWORD*>((char*)lock.pBits+y*lock.Pitch)[x]=(x^y)&1?0xffe0a060:0x30305090;ok(checker->UnlockRect(0));
    auto nativePS=ps(dir+"ModelAlphaPS.bin");
    using NorthlightWaterShaderPatch::dst;using NorthlightWaterShaderPatch::src;
    const DWORD occluderCode[]={0xffff0300u,0x05000051u,dst(2,0),0x3e4ccccdu,0x3e99999au,0x3f000000u,0x3eb33333u,0x02000001u,dst(8,0),src(2,0),65535u};
    IDirect3DPixelShader9* occluderPS=nullptr;ok(d->CreatePixelShader(occluderCode,&occluderPS));
    float bank[256][4]={};const float A=500.f/499.5f,B=-.5f*A;bank[2][0]=bank[3][1]=1;bank[4][2]=A;bank[4][3]=B;bank[5][2]=1;bank[28][3]=1;
    bank[6][0]=bank[7][1]=1;bank[12][2]=-1;bank[10][0]=.6f;bank[10][1]=.7f;bank[10][2]=.8f;for(unsigned c=0;c<3;++c){bank[11][c]=.15f;bank[28][c]=1;}bank[30][1]=bank[30][2]=1;
    auto pose=[&](float z,float side){for(unsigned b=0;b<75;++b){const auto r=31+3*b;bank[r][0]=bank[r+1][1]=z*.4f;bank[r+2][2]=.01f;bank[r][3]=z*side;bank[r+1][3]=-.3f*z;bank[r+2][3]=z;}ok(d->SetVertexShaderConstantF(0,bank[0],256));};
    auto draw=[&]{return d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,nv,ni/3,indices,D3DFMT_INDEX32,vertices,Stride);};
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    if(timing){ok(d->CreateVertexBuffer(nv*Stride,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr));ok(d->CreateIndexBuffer(ni*4,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&ib,nullptr));void* p=nullptr;
        ok(vb->Lock(0,0,&p,0));std::memcpy(p,vertices,nv*Stride);ok(vb->Unlock());ok(ib->Lock(0,0,&p,0));std::memcpy(p,indices,ni*4);ok(ib->Unlock());
        m->sourceIdentity=NorthlightSourceIdentity::make({0x6d7274u,1});m->sharedCapture=true;}
    auto nativeGeometry=[&]{return timing?d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,nv,0,ni/3):draw();};

    auto state=[&](Target& color){for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,color.s));ok(d->SetDepthStencilSurface(depth));viewport(color);
        for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(key,FALSE));
        ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,timing?15:7));ok(d->SetRenderState(D3DRS_CLIPPING,TRUE));ok(d->SetRenderState(D3DRS_DEPTHBIAS,0));ok(d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0));ok(d->SetVertexDeclaration(decl));bind(0,checker);};
    unsigned cases=0,commits=0;
    std::vector<std::string> files={"model-vs3-rigid.bin","model-vs3-skinned.bin","model-vs3-weighted.bin","model-vs3-weighted4.bin"};
    if(std::ifstream(dir+"model-vs3-local.bin").good())files.push_back("model-vs3-local.bin");
    for(const auto& filename:files)for(unsigned material=0;material<3;++material)for(bool hdr:{false,true})for(bool alpha:{false,true}){
        if(timing&&(filename!=(timingLocal?"model-vs3-local.bin":"model-vs3-rigid.bin")||material||alpha))continue;
        const auto* file=filename.c_str();const bool local=filename=="model-vs3-local.bin";
        auto source=vs(dir+file);auto color=target(W,H,hdr?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8,hdr?8:4);
        IDirect3DSurface9 *alternateColor=nullptr,*alternateDepth=nullptr;
        if(!timing){ok(d->CreateRenderTarget(W,H,color.format,D3DMULTISAMPLE_NONE,0,FALSE,&alternateColor,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&alternateDepth,nullptr));}
        IDirect3DSurface9 *msColor=nullptr,*msDepth=nullptr;
        if(!timing&&SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,color.format,TRUE,D3DMULTISAMPLE_2_SAMPLES,nullptr))&&
           SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_D24S8,TRUE,D3DMULTISAMPLE_2_SAMPLES,nullptr))){
            ok(d->CreateRenderTarget(W,H,color.format,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&msColor,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&msDepth,nullptr));}
        auto n=std::make_unique<NorthlightModelNormals>(d);n->setDeferredCapture(true);n->setRasterStateSource(&extension,[](void* e,const D3DRENDERSTATETYPE* keys,unsigned count,DWORD* values){return static_cast<ExtensionDevice*>(e)->knownRenderStates(keys,count,values);});
        n->setEmptyTargetSource(&extension,[](void* e){return static_cast<ExtensionDevice*>(e)->emptyAuxiliaryTargets();});
        NorthlightGpuScene::NativeStore store;NorthlightGpuScene::NativeClient cache(store,NorthlightGpuScene::Consumer::Material);if(timing)n->setGeometryCache(&cache);
        IDirect3DVertexShader9* privateVS=nullptr;IDirect3DPixelShader9* privatePS=nullptr;unsigned pomControl=0;NorthlightDisplacementPatch::Binding displacementBinding;
        UINT vsBytes=0,psBytes=0;ok(source->GetFunction(nullptr,&vsBytes));ok(nativePS->GetFunction(nullptr,&psBytes));std::vector<std::uint32_t> vcode(vsBytes/4),pcode(psBytes/4),patched;
        ok(source->GetFunction(reinterpret_cast<DWORD*>(vcode.data()),&vsBytes));ok(nativePS->GetFunction(reinterpret_cast<DWORD*>(pcode.data()),&psBytes));
        IDirect3DVertexShader9* alternateSource=nullptr;ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(vcode.data()),&alternateSource));assert(alternateSource!=source);
        if(material==1){assert(!NorthlightWaterShaderPatch::patch(vcode.data(),vcode.size(),patched));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&privateVS));
            assert(NorthlightParallaxPatch::pixel(pcode.data(),pcode.size(),false,patched,&pomControl));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&privatePS));}
        if(material==2){assert(NorthlightDisplacementPatch::patch(vcode.data(),vcode.size(),patched,displacementBinding,true,true));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&privateVS));}
        NorthlightNativePOM pom{source,privateVS,nativePS,privatePS,pomControl,2,false,false};
        NorthlightNativeDisplacement displacement{source,privateVS,displacementBinding.constant,displacementBinding.count,displacementBinding.sampler,true};
        IDirect3DTexture9* height=nullptr;if(material==2){ok(d->CreateTexture(4,4,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&height,nullptr));D3DLOCKED_RECT l{};ok(height->LockRect(0,&l,nullptr,0));for(unsigned y=0;y<4;++y)std::fill_n(reinterpret_cast<float*>((char*)l.pBits+y*l.Pitch),4,.5f);ok(height->UnlockRect(0));
            const auto slot=D3DVERTEXTEXTURESAMPLER0+displacementBinding.sampler;ok(d->SetTexture(slot,height));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,key,D3DTEXF_POINT));}
        NorthlightNativeLocalLighting lighting(d);IDirect3DCubeTexture9* cube=nullptr;NorthlightNativeLocalDraw light,second,third;
        if(local){ok(d->CreateCubeTexture(4,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l{};ok(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<4;++y)std::fill_n(reinterpret_cast<float*>((char*)l.pBits+y*l.Pitch),4,.5f);ok(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
            light.cube=cube;light.range=24;light.position[2]=4;for(unsigned i=0;i<16;++i)light.inverseView[i]=i%5==0?1.f:0;second=third=light;second.slot=1;third.slot=2;
            for(unsigned i=0;i<4;++i){bank[17+i][0]=.2f;bank[17+i][1]=.15f;bank[17+i][2]=.1f;bank[21+i][2]=4;bank[25][i]=1;bank[26][i]=.1f;bank[27][i]=.01f;}}
        auto proof=[&](IDirect3DVertexShader9* original,IDirect3DVertexShader9* bound,IDirect3DPixelShader9* pixel)->unsigned{if(lighting.provesReceiverPair(original,bound,pixel))return NorthlightNativeModelNormals::PairValid|NorthlightNativeModelNormals::OuterRestoresVertex|NorthlightNativeModelNormals::OuterRestoresPixel;return original==bound||
            (material==1&&pom.originalVS==original&&pom.matches(bound,pixel))||(material==2&&displacement.originalVS==original&&displacement.matches(bound))||lighting.provesReceiverPair(original,bound,pixel);};
        unsigned frameNumber=0;
        auto sky=target(W,H,D3DFMT_A16B16G16R16F,8);NorthlightHdr::SkyForeground skyCapture;
        std::vector<unsigned char> referenceColor,referenceNormals,referenceSky;
        auto frame=[&](bool mrt,unsigned scenario=0,unsigned fault=0){
            n->endFrame();n->enableNativeMrt(mrt);state(color);
            if(scenario==10){ok(d->SetDepthStencilSurface(alternateDepth));ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));ok(d->SetDepthStencilSurface(depth));}
            if(scenario==7){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,msColor));ok(d->SetDepthStencilSurface(msDepth));}
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff112233,1,0));
            if(scenario==22){IDirect3DSurface9* previous=nullptr;ok(d->GetRenderTarget(0,&previous));ok(d->SetRenderTarget(0,sky.s));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetRenderTarget(0,previous));previous->Release();}
            unsigned nativeDraws=0;ok(d->BeginScene());
            auto occluder=[&]{
                n->beginNativeDraw();ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetPixelShader(scenario>=24?nullptr:occluderPS));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
                if(scenario>=24)ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,scenario%2?8:0));
                if(scenario>=28)ok(d->SetRenderState(D3DRS_ZFUNC,scenario<30?D3DCMP_EQUAL:D3DCMP_NEVER));
                if(scenario==12||(scenario>=14&&scenario<=22)){ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
                    ok(d->SetRenderState(D3DRS_SRCBLEND,scenario==14||scenario==16?D3DBLEND_ONE:D3DBLEND_SRCALPHA));
                    ok(d->SetRenderState(D3DRS_DESTBLEND,scenario==15||scenario==16?D3DBLEND_ZERO:scenario==17?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));
                    ok(d->SetRenderState(D3DRS_BLENDOP,scenario==18?D3DBLENDOP_SUBTRACT:D3DBLENDOP_ADD));}
                if(scenario==19)ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,TRUE));
                if(scenario==13||scenario==20)ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
                Quad q[]={{-.5f,-.5f,.3f,1,0,0},{W*.5f-.5f,-.5f,.3f,1,1,0},{-.5f,H-.5f,.3f,1,0,1},{W*.5f-.5f,H-.5f,.3f,1,1,1}};
                if(scenario==28||scenario==29)for(auto& vertex:q)vertex.z=1; // EQUAL also passes over untouched background.
                auto submit=[&]{return n->nativeDraw([&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Quad));},scenario!=6&&scenario<12,proof);};
                if(scenario==22){HRESULT hr=E_FAIL;assert(skyCapture.draw(d,sky.s,false,submit,hr));ok(hr);}else ok(submit());
                ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
                if(scenario>=24)ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
                if(scenario>=28)ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
                ok(d->SetVertexDeclaration(decl));
            };
            if(scenario==2)occluder();
            if(scenario==23)ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,TRUE));
            for(unsigned i=0;i<N;++i){
                auto* receiver=scenario==11&&i==1?alternateSource:source;
                n->beginNativeDraw();ok(d->SetVertexShader(receiver));ok(d->SetPixelShader(nativePS));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,alpha));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,128));pose(i==2?49.f:i?20.f:2.f,i==2?.5f:0);
                if(timing){pose(10.f+(i%7)*.1f,(int(i%17)-8)*.025f);ok(d->SetStreamSource(0,vb,0,Stride));ok(d->SetIndices(ib));}
                n->capture(receiver,nativeGeometry,!timing,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&mesh,decl);
                IDirect3DSurface9* occupied[3]={};
                if(scenario==1&&i==1)for(unsigned slot=1;slot<4;++slot){ok(d->CreateRenderTarget(W,H,color.format,D3DMULTISAMPLE_NONE,0,FALSE,occupied+slot-1,nullptr));ok(d->SetRenderTarget(slot,occupied[slot-1]));}
                if(privateVS)ok(d->SetVertexShader(privateVS));if(privatePS)ok(d->SetPixelShader(privatePS));
                if(i==1){if(scenario==8)ok(d->SetRenderTarget(0,alternateColor));if(scenario==9){D3DVIEWPORT9 vp={0,0,W/2,H,0,1};ok(d->SetViewport(&vp));}if(scenario==10)ok(d->SetDepthStencilSurface(alternateDepth));}
                if(material==1){const float controls[]={1,1,1,0,0,16,4,0};ok(d->SetPixelShaderConstantF(pomControl,controls,2));}
                if(material==2){const float controls[]={0,0,0,0,.5f,0,0,0};ok(d->SetVertexShaderConstantF(displacementBinding.constant,controls,displacementBinding.count));}
                auto submit=[&](){
                if(fault&&i==1){calls.after=fault&1;calls.failAfter=fault&1;switch(fault/2){case 1:calls.failTarget=calls.targetSets+1;break;case 2:calls.failRender=calls.renderSets+1;break;case 3:calls.failVS=calls.vsSets+1;break;case 4:calls.failPS=calls.psSets+1;break;}}
                const auto hr=n->nativeDraw([&]{++nativeDraws;return nativeGeometry();},true,proof);
                calls.failTarget=calls.failRender=calls.failVS=calls.failPS=0;return hr;};
                light.rendererFrame=++frameNumber;second.rendererFrame=third.rendererFrame=frameNumber;
                const auto hr=local?lighting.draw(light,privateVS?privateVS:source,privatePS?privatePS:nativePS,submit,&second,material==1?&pom:nullptr,material==2?&displacement:nullptr,&third):submit();ok(hr);
                if(i==1){if(scenario==8)ok(d->SetRenderTarget(0,color.s));if(scenario==9)viewport(color);if(scenario==10)ok(d->SetDepthStencilSurface(depth));}
                if(privateVS)ok(d->SetVertexShader(source));if(privatePS)ok(d->SetPixelShader(nativePS));
                for(unsigned slot=1;slot<4;++slot)if(occupied[slot-1]){ok(d->SetRenderTarget(slot,nullptr));occupied[slot-1]->Release();}
                IDirect3DVertexShader9* currentVS=nullptr;IDirect3DPixelShader9* currentPS=nullptr;ok(raw->GetVertexShader(&currentVS));ok(raw->GetPixelShader(&currentPS));assert(currentVS==receiver&&currentPS==nativePS);currentVS->Release();currentPS->Release();
                DWORD mask=0;ok(raw->GetRenderState(D3DRS_COLORWRITEENABLE1,&mask));assert(mask==(timing?15u:7u));
                if(scenario==4&&i==0){n->nativeDepthChanged();ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));}
                if(scenario==5&&i==0)n->beforeTextureWrite(checker);
                if(scenario>=26&&i==0)occluder();
            }
            if(scenario==3||scenario==6||(scenario>=12&&scenario<=22)||scenario==24||scenario==25)occluder();
            assert(nativeDraws==N);assert(n->finishCaptures(true));ok(d->EndScene());assert(n->texture());
            if(timing)return ModelNormalPacketTestAccess::committed(*n);
            if(scenario==7)ok(d->StretchRect(msColor,nullptr,color.s,nullptr,D3DTEXF_NONE));
            auto rgb=pixels(color),normals=canonicalNormals(capturedPixels(n->texture(),W,H));
            const unsigned committed=ModelNormalPacketTestAccess::committed(*n);commits+=committed!=0;++cases;
            if(!mrt){referenceColor=rgb;referenceNormals=normals;}else{if(rgb!=referenceColor)std::printf("RGB_DIFFERENCE fixture=%s material=%u hdr=%u alpha=%u scenario=%u fault=%u localDraws=%llu localFallbacks=%llu %s\n",file,material,hdr,alpha,scenario,fault,(unsigned long long)lighting.draws,(unsigned long long)lighting.fallbacks,ModelNormalPacketTestAccess::stats(*n).c_str());assert(rgb==referenceColor);auto expected=referenceNormals;
                if(committed&&(scenario==2||scenario==3||scenario==6||scenario==12||scenario==19||scenario==22||(scenario>=14&&scenario<=16))){for(unsigned y=0;y<H;++y)std::memset(expected.data()+size_t(y)*W*8,0,W/2*8);}
                assert(normals==expected);}
            if(scenario==22){const auto actual=pixels(sky);if(!mrt)referenceSky=actual;else assert(actual==referenceSky);}
            assert(!extension.audit());return committed;
        };
        if(timing){
            IDirect3DQuery9 *q0=nullptr,*q1=nullptr,*hz=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q0));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q1));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));
            auto get=[](IDirect3DQuery9* q){UINT64 v=0;HRESULT hr;while((hr=q->GetData(&v,sizeof v,D3DGETDATA_FLUSH))==S_FALSE)Sleep(0);ok(hr);return v;};
            LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);std::vector<double> gpu[2],cpu[2];
            for(unsigned block=0;block<4;++block)for(bool mrt:{false,true})for(unsigned round=0;round<8;++round){
                store.setFrame(++frameNumber);ok(hz->Issue(D3DISSUE_END));ok(q0->Issue(D3DISSUE_END));LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
                const auto committed=frame(mrt,timingSrgb?23:timingBlend?12:0);QueryPerformanceCounter(&b);ok(q1->Issue(D3DISSUE_END));const auto ticks=get(q1)-get(q0),rate=get(hz);
                if(round>=4){assert(!mrt||committed==(timingBaseline?0u:N));gpu[mrt].push_back(double(ticks)*1000/rate);cpu[mrt].push_back(double(b.QuadPart-a.QuadPart)*1000/frequency.QuadPart);}
            }
            auto median=[](std::vector<double> a){std::sort(a.begin(),a.end());return a[a.size()/2];};
            std::printf("MODEL_NORMAL_NATIVE_TIMING size=%ux%u draws=%u vertices=%u copies=%u local=%u hdr=%u blendedDepth=%u srgb=%u baseline=%u gpu_replay=%.6f gpu_mrt=%.6f cpu_replay=%.6f cpu_mrt=%.6f scope=capture-queue-native-draw-replay-or-commit\n",W,H,N,nv,copies,timingLocal,hdr,timingBlend,timingSrgb,timingBaseline,median(gpu[0]),median(gpu[1]),median(cpu[0]),median(cpu[1]));
            q0->Release();q1->Release();hz->Release();
        }else{
        auto ready=[&](unsigned scenario=0){unsigned committed=0;for(unsigned i=0;i<8&&!committed;++i)committed=frame(true,scenario);assert(committed>0);};
        // The local-light factory deliberately admits one cold pair/frame:
        // single, then dual, then triple. Compare identical warmed recipes.
        assert(!frame(false));assert(!frame(false));const auto warmColor=referenceColor;assert(!frame(false));assert(warmColor==referenceColor);
        assert(!frame(true));ready();
        assert(!frame(true,1));assert(!frame(true,1));assert(ModelNormalPacketTestAccess::stats(*n).find("modelDraws=0 ")!=std::string::npos);
        assert(!frame(true));ready();
        calls.denyFill=true;assert(frame(true)>0);calls.denyFill=false;
        for(unsigned fault=2;fault<10;++fault){n->reset();assert(!frame(true));ready();assert(!frame(true,0,fault));}
        n->reset();assert(!frame(false,2));assert(!frame(true,2));ready(2);const auto before=canonicalNormals(capturedPixels(n->texture(),W,H));
        assert(!frame(true,3));assert(frame(true,3)>0);assert(canonicalNormals(capturedPixels(n->texture(),W,H))==before);
        frame(false,6);assert(!frame(true,6));ready(6);
        frame(false,13);assert(!frame(true,13));ready(13);
        frame(false,12);assert(!frame(true,12));ready(12);assert(frame(true,12)>0);
        for(unsigned blended:{14u,15u,16u,19u,20u,22u,23u}){frame(false,blended);assert(!frame(true,blended));ready(blended);assert(frame(true,blended)>0);}
        for(unsigned depthOnly:{24u,25u}){frame(false,depthOnly);assert(!frame(true,depthOnly));ready(depthOnly);assert(frame(true,depthOnly)>0);}
        for(unsigned interleaved:{26u,27u}){frame(false,interleaved);assert(!frame(true,interleaved));assert(!frame(true,interleaved));}
        for(unsigned unchangedDepth:{28u,29u,30u,31u}){frame(false,unchangedDepth);assert(!frame(true,unchangedDepth));ready(unchangedDepth);assert(frame(true,unchangedDepth)>0);}
        for(unsigned rejected:{17u,18u}){frame(false,rejected);assert(!frame(true,rejected));assert(!frame(true,rejected));}
        if(msColor){frame(false,7);assert(!frame(true,7));assert(!frame(true,7));assert(ModelNormalPacketTestAccess::stats(*n).find("modelDraws=0 ")!=std::string::npos);}
        for(unsigned changed:{8u,9u,10u}){frame(false,changed);const auto changedColor=referenceColor,changedNormals=referenceNormals;frame(false);ready();referenceColor=changedColor;referenceNormals=changedNormals;assert(!frame(true,changed));assert(!frame(true,changed));assert(ModelNormalPacketTestAccess::stats(*n).find("modelDraws=0 ")!=std::string::npos);}
        for(unsigned hazard:{4u,5u}){
            if(hazard==5&&!alpha)continue;
            frame(false,hazard);const auto expectedColor=referenceColor,expectedNormals=referenceNormals;
            frame(false);assert(!frame(true));ready();referenceColor=expectedColor;referenceNormals=expectedNormals;
            assert(!frame(true,hazard));
        }
        if(!material&&!local){frame(false,11);assert(!frame(true,11));ready(11);}
        n.reset();calls.denyFormat=true;n=std::make_unique<NorthlightModelNormals>(d);calls.denyFormat=false;n->setDeferredCapture(true);
        frame(false,23);assert(!frame(true,23));assert(!frame(true,23));
        assert(ModelNormalPacketTestAccess::stats(*n).find("reason=srgb-format ")!=std::string::npos);
        std::printf("MODEL_NORMAL_NATIVE_CASE_PASS fixture=%s material=%u hdr=%u alpha=%u %s\n",file,material,hdr,alpha,ModelNormalPacketTestAccess::stats(*n).c_str());
        if(local)assert(lighting.draws>0);
        }
        n.reset();if(alternateColor)alternateColor->Release();if(alternateDepth)alternateDepth->Release();if(msColor)msColor->Release();if(msDepth)msDepth->Release();if(cube)cube->Release();if(height){ok(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+displacementBinding.sampler,nullptr));height->Release();}if(privateVS)privateVS->Release();if(privatePS)privatePS->Release();alternateSource->Release();source->Release();dispose(color);dispose(sky);
    }
    if(vb)vb->Release();if(ib)ib->Release();
    if(!timing){assert(commits>0);std::printf("MODEL_NORMAL_NATIVE_GPU_PASS cases=%u committedFrames=%u RGB=exact normals=exact fallback=whole-frame faults=before-after nativeCalls=once hdrSky=exact fillSuccess=%u fillFailure=%u\n",cases,commits,calls.fillSuccess,calls.fillFailure);}
    return 0;
}
