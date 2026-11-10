// Paired production normal replay -> WorldNormals consumer on authored resolved depth.
#define main unused_model_normal_main
#include "model_normals_gpu.cpp"
#undef main
#include "world_compiled_shaders.h"
#include <chrono>
struct ModelNormalPacketTestAccess {
    static void coldSeed(NorthlightModelNormals& n){if(n.depthSeedPS)n.depthSeedPS->Release();n.depthSeedPS=nullptr;}
};
struct SeedCalls:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned fault=0;bool after=false,seen=false;IDirect3DPixelShader9* seed=nullptr;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        const bool target=code==kModelNormalDepthSeedShader;
        if(target&&fault==1){seen=true;fault=0;if(after){ok(real->CreatePixelShader(code,out));seed=*out;}else *out=nullptr;return E_FAIL;}
        const auto hr=real->CreatePixelShader(code,out);if(target&&SUCCEEDED(hr))seed=*out;return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{
        if(fault==2&&shader==seed){seen=true;fault=0;if(after)ok(real->SetPixelShader(shader));return E_FAIL;}
        return real->SetPixelShader(shader);
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rect,DWORD flags,D3DCOLOR color,float z,DWORD stencil)override{
        if(fault==5){seen=true;return E_FAIL;}
        if(fault==3){seen=true;fault=0;if(after)ok(real->Clear(n,rect,flags,color,z,stencil));return E_FAIL;}
        return real->Clear(n,rect,flags,color,z,stencil);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD sampler,IDirect3DBaseTexture9* texture)override{
        if(fault==6&&sampler==0&&!texture){IDirect3DPixelShader9* current=nullptr;ok(real->GetPixelShader(&current));const bool target=current==seed;if(current)current->Release();if(target){seen=true;return E_FAIL;}}return real->SetTexture(sampler,texture);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT count,const void* vertices,UINT stride)override{
        if(fault==4){IDirect3DPixelShader9* current=nullptr;ok(real->GetPixelShader(&current));const bool target=current==seed;if(current)current->Release();if(target){seen=true;fault=0;if(after)ok(real->DrawPrimitiveUP(t,count,vertices,stride));return E_FAIL;}}
        return real->DrawPrimitiveUP(t,count,vertices,stride);
    }
};
static void dispose(Target& t){t.read->Release();t.s->Release();t.t->Release();}
static void finish(IDirect3DQuery9* q){ok(q->Issue(D3DISSUE_END));const auto start=GetTickCount();HRESULT h;while((h=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<30000);Sleep(0);}ok(h);}
int main(int argc,char** argv){assert(argc>=6);std::setvbuf(stdout,nullptr,_IONBF,0);const bool timing=argc>6;const std::string format=argv[5];const bool snapshot=format!="R32F";
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto* raw=d;SeedCalls calls(raw);d=&calls;
    const unsigned W=timing?3840:321,H=timing?2160:181;std::printf("SEED_SETUP width=%u height=%u format=%s\n",W,H,format.c_str());auto scene=target(W,H,D3DFMT_A8R8G8B8,4),output=target(W/2,H/2,D3DFMT_A16B16G16R16F,8);IDirect3DSurface9* ds=nullptr;ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
    IDirect3DTexture9 *resolved=nullptr,*white=nullptr;ok(d->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&resolved,nullptr));ok(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT lock{};ok(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0xffffffff;ok(white->UnlockRect(0));
    IDirect3DTexture9* resolvedSnapshot=nullptr;
    if(snapshot)ok(d->CreateTexture(W,H,1,D3DUSAGE_DEPTHSTENCIL,format=="DF24"?(D3DFORMAT)MAKEFOURCC('D','F','2','4'):(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),D3DPOOL_DEFAULT,&resolvedSnapshot,nullptr));
    const bool directDepth=snapshot&&std::strstr(argv[1],"SysWOW64");
    if(directDepth){ds->Release();ok(resolvedSnapshot->GetSurfaceLevel(0,&ds));}
    auto modelVS=vs(argv[2]);auto modelPS=ps(argv[3]);auto alphaPS=ps(argv[4]);IDirect3DPixelShader9* consumer=nullptr;ok(d->CreatePixelShader(kWorldNormalsNoDistanceShader,&consumer));
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));
    const float xy[6][2]={{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};unsigned char vertices[6*48]={};for(unsigned i=0;i<6;++i){auto* v=vertices+48*i;float p[]={xy[i][0],xy[i][1],0},normal[]={.3f,.4f,.8660254f};std::memcpy(v,p,12);v[12]=128;v[13]=64;v[14]=32;v[15]=31;v[17]=1;v[18]=2;v[19]=3;std::memcpy(v+20,normal,12);const float uv[]={(xy[i][0]+1)*.5f,(xy[i][1]+1)*.5f};std::memcpy(v+32,uv,8);const DWORD color=0xffffffff;std::memcpy(v+40,&color,4);}
    unsigned cases=0;{NorthlightModelNormals normals(d);normals.setDeferredCapture(true);normals.testRange(false);
    IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));if(timing){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));}
    for(unsigned shape=0;shape<(timing?3:152);++shape){const float nearZ=.5f,farZ=500,A=farZ/(farZ-nearZ),B=-nearZ*A;const float bounds[]={.501f,1.f,3.f,4.f,15.999f,16.f,31.999f,47.9f};const float sceneZ=shape>=10?bounds[((shape-10)/8)%8]:shape==3?31.999f:shape==4?.501f:3.f;const float rangeMin=shape==6?.2f:0,rangeMax=shape==6?.8f:1;const float sceneD=rangeMin+(rangeMax-rangeMin)*(A+B/sceneZ);ok(resolved->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)reinterpret_cast<float*>((char*)lock.pBits+y*lock.Pitch)[x]=(shape==7&&x>W/2)?1:sceneD;ok(resolved->UnlockRect(0));
        ok(white->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)reinterpret_cast<DWORD*>((char*)lock.pBits+y*lock.Pitch)[x]=shape>=138&&shape<142&&((x+y)&1)?0x20ffffff:0xffffffff;ok(white->UnlockRect(0));
        const unsigned count=timing?(shape==0?8:64):8,rounds=timing?30:2;std::vector<unsigned char> reference;
        for(unsigned round=0;round<rounds;++round)for(unsigned turn=0;turn<2;++turn){const bool current=((round+turn)&1)!=0;normals.endFrame();normals.testDepthSeed(current);bind(13,nullptr);ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,scene.s));ok(d->SetDepthStencilSurface(ds));viewport(scene);D3DVIEWPORT9 vp{0,0,W,H,rangeMin,rangeMax};ok(d->SetViewport(&vp));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,sceneD,0));
            ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(modelVS));ok(d->SetPixelShader(shape>=138?alphaPS:modelPS));bind(0,white);for(auto k:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(k,FALSE));if(shape>=138&&shape<142){ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,128));}
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_CLIPPING,TRUE));ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));ok(d->SetRenderState(D3DRS_DEPTHBIAS,0));ok(d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0));
            ok(d->BeginScene());
            if(snapshot){
                if(shape==7){D3DRECT rect={LONG(W/2+1),0,LONG(W),LONG(H)};ok(d->Clear(1,&rect,D3DCLEAR_ZBUFFER,0,1,0));}
                if(!directDepth){SavedState state(d);assert(state.ok);ok(d->SetTexture(0,resolvedSnapshot));
                if(std::strstr(argv[1],"SysWOW64")){
                    ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZ));
                    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3})ok(d->SetRenderState(k,0));
                    ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
                    const float dummy[3]={};ok(d->DrawPrimitiveUP(D3DPT_POINTLIST,1,dummy,sizeof dummy));
                }
                ok(d->SetRenderState(D3DRS_POINTSIZE,0x3f800000));ok(d->SetRenderState(D3DRS_POINTSIZE,0x7fa05000));assert(state.restore());}
            }
            for(unsigned i=0;i<count;++i){float bank[256][4]={};const float sign=shape==5||shape>=74?-1:1;bank[2][0]=bank[3][1]=1;bank[4][2]=sign*A;bank[4][3]=B;bank[5][2]=sign;bank[28][3]=1;bank[6][0]=bank[7][1]=1;
                float z=sceneZ;if(timing)z=shape==0?sceneZ:shape==1?30.f-i*.25f:(i==0?sceneZ:sceneZ+1+i*.1f);else if(shape==0)z=sceneZ+1+i*.1f;else if(shape==1)z=i==0?sceneZ-.1f:sceneZ;else if(shape==2||shape==3||shape==4||shape==5||shape==6||shape==7)z=sceneZ+(float(i)-3.5f)*std::max(.006f,sceneZ*.0015f)*.4f;else if(shape==8){z=sceneZ+1;bank[4][0]=.0001f;}else if(shape==9){z=sceneZ+1;ok(d->SetRenderState(D3DRS_DEPTHBIAS,0x358637bd));}else if(shape>=138){z=sceneZ+(shape==139?(float(count-i-1))*.001f:float(i)*.001f);if(shape==140)bank[4][2]=1000000;}else {const float offsets[]={-1.2f,-1.001f,-.999f,0,.999f,1.001f,1.2f,2.f};z=sceneZ+offsets[(shape-10)%8]*std::max(.006f,sceneZ*.0015f);}
                for(unsigned b=0;b<75;++b){const unsigned row=31+3*b;bank[row][0]=bank[row+1][1]=z;bank[row+2][2]=1;bank[row+2][3]=z*sign;}
                ok(d->SetVertexShaderConstantF(0,bank[0],256));const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,6,0,2,false};normals.capture(modelVS,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,48);},true,&geometry,nullptr,D3DFMT_UNKNOWN,vertices,48);
            }
            assert(normals.testPendingPackets()==count);NorthlightModelNormals::DepthSeed seed{snapshot?resolvedSnapshot:resolved,nearZ,farZ,rangeMin,rangeMax};
            if(shape==141){
                ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0));
                ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME));ok(d->SetRenderState(D3DRS_WRAP0,D3DWRAPCOORD_0|D3DWRAPCOORD_1));
                ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,1));ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2));
                D3DMATRIX transform{};transform._11=transform._22=2;transform._33=transform._44=1;transform._41=.3f;ok(d->SetTransform(D3DTS_TEXTURE0,&transform));
            }
            if(current&&shape>=142){calls.fault=shape>=150?5+(shape-150):1+(shape-142)%4;calls.after=shape>=146;calls.seen=false;if(calls.fault==1)ModelNormalPacketTestAccess::coldSeed(normals);}
            if(timing){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(freq->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}const auto start=std::chrono::steady_clock::now();const bool replayOK=normals.finishCaptures(true,&seed);assert(replayOK!=bool(current&&shape>=150));const auto stop=std::chrono::steady_clock::now();if(timing){ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));}ok(d->EndScene());finish(event);const unsigned seeded=normals.testDepthSeeded();if(current&&shape>=142){assert(calls.seen);if(shape<150)assert(calls.fault==0);else {assert(!normals.texture());calls.fault=0;IDirect3DSurface9* restored=nullptr;ok(d->GetDepthStencilSurface(&restored));assert(restored==ds);restored->Release();auto native=pixels(scene);for(auto b:native)assert(!b);++cases;continue;}}assert(seeded==unsigned(current&&shape!=8&&shape!=9&&shape!=140&&shape<142));
            double gpuMs=0;if(timing){UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;ok(begin->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(freq->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&hz&&b>=a);gpuMs=double(b-a)*1000/hz;}
            if(!timing){auto rawImage=capturedPixels(normals.texture(),W,H);unsigned covered=0;for(size_t i=6;i<rawImage.size();i+=8){unsigned short h;std::memcpy(&h,rawImage.data()+i,2);covered+=half(h)>0;}
                if(shape==0)assert(current?covered==0:covered>W*H/2);
                if(shape==138||shape==139)assert(covered>W*H/8&&covered<W*H*3/4);
            }
            if(shape==141){DWORD value=0;ok(d->GetRenderState(D3DRS_MULTISAMPLEMASK,&value));assert(value==0);ok(d->GetRenderState(D3DRS_FILLMODE,&value));assert(value==D3DFILL_WIREFRAME);ok(d->GetTextureStageState(0,D3DTSS_TEXCOORDINDEX,&value));assert(value==1);ok(d->GetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,&value));assert(value==D3DTTFF_COUNT2);}
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffffu));ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));ok(d->SetRenderState(D3DRS_WRAP0,0));ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output.s));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));bind(1,snapshot?resolvedSnapshot:resolved);bind(13,normals.texture());ok(d->SetPixelShader(consumer));float c[80][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=1;c[1][3]=rangeMin;c[2][0]=1/(rangeMax-rangeMin);c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[33][0]=W;c[33][1]=H;c[33][2]=W/2;c[33][3]=H/2;c[79][0]=1;ok(d->SetPixelShaderConstantF(0,c[0],80));quad(output);auto image=pixels(output);unsigned accepted=0;for(size_t i=6;i<image.size();i+=8){unsigned short h;std::memcpy(&h,image.data()+i,2);unsigned short r;std::memcpy(&r,image.data()+i-6,2);accepted+=half(h)==1.f&&std::fabs(half(r))>.01f;}if(!timing&&shape>=10&&shape<138&&(shape-10)%8==3)assert(accepted>W*H/8);if(reference.empty())reference=image;else {if(reference!=image){unsigned changed=0;for(size_t i=0;i<image.size();++i)changed+=image[i]!=reference[i];std::printf("MISMATCH shape=%u current=%u bytes=%u\n",shape,unsigned(current),changed);}assert(reference==image);}++cases;
            std::printf("SEED_FRAME shape=%u round=%u current=%u seed=%u draws=%u accepted=%u gpuMs=%.6f cpuMs=%.6f\n",shape,round,unsigned(current),seeded,count,accepted,gpuMs,std::chrono::duration<double,std::milli>(stop-start).count());
        }
    }
    bind(13,nullptr);bind(1,nullptr);normals.reset();for(auto* q:{event,begin,end,freq,disjoint})if(q)q->Release();decl->Release();consumer->Release();modelVS->Release();modelPS->Release();alphaPS->Release();if(resolvedSnapshot)resolvedSnapshot->Release();white->Release();resolved->Release();ds->Release();dispose(scene);dispose(output);}raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);std::printf("MODEL_NORMAL_DEPTH_SEED_PASS cases=%u consumerChecks=%u expectedRefusals=%u consumer=exact\n",cases,cases-(timing?0:4),timing?0:4);
}
