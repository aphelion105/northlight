// Exercise the production upload and its point bounds, not a cache reimplementation.
#undef NDEBUG
#include "renderer.cpp"
#include <cassert>
#include <fstream>
#include <random>

static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct TerrainCaptureDemandTest {
    using Owner=WorldRenderer::TerrainSnapshot;
    static Owner patch(unsigned id){
        auto p=std::make_shared<NorthlightTerrainCapture::MeshSnapshot>();
        const float x=-.95f+float(id%32)*.06f,y=-.9f+float(id/32)*.09f;
        for(unsigned i=0;i<203;++i)p->positions.push_back({x+float(i%7)*.012f,y+float(i/7)*.004f,.2f+float((i+id)%13)*.02f});
        for(unsigned i=0;i<317;++i){const unsigned v=i%169;p->indices.push_back(v);p->indices.push_back(v+1);p->indices.push_back(v+7);}
        const float low[]={x,y,.2f},high[]={x+.072f,y+.112f,.44f};std::copy(low,low+3,p->bounds.low);std::copy(high,high+3,p->bounds.high);
        p->bounds.chunks.push_back({int(id%32),int(id/32)});return p;
    }
    static double upload(WorldRenderer& w,const std::vector<Owner>& owners){
        const auto start=std::chrono::steady_clock::now();
        w.frameTerrain.clear();w.liveTerrainChunks.clear();w.pointLiveBatches.clear();w.frameTerrainVertices=w.frameTerrainIndices=0;
        for(const auto& owner:owners)w.appendTerrain(owner);
        assert(w.frameTerrain.size()==owners.size()&&w.uploadLiveTerrain());
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        size_t at=0;assert(w.pointLiveBatches.size()==w.uploadedTerrain.size());
        for(size_t i=0;i<w.pointLiveBatches.size();++i){const auto& b=w.pointLiveBatches[i];const auto& p=w.uploadedTerrain[i];
            assert(b.start==at&&b.count==p->indices.size()/3&&b.low.x==p->bounds.low[0]&&b.high.y==p->bounds.high[1]);at+=p->indices.size();}
        assert(at==w.liveTerrainIndexCount);w.frameFence.endFrame();
        // Match the game's submission cadence; a no-Present loop only builds
        // an unbounded backend command backlog, not a warm frame benchmark.
        must(w.d->Present(nullptr,nullptr,nullptr,nullptr));return ms;
    }
    static std::vector<char> image(WorldRenderer& w,IDirect3DSurface9* target,unsigned samples,unsigned shader,unsigned mask){
        auto* d=w.d;must(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));must(d->BeginScene());
        must(d->SetStreamSource(0,w.liveTerrainGPU.vertices(),0,sizeof(StaticShadow::Vertex)));must(d->SetIndices(w.liveIndicesGPU));
        if(shader==1)must(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,w.liveTerrainGPU.vertexCapacity(),UINT(w.liveIndexBase+w.liveTerrainIndexCount),UINT(w.liveDirectionalIndexCount/3)));
        else for(const auto& b:w.pointLiveBatches){if(mask==1&&b.low.x>-.1f)continue;if(mask==2&&b.low.x<-.3f)continue;
            must(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,w.liveTerrainGPU.vertexCapacity(),UINT(w.liveIndexBase+b.start),b.count));}
        NorthlightDynamicRing::touch(w.liveIndexRing,w.fence());must(d->EndScene());w.frameFence.endFrame();
        IDirect3DSurface9 *resolved=nullptr,*read=nullptr;
        if(samples){must(d->CreateRenderTarget(128,96,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));must(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));}
        must(d->CreateOffscreenPlainSurface(128,96,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr));must(d->GetRenderTargetData(resolved?resolved:target,read));
        D3DLOCKED_RECT lock{};must(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<char> out(128*96*4);
        for(unsigned y=0;y<96;++y)std::memcpy(out.data()+y*128*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,128*4);
        must(read->UnlockRect());read->Release();if(resolved)resolved->Release();return out;
    }
    static void run(IDirect3DDevice9* d,const char* output,bool timing){
        WorldRenderer w(d);w.meshGeneration=1;std::vector<Owner> owners;for(unsigned i=0;i<(timing?640u:24u);++i)owners.push_back(patch(i));
        std::mt19937 random(926);std::shuffle(owners.begin(),owners.end(),random);upload(w,owners);
        if(timing){
            for(unsigned mode=0;mode<3;++mode){std::vector<double> times;size_t bytes=0,reused=0;
                for(unsigned frame=0;frame<80;++frame){
                    if(mode==0)std::shuffle(owners.begin(),owners.end(),random);
                    if(mode==2){owners.erase(owners.begin());owners.push_back(patch(640+frame));}
                    const auto ms=upload(w,owners);
                    if(frame>=8){times.push_back(ms);bytes+=w.terrainUploadBytes;reused+=w.terrainUploadReused;}}
                std::sort(times.begin(),times.end());std::printf("TERRAIN_ORDER_BENCH mode=%u snapshots=640 frames=%zu medianMs=%.6f p95Ms=%.6f uploadBytes=%zu reused=%zu\n",mode,times.size(),times[times.size()/2],times[times.size()*95/100],bytes,reused);
            }return;}
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        IDirect3DVertexDeclaration9* decl=nullptr;must(d->CreateVertexDeclaration(elements,&decl));must(d->SetVertexDeclaration(decl));
        IDirect3DVertexShader9* vertex[3]{};IDirect3DPixelShader9* pixel[3]{};
        must(d->CreateVertexShader(kShadowVSShader,&vertex[0]));must(d->CreateVertexShader(kShadowCacheVSShader,&vertex[1]));must(d->CreateVertexShader(kLocalShadowVSShader,&vertex[2]));
        must(d->CreatePixelShader(kShadowPSShader,&pixel[0]));must(d->CreatePixelShader(kShadowPSShader,&pixel[1]));must(d->CreatePixelShader(kLocalShadowPSShader,&pixel[2]));
        for(auto s:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})must(d->SetRenderState(s,FALSE));
        must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));must(d->SetRenderState(D3DRS_ZENABLE,TRUE));must(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));must(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        const float material[]={0,0,0,-1},sphere[]={10000,0,0,0},matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        must(d->SetTexture(0,nullptr));must(d->SetPixelShaderConstantF(0,material,1));must(d->SetPixelShaderConstantF(1,sphere,1));must(d->SetVertexShaderConstantF(0,matrix,4));
        unsigned comparisons=0,nextOwner=100;size_t covered=0;std::ofstream pixels(output,std::ios::binary);assert(pixels);
        for(unsigned samples:{0u,2u,4u,8u}){
            IDirect3DSurface9 *target=nullptr,*depth=nullptr;must(d->SetDepthStencilSurface(nullptr));
            must(d->CreateRenderTarget(128,96,D3DFMT_R32F,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));must(d->CreateDepthStencilSurface(128,96,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(samples),0,TRUE,&depth,nullptr));
            must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp={0,0,128,96,0,1};must(d->SetViewport(&vp));
            for(unsigned frame=0;frame<8;++frame){
                if(frame==2){auto changed=std::make_shared<NorthlightTerrainCapture::MeshSnapshot>(*owners[0]);for(auto& p:changed->positions)p.z+=.1f;changed->bounds.low[2]+=.1f;changed->bounds.high[2]+=.1f;owners[0]=changed;}
                if(frame==3)owners.pop_back();
                if(frame==4){owners.push_back(patch(nextOwner++));++w.meshGeneration;w.fixedTerrain=std::make_shared<const std::set<std::pair<int,int>>>(std::set<std::pair<int,int>>{{0,0},{1,0},{2,0}});}
                if(frame==6){w.liveTerrainGPU.clear();w.liveTerrainGeneration=0;}
                std::shuffle(owners.begin(),owners.end(),random);upload(w,owners);
                for(unsigned shader=0;shader<3;++shader)for(unsigned mask=0;mask<(shader==1?1u:3u);++mask){
                    must(d->SetVertexShader(vertex[shader]));must(d->SetPixelShader(pixel[shader]));const auto result=image(w,target,samples,shader,mask);
                    for(size_t i=0;i<result.size();i+=4){float f;std::memcpy(&f,result.data()+i,4);assert(std::isfinite(f));covered+=f<.99f;}
                    pixels.write(result.data(),result.size());++comparisons;
                }
            }
            must(d->SetDepthStencilSurface(nullptr));target->Release();depth->Release();
        }
        assert(covered>1000);owners.clear();upload(w,owners);assert(!w.liveTerrainIndexCount&&!w.liveDirectionalIndexCount&&w.pointLiveBatches.empty());
        for(auto* p:vertex)p->Release();for(auto* p:pixel)p->Release();decl->Release();
        std::printf("TERRAIN_ORDER_GPU_PASS comparisons=%u covered=%zu samples=0,2,4,8 generation=1 replacement=1 membership=1 reset=1 empty=1 pointBounds=1\n",comparisons,covered);
    }
};
int main(int argc,char** argv){
    assert(argc==5);const bool timing=std::strcmp(argv[4],"timing")==0;SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);
    auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightTerrainOrder";assert(RegisterClassW(&wc));
    auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    TerrainCaptureDemandTest::run(d,argv[3],timing);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
