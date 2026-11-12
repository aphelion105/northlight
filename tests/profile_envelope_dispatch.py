# northlight-test: requires=windows-exe,zig,client,stormlib manual timing
"""Compare material envelope dispatch with a frozen implementation in one executable."""
from pathlib import Path
import argparse,hashlib,json,re,statistics,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
import client_fixtures
p=argparse.ArgumentParser(__doc__);p.add_argument('--reference',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
client_fixtures.actor_client_programs(out);data=client_fixtures.four_bone_vs3().read_bytes();words=struct.unpack('<%dI'%(len(data)//4),data)
with (out/'actor_client_programs.h').open('a',encoding='utf-8') as f:f.write('\nnamespace ClientShaders { static const uint32_t FourBoneVs3[]={'+','.join(map(str,words))+'}; }\n')
old=subprocess.check_output(['git','show',a.reference+':src/replay/replay_envelopes.inl']);(out/'Before.inl').write_bytes(old)
cpp=r'''
#include <chrono>
#include <cstdint>
struct DispatchClock {
 using Clock=std::chrono::steady_clock;using time_point=Clock::time_point;
 static inline bool fake=false;static inline uint64_t calls=0,ticks=0;
 static time_point now(){++calls;return fake?time_point(std::chrono::nanoseconds(++ticks)):Clock::now();}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK DispatchClock
#define main existingEnvelopeMain
#include "material_envelope_fast.cpp"
#undef main
namespace NorthlightReplayBounds {
#define EnvelopeCache BeforeEnvelopeCache
#include "Before.inl"
#undef EnvelopeCache
}
template<class Cache,class Prepared>static void ready(Cache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& m,float* c,float* inverse){
 for(unsigned frame=0;frame<1000;++frame){cache.beginFrame(false);B::Budget budget;B::Bounds bounds;if(cache.calculatePrepared(prepared,*m,m,c,inverse,budget,bounds)==B::Status::Valid)return;}assert(false);
}
struct Sample {double ms;uint64_t clocks=0;unsigned valid=0;B::Bounds last;};
template<class Cache,class Prepared>static Sample sample(Cache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& m,float* c,float* inverse,bool cold,unsigned frames){
 Sample result{};const auto start=std::chrono::steady_clock::now();
 for(unsigned frame=0;frame<frames;++frame){cache.beginFrame(false);B::Budget budget;if(cold)budget.maxVertices=0;const auto clocks=DispatchClock::calls;
  for(unsigned i=0;i<64;++i){B::Bounds bounds;const auto status=cache.calculatePrepared(prepared,*m,m,c,inverse,budget,bounds);
   if(cold)assert(status==B::Status::Budget&&!bounds.valid);else if(status==B::Status::Valid){++result.valid;result.last=bounds;}else assert(!bounds.valid);
  }result.clocks+=DispatchClock::calls-clocks;
 }result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;return result;
}
int main(int argc,char**){using Legacy=B::BeforeEnvelopeCache;float c[1024],inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};pose(c,17);unsigned checked=0;
 for(unsigned mode=0;mode<3;++mode){const auto p=mode==1?program(ClientShaders::OneBoneVs3):program(ClientShaders::FourBoneVs3);auto m=mesh(4);
  auto a=E::prepareProgram(p,decl,4);auto b=Legacy::prepareProgram(p,decl,4);assert(a&&b);E current(true);Legacy before(true);DispatchClock::fake=true;
  if(mode){ready(current,*a,m,c,inverse);ready(before,*b,m,c,inverse);}
  auto x=sample(current,*a,m,c,inverse,!mode,1),y=sample(before,*b,m,c,inverse,!mode,1);
  assert(x.valid==y.valid&&(!mode||(!std::memcmp(x.last.low,y.last.low,12)&&!std::memcmp(x.last.high,y.last.high,12))));
  assert(current.phaseNanoseconds(0)>0&&before.phaseNanoseconds(0)>0);
  assert(mode==1?x.clocks==y.clocks:x.clocks<y.clocks);
  std::printf("ENVELOPE_DISPATCH_COUNTS mode=%u old=%llu new=%llu draws=64\n",mode,(unsigned long long)y.clocks,(unsigned long long)x.clocks);
  for(unsigned frame=0;frame<32;++frame){pose(c,frame);x=sample(current,*a,m,c,inverse,!mode,1);y=sample(before,*b,m,c,inverse,!mode,1);assert(x.valid==y.valid);if(x.valid){assert(!std::memcmp(x.last.low,y.last.low,12)&&!std::memcmp(x.last.high,y.last.high,12));++checked;}}
  current.chargeCheapPreparation(1000000);before.chargeCheapPreparation(1000000);B::Budget budget;B::Bounds bounds;
  assert(current.calculatePrepared(*a,*m,m,c,inverse,budget,bounds)==B::Status::Budget&&!bounds.valid);assert(before.calculatePrepared(*b,*m,m,c,inverse,budget,bounds)==B::Status::Budget&&!bounds.valid);
  DispatchClock::fake=false;
  if(argc>1)for(unsigned block=0;block<20;++block){const bool old=block%4==0||block%4==3;const auto r=old?sample(before,*b,m,c,inverse,!mode,64):sample(current,*a,m,c,inverse,!mode,64);
   std::printf("ENVELOPE_DISPATCH_TIMING mode=%u old=%u warm=%u cpuMs=%.9f valid=%u expected=%u clocks=%llu\n",mode,unsigned(old),unsigned(block<4),r.ms,r.valid,mode?4096u:0u,(unsigned long long)r.clocks);
  }
 }assert(checked==64);puts("ENVELOPE_DISPATCH_PASS exact_bounds cold_budget warm_single warm_weighted counted_clocks exhausted_budget");
}
'''
(out/'test.cpp').write_text(cpp,encoding='utf-8');runs=[];counts=[]
for opt in ('-O0','-O2'):
 exe=out/(opt[1:]+'.exe');subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17','-I'+str(out),'-I'+str(ROOT/'tests'),*fp.test_include_flags(),str(out/'test.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
 run=subprocess.run([str(exe),*(['bench'] if opt=='-O2' else [])],capture_output=True,text=True)
 (out/(opt[1:]+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
 if run.returncode:print(run.stdout+run.stderr,flush=True)
 run.check_returncode();assert 'ENVELOPE_DISPATCH_PASS' in run.stdout
 if opt=='-O2':
  counts=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('ENVELOPE_DISPATCH_COUNTS ')]
  runs=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('ENVELOPE_DISPATCH_TIMING ') and 'warm=0 ' in line]
timing=[]
for mode in range(3):
 row=dict(mode=mode)
 for name,value in [('before_ms',1),('after_ms',0)]:
  samples=[float(r['cpuMs']) for r in runs if int(r['mode'])==mode and int(r['old'])==value and r['valid']==r['expected']]
  assert len(samples)>=4,(mode,name,len(samples));row[name]=statistics.median(samples)
 timing.append(row)
files=[fp.src('replay_envelopes.inl'),ROOT/'tests/material_envelope_fast.cpp',Path(__file__).resolve()]
report=dict(pass_=True,reference=a.reference,inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},counts=counts,timing=timing,runs=runs,scope='64 current-pose dispatches per batch; source already prepared. Cold build budget exhausted / warm single / warm weighted. Same executable, all valid timing samples compared; no game FPS claim.')
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
