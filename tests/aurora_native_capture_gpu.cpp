// Replays the user's native material state through production source admission.
#define NORTHLIGHT_HDR_PIPELINE_LIBRARY
#include "hdr_pipeline_gpu.cpp"
#include "aurora_native_registry.h"
#include "aurora_renderer.h"
#include <chrono>

static void pixelTransfer(DeviceMeter& d){
    using namespace NorthlightAuroraNative;
    constexpr unsigned W=16,H=16;
    Surface linear,legacy;linear.make(&d,W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE);legacy.make(&d,W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE);
    struct VertexUV {float x,y,z,u,v,u1,v1;};
    const VertexUV vertices[]={{-1,1,.5f,0,0,0,0},{1,1,.5f,1,0,1,0},{-1,-1,.5f,0,1,0,1},{1,-1,.5f,1,1,1,1}};
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;ok(d.CreateVertexDeclaration(elements,&declaration));ok(d.SetVertexDeclaration(declaration));
    ok(d.SetDepthStencilSurface(nullptr));ok(d.SetRenderState(D3DRS_ZENABLE,FALSE));ok(d.SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d.SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));ok(d.SetRenderState(D3DRS_FOGENABLE,FALSE));
    auto sample=[&](IDirect3DSurface9* target,bool hdr){IDirect3DSurface9* staging=nullptr;ok(d.CreateOffscreenPlainSurface(W,H,hdr?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d.GetRenderTargetData(target,staging));D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));const auto* p=static_cast<const unsigned char*>(l.pBits)+8*l.Pitch+8*(hdr?8:4);NorthlightAuroraSource::RGB out{};for(unsigned c=0;c<3;++c)out[c]=hdr?half(reinterpret_cast<const std::uint16_t*>(p)[c]):p[2-c]/255.f;ok(staging->UnlockRect());staging->Release();return out;};
    const unsigned colors[][3]={{8,26,3},{38,51,13},{255,255,255}};
    const NorthlightAuroraSource::RGB scales[]={{0,0,0},{.28f,4,1.28f},{3.6f,.36f,4},{7.2f,.72f,8}};
    Renderer renderer;assert(renderer.prepare(&d));unsigned cases=0;float maxError=0;
    for(const auto& color:colors)for(float opacity:{0.f,.1f,.7120884f,1.f})for(const auto& scale:scales){
        IDirect3DTexture9* textures[]={texture(&d,{1,1,D3DFMT_A8R8G8B8,4,{static_cast<unsigned char>(color[2]),static_cast<unsigned char>(color[1]),static_cast<unsigned char>(color[0]),255}}),texture(&d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,77}})};
        for(unsigned i=0;i<2;++i){ok(d.SetTexture(i,textures[i]));ok(d.SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d.SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));}
        float c[256][4]{};for(unsigned i=0;i<4;++i)c[2+i][i]=1;for(unsigned i=0;i<3;++i)c[31+i][i]=1;c[6][0]=c[7][1]=c[8][0]=c[9][1]=1;c[28][0]=c[28][1]=c[28][2]=1;c[28][3]=opacity;c[30][1]=c[30][2]=1;ok(d.SetVertexShaderConstantF(0,c[0],256));
        const std::array<float,4> native={2*color[0]/255.f,2*color[1]/255.f,2*color[2]/255.f,2*opacity*77/255};
        const auto addition=NorthlightAuroraSource::additional(native,scale,true);const float alpha=std::clamp(native[3],0.f,1.f);
        for(bool hdr:{false,true}){auto* target=hdr?linear.surface:legacy.surface;ok(d.SetRenderTarget(0,target));ok(d.Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d.BeginScene());HRESULT result=E_FAIL;auto draw=[&]{return d.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(VertexUV));};if(renderer.draw(&d,hdr,scale,draw,result))ok(result);else ok(draw());ok(d.EndScene());const auto actual=sample(target,hdr);
            for(unsigned channel=0;channel<3;++channel){const float baseline=std::clamp(native[channel],0.f,1.f);
                const float full=native[channel]<=.04045f?native[channel]/12.92f:std::pow((native[channel]+.055f)/1.055f,2.4f);
                float expected=(full-NorthlightAuroraSource::decode(baseline))*alpha+addition[channel];
                if(!hdr){const float value=NorthlightAuroraSource::decode(baseline)+(alpha>0?addition[channel]/alpha:0);const float encoded=value<=.0031308f?value*12.92f:1.055f*std::pow(value,1/2.4f)-.055f;expected=std::clamp(encoded,0.f,1.f)*alpha;}
                const float error=std::fabs(actual[channel]-expected);maxError=std::max(error,maxError);assert(error<(hdr?.012f:2.1f/255));}
            ++cases;
        }for(auto* t:textures)t->Release();
    }
    declaration->Release();std::printf("AURORA_PIXEL_PASS cases=%u maxError=%.7f hdrHeadroom=1 zeroAlpha=1 sharedCPU=1\n",cases,maxError);
}

