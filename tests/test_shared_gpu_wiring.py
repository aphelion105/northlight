#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile the actual world bind lambda; verify bidirectional device wiring and material offsets."""
import subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
import replay_gpu_fixture
out=fp.output_dir()/'shared-gpu-wiring';out.mkdir(parents=True,exist_ok=True)
stub=replay_gpu_fixture.stub;a=stub.index('using UINT');b=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:a]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[b:]
(out/'d3d9.h').write_text(stub);(out/'replay_fixture.h').write_text(replay_gpu_fixture.fixture)
world=fp.src('world_renderer.h').read_text();bump=fp.src('auto_bump_renderer.h').read_text();device=fp.src('renderer.cpp').read_text()
first=world.index('        auto bindReplay=[&](Replay& p){');last=world.index('        replayTimeDeferredIndices.clear();',first)
binding=world[first:last]
assert 'world->setSharedGeometry(autoBump->residentGeometry());autoBump->setSharedGeometry(world->residentGeometry());' in device
assert 'if(world)world->setSharedGeometry({});if(autoBump)autoBump->setSharedGeometry({});' in device
assert 'sharedGeometry.borrow(packet->mesh,bindings.vertex,bindings.index,placement)' in bump
assert 'DrawIndexedPrimitive(mesh.topology,INT(placement.vertexBase),0,mesh.vertexCount,placement.indexStart,mesh.primitiveCount)' in bump
assert 'DrawPrimitive(mesh.topology,placement.vertexBase,mesh.primitiveCount)' in bump
assert 'deferredGPU.setLimit(32u<<20);' in bump and 'ownedLimitMiB=%zu' in bump
source='''#include "replay_fixture.h"
#include "replay_gpu_batches.h"
using Mesh=NorthlightDrawSnapshot::Mesh;
struct Replay {std::shared_ptr<const Mesh> shared;bool gpuCached=false,indexed=true;IDirect3DVertexBuffer9* stream[4]={};IDirect3DIndexBuffer9* index=nullptr;UINT offset[4]={},stride[4]={},start=0;INT base=0;
 const Mesh& mesh()const{return *shared;}~Replay(){for(auto p:stream)if(p)p->Release();if(index)index->Release();}};
int main(){
 Device driver;auto* d=&driver;NorthlightReplayGPU::SelectedCache replayGpuCache;NorthlightReplayGPU::Cache materials;
 auto sharedReplayGeometry=NorthlightReplayGPU::residentSource(materials);auto growthAdmission=[](size_t){return true;};bool allowCache=true;
 unsigned sharedCandidates=0,sharedDraws=0;size_t sharedBytes=0;
'''+binding+'''
 auto m=mesh();m->sharedCapture=true;{Replay p;p.shared=m;materials.beginFrame();assert(!materials.bind(d,m,p.stream,p.index,growthAdmission));materials.beginFrame();assert(materials.bind(d,m,p.stream,p.index,growthAdmission));
 const auto calls=driver.calls;bindReplay(p);assert(p.gpuCached&&sharedDraws==1&&sharedBytes==m->byteSize()&&driver.calls==calls&&!replayGpuCache.population().entries);
 assert(!p.base&&!p.start&&!p.offset[0]&&p.stride[0]==m->streams[0].stride);
 allowCache=false;bindReplay(p);assert(!p.gpuCached&&sharedDraws==1&&driver.calls==calls);
 allowCache=true;m->sharedCapture=false;bindReplay(p);assert(!p.gpuCached&&sharedDraws==1&&replayGpuCache.population().probation==1);
 }
 materials.clear();replayGpuCache.clear();assert(!alive);puts("SHARED_GPU_WIRING_PASS actual_world_lambda no_growth_on_borrow admission_gate offsets lifetime device_links");
}
'''
(out/'wiring.cpp').write_text(source)
for flags in (['-O2'],['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']):
 exe=out/('sanitized.exe' if len(flags)>1 else 'optimized.exe')
 command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG',*flags,'-I',str(out),*fp.test_include_flags(),str(out/'wiring.cpp'),'-o',str(exe)]
 subprocess.run(command,env=fp.zig_env(),check=True);subprocess.run([str(exe)],check=True)
