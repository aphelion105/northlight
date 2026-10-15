#!/usr/bin/env python3
# northlight-test: requires=cxx,world-cache
"""Celestial terrain mask (celestial_terrain.h): the mask matrix and frustum (rays, far mountains, near
plane, invalid input), and the 0.3.175 merged mask draws: contiguous same-page runs against per-batch
submission on real world-cache terrain pages (Stormwind, Elwynn, Stranglethorn, Tanaris) and random mask
frusta, with the gap and page-crossing counterfactuals, plus the renderer wiring (the per-generation
terrain list rebuilt at the mesh commit only, merged DIPs, the counters). Native clang++, plain and
ASan/UBSan. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();sky=fp.src('celestial_disc_renderer.h').read_text();r=fp.src('renderer.cpp').read_text()
draw=w[w.index('    bool drawCelestialTerrain(unsigned body,const float* matrix){'):w.index('    void noteCelestialTerrainReuse(')]
checks={
 'terrain list rebuilt only at the mesh commit and cleared with the mesh':w.count('rebuildTerrainLists();')==1 and '++meshGeneration;rebuildTerrainLists();' in w
    and 'batches.clear();terrainBatchList.clear();shadowTerrainList.clear();terrainBatchListGeneration=UINT64_MAX;' in w and 'rebuildTerrainLists' not in sky and 'rebuildTerrainLists' not in draw,
 'merged runs only with this generation\'s list, per-batch fallback otherwise':'const bool listed=terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size();' in draw
    and 'NorthlightCelestialTerrain::forEachRun(batches,terrainBatchList,NorthlightWorldMeshPages::PageIndexLimit,accept,emit,runs);' in draw
    and 'DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,r.minVertex,UINT(r.vertexEnd-r.minVertex),r.start,r.count)' in draw,
 'counters on the rate-limited CELESTIAL line; the clock only with RenderProfile':'candidates=%zu accepted=%u runs=%u listed=%d redraws=%u,%u reuses=%u,%u ms=%.3f peakMs=%.3f' in draw
    and 'const int64_t started=NorthlightRenderThreadProbe::profiling()?QpcClock::now():0;' in draw and 'if(NorthlightDiagnostics::enabled()&&(!m.lastLog||now-m.lastLog>=10000))' in draw,
 'per-body redraw and reuse callbacks':'!terrainDraw(body,matrix))return false;' in sky and 'if(terrainReuse)terrainReuse(body);' in sky
    and '[this](unsigned body,const float* matrix){return world->drawCelestialTerrain(body,matrix);},[this](unsigned body){world->noteCelestialTerrainReuse(body);}' in r,
 'reuse needs a bitwise identical eye and every probed ray within ReuseTexels of the DRAWN mask':'!std::memcmp(view+12,terrainCamera[body],sizeof terrainCamera[body])' in sky
    and 'NorthlightCelestialTerrain::rayShiftTexels(terrainMatrix[body],matrix,disc,body?MoonMaskRadius:SunMaskRadius,size)<=NorthlightCelestialTerrain::ReuseTexels' in sky and 'constexpr float ReuseTexels=' in fp.src('celestial_terrain.h').read_text(),
 'stored matrix and eye are written only after a successful draw, and reset with the generation':sky.index('terrainDraw(body,matrix))return false;')<sky.index('std::memcpy(terrainMatrix[body],matrix,sizeof matrix);std::memcpy(terrainCamera[body],view+12,sizeof terrainCamera[body]);terrainGeneration[body]=generation;')
    and sky.count('std::memcpy(terrainCamera[body]')==1 and 'terrainGeneration[i]=0;std::fill(terrainCamera[i]' in sky,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-celestial-terrain-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_celestial_terrain.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)],check=True)
        print(f'[{label}]',flush=True);subprocess.run([str(exe),str(fp.world_cache())],check=True)
print('PASS celestial terrain: frustum, merged mask runs and wiring')