int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));
    {
    DeviceMeter d(raw);using namespace NorthlightAuroraNative;
    auto v=read(folder+"/aurora-0-vs.bin"),p=read(folder+"/aurora-0-ps.bin"),decl=read(folder+"/aurora-0-declaration.bin"),args=read(folder+"/draw-args.bin");
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;IDirect3DVertexBuffer9* vb=nullptr;
    ok(d.CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&vs));ok(d.CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&ps));ok(d.CreateVertexDeclaration(reinterpret_cast<const D3DVERTEXELEMENT9*>(decl.data()),&declaration));ok(d.CreateVertexBuffer(256*1024,0,0,D3DPOOL_MANAGED,&vb,nullptr));
    ok(d.SetVertexShader(vs));ok(d.SetPixelShader(ps));ok(d.SetVertexDeclaration(declaration));ok(d.SetStreamSource(0,vb,0,48));
    NorthlightParticleTextures::enabled=true;IDirect3DTexture9* textures[2]{};
    for(unsigned t=0;t<2;++t){auto bytes=read(folder+"/aurora-0-texture-"+std::to_string(t)+".bin");for(unsigned i=0;i<bytes.size();i+=4)std::swap(bytes[i],bytes[i+2]);textures[t]=texture(&d,{32,16,D3DFMT_A8R8G8B8,4,bytes});ok(d.SetTexture(t,textures[t]));
        auto upload=read(folder+"/texture-upload-"+std::to_string(t)+".bin");NorthlightParticleTextures::cache().upload(textures[t],textures[t],32,16,5,D3DFMT_DXT5,upload.data(),upload.size(),128,true,true,1);
        ok(d.SetSamplerState(t,D3DSAMP_ADDRESSU,t?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP));ok(d.SetSamplerState(t,D3DSAMP_ADDRESSV,t?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP));ok(d.SetSamplerState(t,D3DSAMP_MINFILTER,D3DTEXF_ANISOTROPIC));ok(d.SetSamplerState(t,D3DSAMP_MAGFILTER,D3DTEXF_ANISOTROPIC));ok(d.SetSamplerState(t,D3DSAMP_SRGBTEXTURE,FALSE));
    }
    ok(d.SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d.SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d.SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d.SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d.SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));ok(d.SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d.SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL));ok(d.SetRenderState(D3DRS_ALPHAREF,1));
    Registry registry;registry.begin(1,17,true);unsigned admitted=0;std::vector<Packet> packets;double readMs=0;
    for(unsigned n=0;n<7;++n){auto c=read(folder+"/aurora-"+std::to_string(n)+"-vs-constants.bin");ok(d.SetVertexShaderConstantF(0,reinterpret_cast<const float*>(c.data()),256));
        // Cancel the captured native view rotation for this source-space test.
        // Camera invariance is checked below with a second view rotation.
        const auto* constants=reinterpret_cast<const float*>(c.data());float inverse[16]{};inverse[15]=1;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)inverse[i*4+j]=constants[(31+i)*4+j];
        const auto* a=reinterpret_cast<const unsigned*>(args.data())+n*4;auto draw=NorthlightSpellLayer::DrawArgs::indexed(D3DPT_TRIANGLELIST,0,a[2],a[3],a[1],a[0]);Packet packet;
        const auto start=std::chrono::steady_clock::now();const bool accepted=Reader::read(&d,draw,inverse,packet);readMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(!accepted){std::fprintf(stderr,"AURORA_READER_REJECT draw=%u\n",n);return 2;}++admitted;
        Packet wrong;auto unknown=draw;++unknown.first;assert(!Reader::read(&d,unknown,inverse,wrong));
        assert(Reader::expand(packet));
        for(const auto& triangle:packet.triangles)for(const auto& vertex:triangle.vertices){assert(std::isfinite(vertex.position.x));}
        Packet rotated=packet;
        constexpr float cs=.764842187f,sn=.644217687f;
        for(unsigned j=0;j<4;++j){rotated.constants[31*4+j]=cs*packet.constants[31*4+j]-sn*packet.constants[32*4+j];rotated.constants[32*4+j]=sn*packet.constants[31*4+j]+cs*packet.constants[32*4+j];}
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)rotated.inverse[i*4+j]=rotated.constants[(31+i)*4+j];
        assert(Reader::expand(rotated));
        for(unsigned i=0;i<packet.triangles.size();++i)for(unsigned j=0;j<3;++j){const auto a=packet.triangles[i].vertices[j].position,b=rotated.triangles[i].vertices[j].position;assert(std::fabs(a.x-b.x)<.0001f&&std::fabs(a.y-b.y)<.0001f&&std::fabs(a.z-b.z)<.0001f);}
        Packet animated=packet;animated.constants[6*4+3]+=.2f;assert(Reader::expand(animated));
        assert(std::fabs(animated.triangles[0].vertices[0].uv[0][0]-packet.triangles[0].vertices[0].uv[0][0]-.2f)<.00001f);

        packet.drawGroup=n<3?1:2;packets.push_back(packet);registry.observe(packet.drawGroup,std::move(packet));
    }
    const NorthlightAurora::Style green{true,4,0,1},purple{true,4,1,2};assert(!registry.source(green));registry.end();
    std::shared_ptr<const NorthlightAuroraSource::Snapshot> source;
    for(unsigned tick=0;tick<5000;++tick){registry.begin(2+tick,17,false);source=registry.source(green);if(source){bool owns=true;for(const auto& packet:packets)owns&=registry.owns(packet.assetBatch,packet.drawGroup);if(owns)break;}Sleep(1);}
    assert(source);for(const auto& packet:packets)assert(registry.owns(packet.assetBatch,packet.drawGroup));
    float energy=0;for(const auto& cell:source->radiance)energy+=cell[1];assert(energy>0);
    assert(registry.source(green)==source);auto recolored=registry.source(purple);assert(recolored&&recolored->radiance[0][2]>=0);assert(!registry.source({}));
    registry.begin(9000,18,false);assert(!registry.source(green));for(const auto& packet:packets)assert(!registry.owns(packet.assetBatch,packet.drawGroup));registry.reset();
    Renderer renderer;assert(renderer.prepare(&d));
    const float sentinel[]={.11f,.22f,.33f,.44f};float bank[224][4];for(auto& row:bank)std::copy(sentinel,sentinel+4,row);ok(d.SetPixelShaderConstantF(0,bank[0],224));
    for(bool linear:{false,true}){HRESULT result=E_FAIL;unsigned calls=0;assert(renderer.draw(&d,linear,{.28f,4,1.28f},[&]{++calls;return D3D_OK;},result));assert(result==D3D_OK&&calls==1);float after[224][4];ok(d.GetPixelShaderConstantF(0,after[0],224));assert(!std::memcmp(bank,after,sizeof bank));IDirect3DPixelShader9* restored=nullptr;ok(d.GetPixelShader(&restored));assert(restored==ps);restored->Release();}
    registry.begin(9001,19,true);for(unsigned n=0;n<2;++n)registry.observe(1,packets[n]);registry.end();registry.begin(9002,19,false);assert(!registry.source(green));
    registry.begin(9003,20,true);for(const auto& packet:packets)registry.observe(packet.drawGroup,packet);registry.end();registry.reset();
    for(unsigned tick=0;tick<200;++tick){registry.begin(9010+tick,20,false);assert(!registry.source(green));Sleep(1);}
    registry.begin(9300,21,true);for(const auto& packet:packets)if(packet.drawGroup==1)registry.observe(1,packet);registry.end();
    registry.begin(9301,21,true);for(const auto& packet:packets)if(packet.drawGroup==1){registry.observe(1,packet);auto duplicate=packet;duplicate.drawGroup=3;registry.observe(3,std::move(duplicate));}registry.end();
    for(unsigned tick=0;tick<200;++tick){registry.begin(9310+tick,21,false);assert(!registry.source(green));Sleep(1);}
    renderer.reset();pixelTransfer(d);NorthlightParticleTextures::cache().clear();for(auto* t:textures)t->Release();vb->Release();declaration->Release();vs->Release();ps->Release();
    std::printf("AURORA_NATIVE_CAPTURE_PASS draws=%u families=2 greenHemisphereMean=%.9f readerMs=%.4f unknownDrawRejected=1 sourceRemoval=1 stateRestored=1 cameraRotation=1 nativeUVAnimation=1 partialAndReset=1\n",admitted,energy/32,readMs);
    }raw->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
