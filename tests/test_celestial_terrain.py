#!/usr/bin/env python3
# northlight-test: requires=zig,world-cache
"""Celestial terrain mask (celestial_terrain.h): the mask matrix and frustum (rays, far mountains, near
plane, invalid input), and the 0.3.175 merged mask draws: contiguous same-page runs against per-batch
submission on real world-cache terrain pages (Stormwind, Elwynn, Stranglethorn, Tanaris) and random mask
frusta, with the gap and page-crossing counterfactuals, plus the renderer wiring (the per-generation
terrain list rebuilt at the mesh commit only, submitted ranges and counters).
Runs with Zig on Windows and clang++ on other hosts. No game or GPU."""

from source_units import source_text
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=source_text(fp.src('world_renderer.h'));sky=fp.src('celestial_disc_renderer.h').read_text();r=fp.src('renderer.cpp').read_text()
terrain_pass=fp.src('celestial_terrain_pass.h').read_text()
draw=w[w.index('    bool drawCelestialTerrain(unsigned body,const float* matrix){'):w.index('    void noteCelestialTerrainReuse(')]
clear_mesh=w.split('    void clearMesh(',1)[1].split('\n',1)[0]
checks={
 'terrain list rebuilt only at the mesh commit and cleared with the mesh':w.count('rebuildTerrainLists();')==1 and '++meshGeneration;rebuildTerrainLists();' in w
    and all(field+'.clear();' in clear_mesh for field in ('batches','terrainBatchList','shadowTerrainList','pointStaticList'))
    and 'terrainBatchListGeneration=UINT64_MAX;' in clear_mesh and 'rebuildTerrainLists' not in sky and 'rebuildTerrainLists' not in draw,
 'merged runs only with this generation\'s list, per-batch fallback otherwise':'const bool listed=terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size();' in draw
    and 'NorthlightCelestialTerrain::submitMask(d,batches,terrainBatchList,listed,NorthlightWorldMeshPages::PageIndexLimit,matrix,' in draw
    and 'DrawIndexedPrimitive' not in draw and 'device->DrawIndexedPrimitive(' in terrain_pass,
 'counters on the rate-limited CELESTIAL line; the clock only with RenderProfile':'candidates=%zu accepted=%u runs=%u listed=%d redraws=%u,%u reuses=%u,%u ms=%.3f peakMs=%.3f' in draw
    and 'const int64_t started=NorthlightRenderThreadProbe::profiling()?QpcClock::now():0;' in draw and 'if(NorthlightDiagnostics::enabled()&&(!m.lastLog||now-m.lastLog>=10000))' in draw,
 'per-body redraw and reuse callbacks':'!terrainDraw(body,matrix))return false;' in sky and 'if(terrainReuse)terrainReuse(body);' in sky
    and '[this](unsigned body,const float* matrix){return world->drawCelestialTerrain(body,matrix);},[this](unsigned body){world->noteCelestialTerrainReuse(body);}' in r,
 'reuse needs an eye within the parallax limit of the DRAWN cut and every probed ray within ReuseTexels of the DRAWN mask':'NorthlightCelestialTerrain::eyeReusable(terrainCamera[body],view+12,terrainNear[body],nearCut)' in sky
    and 'const float nearCut=NorthlightCelestialTerrain::maskNear(maskFarClip);' in sky and 'NorthlightCelestialTerrain::matrix(disc,view+12,body?MoonMaskRadius:SunMaskRadius,size,matrix,nearCut);' in sky
    and sky.count('maskFarClip=farZ;')==1 and 'maskFarClip=0;' in sky
    and 'NorthlightCelestialTerrain::rayShiftTexels(terrainMatrix[body],matrix,disc,body?MoonMaskRadius:SunMaskRadius,size)<=NorthlightCelestialTerrain::ReuseTexels' in sky and 'constexpr float ReuseTexels=' in fp.src('celestial_terrain.h').read_text(),
 'stored matrix and eye are written only after a successful draw, and reset with the generation':sky.index('terrainDraw(body,matrix))return false;')<sky.index('std::memcpy(terrainMatrix[body],matrix,sizeof matrix);std::memcpy(terrainCamera[body],view+12,sizeof terrainCamera[body]);terrainNear[body]=nearCut;terrainGeneration[body]=generation;')
    and sky.count('std::memcpy(terrainCamera[body]')==1 and 'terrainGeneration[i]=0;std::fill(terrainCamera[i],terrainCamera[i]+3,0.f);terrainNear[i]=0;}' in sky,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
windows=sys.platform=='win32'
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if windows else ['clang++']
sanitizers='undefined' if windows else 'address,undefined'
with tempfile.TemporaryDirectory(prefix='northlight-celestial-terrain-',dir=fp.output_dir()) as tmp:
    for label,flags in [('O2',['-O2']),('sanitizer',['-O1','-g',f'-fsanitize={sanitizers}','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label+('.exe' if windows else ''))
        subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),
                        str(HERE/'test_celestial_terrain.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),
                        '-o',str(exe)],env=fp.zig_env() if windows else None,check=True)
        print(f'[{label}]',flush=True);subprocess.run([str(exe),str(fp.world_cache())],check=True)
print('PASS celestial terrain: frustum, merged mask runs and wiring')
