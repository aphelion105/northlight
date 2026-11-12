#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib timing
"""Indexed high-tuple cache/work scheduling regressions; never launches game/Wine."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast,subprocess,tempfile,json
HERE=Path(__file__).resolve().parent
stub=next(ast.literal_eval(n.value) for n in ast.parse((HERE/'test_terrain_snapshot.py').read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stub' for t in n.targets))
harness=r'''
#include "replay_bounds.h"
#include "replay_bounds_schedule.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightReplayBounds;
using namespace NorthlightActorDeformation;
using Mesh=NorthlightDrawSnapshot::Mesh;
Program load(const char* path){FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);std::vector<Word>w(n/4);assert(std::fread(w.data(),1,n,f)==size_t(n));std::fclose(f);Program p;assert(compile(w.data(),w.size(),p));return p;}
D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,12,3,0,1,0},{0,28,5,0,2,0},{0xff,0,17,0,0,0}};
Mesh geometry(unsigned n,unsigned offset=0){Mesh m;m.vertexCount=n;m.primitiveCount=n/3;m.indexed=true;m.indices.resize(m.primitiveCount*3);for(size_t j=0;j<m.indices.size();++j)m.indices[j]=unsigned((j*7)%n);m.streams[0].stride=32;m.streams[0].bytes.resize(n*32);
 for(unsigned j=0;j<n;++j){float v[]={std::sin(float(j))*.6f+float(offset)*.001f,std::cos(float(j))*.7f,float(j%7)*.1f,.1f,.2f,.3f,.4f};auto* out=m.streams[0].bytes.data()+j*32;std::memcpy(out,v,28);out[28]=j%64;out[29]=(j/64)%64;out[30]=(j+17)%64;out[31]=(j/64+29)%64;}return m;}
void pose(float* c,unsigned frame){std::fill(c,c+1024,0.f);float a=float(frame)*.013f;for(unsigned b=0;b<75;++b){unsigned r=31+3*b;c[r*4]=std::cos(a);c[r*4+1]=std::sin(a);c[(r+1)*4]=-std::sin(a);c[(r+1)*4+1]=std::cos(a);c[(r+2)*4+2]=1;c[r*4+3]=float(b)*.1f;c[(r+1)*4+3]=float(b%5)*.2f;}}
void enclosed(const Program& p,const Mesh& m,const float* c,const float* inv,const Bounds& b){std::vector<Position> reference;assert(worldPositions(p,m,decl,4,c,inv,reference));for(auto v:reference){const float xyz[]={v.x,v.y,v.z};for(unsigned k=0;k<3;++k)assert(b.valid&&xyz[k]>=b.low[k]&&xyz[k]<=b.high[k]);}}
int main(int argc,char**argv){assert(argc>1);auto p=load(argv[1]);assert(SkinEnvelope::supports(p));float constants[1024];pose(constants,0);float inverse[16]={};for(unsigned j=0;j<4;++j)inverse[j*5]=1;inverse[12]=9000;Bounds b;
 // >512 tuples in the generic evaluator must release ALL charged group storage.
 Program generic=p;Operation extra;extra.code=1;extra.destination=0x80070001;extra.source[0].token=0x80e40001;generic.operations.push_back(extra);assert(!SkinEnvelope::supports(generic));
 auto large=std::make_shared<const Mesh>(geometry(1536));EnvelopeCache rejected;size_t released=0;bool unsupported=false;
 for(unsigned frame=0;frame<2000&&!unsupported;++frame){rejected.beginFrame();Budget budget;budget.maxVertices=2048;auto status=rejected.calculate(generic,*large,large,decl,4,constants,inverse,budget,b);released+=rejected.stats().releasedGroupBytes;if(status==Status::Unsupported)unsupported=true;else assert(status==Status::Budget);}
 assert(unsupported&&released>100000&&rejected.reservedBytes()<8192&&rejected.entries()==1);
 const size_t tombstone=rejected.reservedBytes();for(unsigned n=0;n<10;++n){rejected.beginFrame();Budget budget;assert(rejected.calculate(generic,*large,large,decl,4,constants,inverse,budget,b)==Status::Unsupported&&budget.vertices==0&&rejected.reservedBytes()==tombstone);}
 // A cold mesh cannot spend the ready pass's vertex budget or allocate entries.
 Cache cache;Budget ready;ready.maxVertices=0;cache.beginFrame();assert(cache.calculateReady(p,*large,large,decl,4,constants,inverse,ready,b)==Status::Budget&&!b.valid&&cache.envelopeEntries()==0&&ready.vertices==0);
 bool warmed=false;for(unsigned frame=0;frame<2000&&!warmed;++frame){cache.beginFrame();Budget build;build.maxVertices=2048;build.maxOperations=65536;cache.buildEnclosed(p,*large,large,decl,4,constants,inverse,build,b);ready=Budget{};ready.maxVertices=0;ready.maxOperations=196608;cache.beginFrame();auto s=cache.calculateReady(p,*large,large,decl,4,constants,inverse,ready,b);if(s==Status::Valid){enclosed(p,*large,constants,inverse,b);warmed=true;}}assert(warmed);
 cache.beginFrame();Budget build;build.maxVertices=2048;assert(cache.buildEnclosed(p,*large,large,decl,4,constants,inverse,build,b)==Status::Unsupported&&build.vertices==0&&cache.envelopeStats().buildSkippedReady==1);
 // Ready work still runs when cold construction consumed its entire time cap.
 EnvelopeCache separated;for(unsigned n=0;n<2000;++n){separated.beginFrame();Budget budget;auto s=separated.calculate(p,*large,large,decl,4,constants,inverse,budget,b);if(s==Status::Valid)break;assert(n<1999);}
 separated.beginFrame();std::vector<std::shared_ptr<const Mesh>> coldOwners;
 for(unsigned n=0;n<2000&&separated.canBuild();++n){coldOwners.push_back(std::make_shared<const Mesh>(geometry(1536,n)));Budget budget;separated.calculate(p,*coldOwners.back(),coldOwners.back(),decl,4,constants,inverse,budget,b,EnvelopeCache::Stage::Build);}
 assert(!separated.canBuild());ready=Budget{};ready.maxVertices=0;assert(separated.calculate(p,*large,large,decl,4,constants,inverse,ready,b,EnvelopeCache::Stage::Ready)==Status::Valid);
 // Near-capacity live ownership: eviction stays within8MiB and retains no meshes.
 EnvelopeCache capacity;std::vector<std::shared_ptr<const Mesh>> owners;size_t evicted=0;
 for(unsigned n=0;n<4200;++n){owners.push_back(std::make_shared<const Mesh>(geometry(66,n)));bool complete=false;for(unsigned frame=0;frame<100&&!complete;++frame){capacity.beginFrame();Budget budget;auto s=capacity.calculate(p,*owners.back(),owners.back(),decl,4,constants,inverse,budget,b);evicted+=capacity.stats().evictions;complete=s==Status::Valid;assert(s==Status::Valid||s==Status::Budget);}assert(complete&&owners.back().use_count()==1&&capacity.reservedBytes()<=8u*1024u*1024u);}assert(evicted>0&&capacity.entries()<=4096);
 // Representative crowd fixture:588 draws,360 indexed meshes,1536 distinct
 // tuples/vertices each,64 bones, changing pose every frame. No warmed cache assumed.
 std::vector<std::shared_ptr<const Mesh>> crowd;for(unsigned n=0;n<360;++n)crowd.push_back(std::make_shared<const Mesh>(geometry(1536,n)));
 Cache crowded;std::vector<bool> covered(360);size_t coverage=0,totalValid=0,totalBuilt=0,evictions=0,frames=0,warmFrames=0,warmValid=0;double totalMs=0,warmMs=0;size_t warmAt=0;
 for(unsigned frame=0;frame<6000;++frame){pose(constants,frame);crowded.beginFrame();std::vector<Bounds> boxes(588);Budget rb,bb;rb.maxVertices=0;rb.maxOperations=196608;bb.maxVertices=2048;bb.maxOperations=65536;const auto start=std::chrono::steady_clock::now();
  crowded.readyResume(NorthlightReplaySchedule::pass(588,crowded.readyStart(588),[&](){return crowded.canReady()&&rb.operations<rb.maxOperations;},[&](size_t draw){auto& mesh=crowd[draw%360];return crowded.calculateReady(p,*mesh,mesh,decl,4,constants,inverse,rb,boxes[draw])==Status::Budget;}));
  crowded.buildResume(NorthlightReplaySchedule::pass(588,crowded.buildStart(588),[&](){return crowded.canBuild()&&bb.vertices<bb.maxVertices&&bb.operations<bb.maxOperations;},[&](size_t draw){if(boxes[draw].valid)return false;auto& mesh=crowd[draw%360];return crowded.buildEnclosed(p,*mesh,mesh,decl,4,constants,inverse,bb,boxes[draw])==Status::Budget;}));
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();size_t valid=0;for(size_t draw=0;draw<588;++draw)if(boxes[draw].valid){++valid;if(!covered[draw%360]){covered[draw%360]=true;++coverage;if(draw%79==0)enclosed(p,*crowd[draw%360],constants,inverse,boxes[draw]);}}
  assert(rb.vertices==0&&bb.vertices<=2048&&rb.operations+bb.operations<=262144&&crowded.envelopeBytes()<=8u*1024u*1024u);++frames;totalMs+=ms;totalValid+=valid;totalBuilt+=crowded.envelopeStats().buildVertices;evictions+=crowded.envelopeStats().evictions;
  if(coverage==360){if(!warmAt)warmAt=frame+1;++warmFrames;warmValid+=valid;warmMs+=ms;if(warmFrames==240)break;}
 }
 assert(coverage==360&&warmFrames==240&&totalBuilt==360*1536&&evictions==0&&warmValid>0);
 std::printf("{\"status\":\"pass\",\"tuple_limit_released_bytes\":%zu,\"unsupported_tombstone_bytes\":%zu,\"capacity_evictions\":%zu,\"crowd_draws\":588,\"crowd_meshes\":360,\"tuples_per_mesh\":1536,\"bones_per_mesh\":64,\"cold_frames_to_full_coverage\":%zu,\"frames\":%zu,\"all_meshes_covered\":%zu,\"average_valid\":%.3f,\"average_ms\":%.6f,\"warm_average_valid\":%.3f,\"warm_average_ms\":%.6f,\"build_vertices\":%zu,\"cache_bytes\":%zu,\"crowd_evictions\":%zu,\"phase_budget_isolation\":true,\"indexed_all_vertex_enclosure\":true}\n",released,tombstone,evicted,warmAt,frames,coverage,double(totalValid)/frames,totalMs/frames,double(warmValid)/warmFrames,warmMs/warmFrames,totalBuilt,crowded.envelopeBytes(),evictions);
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-envelope-cache-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(harness);results=[]
 for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']):
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  results.append(json.loads(subprocess.check_output([str(p/'test'),str(client_fixtures.four_bone_vs3())],text=True)))
 report=dict(status='pass',native_tests=results,real_gpu_test=False,game_launched=False)
 (fp.output_dir()/'replay-envelope-cache-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
