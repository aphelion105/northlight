#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Run the production Replay ownership/pool functions without a game/device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,subprocess,tempfile
import argparse
parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--zig',action='store_true',help='Run the Windows CPU oracle with the pinned Zig compiler; native sanitizer runs remain the default.')
args=parser.parse_args()
HERE=Path(__file__).resolve().parent
source=fp.src('world_renderer.h').read_text()
def block(needle):
 start=source.index(needle);first=source.index('{',start);depth=1;end=first+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 if source[end:end+1]==';':end+=1
 return source[start:end]
def literal(file,key):
 return next(ast.literal_eval(n.value)for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id==key for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
if args.zig:
 # Match the Win32 aliases also included by cpu_retirement.h.
 stub=stub.replace('using DWORD=unsigned;using HRESULT=std::int32_t;', 'using DWORD=unsigned long;using HRESULT=long;')
fixture=literal(HERE/'test_draw_snapshot.py','harness').split('int main()')[0]
production='\n'.join(block(needle)for needle in ['struct Replay {','std::unique_ptr<Replay> acquireReplay(){','void recycleReplay(Replay* raw){','struct ReplayRecycle {'])
# Fields before methods are declared as in the real owner; only this small
# dependency shell is synthetic. Tested implementations are extracted verbatim.
replay=block('struct Replay {')
methods='\n'.join(block(needle)for needle in ['std::unique_ptr<Replay> acquireReplay(){','void recycleReplay(Replay* raw){','struct ReplayRecycle {'])
text=fixture+r'''
#include <memory>
#include <cmath>
#include "shader_constant_usage.h"
#include "replay_capture_constants.h" /* Replay::constantStamp, releaseResources() (0.3.136) */
#include "world_gi.h"
using V=NorthlightGI::Vec3;
namespace NorthlightReplayBounds {struct Bounds {float low[3]={},high[3]={};bool valid=false;};struct WorkInfo {};struct Prepared {};}
namespace NorthlightActorDeformation {struct Program {};} /* 0.3.177: the Replay's program */
namespace NorthlightActorPrepare {struct DeclCopy {UINT count=0;D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]={};};} /* 0.3.179: the Replay's declaration copy */
using BOOL=int;constexpr DWORD D3DTADDRESS_WRAP=1;
struct ModelRef:IRef{unsigned refs=1,releases=0;unsigned AddRef()override{return ++refs;}unsigned Release()override{++releases;return --refs;}};
using IDirect3DVertexShader9=ModelRef;using IDirect3DBaseTexture9=ModelRef;
template<class T>void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
class WorldRenderer {public:
'''+replay+r'''
 std::vector<std::unique_ptr<Replay>> replays,freeReplays;size_t pooledSnapshotBytes=0;
 std::vector<std::unique_ptr<Replay>> prepareQuarantine;bool prepareHeld(const Replay&)const{return false;} /* 0.3.183: no abandoned prepare worker */
 size_t poolLimit=48u*1024u*1024u;
 size_t replayPoolLimit()const{return poolLimit;}
'''+methods+r'''
};
int main(){WorldRenderer owner;ModelRef shader,texture;Device device;WorldRenderer::Replay* first=nullptr;
 for(unsigned draw=0;draw<10000;++draw){
  std::unique_ptr<WorldRenderer::Replay,WorldRenderer::ReplayRecycle> p(owner.acquireReplay().release(),{&owner});
  if(!first)first=p.get();assert(first==p.get());assert(owner.pooledSnapshotBytes==0);
  p->shader=&shader;shader.AddRef();p->texture=&texture;texture.AddRef();p->stream[0]=&device.vb[0];device.vb[0].AddRef();p->index=&device.ib;device.ib.AddRef();p->decl=&device.decl;device.decl.AddRef();
  p->snapshot.streams[0].bytes.resize(4096);p->snapshot.indices.resize(300); // simulated later capture rejection
 }
 assert(owner.freeReplays.size()==1&&owner.pooledSnapshotBytes==5296);
 assert(shader.refs==1&&shader.releases==10000&&texture.refs==1&&device.vb[0].refs==1&&device.ib.refs==1&&device.decl.refs==1);
 {std::unique_ptr<WorldRenderer::Replay,WorldRenderer::ReplayRecycle> p(owner.acquireReplay().release(),{&owner});p->shader=&shader;shader.AddRef();owner.replays.emplace_back(p.release());}
 assert(owner.freeReplays.empty()&&owner.pooledSnapshotBytes==0&&owner.replays.size()==1&&shader.refs==2);
 owner.replays.clear();assert(shader.refs==1); // accepted record transfers ownership exactly once.
 {std::unique_ptr<WorldRenderer::Replay,WorldRenderer::ReplayRecycle> p(owner.acquireReplay().release(),{&owner});p->snapshot.streams[0].bytes.resize(49u*1024u*1024u);}
 assert(owner.pooledSnapshotBytes==0&&owner.freeReplays.size()==1&&owner.freeReplays[0]->snapshot.capacityBytes()==0);
 owner.freeReplays.clear();owner.pooledSnapshotBytes=0;owner.poolLimit=3*sizeof(WorldRenderer::Replay);
 for(unsigned i=0;i<12;++i)owner.recycleReplay(new WorldRenderer::Replay);
 assert(owner.freeReplays.size()==3&&owner.pooledSnapshotBytes==0);
 owner.poolLimit=sizeof(WorldRenderer::Replay)-1;owner.recycleReplay(new WorldRenderer::Replay);
 assert(owner.freeReplays.size()==3); // A lowered cap cannot grow an already oversized pool.
 owner.freeReplays.clear();owner.poolLimit=sizeof(WorldRenderer::Replay)+4096;
 {auto p=owner.acquireReplay();p->snapshot.streams[0].bytes.reserve(8192);owner.recycleReplay(p.release());}
 assert(owner.freeReplays.size()==1&&owner.pooledSnapshotBytes==0);
 std::puts("Production replay pool: 10000 rejected draws, COM refs balanced, ownership transfer, record+snapshot cap, lowered cap and empty-record bound passed");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-model-pool-')as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(text)
 for flags in (['-O2'],) if args.zig else (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']):
  compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if args.zig else ['clang++']
  exe=p/('test.exe' if args.zig else 'test')
  subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror',*flags,'-UNDEBUG','-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(exe)],check=True,env=fp.zig_env() if args.zig else None)
  subprocess.run([str(exe)],check=True)
