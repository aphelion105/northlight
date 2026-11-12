#!/usr/bin/env python3
# northlight-test: requires=cxx,client manual
"""Conservative ModelNormals liveness: real programs, malformed input and OOM."""
import argparse,hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
programs=sorted(a.fixture.resolve().glob('model-vs3-*.bin'));assert len(programs)>=4
files=[fp.src(n) for n in ('model_normal_slice.h','model_normal_patch.h','water_shader_patch.h','patch_shadow_shader.h','shader_read_set.h')]+[Path(__file__),fp.TESTS/'model_normal_slice.cpp',*programs]
inputs={str(p):sha(p) for p in files};runs=[]
def execute(path):return ['wsl','--exec','/mnt/host/'+path.drive[0].lower()+'/'+path.relative_to(path.anchor).as_posix()] if os.name=='nt' else [str(path)]
def argument(path):return '/mnt/host/'+path.drive[0].lower()+'/'+path.relative_to(path.anchor).as_posix() if os.name=='nt' else str(path)
with tempfile.TemporaryDirectory(prefix='model-slice-',dir=fp.REPO/'out') as temp:
    root=Path(temp)
    for name,flags in [('O2',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        binary=root/name;compiler=[fp.zig(),'c++','-target','x86_64-linux-musl'] if os.name=='nt' else ['clang++']
        subprocess.run(list(map(str,[*compiler,'-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),fp.TESTS/'model_normal_slice.cpp','-o',binary])),check=True,env=fp.zig_env())
        result=subprocess.run([*execute(binary),*map(argument,programs)],capture_output=True,text=True);print(name,result.stdout+result.stderr,flush=True);result.check_returncode();runs.append(dict(name=name,stdout=result.stdout))
assert all(sha(Path(p))==h for p,h in inputs.items())
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs},indent=2)+'\n',encoding='utf-8',newline='\n')
