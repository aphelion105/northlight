#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib slow
"""Stable actor shadow quota: policy simulation plus rigid-palette cache on real snapshot/declaration types."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast,subprocess,tempfile
HERE=Path(__file__).resolve().parent
stub=next(ast.literal_eval(n.value) for n in ast.parse((HERE/'test_terrain_snapshot.py').read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stub' for t in n.targets))
harness=r'''
#include "sampled_vertex_cache.h"
#include "replay_bounds.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightActorDeformation;
static std::shared_ptr<NorthlightDrawSnapshot::Mesh> mesh(const std::vector<std::array<std::uint8_t,8>>& blend){
 auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->vertexCount=UINT(blend.size());m->streams[0].stride=20;m->streams[0].bytes.resize(20*blend.size());
 for(size_t v=0;v<blend.size();++v){float xyz[]={float(v),2,3};std::memcpy(m->streams[0].bytes.data()+20*v,xyz,12);std::memcpy(m->streams[0].bytes.data()+20*v+12,blend[v].data(),8);}
 return m;}
int main(){
 Program p;p.inputs={{0,0,0},{1,2,0}};
 D3DVERTEXELEMENT9 weighted[]={{0,0,2,0,0,0},{0,12,5,0,2,0},{0,16,8,0,1,0},{0xff,0,17,0,0,0}};
 D3DVERTEXELEMENT9 plain[]={{0,0,2,0,0,0},{0,12,5,0,2,0},{0xff,0,17,0,0,0}};
 RigidBoneCache cache;
 auto fence=mesh({{7,0,0,0,255,0,0,0},{7,3,0,0,255,0,0,0}});
 assert(cache.bone(p,*fence,fence,weighted,weighted,4)==7&&cache.misses==1);
 assert(cache.bone(p,*fence,fence,weighted,weighted,4)==7&&cache.hits==1);
 assert(std::isnan(cache.bone(p,*fence,fence,plain,plain,3))); /* unweighted lanes differ */
 auto npc=mesh({{7,0,0,0,255,0,0,0},{8,0,0,0,255,0,0,0}});
 assert(std::isnan(cache.bone(p,*npc,npc,weighted,weighted,4)));
 auto blend=mesh({{7,8,0,0,128,127,0,0}});assert(std::isnan(cache.bone(p,*blend,blend,weighted,weighted,4)));
 auto partial=mesh({{7,0,0,0,200,0,0,0}});assert(std::isnan(cache.bone(p,*partial,partial,weighted,weighted,4)));
 auto same=mesh({{4,4,4,4,0,0,0,0}});assert(cache.bone(p,*same,same,plain,plain,3)==4);
 Program noIndices;noIndices.inputs={{0,0,0}};assert(std::isnan(cache.bone(noIndices,*fence,fence,weighted,weighted,4)));
 NorthlightDrawSnapshot::Mesh copy=*fence;assert(cache.bone(p,copy,nullptr,weighted,weighted,4)==7); /* ownerless: direct scan */
 auto short_=mesh({{7,0,0,0,255,0,0,0}});short_->streams[0].bytes.resize(10);assert(std::isnan(cache.bone(p,*short_,short_,weighted,weighted,4)));
 { auto big=std::make_shared<NorthlightDrawSnapshot::Mesh>(*fence);big->vertexCount=RigidBoneCache::MaxVertices+1;assert(std::isnan(cache.bone(p,*big,big,weighted,weighted,4))); }
 { RigidBoneCache big;std::vector<std::shared_ptr<NorthlightDrawSnapshot::Mesh>> crowd;
   for(unsigned i=0;i<3000;++i)crowd.push_back(mesh({{std::uint8_t(i%5),0,0,0,255,0,0,0},{std::uint8_t(i%5),0,0,0,255,0,0,0}}));
   for(size_t i=0;i<crowd.size();++i)assert(big.bone(p,*crowd[i],crowd[i],weighted,weighted,4)==float(i%5));
   big.beginFrame();for(size_t i=0;i<crowd.size();++i)assert(big.bone(p,*crowd[i],crowd[i],weighted,weighted,4)==float(i%5));
   std::printf("rigid cache crowd working set 3000: second frame hits=%u misses=%u scannedVertices=%zu\n",big.hits,big.misses,big.scannedVertices);
   assert(big.misses*20<crowd.size()); }
 { // Palette root: only the audited four-bone template (skin-envelope specialization).
   FILE* f=std::fopen(REFERENCE,"rb");assert(f);std::vector<Word> w(512);w.resize(std::fread(w.data(),4,w.size(),f));std::fclose(f);
   Program four;assert(compile(w.data(),w.size(),four)&&four.paletteBase==31&&NorthlightReplayBounds::SkinEnvelope::supports(four));
   float bank[1024]={};bank[4*31+3]=7;bank[4*32+3]=-2;bank[4*33+3]=3; /* bone 0 view-space translation */
   float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,100,200,300,1},root[3];
   assert(rootWorld(four,bank,inverse,root)&&root[0]==107&&root[1]==198&&root[2]==303);
   float rows[12];std::memcpy(rows,bank+4*31,sizeof rows);
   float fromRows[3];assert(rootWorldFromPaletteRows(rows,inverse,fromRows)&&!std::memcmp(root,fromRows,sizeof root));
   rows[7]=NAN;assert(!rootWorldFromPaletteRows(rows,inverse,fromRows));
   Program other=four;other.operations.pop_back();assert(!NorthlightReplayBounds::SkinEnvelope::supports(other)); /* any deviation: vertex-sample fallback */
   Program flat;assert(!rootWorld(flat,bank,inverse,root)); /* no a0 palette */
   // evaluateFast (sampled distances): the skinning template written out and the
   // decoded generic kernel give evaluate()'s result and failures bit for bit.
   assert(four.kernel.skin&&four.kernel.valid);Program generic=four;generic.kernel.skin=false;Program plain=four;plain.kernel=Kernel{};
   std::uint32_t state=12345;auto next=[&]{state=state*1664525u+1013904223u;return state;};
   unsigned n=0;auto pick=[&](int kind)->float{const std::uint32_t r=next();switch(r%(n%8?4096:16)){case 0:return NAN;case 1:return INFINITY;case 2:return -1e30f;case 3:return float(int(next()%80))+.5f;case 4:return -float(next()%40);case 5:return 0.f;
       default:return kind==2?float(next()%75)+(r%3==0?float(next()%1000)*1e-7f:0.f):kind==1?float(next()%1000)/999.f:(float(int(next()%20001))-10000.f)*.01f;}};
   std::size_t same=0,failed=0;float fuzz[1024];
   for(;n<200000;++n){Four in[16]={};for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)in[r][c]=pick(int(r));if(n%2)in[0][3]=1;
       if(n%97==0)for(auto& x:fuzz)x=next()%64?(float(int(next()%2001))-1000.f)*.01f:pick(0);else if(n%1000==0||n==0)for(unsigned k=0;k<1024;++k)fuzz[k]=(float(int(next()%2001))-1000.f)*.01f;
       Four a{},b{},c{},d{};const bool ra=evaluate(four,in,fuzz,a),rb=evaluateFast(four,in,fuzz,b),rc=evaluateFast(generic,in,fuzz,c),rd=evaluateFast(plain,in,fuzz,d);
       assert(ra==rb&&ra==rc&&ra==rd);if(ra){assert(!std::memcmp(a.data(),b.data(),16)&&!std::memcmp(a.data(),c.data(),16)&&!std::memcmp(a.data(),d.data(),16));++same;}else ++failed;}
   std::printf("evaluateFast == evaluate bit for bit: %zu evaluated, %zu failing alike (NaN/inf, half indices, palette out of range)\n",same,failed);assert(same>50000&&failed>10000); }
 std::puts("PASS rigid bone cache + palette root template gate + exact fast evaluator: weighted/unweighted palettes, two bones, blended and partial weights, shader without indices, ownerless scan, bounds, vertex cap");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-actor-shadow-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(harness.replace('REFERENCE','"'+str(client_fixtures.four_bone_vs3())+'"'))
 # 0.3.138 reference for the decision-for-decision equivalence test.
 (p/'legacy_selection.h').write_text((fp.FIXTURES/'reference/actor_shadow_selection-0.3.138.h').read_text().replace('NorthlightActorShadowSelection','LegacyActorShadowSelection'))
 # ActorShadowRadius off must be the 0.3.144 selection: the policy simulation is also
 # built against the 0.3.144 header (a copy beside the test source wins the quoted
 # include) and must print identical output, decision digest included.
 base=p/'base';base.mkdir();(base/'actor_shadow_selection.h').write_text((fp.FIXTURES/'reference/actor_shadow_selection-0.3.144.h').read_text())
 (base/'test_actor_shadow_selection.cpp').write_text((HERE/'test_actor_shadow_selection.cpp').read_text())
 for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']):
  outputs={}
  for source,out in ((p/'test.cpp','cache'),(HERE/'test_actor_shadow_selection.cpp','policy'),(base/'test_actor_shadow_selection.cpp','policy-0.3.144'),(HERE/'test_actor_shadow_selection_equivalence.cpp','equivalence'),(HERE/'test_actor_shadow_radius.cpp','radius')):
   subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(source),'-o',str(p/out)],check=True)
   outputs[out]=subprocess.run([str(p/out)],check=True,capture_output=True,text=True).stdout;print(outputs[out],end='',flush=True)
  assert 'decision digest=' in outputs['policy'] and outputs['policy']==outputs['policy-0.3.144'],'radius off differs from 0.3.144'
  print('PASS radius off == 0.3.144: identical policy output',[l for l in outputs['policy'].splitlines() if l.startswith('decision digest')][0],flush=True)
