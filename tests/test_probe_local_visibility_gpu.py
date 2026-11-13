# northlight-test: requires=zig,windows-gpu,client timing
"""FP16 visibility parity on D3D9/DXVK, plus optional isolated shader timings."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--output',type=Path,default=fp.output_dir()/'probe-local-visibility-gpu')
parser.add_argument('--timing',action='store_true')
parser.add_argument('--backend',type=Path,action='append')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
inputs=[Path(__file__),fp.TESTS/'probe_local_visibility_gpu.cpp',fp.TESTS/'probe_local_visibility_gpu.hlsl',
        fp.src('probe_local_visibility.h'),fp.src('world_gi.cpp'),
        fp.SHADERS/'world_effects.hlsl',fp.SHADERS/'auto_bump_normal.hlsl',fp.COMPILED/'WorldGI.bin']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
hashes={str(p):sha(p) for p in inputs}
compiler=Compiler(allow_compatible_binary=True)
# Exercise the actual production helper and bytecode. The legacy control uses
# LocalVisibilityInfo.x=0; the runtime always binds its resident local atlas.
world=(fp.SHADERS/'world_effects.hlsl').read_text().replace('"auto_bump_normal.hlsl"','"'+(fp.SHADERS/'auto_bump_normal.hlsl').as_posix()+'"')
query=(fp.TESTS/'probe_local_visibility_gpu.hlsl').read_text()
stats={}
for name,entry,body in [('VisibilityQuery','VisibilityQuery',world+'\n'+query),
    ('VisibilityFour','VisibilityQuery','#define LOCAL_VISIBILITY_FOUR_READS\n'+world+'\n'+query),
    ('WorldGILocalFour','WorldGI','#define LOCAL_VISIBILITY_FOUR_READS\n'+world)]:
    source=out/(name+'.hlsl');source.write_text(body)
    compiler.compile(source,entry,'ps_3_0',out/(name+'.bin'))
    stats[name]=validate_assembly(out/(name+'.bin.asm'),'ps_3_0')
import shutil
shutil.copyfile(fp.COMPILED/'WorldGI.bin',out/'WorldGILocal.bin')
stats['WorldGILocal']=validate_assembly(fp.COMPILED/'WorldGI.bin.asm','ps_3_0')
exe=out/'visibility-gpu.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
    *fp.test_include_flags(),str(fp.TESTS/'probe_local_visibility_gpu.cpp'),str(fp.src('world_gi.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
backends=args.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
    fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
runs=[]
for index,backend in enumerate(backends):
    for timing in ([False,True] if args.timing else [False]):
        run=out/f'{index}-{int(timing)}';run.mkdir(exist_ok=True)
        result=subprocess.run([str(exe),str(backend.resolve()),str(out),str(fp.COMPILED),str(int(timing))],cwd=run,
            env={**os.environ,'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'},capture_output=True,text=True,timeout=120)
        (run/'run.log').write_text(result.stdout+result.stderr);print(result.stdout[-1200:],flush=True)
        result.check_returncode();assert 'VISIBILITY_GPU_PASS' in result.stdout
        record={'backend':str(backend),'sha256':sha(backend),'timing':timing}
        if timing:
            samples=[(int(p),int(f),float(ms)) for p,f,ms in re.findall(r'VISIBILITY_TIMING pass=(\d+) four=(\d+).*?gpuMs=([\d.]+)',result.stdout)]
            record['median_ms_after_warmup']={name:statistics.median(ms for p,f,ms in samples if p>=8 and f==mode) for mode,name in [(0,'uniform_fast_path'),(1,'four_reads')]}
            full=[(int(p),int(mode),float(ms)) for p,mode,ms in re.findall(r'FULL_GI_TIMING pass=(\d+) mode=(\d+) gpuMs=([\d.]+)',result.stdout)]
            record['full_gi_median_ms_after_warmup']={name:statistics.median(ms for p,mode,ms in full if p>=8 and mode==m) for m,name in enumerate(('legacy','local_fast','local_four'))}
        runs.append(record)
assert all(sha(Path(p))==value for p,value in hashes.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'scope':'Prototype visibility and full GI pass on synthetic textures, not game FPS',
    'inputs':hashes,'compiler':compiler.provenance,'shaders':stats,'runs':runs},indent=2)+'\n')
