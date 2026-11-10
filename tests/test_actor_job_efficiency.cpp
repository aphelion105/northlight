#include <cstdlib>
#include <new>
#include <cstdio>
static unsigned allocations=0;
void* operator new(size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#include "actor_scene_job.h"
#include "actor_job_reference.h"
#include "actor_hash_reference.h"
#include <cassert>
#include <chrono>
using namespace NorthlightActorDeformation;
#include "actor_probe_program.h"
static void equal(const NorthlightGI::WorldScene& a,const NorthlightGI::WorldScene& b){
    assert(a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size());
    for(size_t i=0;i<a.vertices.size();++i){auto& x=a.vertices[i];auto& y=b.vertices[i];assert(!std::memcmp(&x.position,&y.position,sizeof x.position)&&!std::memcmp(&x.normal,&y.normal,sizeof x.normal)&&x.u==y.u&&x.v==y.v);}
    for(size_t i=0;i<a.triangles.size();++i)assert(!std::memcmp(&a.triangles[i],&b.triangles[i],sizeof a.triangles[i]));
    for(size_t i=0;i<a.materials.size();++i){auto& x=a.materials[i];auto& y=b.materials[i];assert(!std::memcmp(&x.albedo,&y.albedo,sizeof x.albedo)&&x.alphaCutoff==y.alphaCutoff&&x.width==y.width&&x.height==y.height&&x.addressU==y.addressU&&x.addressV==y.addressV&&x.rgba==y.rgba);}
}
static NorthlightActorGeometry::Packet packet(){
    NorthlightActorGeometry::Packet p;auto words=code(3);assert(compile(words.data(),words.size(),p.position)&&compile(words.data(),words.size(),p.uv,true));p.hasUV=true;
    p.mesh.vertexCount=3;p.mesh.primitiveCount=1;p.mesh.indexed=true;p.mesh.indices={0,1,2};p.mesh.streams[0].stride=24;p.mesh.streams[0].bytes.resize(72);
    float positions[3][3]={{2,3,4},{3,3,4},{2,4,4}},uv[]={.1f,.2f};for(unsigned i=0;i<3;++i){auto* out=p.mesh.streams[0].bytes.data()+i*24;std::memcpy(out,positions[i],12);std::memcpy(out+12,uv,8);out[20]=1;}
    p.elements={{0,0,2,0,0,0},{0,12,1,0,5,0},{0,20,5,0,2,0},{0xff,0,17,0,0,0}};
    for(unsigned j=0;j<4;++j)p.inverseView[j*5]=1;
    p.constants[34*4]=1;p.constants[35*4+1]=1;p.constants[36*4+2]=1;p.constants[34*4+3]=100;p.constants[35*4+3]=10;
    p.constants[6*4]=2;p.constants[6*4+1]=3;p.constants[7*4]=.25f;p.constants[7*4+1]=-.5f;
    p.material.albedo={1,1,1};p.material.alphaCutoff=.5f;p.material.addressU=3;
    p.texture.width=p.texture.height=1;p.texture.pitch=4;p.texture.format=NorthlightActorTexture::Format::BGRA8;p.texture.bytes={0,0,255,255};return p;
}
static void hashSemantics(){
    using namespace NorthlightActorGeometry;using NorthlightGI::WorldScene;
    const auto scene=ActorJob{{102,13,4},{packet()}}.resolve().scene;
    auto check=[&](const WorldScene& a,const WorldScene& b,bool same){
        assert((sceneHash(a)==sceneHash(b))==same);
        assert((ActorHashReference::sceneHash(a)==ActorHashReference::sceneHash(b))==same);
    };
    check(*scene,WorldScene(*scene),true);
    for(unsigned size:{1u,3u,128u}){
        WorldScene a=*scene;a.materials[0].width=size;a.materials[0].height=size==128?128:1;
        auto& bytes=a.materials[0].rgba;bytes.resize(size*a.materials[0].height*4);
        for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(i*71+19);
        WorldScene b=a;check(a,b,true);
        auto shared=std::shared_ptr<uint8_t>(new uint8_t[bytes.size()],std::default_delete<uint8_t[]>());
        std::memcpy(shared.get(),bytes.data(),bytes.size());b.materials[0].sharedTexels=shared;b.materials[0].sharedTexelBytes=bytes.size();b.materials[0].rgba.clear();check(a,b,true);
        for(size_t index:{size_t(0),size_t(3),bytes.size()/2,bytes.size()-1}){b=a;b.materials[0].rgba[index]^=1;check(a,b,false);}
        b=a;b.materials[0].rgba.push_back(0);check(a,b,false);
        b=a;std::swap(b.materials[0].width,b.materials[0].height);check(a,b,size!=3);
    }
    for(unsigned change=0;change<10;++change){WorldScene b=*scene;
        switch(change){case 0:b.materials[0].alphaCutoff+=.1f;break;case 1:++b.materials[0].addressU;break;case 2:++b.materials[0].addressV;break;
        case 3:b.materials[0].albedo.x+=.1f;break;case 4:b.vertices[0].u+=.1f;break;case 5:b.vertices[0].position.x+=.1f;break;
        case 6:std::swap(b.triangles[0].v0,b.triangles[0].v1);break;case 7:++b.triangles[0].material;break;
        case 8:b.triangles.push_back(b.triangles[0]);break;case 9:b.materials.push_back(b.materials[0]);break;}
        check(*scene,b,false);
    }
    WorldScene rounded=*scene;rounded.vertices[0].position.x+=.001f;check(*scene,rounded,true);
    auto p=packet();p.texture.format=NorthlightActorTexture::Format::BGRX8;p.texture.pitch=8;p.texture.bytes.resize(8,99);
    auto decoded=ActorJob{{102,13,4},{p}}.resolve();p.texture.bytes[3]^=255;p.texture.bytes[7]^=255;
    auto padded=ActorJob{{102,13,4},{p}}.resolve();check(*decoded.scene,*padded.scene,true);
    p.material=decoded.scene->materials[0];p.texture.bytes.clear();auto predecoded=ActorJob{{102,13,4},{p}}.resolve();check(*decoded.scene,*predecoded.scene,true);
    std::puts("PASS actor hash semantics: ownership, shared texels, decoded/padded BGRX, RGBA/alpha, dimensions/tail, sampler, UV, topology, quantized pose");
}
static void hashBenchmark(){
    NorthlightActorGeometry::ActorJob current;ActorHashReference::ActorJob previous;current.center=previous.center={102,13,4};
    for(unsigned n=0;n<53;++n){auto p=packet();p.mesh.vertexCount=300;p.mesh.streams[0].bytes.resize(300*24);
        for(unsigned i=3;i<300;++i)std::memcpy(p.mesh.streams[0].bytes.data()+i*24,p.mesh.streams[0].bytes.data()+(i%3)*24,24);
        p.texture.width=p.texture.height=128;p.texture.pitch=512;p.texture.bytes.resize(128*128*4);
        for(size_t i=0;i<p.texture.bytes.size();++i)p.texture.bytes[i]=uint8_t(i*71+n*13);
        ActorHashReference::Packet q;q.mesh=p.mesh;q.position=p.position;q.uv=p.uv;q.elements=p.elements;q.constants=p.constants;q.inverseView=p.inverseView;q.material=p.material;q.texture=p.texture;q.hasUV=p.hasUV;
        previous.packets.push_back(std::move(q));current.packets.push_back(std::move(p));
    }
    auto old=previous.resolve();auto now=current.resolve();equal(*old.scene,*now.scene);assert(now.scene->materials.size()==53&&now.scene->vertices.size()==15900);
    double hashUs[2]={},resolveUs[2]={};volatile std::uint64_t checksum=0;unsigned hashAllocations=0;
    constexpr unsigned Rounds=8;
    for(unsigned round=0;round<Rounds;++round)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(round+turn)&1;auto count=allocations;
        auto start=std::chrono::steady_clock::now();auto h=mode?NorthlightActorGeometry::sceneHash(*now.scene):ActorHashReference::sceneHash(*now.scene);
        hashUs[mode]+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();hashAllocations+=allocations-count;checksum^=h;
        start=std::chrono::steady_clock::now();if(mode){auto x=current.resolve();assert(x.hash==now.hash);checksum^=x.hash;}else{auto x=previous.resolve();assert(x.hash==old.hash);checksum^=x.hash;}
        resolveUs[mode]+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    }
    assert(!hashAllocations);std::printf("PASS actor hash ABBA: materials=53 textureBytes=3473408 vertices=15900 hashAllocations=0 oldHashUs=%.3f newHashUs=%.3f oldResolveUs=%.3f newResolveUs=%.3f checksum=%llu\n",hashUs[0]/Rounds,hashUs[1]/Rounds,resolveUs[0]/Rounds,resolveUs[1]/Rounds,(unsigned long long)checksum);
}
int main(){
    hashSemantics();hashBenchmark();
    NorthlightActorGeometry::ActorJob job;ActorJobReference::ActorJob before;job.center=before.center={102,13,4};
    for(unsigned i=0;i<128;++i){auto p=packet();p.constants[34*4+3]+=float(i%8);switch(i%13){case 0:p.hasUV=false;p.alphaTest=true;break;case 1:p.mesh.indices[0]=999;break;case 2:p.mesh.primitiveCount=0;break;case 3:p.constants[34*4+3]=999;break;case 4:p.mesh.topology=D3DPT_TRIANGLESTRIP;break;case 5:p.texture.bytes.clear();break;case 6:p.mesh.streams[0].bytes.resize(1);break;case 7:p.material.rgba={50,60,70,255};p.texture.bytes.clear();p.material.width=p.material.height=1;break;}
        ActorJobReference::Packet q;q.mesh=p.mesh;q.sharedMesh=p.sharedMesh;q.position=p.position;q.uv=p.uv;q.elements=p.elements;q.constants=p.constants;q.inverseView=p.inverseView;q.material=p.material;q.texture=p.texture;q.hasUV=p.hasUV;q.alphaTest=p.alphaTest;
        job.packets.push_back(std::move(p));before.packets.push_back(std::move(q));}
    auto expected=before.resolve();auto result=job.resolve();equal(*expected.scene,*result.scene);
    assert(ActorJobReference::sceneHash(*result.scene)==expected.hash&&NorthlightActorGeometry::sceneHash(*expected.scene)==result.hash);
    assert(expected.texturesDecoded==result.texturesDecoded&&expected.textureEncodedBytes==result.textureEncodedBytes);
    auto sourcePosition=std::make_shared<const Program>(job.packets[0].position),sourceUV=std::make_shared<const Program>(job.packets[0].uv);
    std::weak_ptr<const Program> weakPosition=sourcePosition,weakUV=sourceUV;
    unsigned copyAllocations=0,sharedAllocations=0;double captureNs[2]={};
    for(unsigned round=0;round<20;++round)for(unsigned mode=0;mode<2;++mode){auto start=std::chrono::steady_clock::now();unsigned count=allocations;
        for(unsigned i=0;i<128;++i){NorthlightActorGeometry::Packet p;if(mode){p.sharedPosition=sourcePosition;p.sharedUV=sourceUV;assert(&p.positionView()==sourcePosition.get()&&&p.uvView()==sourceUV.get());}
            else{p.position=*sourcePosition;p.uv=*sourceUV;assert(p.position.operations.size()==sourcePosition->operations.size()&&p.uv.operations.size()==sourceUV->operations.size());}}
        (mode?sharedAllocations:copyAllocations)+=allocations-count;captureNs[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/128;}
    assert(copyAllocations>0&&!sharedAllocations);
    for(auto& p:job.packets){p.sharedPosition=sourcePosition;p.sharedUV=sourceUV;p.position={};p.uv={};}
    sourcePosition.reset();sourceUV.reset();assert(!weakPosition.expired()&&!weakUV.expired());
    auto sharedResult=job.resolve();equal(*result.scene,*sharedResult.scene);assert(result.hash==sharedResult.hash);
    std::printf("PASS actor immutable program capture: oldAllocations=%u newAllocations=%u oldNs=%.3f newNs=%.3f sourceLifetime=1 exactSharedOutput=1\n",copyAllocations,sharedAllocations,captureNs[0]/20,captureNs[1]/20);
    double time[2]={};unsigned alloc[2]={};
    for(unsigned round=0;round<20;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1,startAlloc=allocations;auto start=std::chrono::steady_clock::now();
        if(mode){auto x=job.resolve();assert(x.hash==result.hash);}else{auto x=before.resolve();assert(x.hash==expected.hash);}
        time[mode]+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();alloc[mode]+=allocations-startAlloc;}
    assert(alloc[1]<alloc[0]);std::printf("PASS actor worker: packets=128 exactVertices=%zu exactTriangles=%zu oldAllocations=%u newAllocations=%u oldUs=%.3f newUs=%.3f ratio=%.4f\n",result.scene->vertices.size(),result.scene->triangles.size(),alloc[0]/20,alloc[1]/20,time[0]/20,time[1]/20,time[1]/time[0]);
    job.packets.clear();assert(weakPosition.expired()&&weakUV.expired());
}
