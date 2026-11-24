#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Live terrain upload (0.3.176 U1a/U1b/U1c): native fake-D3D test of live_terrain_gpu.h (the arena,
retirement and rollover cases, plus prepare()/write() against the 0.3.175 indices() concatenation on
twin caches), the fixed-chunk bitmap beside the set (test_fixed_chunk_bits.cpp), each plain and
ASan/UBSan, and a wiring audit of uploadLiveTerrain (counts instead of index vectors, the same resets
on the same paths, the bitmap filled at the mesh commit and dropped with the set). No game or GPU."""

from source_units import source_text
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=source_text(fp.src('world_renderer.h'));pr=fp.src('world_point_rendering.inl').read_text()
indices=fp.src('live_terrain_indices.h').read_text()
flash=fp.src('world_flash_rendering.inl').read_text()
dgpu=fp.src('directional_gpu_pass.h').read_text()
pgpu=fp.src('point_shadow_face_pass.h').read_text()
point_live=pgpu[pgpu.index('bool drawLiveTerrain('):pgpu.index('bool resolve(')]
live_gpu=dgpu[dgpu.index('    bool liveTerrain('):dgpu.index('    bool beginNearReplay(')]
up=w[w.index('    bool uploadLiveTerrain(){'):w.index('    // Win32 wide-path read')]
reset='liveTerrainIndexCount=liveDirectionalIndexCount=0;liveTerrainIndices.clearBase();liveTerrainChunks.clear();uploadedTerrain.clear();return true;'
release=w[w.index('    void releaseGPU(bool keepMesh=false){'):]
release=release[:release.index('\n    }\n')]
programs=fp.src('world_shadow_programs.h').read_text()
geometry=fp.src('world_geometry_resources.inl').read_text()
assert w.count('void endFrame(')==1
frame_start=w.index('void endFrame(')
frame_prefix=w[frame_start:w.index('frameFence.endFrame();',frame_start)]
pivot_reset=w[w.index('    void resetShadowPivot(){'):w.index('    V shadowPivot(){')]
replay_release=fp.src('world_replay_upload.inl').read_text().split('    bool uploadReplay(',1)[0]
replay_buffers=fp.src('world_replay_buffers.h').read_text()
checks={
 'live terrain uses lossless20B caster vertices and a dedicated declaration in both consumers':
    'NorthlightLiveTerrainGPU::Cache<NorthlightTerrainCapture::MeshSnapshot,StaticShadow::Vertex>& liveTerrainGPU=gpuScene->terrain;' in w
    and 'NorthlightLiveTerrainGPU::Cache<NorthlightTerrainCapture::MeshSnapshot,StaticShadow::Vertex> terrain;' in fp.src('gpu_scene.h').read_text()
    and 'return StaticShadow::Vertex{p.x,p.y,p.z,0,0};' in up
    and '#include "world_screen_resources.inl"' in w
    and 'CreateVertexDeclaration(elements,&liveLayout_)' in programs
    and 'shadowPrograms.createLiveLayout(d)' in fp.src('world_screen_resources.inl').read_text()
    and 'drop(liveLayout_);' in programs and 'shadowPrograms.releaseCasters();' in release
    and w.count('gpuPass.liveTerrain({shadowPrograms.liveLayout(),liveTerrainGPU.vertices(),liveTerrainIndices.buffer(),sizeof(StaticShadow::Vertex)}')==1
    and pr.count('facePass.drawLiveTerrain({shadowPrograms.liveLayout(),liveTerrainGPU.vertices(),liveTerrainIndices.buffer(),')==1
    and 'sizeof(StaticShadow::Vertex),liveTerrainGPU.vertexCapacity(),liveTerrainIndices.base()' in pr
    and 'SetVertexDeclaration(geometry.declaration)' in live_gpu and 'SetVertexDeclaration(declaration)' in pgpu
    and '"restore terrain declaration"' in live_gpu and '"cube restore terrain declaration"' in pgpu
    and 'return restoreStaticDeclaration(staticDeclaration);' in point_live
    and 'frameTerrainVertices+snapshot->positions.size()>262144' in w,
 'no index vectors left: counts only':'liveTerrainIndices[' not in w+pr and 'liveDirectionalIndices' not in w+pr
    and 'size_t liveTerrainIndexCount=0,liveDirectionalIndexCount=0;' in w,
 'S_FALSE and index-growth refusal keep the 0.3.175 reset':up.count(reset)==2
    and 'if(terrainResult==S_FALSE){' in up and 'if(uploaded==NorthlightLiveTerrainIndices::Upload::Deferred){\n            '+reset in up
    and 'if(!admit(capacity))return Upload::Deferred;' in indices,
 'totals before the Lock, the same ring Lock size, lists written inside it, Unlock right after':
    'const UINT ib=UINT((liveTerrainIndexCount+liveDirectionalIndexCount)*sizeof(uint32_t));' in up
    and 'return admitsGrowth("live-terrain-index-growth",capacity);' in up
    and 'liveTerrainGPU.write(data);' in up and up.index('liveTerrainGPU.prepare(')<up.index('liveTerrainIndices.upload(d,ib,fence(),')
    and indices.index('buffer_->Lock(slot.offset,bytes,&data,slot.flags)')<indices.index('write(static_cast<std::uint32_t*>(data));')<indices.index('buffer_->Unlock()'),
 'membership failure and the empty case as before':'},liveTerrainIndexCount,liveDirectionalIndexCount,[&](const auto& snapshot){' in up
    and '}))return check(E_FAIL,"live terrain arena membership");' in up
    and 'if(!liveTerrainIndexCount){uploadedTerrain=frameTerrain;liveTerrainGeneration=meshGeneration;return true;}' in up,
 'readers use the counts and the point face candidates':
    'const bool drawLiveTerrain=liveTerrainIndexCount&&!pointLiveCandidates[face].empty();' in pr
    and 'if(drawLiveTerrain){' in pr and 'if(liveDirectionalIndexCount){' in w
    and 'UINT(liveTerrainIndices.base()+liveTerrainIndexCount),UINT(liveDirectionalIndexCount/3)' in w,
 'the same ring slice supplies directional, flash and point ranges':
    'SetIndices(geometry.indices)' in live_gpu and 'SetIndices(indices),"cube live indices"' in pgpu
    and 'DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,vertices,start,count)' in live_gpu
    and 'UINT(liveTerrainIndices.base()+liveTerrainIndexCount),UINT(liveDirectionalIndexCount/3)' in w
    and 'UINT(liveTerrainIndices.base()),UINT((liveTerrainIndexCount+liveDirectionalIndexCount)/3)' in flash
    and 'UINT(geometry.indexBase+batch.start),batch.count),"cube live draw")' in point_live,
 'the ring owner publishes the base after Unlock and clears it on release':
    indices.index('buffer_->Unlock()')<indices.index('base_=slot.offset/sizeof(std::uint32_t);')
    and indices.count('base_=slot.offset')==1 and 'void clearBase(){base_=0;}' in indices
    and 'releaseLiveTerrainBuffers();' in release and 'liveTerrainIndices.release(fence());' in geometry,
 'one EVENT after the frame and a touch before each read of the live slice':
    w.count('NorthlightDynamicRing::FrameFence frameFence;')==1 and w.count('DeviceQueries')==0
    and 'flushDeferredLogs();' in frame_prefix and 'return' not in frame_prefix
    and 'replayBuffers.release(fence());' in replay_release
    and replay_buffers.index('NorthlightDynamicRing::reset(indexRing_,0,queries);')<replay_buffers.index('queries.drop();')<replay_buffers.index('for(auto& buffer:vertices_)drop(buffer);')
    and 'if(reuse){terrainUploadReused=true;if(liveTerrainIndexCount||liveDirectionalIndexCount)liveTerrainIndices.touch(fence());return true;}' in up
    and w.count('liveTerrainIndices.touch(fence());')==2 and pr.count('liveTerrainIndices.touch(fence());')==1
    and '[&]{liveTerrainIndices.touch(fence());}' in flash
    and live_gpu.index('SetIndices(geometry.indices)')<live_gpu.index('touch();')<live_gpu.index('DrawIndexedPrimitive(')
    and pr.index('facePass.drawLiveTerrain(')<pr.index('liveTerrainIndices.touch(fence());')
    and point_live.index('bindLiveTerrain(')<point_live.index('touch();')<point_live.index('"cube live draw"')
    and 'cascadeAnchor.reset();' in pivot_reset and 'resetShadowPivot();endFrame();' in w,
 'bitmap filled from the committed set at the swap, dropped with the set':'std::swap(fixedTerrain,committedFixed);std::swap(uploadedStaticOwners,committedOwners);fixedTerrainBits.assign(fixedTerrain.get());' in w
    and 'fixedTerrain.reset();fixedTerrainBits.reset();' in release and w.count('fixedTerrainBits.assign(')==1,
 'bitmap only for the set it describes, the set otherwise':'const auto& fixed=fixedTerrainChunks();const bool bits=fixedTerrainBits.source()==&fixed;' in up
    and 'return bits?fixedTerrainBits.contains(c):fixed.count(c)!=0;' in up,
 'fixedTerrain stays the shared immutable set':'std::shared_ptr<const std::set<std::pair<int,int>>> fixedTerrain;' in w,
 'directional aliases require exact nonempty known chunk metadata and retain generation invalidation':
    'liveTerrainGPU.prepare(frameTerrain,meshGeneration,' in up
    and '!snapshot.bounds.chunks.empty()&&std::none_of(snapshot.bounds.chunks.begin(),snapshot.bounds.chunks.end(),' in up
    and 'c.x<0||c.y<0||c.x>=1024||c.y>=1024||containsFixed({c.x,c.y})' in up
    and 'appendLiveDirectionalBy(snapshot,containsFixed,' in up,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-live-terrain-') as tmp:
    for source in ['test_live_terrain_gpu.cpp','test_live_terrain_layout.cpp','test_live_terrain_directional_alias.cpp','test_fixed_chunk_bits.cpp']:
        for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
            exe=Path(tmp)/(Path(source).stem+'-'+label)
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/source),'-o',str(exe)],check=True)
            print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS live terrain upload: cache, prepare/write, bitmap and wiring')
