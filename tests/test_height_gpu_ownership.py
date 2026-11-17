# northlight-test: requires=windows-exe,zig
"""GPU-owned height fields: exact pixels, bounded CPU fallback and upload failure ownership."""
import argparse,hashlib,json,re,statistics,subprocess,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--backend',type=Path,action='append',default=[]);p.add_argument('--benchmark',action='store_true');args=p.parse_args()
out=fp.output_dir()/'height-gpu-ownership';out.mkdir(parents=True,exist_ok=True)
prefix=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('struct DisplacementStateTestAccess {')[0];(out/'height_ownership_fixture.h').write_text(prefix)
watched=[*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),Path(__file__),fp.TESTS/'height_gpu_ownership.cpp',fp.TESTS/'displacement_subdivision.cpp',fp.TESTS/'displacement_state_reuse.cpp']
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest();inputs={str(path):sha(path) for path in watched};runs=[]
for mode in ['O2','O0']:
    exe=out/('height-'+mode+'.exe');command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'height_gpu_ownership.cpp'),'-luser32','-luuid','-o',str(exe)]
    with (out/(mode+'-build.log')).open('w') as log:subprocess.run(command,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=120);(out/(mode+'.log')).write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True);result.check_returncode();assert 'HEIGHT_OWNERSHIP_PASS' in result.stdout;runs.append({'mode':mode,'output':result.stdout})
    if mode=='O2':optimized=exe
rows=[];summary={};compiler_provenance=None
if args.benchmark:
    result=subprocess.run([str(optimized),'--benchmark'],text=True,capture_output=True,timeout=180);(out/'benchmark.log').write_text(result.stdout+result.stderr);result.check_returncode()
    rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in result.stdout.splitlines() if line.startswith('HEIGHT_BUILD_TIMING')]
    assert len(rows)==16 and len({r['digest'] for r in rows})==1
    summary={mode:statistics.median(float(r['cpuMs']) for r in rows if r['mode']==mode and int(r['pair'])>0) for mode in ['retained','gpu-only']};summary['speedup']=summary['retained']/summary['gpu-only'];print(json.dumps(summary),flush=True)
if args.backend:
    from windows_gpu_guard import processes
    assert not [p for p in processes() if p['name'].lower().startswith('wow')],'Close the game before the GPU oracle'
    sys.path.insert(0,str(fp.SCRIPTS/'shaders'));from shader_compiler import Compiler
    shader=out/'copy.hlsl';shader.write_text('sampler2D S:register(s0);float4 Copy(float2 uv:TEXCOORD0):COLOR0{return tex2D(S,uv).rrrr;}')
    compiler=Compiler(allow_compatible_binary=True);compiler.compile(shader,'Copy','ps_3_0',out/'copy.bin');compiler_provenance=compiler.provenance
    for i,backend in enumerate(args.backend):
        result=subprocess.run([str(optimized),str(backend),str(out/'copy.bin')],cwd=out,env=fp.zig_env(),text=True,capture_output=True,timeout=180);(out/('gpu-'+str(i)+'.log')).write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True);result.check_returncode();assert 'HEIGHT_GPU_PIXELS_PASS' in result.stdout;runs.append({'backend':str(backend),'sha256':sha(backend),'output':result.stdout})
assert all(sha(path)==value for path,value in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'rows':rows,'summary':summary,'fixture_compiler':compiler_provenance,'game_launched':False,'scope':'Exact height upload, GPU samples, CPU fallback and reset. Optional timing is cold skinned geometry construction with an already uploaded shared height field; excludes its one-time generation/upload. No game FPS or driver resident-memory claim.'},indent=2)+'\n')
