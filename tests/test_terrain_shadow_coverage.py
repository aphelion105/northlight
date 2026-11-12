# northlight-test: requires=cxx
"""Execute the generated terrain handoff arithmetic; never starts a device/game."""
from pathlib import Path
import argparse,os, subprocess, sys, tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--source-ref',help='Use a prior renderer.cpp to verify the regression counterfactual');args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='northlight-terrain-coverage-') as temporary:
    compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['c++']
    exe=Path(temporary)/'test-coverage'
    subprocess.run([*compiler,'-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    source=subprocess.check_output(['git','show',args.source_ref+':src/proxy/renderer.cpp'],cwd=fp.REPO).decode() if args.source_ref else fp.src('renderer.cpp').read_text(encoding='utf-8')
    start=source.index('template<class Draw> HRESULT terrainShadowDraw(')
    end=source.index('{',start);depth=1;end+=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    (Path(temporary)/'terrain_shadow_draw.inc').write_text(source[start:end],encoding='utf-8')
    start=source.index('void planTerrainShadowSwap(')
    end=source.index('{',start);depth=1;end+=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    (Path(temporary)/'terrain_shadow_plan.inc').write_text(source[start:end],encoding='utf-8')
    start=source.index('if(applied){',source.index('template<class Capture> void prepareDrawImpl('));end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    (Path(temporary)/'terrain_shadow_late.inc').write_text(source[start:end],encoding='utf-8')
    state=Path(temporary)/'test-state'
    for flags in ([['-O2']] if os.name=='nt' else [['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']]):
        subprocess.run([*compiler,'-std=c++17',*flags,'-UNDEBUG','-I',temporary,*fp.test_include_flags(),str(Path(__file__).with_name('test_terrain_shadow_state.cpp')),'-o',str(state)],check=True)
        subprocess.run([str(state)],check=True)
