// 0.3.210 (task 31) capture admission (capture_admission.h): the distance cut against the real
// NorthlightDrawSnapshot::Frame budget, nearest-first ordering, hysteresis, near-reserve hits, the
// radius gate (identity, self-sustaining list, walking actor, no self, radius 0) and the GI exception.
#include "capture_admission.h"
#include "draw_snapshot.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <vector>
using namespace NorthlightCaptureAdmission;
constexpr std::size_t MiB=1u<<20;
// ---- the real Frame behind a one-stream mock device ----
template<class Interface,class Desc>struct Buffer:Interface{
 unsigned refs=1;Desc desc;std::vector<std::uint8_t> bytes;
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDesc(Desc*out)override{*out=desc;out->Size=UINT(bytes.size());return 0;}
 HRESULT Lock(UINT offset,UINT size,void**out,DWORD)override{assert(std::uint64_t(offset)+size<=bytes.size());*out=bytes.data()+offset;return 0;}
 HRESULT Unlock()override{return 0;}
};
struct Declaration:IDirect3DVertexDeclaration9{
 unsigned refs=1;std::vector<D3DVERTEXELEMENT9> e={{0,0,2,0,0,0},{0xff,0,17,0,0,0}};
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDeclaration(D3DVERTEXELEMENT9*out,UINT*n)override{*n=UINT(e.size());std::memcpy(out,e.data(),e.size()*sizeof(*out));return 0;}
};
struct Device:IDirect3DDevice9{
 Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC> vb;Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC> ib;Declaration decl;
 Device(unsigned vertices,unsigned triangles){vb.desc.Usage=0;vb.bytes.assign(std::size_t(vertices)*24,7);
  ib.desc.Usage=0;ib.desc.Format=D3DFMT_INDEX16;ib.bytes.resize(std::size_t(triangles)*3*2);
  for(unsigned i=0;i<triangles*3;++i){const std::uint16_t v=std::uint16_t(i%vertices);std::memcpy(ib.bytes.data()+2*i,&v,2);}}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return D3DERR_INVALIDCALL;}
 HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**out)override{*out=&decl;decl.AddRef();return 0;}
 HRESULT GetStreamSourceFreq(UINT,UINT*out)override{*out=1;return 0;}
 HRESULT GetStreamSource(UINT s,IDirect3DVertexBuffer9**out,UINT*o,UINT*st)override{if(s){*out=nullptr;*o=*st=0;return 0;}*out=&vb;vb.AddRef();*o=0;*st=24;return 0;}
 HRESULT GetIndices(IDirect3DIndexBuffer9**out)override{*out=&ib;ib.AddRef();return 0;}
};
// ---- a synthetic budget: the same admission calls, a counter instead of the Frame ----
struct Char {std::uintptr_t shader=1;unsigned count=500,vertexTotal=8192;float root[3]={};std::size_t bytes=MiB;bool near=false;};
struct Outcome {std::vector<Verdict> verdict;std::vector<bool> read;std::size_t used=0;};
struct Sim {
 Admission a;std::size_t limit=0,nonPriority=0,reserve=0;Context ctx;
 Outcome frame(const std::vector<Char>& draws){
  if(!a.armed())a.arm(ctx);
  Outcome r;r.used=nonPriority;
  for(const auto& c:draws){const Decision d=a.decide(c.shader,c.count,c.vertexTotal,c.root,c.near);r.verdict.push_back(d.verdict);bool ok=false;
   if(d.verdict==Verdict::Admit){const std::size_t cap=limit+(c.near?reserve:0);
    if(r.used+c.bytes<=cap){r.used+=c.bytes;a.admitted(d,c.shader,c.count,c.root,c.bytes);ok=true;}else a.budgetRefused(d,c.root,c.vertexTotal,c.count);}
   r.read.push_back(ok);}
  a.endFrame(limit,r.used);return r;
 }
};
static Char at(float x,float y,float z=0){Char c;c.root[0]=x;c.root[1]=y;c.root[2]=z;return c;}
static float dist(const Char& c){return std::sqrt(c.root[0]*c.root[0]+c.root[1]*c.root[1]+c.root[2]*c.root[2]);}
// A crowd of n identical characters, nearest first by index: distance 10+0.9*i, spread round the centre.
static std::vector<Char> crowd(unsigned n){std::vector<Char> v;for(unsigned i=0;i<n;++i){const float d=10.f+.9f*i,a=float(i)*2.4f;v.push_back(at(d*std::cos(a),d*std::sin(a)));}return v;}
static std::vector<Char> farFirst(std::vector<Char> v){return std::vector<Char>(v.rbegin(),v.rend());}
static Context self(float yards=0,bool gi=false){static const float origin[3]={0,0,0};Context c;c.centre=origin;c.self=true;c.camera=origin;c.yards=yards;c.gi=gi;return c;}
int main(){
 // 0. The real Frame: 32 MiB / 16 MiB, 4 MiB near reserve, ~4 MiB of non-priority draws first, then the
 // priority crowd far-first with the cut's target. After the cut has settled no in-cut draw fails with Error::Budget.
 {Device device(18000,6000);NorthlightDrawSnapshot::Frame frame;bool configured=frame.configureBudget(32*MiB,16*MiB);assert(configured);(void)configured;
  frame.setNearReserve(4*MiB);Admission admission;const Context ctx=self(0);const auto people=crowd(80);
  unsigned unfit=0,settled=0;std::size_t perDraw=0;
  for(unsigned f=0;f<8;++f){
   frame.clearFrame();NorthlightDrawSnapshot::Mesh mesh;NorthlightDrawSnapshot::Diagnostics why;NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,18000,0,6000,true};
   for(unsigned k=0;k<8;++k)while(frame.bytesRead()<(k+1)*MiB/2)assert(frame.read(&device,&device.decl,draw,mesh,&why,false));
   const std::size_t nonPriority=frame.bytesRead();admission.arm(ctx);unsigned inCutFailures=0,admitted=0;
   for(auto it=people.rbegin();it!=people.rend();++it){
    const Decision d=admission.decide(1,500,18000,it->root,false);if(d.verdict!=Verdict::Admit)continue;
    const std::size_t before=frame.bytesRead();
    if(frame.read(&device,&device.decl,draw,mesh,&why,true)){++admitted;perDraw=frame.bytesRead()-before;admission.admitted(d,1,500,it->root,perDraw);}
    else{assert(why.error==NorthlightDrawSnapshot::Error::Budget);++inCutFailures;admission.budgetRefused(d,it->root,18000,500);}}
   if(f>=2){settled+=1;unfit+=inCutFailures;assert(admitted>0&&admitted<people.size());}
   assert(nonPriority>=4*MiB&&nonPriority<=5*MiB);
   admission.endFrame(32*MiB,frame.bytesRead());}
  assert(perDraw>0&&settled==6);
  assert(unfit==0); /* no in-cut draw failed with Error::Budget after the first exhausted frame */
  assert(admission.cutActive());
  std::puts("PASS 0: real Frame 32/16 MiB + 4 MiB near reserve, 4 MiB non-priority first: no in-cut draw fails with Error::Budget once the cut settled");}
 // 1. Distance ordering: 60 characters far-first, the budget holds 20 (24 MiB, target 20.4): after frame 2 the 20 nearest.
 {Sim s;s.limit=24*MiB;s.ctx=self(0);const auto people=crowd(60);const auto order=farFirst(people);
  for(unsigned f=0;f<8;++f){const auto r=s.frame(order);
   if(!f)assert(s.a.cutActive()); /* frame 1 exhausts far-first; the cut is set at its end */
   if(!f)continue;
   unsigned admitted=0;for(std::size_t i=0;i<order.size();++i){const std::size_t index=order.size()-1-i; /* order[i] is people[index] */
    const bool shouldBe=index<20;assert(r.read[i]==shouldBe);admitted+=r.read[i];
    if(!shouldBe)assert(r.verdict[i]==Verdict::DistanceCut);}
   assert(admitted==20&&r.used<=s.limit);}
  const float cut=s.a.cut();assert(cut>people[19].root[0]*0+dist(people[19])&&cut<dist(people[20]));
  std::puts("PASS 1: 60 characters far-first, budget for 20: from frame 2 the 20 nearest are read, the refused are the farthest");}
 // 2. Hysteresis: a character oscillating +-0.3 yd across the cut for 300 frames never changes; jittering crowd, no toggles.
 {auto people=crowd(60);Sim s;s.limit=24*MiB;s.ctx=self(0);
  for(unsigned f=0;f<6;++f)s.frame(farFirst(people));
  const float cut=s.a.cut();assert(std::isfinite(cut));
  // The 20th character (index 19) walks to and fro across the cut.
  std::vector<bool> previous;unsigned changes=0;
  for(unsigned f=0;f<300;++f){auto now=people;const float want=cut+((f/3)%2?.3f:-.3f),scale=want/dist(now[19]);for(unsigned k=0;k<3;++k)now[19].root[k]*=scale;
   const auto r=s.frame(farFirst(now));std::vector<bool> read(r.read.rbegin(),r.read.rend());if(f&&read!=previous)++changes;previous=read;}
  assert(changes<=1);
  Sim t;t.limit=24*MiB;t.ctx=self(0);for(unsigned f=0;f<6;++f)t.frame(farFirst(people)); /* a fresh settled cut: the oscillation above may have grown it */
  std::mt19937 rng(7);std::uniform_real_distribution<float> jitter(-.3f,.3f);unsigned toggles=0;previous.clear();
  for(unsigned f=0;f<300;++f){auto now=people;for(auto& c:now){const float scale=(dist(c)+jitter(rng))/dist(c);for(unsigned k=0;k<3;++k)c.root[k]*=scale;}
   const auto r=t.frame(farFirst(now));std::vector<bool> read(r.read.rbegin(),r.read.rend());if(f&&read!=previous)++toggles;previous=read;}
  assert(toggles==0);
  std::puts("PASS 2: +-0.3 yd at the cut over 300 frames: no change; whole-crowd jitter: no toggles");}
 // 3. Near-reserve hits always pass the cut, also far beyond it and past the main limit.
 {Sim s;s.limit=24*MiB;s.reserve=4*MiB;s.ctx=self(0);const auto people=crowd(60);
  for(unsigned f=0;f<4;++f)s.frame(farFirst(people));assert(s.a.cutActive());
  auto draws=farFirst(people);Char companion=at(55,0,0);companion.near=true;draws.insert(draws.begin(),companion);
  const auto r=s.frame(draws);assert(r.verdict[0]==Verdict::Admit);
  Char same=companion;same.near=false;draws[0]=same;const auto r2=s.frame(draws);assert(r2.verdict[0]==Verdict::DistanceCut);
  std::puts("PASS 3: a near-reserve hit passes the cut at 55 yd; the same draw without the hit is cut");}
 // 4. Radius gate.
 {const std::uintptr_t S=0x1000,P=0x2000;const float far[3]={70,0,0};
  Item entry;entry.shader=S;entry.count=900;for(unsigned k=0;k<3;++k)entry.at[k]=far[k];
  const float gate=gateYards(40);assert(gate==56);
  auto body=[&](float x){Char c=at(x,0);c.shader=S;c.count=900;return c;};
  auto verdictOf=[&](Sim& s,const Char& c){return s.a.decide(c.shader,c.count,c.vertexTotal,c.root,false).verdict;};
  // The listed body beyond the gate is rejected; the same position with another count or shader is admitted; a same-model
  // newcomer 3 yd away is admitted.
  {Sim s;s.limit=1000*MiB;s.ctx=self(40);s.a.importFar({entry});s.a.arm(s.ctx);
   assert(verdictOf(s,body(70))==Verdict::RadiusGate);
   Char prop=body(70);prop.count=901;assert(verdictOf(s,prop)==Verdict::Admit);
   Char other=body(70);other.shader=P;assert(verdictOf(s,other)==Verdict::Admit);
   assert(verdictOf(s,body(73))==Verdict::Admit);
   assert(s.a.counters().radiusGated==1&&s.a.counters().radiusGatedBytes>0);
   // Inside the gate: admitted though listed. Near hit: admitted though listed and beyond.
   assert(verdictOf(s,body(55.5f))==Verdict::Admit);
   assert(s.a.decide(S,900,8192,body(70).root,true).verdict==Verdict::Admit);}
  // Self-sustaining: the list is imported once and then empty (the selection forgot it); 100 frames later the body is still
  // gated, the prop beside it never is (no spreading).
  {Sim s;s.limit=1000*MiB;s.ctx=self(40);s.a.importFar({entry});Char prop=body(70.5f);prop.count=901;unsigned gated=0;
   for(unsigned f=0;f<100;++f){const auto r=s.frame({body(70),prop});gated+=r.verdict[0]==Verdict::RadiusGate;assert(r.verdict[1]==Verdict::Admit&&r.read[1]);
    s.a.importFar({});}
   assert(gated==100);}
  // A walking actor 70 -> 30 yd (0.5 yd per capture frame): gated exactly while beyond the gate, admitted from there on.
  {Sim s;s.limit=1000*MiB;s.ctx=self(40);s.a.importFar({entry});bool admitted=false;float firstAdmitted=0;
   for(float x=70;x>=30;x-=.5f){const auto r=s.frame({body(x)});s.a.importFar({});
    if(x>gate+1e-3f)assert(r.verdict[0]==Verdict::RadiusGate&&!admitted);
    else{if(!admitted)firstAdmitted=x;admitted=true;assert(r.verdict[0]==Verdict::Admit);}}
   assert(admitted&&firstAdmitted<=gate&&firstAdmitted>gate-.5f-1e-3f);}
  // No self (pivot centre): no gate. ActorShadowRadius 0: no gate.
  {Sim s;s.limit=1000*MiB;s.ctx=self(40);s.ctx.self=false;s.a.importFar({entry});s.a.arm(s.ctx);assert(verdictOf(s,body(70))==Verdict::Admit);}
  {Sim s;s.limit=1000*MiB;s.ctx=self(0);s.a.importFar({entry});s.a.arm(s.ctx);assert(verdictOf(s,body(70))==Verdict::Admit);}
  // importOutside: every draw of every outside body section, none of the inside, companion, self, unfiltered, rigid or rootless ones.
  {using namespace NorthlightActorShadowSelection;
   auto section=[&](std::vector<Actor>& actors,std::vector<Draw>& draws,unsigned batches,float x,std::uintptr_t shader){Actor a;a.first=draws.size();a.count=batches;a.outside=true;
    for(unsigned b=0;b<batches;++b){Draw d;d.shader=shader+b;d.count=100+b;d.captureRoot[0]=x;d.captureRootValid=true;draws.push_back(d);}actors.push_back(a);};
   std::vector<Actor> actors;std::vector<Draw> draws;
   section(actors,draws,5,70,0x10); /* the far body: five batches, five identities */
   section(actors,draws,2,71,0x20);actors.back().companion=true;section(actors,draws,2,72,0x30);actors.back().self=true;
   section(actors,draws,2,73,0x40);actors.back().unfiltered=true;section(actors,draws,2,74,0x50);actors.back().rigid=true;section(actors,draws,2,75,0x60);actors.back().outside=false;
   section(actors,draws,2,76,0x70);for(unsigned k=0;k<2;++k)draws[draws.size()-1-k].captureRootValid=false;
   Sim s;s.limit=1000*MiB;s.ctx=self(40);s.a.importOutside(actors,draws);s.a.arm(s.ctx);
   for(unsigned b=0;b<5;++b){Char c=at(70,0);c.shader=0x10+b;c.count=100+b;assert(verdictOf(s,c)==Verdict::RadiusGate);}
   for(std::uintptr_t shader:{0x20,0x30,0x40,0x50,0x60,0x70}){Char c=at(70+float(shader>>4)-1,0);c.shader=shader;c.count=100;assert(verdictOf(s,c)==Verdict::Admit);}}
  // A far body with 5 draws (different shader and count, one root): all five gated from the frame after the selection put it outside, for
  // 100 frames with no further import; a free rigid prop 1 yd away with its own identity is never gated.
  {Sim s;s.limit=1000*MiB;s.ctx=self(40);std::vector<Item> body5;for(unsigned b=0;b<5;++b){Item i;i.shader=0x100+b;i.count=300+7*b;i.at[0]=70;body5.push_back(i);}
   s.a.importFar(body5);unsigned gated=0;
   for(unsigned f=0;f<100;++f){std::vector<Char> draws;for(const auto& i:body5){Char c=at(70,0);c.shader=i.shader;c.count=i.count;draws.push_back(c);}
    Char prop=at(71,0);prop.shader=0x900;prop.count=64;draws.push_back(prop);
    const auto r=s.frame(draws);for(unsigned b=0;b<5;++b)gated+=r.verdict[b]==Verdict::RadiusGate;assert(r.verdict[5]==Verdict::Admit&&r.read[5]);
    if(f)s.a.importFar({});}
   assert(gated==500);}
  // A draw without a root is never gated or cut.
  {Sim s;s.limit=1000*MiB;s.ctx=self(40);s.a.arm(s.ctx);assert(s.a.decide(S,900,8192,nullptr,false).verdict==Verdict::Admit&&s.a.counters().noRoot==1);}
  std::puts("PASS 4: radius gate: identity match only, no spreading over 100 frames, walking actor admitted at the gate, no self / radius 0 / non-outside entries: no gate, no root: never gated");}
 // 5. GI exception: on a GI frame a gated draw within 100 yd of the camera is admitted while the cut is inactive.
 {const std::uintptr_t S=0x1000;Item entry;entry.shader=S;entry.count=900;entry.at[0]=70;
  Char b=at(70,0);b.shader=S;b.count=900;
  {Sim s;s.limit=1000*MiB;s.ctx=self(40,true);s.a.importFar({entry});s.a.arm(s.ctx);assert(s.a.decide(S,900,8192,b.root,false).verdict==Verdict::Admit);}
  {Sim s;s.limit=1000*MiB;s.ctx=self(40,false);s.a.importFar({entry});s.a.arm(s.ctx);assert(s.a.decide(S,900,8192,b.root,false).verdict==Verdict::RadiusGate);}
  // Beyond 100 yd of the camera the GI frame gates it as well.
  {Sim s;s.limit=1000*MiB;s.ctx=self(40,true);const float camera[3]={-40,0,0};s.ctx.camera=camera;s.a.importFar({entry});s.a.arm(s.ctx);
   assert(s.a.decide(S,900,8192,b.root,false).verdict==Verdict::RadiusGate);}
  // The cut active: the GI exception is off (no draw-order exhaustion at 5 Hz).
  {Sim s;s.limit=24*MiB;s.ctx=self(40,true);const auto people=crowd(60);for(unsigned f=0;f<4;++f)s.frame(farFirst(people));assert(s.a.cutActive());
   s.a.importFar({entry});s.a.arm(s.ctx);assert(s.a.decide(S,900,8192,b.root,false).verdict==Verdict::RadiusGate);}
  std::puts("PASS 5: GI exception: admitted within 100 yd of the camera only while the cut is inactive");}
 // 6. Counters: gated draws are not demand; refused draws inside the radius are counted by cell.
 {Sim s;s.limit=24*MiB;s.ctx=self(60);const auto people=crowd(60);for(unsigned f=0;f<3;++f)s.frame(farFirst(people));
  s.a.arm(s.ctx);for(const auto& c:farFirst(people)){const Decision d=s.a.decide(c.shader,c.count,c.vertexTotal,c.root,false);(void)d;}
  const auto& n=s.a.counters();assert(n.cutRejected>0&&n.insideRefusedDraws>0&&n.insideRefusedActors>0&&n.insideRefusedActors<=n.insideRefusedDraws);
  std::puts("PASS 6: counters");}
 // 7. Room: a crowd that thins (and a larger budget) gives the cut back, after GrowFrames frames and in steps of at most band(D); everything fits -> inactive.
 {Sim s;s.limit=24*MiB;s.ctx=self(0);auto people=crowd(60);for(unsigned f=0;f<4;++f)s.frame(farFirst(people));assert(s.a.cutActive());
  const float cut=s.a.cut();s.limit=36*MiB;people.resize(30);float previous=cut;unsigned grown=0;
  for(unsigned f=0;f<200;++f){s.frame(farFirst(people));const float now=s.a.cutActive()?s.a.cut():INFINITY;
   if(now!=previous){assert(now>previous);if(std::isfinite(now))assert(now-previous<=NorthlightActorShadowSelection::band(previous)+1e-3f);++grown;}previous=now;
   if(f<GrowFrames-1)assert(now==cut);}
  assert(!std::isfinite(previous)&&grown>=2);
  std::puts("PASS 7: the cut grows only after 10 frames with room, in steps of at most band(D), and ends inactive");}
 // 8. Persistent exhaustion shrinks stepwise (a tight estimate cannot pin the cut): the budget halves under a settled cut.
 {Sim s;s.limit=24*MiB;s.ctx=self(0);const auto people=crowd(60);for(unsigned f=0;f<4;++f)s.frame(farFirst(people));
  const float settled=s.a.cut();s.limit=11*MiB;float previous=settled;unsigned steps=0;
  for(unsigned f=0;f<200;++f){s.frame(farFirst(people));const float now=s.a.cut();assert(now<=previous&&previous-now<=NorthlightActorShadowSelection::band(previous)+1e-3f);steps+=now<previous;previous=now;}
  assert(steps>=1&&previous<settled&&previous>=MinCut);
  const auto r=s.frame(farFirst(people));unsigned admitted=0;for(bool b:r.read)admitted+=b;assert(admitted<=11);
  std::puts("PASS 8: persistent exhaustion shrinks the cut in steps of at most band(D) down to what fits");}
 // 9. reset() forgets everything; the estimate of a cold shape errs high.
 {Sim s;s.limit=24*MiB;s.ctx=self(0);const auto people=crowd(60);for(unsigned f=0;f<4;++f)s.frame(farFirst(people));assert(s.a.cutActive()&&s.a.shapeCount()>0);
  s.a.reset();assert(!s.a.cutActive()&&!s.a.shapeCount()&&!s.a.armed());
  std::puts("PASS 9: reset");}
}
