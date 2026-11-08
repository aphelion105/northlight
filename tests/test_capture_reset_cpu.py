#!/usr/bin/env python3
# northlight-test: requires=zig manual
"""Immutable source retention across Reset without stale-byte/prediction reuse."""
import argparse,ast,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,default=fp.output_dir()/'capture-reset-cpu');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
def literal(path,name):return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==name for t in n.targets))
(out/'d3d9.h').write_text(literal(fp.TESTS/'test_terrain_snapshot.py','stub'));(out/'snapshot_harness.h').write_text(literal(fp.TESTS/'test_draw_snapshot.py','harness').split('int main()')[0])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files=[p for p in fp.SRC.rglob('*') if p.is_file()]+[fp.TESTS/'test_capture_reset_cpu.cpp',Path(__file__)];inputs={str(p):sha(p) for p in files};runs=[]
for mode,flags in [('O2',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),('hash4',['-O2','-DNORTHLIGHT_SNAPSHOT_HASH_BITS=4'])]:
 binary=out/mode;cmd=[fp.zig(),'c++','-target','x86_64-linux-musl','-std=c++17','-UNDEBUG',*flags,'-I',out,*fp.test_include_flags(),fp.TESTS/'test_capture_reset_cpu.cpp','-o',binary];subprocess.run(list(map(str,cmd)),env=fp.zig_env(),check=True)
 command=['wsl','--exec','/mnt/host/'+binary.drive[0].lower()+'/'+binary.relative_to(binary.anchor).as_posix()] if os.name=='nt' else [str(binary)]
 r=subprocess.run(command,capture_output=True,text=True);print(mode,r.stdout+r.stderr,flush=True);r.check_returncode();runs.append({'mode':mode,'stdout':r.stdout})
assert all(sha(Path(p))==h for p,h in inputs.items());(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs},indent=2)+'\n')
