# northlight-test: requires=cxx
"""Same-draw source admission preserves snapshot bytes, budgets and count limits."""
from pathlib import Path
import ast
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
        if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

stub = literal(fp.TESTS / 'test_terrain_snapshot.py', 'stub')
prefix = literal(fp.TESTS / 'test_draw_snapshot.py', 'harness').split('int main(){', 1)[0]
prefix = prefix.replace('struct Device:IDirect3DDevice9{', 'struct Device:IDirect3DDevice9{ unsigned geometryCalls=0;')
prefix = prefix.replace('override{*out=freq[s]', 'override{++geometryCalls;*out=freq[s]')
prefix = prefix.replace('override{*out=&vb[s]', 'override{++geometryCalls;*out=&vb[s]')
prefix = prefix.replace('override{*out=&ib;', 'override{++geometryCalls;*out=&ib;')
source = prefix + r'''
static uint64_t revision=1;
static uint64_t identity(void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));}
static uint64_t version(void*,bool){return revision;}
static void configure(Frame& f){f.setIdentityProvider(identity);f.setVersionProvider(version);f.setSnapshotCacheLimit(1u<<20);}
int main(){
 unsigned checks=0;
 for(unsigned kind=0;kind<4;++kind){Device d;Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};
  if(kind==1)d.indices({999,7,9,8},true);
  if(kind==2){d.indices({999,7,10,7});draw.vertices=4;}
  if(kind==3){draw.indexed=false;draw.start=4;draw.minimum=draw.vertices=draw.base=0;}
  Frame producer;configure(producer);Mesh scratch;std::shared_ptr<const Mesh> snapshot;
  assert(producer.read(&d,&d.decl,draw,scratch,nullptr,false,&snapshot)&&snapshot&&snapshot->sourceIdentity);
  const auto charge=producer.bytesRead();assert(charge>0);const auto original=snapshot->streams[0].bytes;
  Frame reference,borrowed;configure(reference);configure(borrowed);assert(reference.shareSnapshotPool(producer));assert(borrowed.shareSnapshotPool(producer));
  for(unsigned sample=0;sample<24;++sample){const size_t limit=charge*(2+sample%5)+sample,regular=limit/3;
   reference.clearFrame();borrowed.clearFrame();assert(reference.configureBudget(limit,regular)&&borrowed.configureBudget(limit,regular));reference.setNearReserve(charge*2);borrowed.setNearReserve(charge*2);
   for(unsigned i=0;i<40;++i){const bool priority=i%3==0,nearby=i%5==0;Diagnostics a,b;std::shared_ptr<const Mesh> expectedMesh,actualMesh;bool served=false;
    const bool expected=reference.read(&d,&d.decl,draw,scratch,&a,priority,&expectedMesh,nearby);const auto calls=d.geometryCalls;
    const bool actual=borrowed.readShared(&d.decl,draw,snapshot,actualMesh,served,&b,priority,nearby);assert(actual==expected&&a.error==b.error&&a.hr==b.hr&&d.geometryCalls==calls);assert(served==actual);
    assert(reference.bytesRead()==borrowed.bytesRead());assert(reference.countExhausted(priority)==borrowed.countExhausted(priority));assert(reference.captureExhausted(priority,nearby)==borrowed.captureExhausted(priority,nearby));if(actual)assert(expectedMesh==actualMesh&&actualMesh==snapshot&&snapshot->streams[0].bytes==original);++checks;
   }
  }
  // Every draw argument and tracking domain is part of the handoff proof.
  borrowed.clearFrame();
  for(unsigned field=0;field<8;++field){Draw changed=draw;if(field==0)++changed.base;if(field==1)++changed.minimum;if(field==2)++changed.vertices;if(field==3)++changed.start;if(field==4)++changed.primitives;if(field==5)changed.topology=D3DPT_TRIANGLESTRIP;if(field==6)changed.indexed=!changed.indexed;Declaration other;
   bool served=true;std::shared_ptr<const Mesh> out;assert(borrowed.readShared(field==7?&other:&d.decl,changed,snapshot,out,served)&&!served&&!out&&!borrowed.bytesRead());}
  borrowed.setIdentityProvider([](void*,bool){return uint64_t(7);});bool servedDomain=true;std::shared_ptr<const Mesh> domainOut;assert(borrowed.readShared(&d.decl,draw,snapshot,domainOut,servedDomain)&&!servedDomain&&!borrowed.bytesRead());borrowed.setIdentityProvider(identity);
  reference.clearFrame();borrowed.clearFrame();assert(reference.configureBudget(charge*6000,0)&&borrowed.configureBudget(charge*6000,0));
  for(unsigned i=0;i<4097;++i){const bool priority=i>=3072;Diagnostics a,b;std::shared_ptr<const Mesh> one,two;bool served=false;
   assert(reference.read(&d,&d.decl,draw,scratch,&a,priority,&one,true)==borrowed.readShared(&d.decl,draw,snapshot,two,served,&b,priority,true));assert(a.error==b.error&&reference.bytesRead()==borrowed.bytesRead());}
  assert(borrowed.countExhausted(true));borrowed.clearFrame();assert(!borrowed.bytesRead()&&!borrowed.countExhausted(false));
  // Missing/evicted entries cannot authorize the shortcut; ordinary reading remains available.
  reference.clearSnapshotCache();bool served=false;std::shared_ptr<const Mesh> miss;Diagnostics why;const auto calls=d.geometryCalls;
  assert(borrowed.readShared(&d.decl,draw,snapshot,miss,served,&why)&&!served&&!miss&&!borrowed.bytesRead()&&d.geometryCalls==calls);
  assert(borrowed.read(&d,&d.decl,draw,scratch,&why,false,&miss)&&miss&&miss->streams[0].bytes==original);
  ++revision;d.vb[0].bytes[d.offset[0]+4*d.stride[0]+4]^=37;
  std::shared_ptr<const Mesh> updated;assert(producer.read(&d,&d.decl,draw,scratch,nullptr,false,&updated)&&updated!=snapshot);
  assert(snapshot->streams[0].bytes==original&&updated->streams[0].bytes!=original);
 }
 {Device d;for(auto& b:d.vb)b.desc.Usage=0;d.ib.desc.Usage=0;Frame producer;producer.setIdentityProvider(identity);std::shared_ptr<const Mesh> untracked;Mesh scratch;Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};assert(producer.read(&d,&d.decl,draw,scratch,nullptr,false,&untracked)&&untracked&&!untracked->sourceIdentity);Frame consumer;std::shared_ptr<const Mesh> out;bool served=true;assert(consumer.readShared(&d.decl,draw,untracked,out,served)&&!served&&!consumer.bytesRead());}
 printf("CAPTURE_HANDOFF_PASS budgetChecks=%u indexed16_32 sparse_multistream nonindexed near_reserve count_caps immutable_generations evicted_untracked_miss no_device_calls\n",checks);
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-handoff-') as tmp:
    directory = Path(tmp);(directory / 'd3d9.h').write_text(stub);(directory / 'test.cpp').write_text(source)
    for label, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
        exe = directory / label
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, '-I', str(directory),
                        *fp.test_include_flags(), str(directory / 'test.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
