// Production page partition, point shaders and merger against unpartitioned pages.
#define main unused_water_endpoint_main
#include "water_fog_endpoint_gpu.cpp"
#undef main
#include "point_mesh_candidates.h"
#include "shadow_terrain.h"
#include "point_light_shadow.h"
#include "point_transmission_filter.h"
#include "local_light_compiled_shaders.h"
#include "world_compiled_shaders.h"
#include "static_shadow_compiled_shaders.h"
#include "shadow_bounds.h"
#include "directional_shadow_batches.h"
#include "world_math.h"
#include <filesystem>
#include <chrono>

namespace P=NorthlightWorldMeshPages;
using NorthlightGI::Vec3;
struct Batcher {
    using Batch=P::Batch;
    std::vector<Batch> batches,pointSubBatches;
    std::vector<P::PointRange> pointSubRanges;
    size_t pointMeshBatchCount()const{return batches.size()+pointSubBatches.size();}
    const Batch& pointMeshBatch(size_t i)const{return i<batches.size()?batches[i]:pointSubBatches[i-batches.size()];}
    std::vector<IDirect3DTexture9*> materials;
    std::vector<float> uploadedAlphaCutoffs;
    std::vector<unsigned char> pointBatchMasks;
    unsigned pointPrimitiveLimit=0,pointBatchesMerged=0;
    struct {uint64_t bridged=0;} pointWork;
#include "point_spatial_batch_method.inl"
};
struct Mesh : Batcher {
    std::shared_ptr<const P::PointTree> pointTree;
    std::vector<unsigned char> nearby;
    std::vector<uint32_t> staticParents;
    std::vector<IDirect3DVertexBuffer9*> vertices;
    std::vector<IDirect3DIndexBuffer9*> indices;
    std::vector<Batch> draws[6],directionalBatches;
    std::set<std::pair<int,int>> fixedChunks;
    uint64_t triangles=0;
    ~Mesh(){for(auto* p:vertices)p->Release();for(auto* p:indices)p->Release();for(auto* p:materials)if(p)p->Release();}
    void upload(IDirect3DDevice9* d,const P::Plan& pages,const NorthlightWorldMesh::WorldMeshUploadPlan& plan){
        batches=pages.batches;directionalBatches=batches;pointSubBatches=pages.pointSubBatches;pointSubRanges=pages.pointSubRanges;pointTree=pages.pointTree;
        staticParents.clear();for(size_t i=0;i<batches.size();++i)if(!batches[i].terrain)staticParents.push_back(uint32_t(i));
        fixedChunks=plan.fixedTerrainChunks;D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));pointPrimitiveLimit=caps.MaxPrimitiveCount;
        for(const auto& page:pages.pages){
            IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* data=nullptr;
            ok(d->CreateVertexBuffer(UINT(page.key.vertexBytes),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr));ok(vb->Lock(0,0,&data,0));assert(pages.copy(page,true,0,page.key.vertexBytes,data));ok(vb->Unlock());vertices.push_back(vb);
            ok(d->CreateIndexBuffer(UINT(page.key.indexBytes),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));ok(ib->Lock(0,0,&data,0));assert(pages.copy(page,false,0,page.key.indexBytes,data));ok(ib->Unlock());indices.push_back(ib);
        }
        for(const auto& m:plan.materials){
            uploadedAlphaCutoffs.push_back(m.alphaCutoff);
            if(m.alphaCutoff<=0){materials.push_back(nullptr);continue;}
            if(m.textureOwner<materials.size()&&materials[m.textureOwner]){auto* t=materials[m.textureOwner];t->AddRef();materials.push_back(t);continue;}
            const auto& pixels=m.textureOwner<plan.materials.size()?plan.materials[m.textureOwner].pixels():m.pixels();
            Image im{m.width,m.height,D3DFMT_A8R8G8B8,4,pixels};assert(im.pixels.size()==size_t(im.width)*im.height*4);materials.push_back(texture(d,im));
        }
    }
    double prepare(const NorthlightLocalLights::Light& light,float (&matrices)[6][16],bool indexed=true){
        const auto start=std::chrono::steady_clock::now();for(unsigned face=0;face<6;++face)assert(NorthlightPointShadow::faceMatrix(light.position,.1f,light.attenuationEnd,face,matrices[face],256));
        NorthlightPointShadow::FaceClip clip(matrices,light);pointBatchMasks.assign(pointMeshBatchCount(),63);std::vector<size_t> candidates[6];
        const bool ready=indexed&&pointTree&&pointTree->nearby(pointMeshBatchCount(),[&](Vec3 lo,Vec3 hi){return NorthlightPointShadow::recordOutsideSphere(lo,hi,light);},nearby);
        for(size_t i=0;i<batches.size();++i){const auto& b=batches[i];if(!b.terrain)continue;pointBatchMasks[i]=static_cast<unsigned char>(ready&&!nearby[i]?0:clip.mask(&b.boundsLow.x,&b.boundsHigh.x,true));}
        if(indexed){
            NorthlightPointMesh::classifyStatic(batches,pointSubBatches,pointSubRanges,&staticParents,ready?nearby.data():nullptr,63,pointBatchMasks,
                [&](const Batch& b,unsigned faces){return clip.mask(&b.boundsLow.x,&b.boundsHigh.x,true,faces);},
                [&](size_t i,unsigned mask){for(unsigned f=0;f<6;++f)if(mask&(1u<<f))candidates[f].push_back(i);});
        }else for(size_t i=0;i<batches.size();++i){const auto& b=batches[i];if(b.terrain)continue;const unsigned mask=clip.mask(&b.boundsLow.x,&b.boundsHigh.x,true);pointBatchMasks[i]=static_cast<unsigned char>(mask);
            const auto range=pointSubRanges.empty()?P::PointRange{}:pointSubRanges[i];
            auto append=[&](size_t index,unsigned bits){pointBatchMasks[index]=static_cast<unsigned char>(bits);for(unsigned f=0;f<6;++f)if(bits&(1u<<f))candidates[f].push_back(index);};
            if(!range.count){append(i,mask);continue;}
            const size_t first=batches.size()+range.first;
            if(!mask){std::fill(pointBatchMasks.begin()+first,pointBatchMasks.begin()+first+range.count,0);continue;}
            for(size_t n=first;n<first+range.count;++n){const auto& sub=pointMeshBatch(n);append(n,clip.mask(&sub.boundsLow.x,&sub.boundsHigh.x,true,mask));}
        }
        triangles=pointWork.bridged=pointBatchesMerged=0;
        for(unsigned f=0;f<6;++f){draws[f].clear();for(size_t at=0;at<candidates[f].size();){auto b=pointBatchRun(candidates[f],at,f);triangles+=b.count;draws[f].push_back(b);}}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
};
static void broadphase(Mesh& mesh,const NorthlightLocalLights::Light& light,size_t index,bool timing){
    float matrices[6][16];mesh.prepare(light,matrices,false);const auto expected=mesh.pointBatchMasks;
    std::vector<P::Batch> expectedDraws[6];for(unsigned f=0;f<6;++f)expectedDraws[f]=mesh.draws[f];
    mesh.prepare(light,matrices,true);assert(expected==mesh.pointBatchMasks);
    const auto key=[](const P::Batch& b){return std::make_tuple(b.page,b.start,b.count,b.material,b.minVertex,b.vertexCount,b.pointOnly);};
    for(unsigned f=0;f<6;++f){assert(expectedDraws[f].size()==mesh.draws[f].size());for(size_t n=0;n<mesh.draws[f].size();++n)assert(key(expectedDraws[f][n])==key(mesh.draws[f][n]));}
    double ms[2]={};
    if(timing){std::vector<double> samples[2];
        for(unsigned block=0;block<24;++block){const unsigned v=(block/2)%2?1-block%2:block%2;double elapsed=0;
            for(unsigned n=0;n<32;++n){auto moving=light;moving.position[0]+=float(n)*.0005f;elapsed+=mesh.prepare(moving,matrices,v!=0);}
            if(block>=8)samples[v].push_back(elapsed/32);}
        for(unsigned v=0;v<2;++v){std::sort(samples[v].begin(),samples[v].end());ms[v]=(samples[v][3]+samples[v][4])*.5;}}
    std::printf("POINT_TREE_SCENE light=%zu exactMasks=1 exactDraws=1 batches=%zu bytes=%zu linearMs=%.6f indexedMs=%.6f\n",index,mesh.batches.size(),mesh.pointTree?mesh.pointTree->bytes():0,ms[0],ms[1]);
}
static void directionalBoundaries(IDirect3DPixelShader9* opaque,IDirect3DPixelShader9* cutout){
    NorthlightDirectionalShadowBatches batches(2);
    NorthlightShadowBounds::TexelRect a{0,0,8,8},b{8,0,16,8};
    std::vector<unsigned> sizes;
    auto submit=[&](const P::Batch& run,auto*,const auto*){sizes.push_back(run.count);return true;};
    auto add=[&](unsigned start,unsigned page,unsigned material,auto* shader,const auto& rect){
        P::Batch packet{};packet.start=start;packet.count=1;packet.vertexCount=3;
        packet.page=page;packet.material=material;
        assert(batches.add(packet,shader,&rect,shader==opaque,submit));
    };
    add(0,0,1,opaque,a);add(3,0,2,opaque,a);add(6,0,2,opaque,a);
    add(9,0,2,opaque,b);add(12,0,2,cutout,b);add(15,0,3,cutout,b);
    add(18,1,3,cutout,b);add(24,1,3,cutout,b);add(27,1,3,cutout,b);
    assert(batches.flush(submit)&&sizes==std::vector<unsigned>({2,1,1,1,1,1,2}));
}
static void directional(IDirect3DDevice9* d,Mesh (&mesh)[2],Vec3 center,bool timing){
    IDirect3DVertexShader9* vs=nullptr;ok(d->CreateVertexShader(kShadowCacheVSShader,&vs));IDirect3DPixelShader9* shaders[4]{};const DWORD* codes[]={kStaticCasterPSShader,kStaticCasterOpaquePSShader,kStaticCasterFastPSShader,kStaticCasterOpaqueFastPSShader};for(unsigned i=0;i<4;++i)ok(d->CreatePixelShader(codes[i],&shaders[i]));
    IDirect3DSurface9 *target=nullptr,*depth=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(1024,1024,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateDepthStencilSurface(1024,1024,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));ok(d->CreateOffscreenPlainSurface(1024,1024,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));auto wait=[&]{ok(event->Issue(D3DISSUE_END));while(event->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();};
    directionalBoundaries(shaders[1],shaders[0]);
    for(float radius:{48.f,192.f})for(float elevation:{.08726646f,.78539816f}){
        const Vec3 direction{std::cos(elevation)*.70710678f,std::cos(elevation)*.70710678f,std::sin(elevation)};float matrix[16];NorthlightWorldMath::shadowMatrix(center,direction,radius,matrix);std::vector<P::Batch> selected[2];uint64_t triangles[2]{};
        for(unsigned v=0;v<2;++v)for(const auto& b:mesh[v].directionalBatches)if(!b.pointOnly&&(!b.terrain||mesh[v].fixedChunks.count({b.chunkX,b.chunkY}))&&!NorthlightShadowBounds::directionalClipRejectAffine(b.boundsLow,b.boundsHigh,matrix)){selected[v].push_back(b);triangles[v]+=b.count;}
        auto draw=[&](unsigned v,bool merge){
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));
            D3DVIEWPORT9 viewport={0,0,1024,1024,0,1};ok(d->SetViewport(&viewport));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));
            ok(d->SetVertexShader(vs));ok(d->SetVertexShaderConstantF(0,matrix,4));ok(d->BeginScene());
            unsigned page=UINT_MAX,draws=0,materialId=UINT_MAX;IDirect3DPixelShader9* bound=nullptr;
            auto submit=[&](const P::Batch& b,IDirect3DPixelShader9* shader,const NorthlightShadowBounds::TexelRect*){
                if(bound!=shader){ok(d->SetPixelShader(shader));bound=shader;}
                if(page!=b.page){page=b.page;ok(d->SetStreamSource(0,mesh[v].vertices[page],0,sizeof(NorthlightGI::WorldVertex)));ok(d->SetIndices(mesh[v].indices[page]));}
                if(!merge||(shader!=shaders[1]&&shader!=shaders[3]&&b.material!=materialId)){
                    const float material[]={1,1,1,mesh[v].uploadedAlphaCutoffs[b.material]};
                    ok(d->SetPixelShaderConstantF(0,material,1));ok(d->SetTexture(0,mesh[v].materials[b.material]));
                    materialId=b.material;
                }
                ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,b.minVertex,b.vertexCount,b.start,b.count));
                ++draws;return true;
            };
            NorthlightDirectionalShadowBatches batches(mesh[v].pointPrimitiveLimit);
            for(const auto& b:selected[v]){
                const bool opaque=mesh[v].uploadedAlphaCutoffs[b.material]<=0;
                const unsigned choice=(NorthlightShadowBounds::depthFullyInsideAffine(b.boundsLow,b.boundsHigh,matrix)?2u:0u)+opaque;
                if(merge)assert(batches.add(b,shaders[choice],nullptr,opaque,submit));
                else submit(b,shaders[choice],nullptr);
            }
            assert(batches.flush(submit));ok(d->EndScene());return draws;
        };
        auto pixels=[&]{
            ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};
            ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(1024*1024);
            for(unsigned y=0;y<1024;++y)std::memcpy(result.data()+y*1024,static_cast<char*>(lock.pBits)+y*lock.Pitch,1024*4);
            ok(staging->UnlockRect());return result;
        };
        draw(0,false);const auto original=pixels();
        const unsigned separate=draw(1,false);const auto reference=pixels();
        const unsigned merged=draw(1,true);const auto candidate=pixels();
        assert(!std::memcmp(reference.data(),candidate.data(),reference.size()*sizeof(float))&&merged<=separate);
        unsigned changed=0;float peak=0;
        for(size_t i=0;i<original.size();++i){assert(std::isfinite(original[i])&&std::isfinite(reference[i]));changed+=original[i]!=reference[i];peak=std::max(peak,std::abs(original[i]-reference[i]));}assert(peak<2e-6f);
        struct Times {std::array<double,2> gpu{},cpu{};};
        auto measure=[&](bool merging){
            Times result;
            if(!timing)return result;
            std::vector<double> gpu[2],cpu[2];
            for(unsigned block=0;block<24;++block){
                const unsigned mode=(block/2)%2?1-block%2:block%2;
                auto submit=[&]{draw(merging?1:mode,merging&&mode);};
                submit();wait();ok(disjoint->Issue(D3DISSUE_BEGIN));ok(begin->Issue(D3DISSUE_END));
                const auto start=std::chrono::steady_clock::now();
                for(unsigned n=0;n<4;++n)submit();
                const double submitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/4;
                ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait();
                UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;
                ok(begin->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));
                ok(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&hz&&b>=a);
                if(block>=4){gpu[mode].push_back(1000.*double(b-a)/double(hz)/4);cpu[mode].push_back(submitMs);}
            }
            const auto median=[](auto& samples){std::sort(samples.begin(),samples.end());return (samples[4]+samples[5])*.5;};
            for(unsigned v=0;v<2;++v){result.gpu[v]=median(gpu[v]);result.cpu[v]=median(cpu[v]);}
            return result;
        };
        const auto ms=measure(false).gpu;
        const auto batchMs=measure(true);
        std::printf("DIRECTIONAL_BATCH radius=%g elevation=%g beforeDraws=%u afterDraws=%u beforeMs=%.6f afterMs=%.6f beforeCpuMs=%.6f afterCpuMs=%.6f depthExact=1\n",
            radius,elevation,separate,merged,batchMs.gpu[0],batchMs.gpu[1],batchMs.cpu[0],batchMs.cpu[1]);
        std::printf("SPATIAL_DIRECTIONAL radius=%g elevation=%g changed=%u max=%g oldTriangles=%llu newTriangles=%llu oldDraws=%zu newDraws=%zu oldGpuMs=%g newGpuMs=%g\n",radius,elevation,changed,peak,(unsigned long long)triangles[0],(unsigned long long)triangles[1],selected[0].size(),selected[1].size(),ms[0],ms[1]);
    }
    ok(d->SetDepthStencilSurface(nullptr));target->Release();depth->Release();staging->Release();vs->Release();for(auto* p:shaders)p->Release();for(auto* p:{event,begin,end,frequency,disjoint})p->Release();
}
static void quad(NorthlightGI::WorldScene& s,Vec3 p,Vec3 a,Vec3 b,unsigned material){
    const auto first=uint32_t(s.vertices.size());for(unsigned i=0;i<4;++i){NorthlightGI::WorldVertex v{};v.position=p+a*float(i&1)+b*float(i>>1);v.normal={0,0,1};v.u=float(i&1);v.v=float(i>>1);s.vertices.push_back(v);}s.triangles.push_back({first,first+1,first+2,material});s.triangles.push_back({first+1,first+3,first+2,material});
}
int main(int argc,char** argv){
    assert(argc==9);std::setvbuf(stdout,nullptr,_IONBF,0);const bool real=std::strcmp(argv[2],"-")!=0,timing=std::atoi(argv[8])!=0;std::string error;
    const Vec3 center{std::stof(argv[3]),std::stof(argv[4]),std::stof(argv[5])};std::vector<NorthlightLocalLights::Light> lights;
    {std::ifstream file(argv[6]);assert(file);NorthlightLocalLights::Light l;while(file>>l.position[0]>>l.position[1]>>l.position[2]>>l.attenuationEnd){l.attenuationStart=.5f;lights.push_back(l);}}assert(!lights.empty());
    NorthlightGI::WorldScene local,terrain;
    if(real){
        const std::string root=argv[2],map=argv[7];const int tx=int(std::floor((17066.6666667-center.y)/533.3333333)),ty=int(std::floor((17066.6666667-center.x)/533.3333333));std::vector<std::string> nearFiles,farFiles;
        for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){const auto p=root+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(!std::filesystem::exists(p))continue;farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(p);}
        // This smaller loading region still contains every light sphere. It
        // avoids the legacy fixture loader's 256 MiB duplicate-texture ceiling.
        for(const auto& l:lights)for(unsigned axis=0;axis<3;++axis)assert(std::abs(l.position[axis]-(&center.x)[axis])+l.attenuationEnd<64);
        if(!loadInstancedScenes(nearFiles,root+"/models",center-Vec3(64,64,64),center+Vec3(64,64,64),local,error)){std::fprintf(stderr,"%s\n",error.c_str());return 2;}
        const float r=NorthlightShadowTerrain::Radius;if(!loadInstancedScenes(farFiles,root+"/models",center-Vec3(r,r,r),center+Vec3(r,r,r),terrain,error,1)){std::fprintf(stderr,"%s\n",error.c_str());return 2;}
    }else{
        local.materials.resize(3);for(unsigned m=0;m<3;++m){auto& v=local.materials[m];v.width=v.height=8;v.rgba.resize(8*8*4,255);v.alphaCutoff=m?.5f:0;v.pointOnly=m==2;for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)v.rgba[4*(y*8+x)+3]=(x+y)%3?255:0;}
        for(int y=-48;y<=48;++y)for(int x=-48;x<=48;++x){const float z=float((x*x+y*y)%11)*.3f;quad(local,{x*2.f,y*2.f,z},{1.8f,0,0},{0,1.8f,0},0);quad(local,{x*2.f,y*2.f,z},{1.4f,0,0},{0,0,3},unsigned((x*x+y*y)%3));}
    }
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    {
    Mesh mesh[2];unsigned variant=0;for(unsigned chunks:{4u,4u}){NorthlightWorldMesh::WorldMeshUploadPlan plan;if(real)assert(NorthlightShadowTerrain::build(local,terrain,center,plan,error,{},NorthlightShadowTerrain::Radius,{chunks,false}));else assert(NorthlightWorldMesh::buildUploadPlan(local,plan,error,{},nullptr,{chunks,false}));P::Plan pages;P::Options options;options.pointCells=variant!=0;assert(P::build(plan,pages,error,{},options));assert(!P::validatePages(plan,pages));mesh[variant].upload(d,pages,plan);++variant;std::printf("SPATIAL_PLAN chunks=%u batches=%zu pages=%zu vertices=%llu triangles=%u subBatches=%zu metadataBytes=%zu\n",chunks,pages.batches.size(),pages.pages.size(),(unsigned long long)(pages.vertexBytes/sizeof(NorthlightGI::WorldVertex)),plan.triangleCount,pages.pointSubBatches.size(),pages.pointSubBatches.capacity()*sizeof(P::Batch)+pages.pointSubRanges.capacity()*sizeof(P::PointRange));}
    local=NorthlightGI::WorldScene{};terrain=NorthlightGI::WorldScene{};
    IDirect3DVertexDeclaration9* declaration=nullptr;const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};ok(d->CreateVertexDeclaration(elements,&declaration));IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;ok(d->CreateVertexShader(kLocalShadowVSShader,&vs));ok(d->CreatePixelShader(kLocalShadowPSShader,&ps));
    IDirect3DSurface9* targets[6]={};for(auto& p:targets)ok(d->CreateRenderTarget(256,256,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&p,nullptr));IDirect3DSurface9 *depth=nullptr,*staging=nullptr;ok(d->CreateDepthStencilSurface(256,256,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));ok(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    for(auto s:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(s,FALSE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));ok(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP));ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
    IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));auto wait=[&]{ok(event->Issue(D3DISSUE_END));while(event->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();};
    for(size_t index=0;index<lights.size();++index){const auto& l=lights[index];broadphase(mesh[1],l,index,timing);float matrices[2][6][16];double preparation[2];for(unsigned v=0;v<2;++v)preparation[v]=mesh[v].prepare(l,matrices[v]);
        if(timing){std::vector<double> samples[2];for(unsigned round=0;round<64;++round)for(unsigned order=0;order<2;++order){const unsigned v=(round+order)%2;const double ms=mesh[v].prepare(l,matrices[v]);if(round>=8)samples[v].push_back(ms);}for(unsigned v=0;v<2;++v){std::sort(samples[v].begin(),samples[v].end());preparation[v]=(samples[v][27]+samples[v][28])*.5;}}

        auto draw=[&](unsigned v){ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));const float sphere[]={l.attenuationEnd*l.attenuationEnd,1.f/256,0,0};ok(d->SetPixelShaderConstantF(1,sphere,1));D3DVIEWPORT9 viewport={0,0,256,256,0,1};ok(d->BeginScene());for(unsigned face=0;face<6;++face){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,targets[face]));ok(d->SetDepthStencilSurface(depth));ok(d->SetViewport(&viewport));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetVertexShaderConstantF(0,matrices[v][face],4));UINT page=UINT_MAX,material=UINT_MAX;for(const auto& b:mesh[v].draws[face]){if(page!=b.page){page=b.page;ok(d->SetStreamSource(0,mesh[v].vertices[page],0,sizeof(NorthlightGI::WorldVertex)));ok(d->SetIndices(mesh[v].indices[page]));}if(material!=b.material){material=b.material;const float value[]={0,0,0,mesh[v].uploadedAlphaCutoffs[material]<=0?-1.f:mesh[v].uploadedAlphaCutoffs[material]};ok(d->SetPixelShaderConstantF(0,value,1));ok(d->SetTexture(0,mesh[v].materials[material]));}NorthlightPointTransmissionFilter filter;ok(filter.begin(d,b.pointOnly&&mesh[v].uploadedAlphaCutoffs[b.material]>0));ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,b.minVertex,b.vertexCount,b.start,b.count));ok(filter.restore());}}ok(d->EndScene());};
        std::vector<float> output[2];unsigned changed=0,casters=0;float peak=0;for(unsigned v=0;v<2;++v){draw(v);output[v].resize(6*256*256);for(unsigned face=0;face<6;++face){ok(d->GetRenderTargetData(targets[face],staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y)std::memcpy(output[v].data()+face*256*256+y*256,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);ok(staging->UnlockRect());}}
        for(size_t i=0;i<output[0].size();++i){const float a=output[0][i],b=output[1][i];assert(std::isfinite(a)&&std::isfinite(b));changed+=a!=b;casters+=a<1;peak=std::max(peak,std::abs(a-b));}assert(casters&&peak<2e-6f);
        double ms[2]{};if(timing){std::vector<double> samples[2];for(unsigned block=0;block<24;++block){const unsigned v=(block/2)%2?1-block%2:block%2;draw(v);wait();ok(disjoint->Issue(D3DISSUE_BEGIN));ok(begin->Issue(D3DISSUE_END));for(unsigned n=0;n<4;++n)draw(v);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait();UINT64 a=0,b=0,hz=0;BOOL invalid=TRUE;ok(begin->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&hz&&b>=a);if(block>=4)samples[v].push_back(1000.*double(b-a)/double(hz)/4);}for(unsigned v=0;v<2;++v){std::sort(samples[v].begin(),samples[v].end());ms[v]=(samples[v][4]+samples[v][5])*.5;}}
        uint64_t draws[2]{};for(unsigned v=0;v<2;++v)for(auto& f:mesh[v].draws)draws[v]+=f.size();std::printf("SPATIAL_CUBE light=%zu casters=%u changed=%u max=%g oldTriangles=%llu newTriangles=%llu oldDraws=%llu newDraws=%llu oldPrepareMs=%g newPrepareMs=%g oldGpuMs=%g newGpuMs=%g\n",index,casters,changed,peak,(unsigned long long)mesh[0].triangles,(unsigned long long)mesh[1].triangles,(unsigned long long)draws[0],(unsigned long long)draws[1],preparation[0],preparation[1],ms[0],ms[1]);
    }
    directional(d,mesh,center,timing);
    ok(d->SetTexture(0,nullptr));ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));ok(d->SetVertexDeclaration(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));for(auto* p:targets)p->Release();depth->Release();staging->Release();declaration->Release();vs->Release();ps->Release();for(auto* p:{event,begin,end,frequency,disjoint})p->Release();
    }d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);std::printf("POINT_SPATIAL_GPU_PASS lights=%zu real=%u\n",lights.size(),unsigned(real));
}
