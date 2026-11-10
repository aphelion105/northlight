#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib timing
"""Offline grouped skin-envelope enclosure and bounded crowd-work validation."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast, subprocess, tempfile, json
HERE=Path(__file__).resolve().parent
def literal(path,name):
 return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
fixture=literal(HERE/'test_actor_deformation.py','harness').split('int main(')[0]
harness=fixture+r'''
#include "replay_bounds.h"
#include <chrono>
using namespace NorthlightReplayBounds;
using Mesh=NorthlightDrawSnapshot::Mesh;
Program load(const char* path){FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);std::vector<Word> words(n/4);assert(std::fread(words.data(),1,n,f)==size_t(n));std::fclose(f);Program p;assert(compile(words.data(),words.size(),p));return p;}
D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,12,3,0,1,0},{0,28,5,0,2,0},{0xff,0,17,0,0,0}};
Mesh geometry(unsigned n){Mesh mesh;mesh.vertexCount=n;mesh.primitiveCount=n/3;mesh.indexed=false;mesh.streams[0].stride=32;mesh.streams[0].bytes.resize(n*32);
 for(unsigned j=0;j<n;++j){float data[]={std::sin(float(j)*.13f)*3,std::cos(float(j)*.07f)*5,float(int(j%11)-5),.1f,.2f,.3f,.4f};std::memcpy(mesh.streams[0].bytes.data()+j*32,data,sizeof data);for(unsigned k=0;k<4;++k)mesh.streams[0].bytes[j*32+28+k]=(j%4+k)%4;}return mesh;}
void constantsFor(float* c,unsigned frame){std::fill(c,c+1024,0.f);const float a=frame*.03f;for(unsigned b=0;b<4;++b){unsigned r=31+3*b;c[r*4]=std::cos(a);c[r*4+1]=std::sin(a);c[(r+1)*4]=-std::sin(a);c[(r+1)*4+1]=std::cos(a);c[(r+2)*4+2]=1;c[r*4+3]=float(b*10)+std::sin(a)*3;c[(r+1)*4+3]=float(b*3);}}
void enclosed(const Program& p,const Mesh& mesh,const D3DVERTEXELEMENT9* d,size_t count,const float* c,const float* inv,const Bounds& b){assert(b.valid);std::vector<Position> positions;assert(worldPositions(p,mesh,d,count,c,inv,positions));for(const auto& position:positions){float value[]={position.x,position.y,position.z};for(unsigned k=0;k<3;++k)assert(value[k]>=b.low[k]&&value[k]<=b.high[k]);}}
Status finish(EnvelopeCache& cache,const Program& p,const std::shared_ptr<const Mesh>& mesh,const D3DVERTEXELEMENT9* d,size_t count,const float* c,const float* inv,Bounds& b,unsigned* frames=nullptr){
 for(unsigned frame=0;frame<10000;++frame){cache.beginFrame();Budget budget;budget.maxVertices=2048;auto s=cache.calculate(p,*mesh,mesh,d,count,c,inv,budget,b);assert(budget.vertices<=2048&&budget.operations<=budget.maxOperations);if(s!=Status::Budget){if(frames)*frames=frame+1;return s;}}assert(false);return Status::Budget;}
int main(int argc,char** argv){assert(argc>1);Program real=load(argv[1]);float constants[1024];constantsFor(constants,0);float inverse[16]={};for(unsigned j=0;j<4;++j)inverse[j*5]=1;inverse[12]=10000;inverse[13]=-900;auto owner=std::make_shared<const Mesh>(geometry(4096));EnvelopeCache cache;Bounds b;unsigned coldFrames=0;
 assert(finish(cache,real,owner,decl,4,constants,inverse,b,&coldFrames)==Status::Valid&&coldFrames>=2);enclosed(real,*owner,decl,4,constants,inverse,b);
 unsigned cases=1;
 for(unsigned frame=0;frame<100;++frame){constantsFor(constants,frame);float a=frame*.011f;inverse[0]=inverse[5]=std::cos(a);inverse[1]=std::sin(a);inverse[4]=-std::sin(a);inverse[12]=10000+frame;
  assert(finish(cache,real,owner,decl,4,constants,inverse,b)==Status::Valid);enclosed(real,*owner,decl,4,constants,inverse,b);++cases;}
 // Geometry/declaration/program ownership cannot carry old input envelopes.
 auto altered=*owner;float far=500;std::memcpy(altered.streams[0].bytes.data(),&far,4);auto changed=std::make_shared<const Mesh>(altered);
 assert(finish(cache,real,changed,decl,4,constants,inverse,b)==Status::Valid);enclosed(real,*changed,decl,4,constants,inverse,b);++cases;
 D3DVERTEXELEMENT9 changedDecl[4];std::memcpy(changedDecl,decl,sizeof decl);changedDecl[0].Offset=4;
 assert(finish(cache,real,owner,changedDecl,4,constants,inverse,b)==Status::Valid);enclosed(real,*owner,changedDecl,4,constants,inverse,b);++cases;
 auto changedProgram=real;changedProgram.definitions.push_back({250,{1,2,3,4}});
 assert(finish(cache,changedProgram,owner,decl,4,constants,inverse,b)==Status::Valid);enclosed(changedProgram,*owner,decl,4,constants,inverse,b);++cases;
 // VS1 floor and VS2/3 round addressing, exact grouped bone indices.
 for(unsigned major=1;major<=3;++major){auto words=code(major);Program p;assert(compile(words.data(),words.size(),p));D3DVERTEXELEMENT9 d[]={{0,0,2,0,0,0},{0,28,5,0,2,0},{0xff,0,17,0,0,0}};
  assert(finish(cache,p,owner,d,3,constants,inverse,b)==Status::Valid);enclosed(p,*owner,d,3,constants,inverse,b);++cases;}
 // Unsupported, invalid, mutable ownership, non-affine camera and exhausted work.
 auto badProgram=real;badProgram.operations[0].code=7;assert(finish(cache,badProgram,owner,decl,4,constants,inverse,b)==Status::Unsupported&&!b.valid);
 auto invalid=*owner;invalid.indexed=true;invalid.indices.resize(size_t(invalid.primitiveCount)*3);invalid.indices.back()=invalid.vertexCount;auto badOwner=std::make_shared<const Mesh>(invalid);
 assert(finish(cache,real,badOwner,decl,4,constants,inverse,b)==Status::Invalid&&!b.valid);
 Budget budget;cache.beginFrame();assert(cache.calculate(real,*owner,{},decl,4,constants,inverse,budget,b)==Status::Unsupported&&!b.valid);
 float saved=inverse[15];inverse[15]=2;assert(finish(cache,real,owner,decl,4,constants,inverse,b)==Status::Invalid&&!b.valid);inverse[15]=saved;
 // A changed camera forces actual evaluation: an exact memo hit correctly
 // needs no deformation operations and is covered separately by memo tests.
 float savedTranslation=inverse[12];inverse[12]+=1;
 budget=Budget{};budget.maxOperations=0;cache.beginFrame();assert(cache.calculate(real,*owner,owner,decl,4,constants,inverse,budget,b)==Status::Budget&&!b.valid&&!budget.operations);inverse[12]=savedTranslation;
 // Non-index input in address calculation has interval width: must fail open.
 auto addressProgram=real;for(auto& op:addressProgram.operations)for(auto& src:op.source)if(NorthlightShadowShader::regType(src.token)==1&&NorthlightShadowShader::regIndex(src.token)==2)src.token=NorthlightShadowShader::replaceReg(src.token,1,0);
 assert(finish(cache,addressProgram,owner,decl,4,constants,inverse,b)==Status::Unsupported&&!b.valid);
 // Four-weight variations exercise interval multiplication, not only rigid bones.
 auto weighted=*owner;for(unsigned j=0;j<weighted.vertexCount;++j){float w0=float(j%17)/32,w1=.2f,w2=.1f,w3=1-w0-w1-w2;float weights[]={w0,w1,w2,w3};std::memcpy(weighted.streams[0].bytes.data()+j*32+12,weights,16);}auto weightedOwner=std::make_shared<const Mesh>(weighted);
 assert(finish(cache,real,weightedOwner,decl,4,constants,inverse,b)==Status::Valid);enclosed(real,*weightedOwner,decl,4,constants,inverse,b);++cases;
 // Full all-vertex deformation vs warmed envelopes with changing animation.
 double fullMs=0,envelopeMs=0;for(unsigned frame=0;frame<30;++frame){constantsFor(constants,frame);budget=Budget{};budget.maxVertices=4096;auto t=std::chrono::steady_clock::now();assert(calculate(real,*owner,decl,4,constants,inverse,budget,b)==Status::Valid);fullMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();t=std::chrono::steady_clock::now();assert(finish(cache,real,owner,decl,4,constants,inverse,b)==Status::Valid);envelopeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();}
 // 1400 replay draws, 80 immutable meshes, changing pose each frame, same caps.
 // Rotating work start avoids permanent starvation at the head of the crowd.
 std::vector<std::shared_ptr<const Mesh>> crowd;for(unsigned i=0;i<80;++i){auto m=geometry(256);float f=float(i);std::memcpy(m.streams[0].bytes.data(),&f,4);crowd.push_back(std::make_shared<const Mesh>(std::move(m)));}
 EnvelopeCache crowded;size_t start=0,totalValid=0,totalVertices=0,maxValid=0;std::vector<bool> covered(80);double crowdMs=0;
 for(unsigned frame=0;frame<400;++frame){constantsFor(constants,frame);crowded.beginFrame();budget=Budget{};budget.maxVertices=2048;bool resumed=false;size_t valid=0;const auto t=std::chrono::steady_clock::now();const size_t frameStart=start;
  for(size_t offset=0;offset<1400;++offset){const size_t draw=(frameStart+offset)%1400;auto& mesh=crowd[draw%80];if(!resumed&&!crowded.canWork()){start=draw;resumed=true;}
   auto s=crowded.calculate(real,*mesh,mesh,decl,4,constants,inverse,budget,b);if(s==Status::Valid){++valid;covered[draw%80]=true;}else assert(!b.valid);
   if(!resumed&&!crowded.canWork()){start=s==Status::Budget?draw:draw+1;resumed=true;}
  }assert(budget.vertices<=2048&&budget.operations<=budget.maxOperations);assert(crowded.reservedBytes()<=8u*1024u*1024u);totalVertices+=budget.vertices;totalValid+=valid;maxValid=std::max(maxValid,valid);crowdMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
 }
 unsigned coverage=0;for(bool v:covered)coverage+=v;
#ifndef NORTHLIGHT_SANITIZED
 assert(coverage==80&&totalValid>0&&crowded.entries()<=4096);
#else
 // Sanitizer builds run 3-10x slower against the same wall-clock budgets (on an efficiency core, e.g. at
 // background QoS, some meshes never finish inside a frame's slice); full coverage is the O2 build's check.
 assert(coverage>0&&totalValid>0&&crowded.entries()<=4096);
#endif
 Cache previous;size_t oldValid=0;double oldMs=0;
 for(unsigned frame=0;frame<40;++frame){constantsFor(constants,frame+1);previous.beginFrame();budget=Budget{};budget.maxVertices=2048;auto t=std::chrono::steady_clock::now();
  for(size_t draw=0;draw<1400;++draw){auto& mesh=crowd[draw%80];if(previous.calculate(real,*mesh,mesh,decl,4,constants,inverse,budget,b)==Status::Valid)++oldValid;}
  oldMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();assert(budget.vertices<=2048);
 }
 assert(double(totalValid)/400>double(oldValid)/40);
 // Allocation limits, weak geometry ownership and LRU eviction with live owners.
 EnvelopeCache limited;std::vector<std::shared_ptr<const Mesh>> owners;
 for(unsigned index=0;index<4200;++index){owners.push_back(std::make_shared<const Mesh>(geometry(64)));auto& mesh=owners.back();assert(finish(limited,real,mesh,decl,4,constants,inverse,b)==Status::Valid);assert(mesh.use_count()==1);assert(limited.entries()<=4096&&limited.reservedBytes()<=8u*1024u*1024u);}
 assert(limited.entries()<4200);owners.clear();limited.clear();assert(limited.entries()==0&&limited.reservedBytes()==0);
 unsigned corpusPrograms=0,corpusEnclosed=0,corpusSkinTemplates=0;
 if(argc>2){FILE* f=std::fopen(argv[2],"rb");assert(f);Word bytes;
  while(std::fread(&bytes,4,1,f)==1){assert(bytes%4==0&&bytes<1000000);std::vector<Word> words(bytes/4);assert(std::fread(words.data(),1,bytes,f)==bytes);++corpusPrograms;Program p;if(!compile(words.data(),words.size(),p))continue;
   corpusSkinTemplates+=SkinEnvelope::supports(p);
   Mesh mesh;mesh.vertexCount=12;mesh.primitiveCount=4;mesh.streams[0].stride=256;mesh.streams[0].bytes.resize(12*256);std::vector<D3DVERTEXELEMENT9> declarations;
   for(const auto& input:p.inputs){assert(input.reg<16);D3DVERTEXELEMENT9 e={0,static_cast<std::uint16_t>(input.reg*16),static_cast<std::uint8_t>(input.usage==0?2:input.usage==2?5:3),0,static_cast<std::uint8_t>(input.usage),static_cast<std::uint8_t>(input.index)};declarations.push_back(e);
    for(unsigned vertex=0;vertex<12;++vertex){auto* target=mesh.streams[0].bytes.data()+vertex*256+input.reg*16;
     if(input.usage==2){for(unsigned j=0;j<4;++j)target[j]=(vertex+j)%4;}else{float values[]={std::sin(float(vertex))*.5f,std::cos(float(vertex))*.5f,float(int(vertex%3)-1),1};if(input.usage==1){values[0]=.1f;values[1]=.2f;values[2]=.3f;values[3]=.4f;}std::memcpy(target,values,16);}
    }
   }declarations.push_back({0xff,0,17,0,0,0});auto m=std::make_shared<const Mesh>(std::move(mesh));cache.clear();auto status=finish(cache,p,m,declarations.data(),declarations.size(),constants,inverse,b);
   if(status==Status::Valid){enclosed(p,*m,declarations.data(),declarations.size(),constants,inverse,b);++corpusEnclosed;}else assert(!b.valid);
  }std::fclose(f);assert(corpusPrograms==9812&&corpusEnclosed>1000);
 }
 // An unshared full-budget first draw must not starve a later shared build.
 Cache mixed;auto mutableMesh=geometry(2048);auto later=std::make_shared<const Mesh>(geometry(4096));unsigned laterValid=0;size_t laterBuilt=0;
 for(unsigned frame=0;frame<30;++frame){constantsFor(constants,frame);mixed.beginFrame();budget=Budget{};budget.maxVertices=2048;const size_t first=mixed.replayStart(2);bool resumed=false;
  for(size_t offset=0;offset<2;++offset){size_t index=(first+offset)%2;const Mesh& mesh=index?*later:mutableMesh;std::shared_ptr<const Mesh> meshOwner=index?later:std::shared_ptr<const Mesh>{};
   if(!resumed&&!mixed.canEnclose()){mixed.replayResume(index);resumed=true;}
   if((!meshOwner||(!mixed.canEnclose()&&!mixed.canLookup()))&&(budget.vertices>=budget.maxVertices||budget.operations>=budget.maxOperations)){if(!resumed){mixed.replayResume(index==first?index+1:index);resumed=true;}continue;}
   const auto before=budget.vertices;auto status=mixed.calculateEnclosed(real,mesh,meshOwner,decl,4,constants,inverse,budget,b);if(index){laterBuilt+=budget.vertices-before;if(status==Status::Valid){++laterValid;enclosed(real,mesh,decl,4,constants,inverse,b);}}
   if(!resumed&&(status==Status::Budget||!mixed.canEnclose())){mixed.replayResume(status==Status::Budget&&index!=first?index:index+1);resumed=true;}
  }
 }
 assert(laterBuilt==4096&&laterValid>0);
 // A first draw that can never fit the vertex cap must not pin the cursor.
 Cache oversizedMixed;auto tooLarge=geometry(4096);auto smallOwner=std::make_shared<const Mesh>(geometry(256));unsigned tailValid=0;
 for(unsigned frame=0;frame<30;++frame){constantsFor(constants,frame);oversizedMixed.beginFrame();budget=Budget{};budget.maxVertices=2048;const size_t first=oversizedMixed.replayStart(3);bool resumed=false;
  for(size_t offset=0;offset<3;++offset){const size_t index=(first+offset)%3;const Mesh& mesh=index==0?tooLarge:index==1?mutableMesh:*smallOwner;std::shared_ptr<const Mesh> meshOwner=index==2?smallOwner:std::shared_ptr<const Mesh>{};
   if(!resumed&&!oversizedMixed.canEnclose()){oversizedMixed.replayResume(index);resumed=true;}
   if((!meshOwner||(!oversizedMixed.canEnclose()&&!oversizedMixed.canLookup()))&&(budget.vertices>=budget.maxVertices||budget.operations>=budget.maxOperations)){if(!resumed){oversizedMixed.replayResume(index==first?index+1:index);resumed=true;}continue;}
   auto status=oversizedMixed.calculateEnclosed(real,mesh,meshOwner,decl,4,constants,inverse,budget,b);if(index==2&&status==Status::Valid){++tailValid;enclosed(real,mesh,decl,4,constants,inverse,b);}
   if(!resumed&&(status==Status::Budget||!oversizedMixed.canEnclose())){oversizedMixed.replayResume(status==Status::Budget&&index!=first?index:index+1);resumed=true;}
  }
 }assert(tailValid>0);
 cache.clear();assert(cache.entries()==0&&cache.reservedBytes()==0);
 std::printf("{\"status\":\"pass\",\"enclosure_cases\":%u,\"corpus_programs\":%u,\"corpus_enclosed\":%u,\"corpus_four_bone_templates\":%u,\"cold_4096_frames\":%u,\"all_vertex_4096_ms\":%.6f,\"envelope_4096_ms\":%.6f,\"crowd_draws\":1400,\"crowd_meshes\":80,\"crowd_meshes_covered\":%u,\"crowd_frames\":400,\"crowd_average_valid\":%.3f,\"crowd_max_valid\":%zu,\"crowd_average_ms\":%.6f,\"crowd_vertices_built\":%zu,\"crowd_cache_bytes\":%zu,\"previous_crowd_average_valid\":%.3f,\"previous_crowd_average_ms\":%.6f,\"mixed_late_draw_valid\":%u,\"oversized_head_tail_valid\":%u,\"bounded_work\":true,\"fail_open\":true}\n",cases,corpusPrograms,corpusEnclosed,corpusSkinTemplates,coldFrames,fullMs/30,envelopeMs/30,coverage,double(totalValid)/400,maxValid,crowdMs/400,totalVertices,crowded.reservedBytes(),double(oldValid)/40,oldMs/40,laterValid,tailValid);
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-replay-envelopes-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(harness);results=[]
 for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DNORTHLIGHT_SANITIZED']):
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  args=[str(p/'test'),str(client_fixtures.four_bone_vs3())];corpus=fp.shader_corpus()
  if corpus:args.append(str(corpus))
  else:print('SKIP sub-case: captured shader corpus (NORTHLIGHT_SHADER_CORPUS not set)')
  results.append(json.loads(subprocess.check_output(args,text=True)))
 report=dict(status='pass',native_tests=results,runtime_gpu_test=False,game_launched=False)
 (fp.output_dir()/'replay-envelopes-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
