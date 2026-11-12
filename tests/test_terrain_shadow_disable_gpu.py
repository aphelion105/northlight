# northlight-test: requires=windows-gpu,zig,client manual
"""Validate all stock terrain shadow consumers and Terrain0 color preservation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
fp.use_source_modules()
from extract_signatures import variants,fnv1a64
from world_scene_builder import Assets
import windows_gpu_guard

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend',type=Path,action='append',required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def run(tag,command):
    log=out/(tag+'.log')
    with log.open('w') as f:result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
    text=log.read_text(errors='replace')
    if result.returncode:print(text[-2500:],flush=True)
    result.check_returncode();return text

helper=out/'terrain-check.exe';gpu=out/'terrain-gpu.exe'
compile=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags()]
run('compile-patch',[*compile,ROOT/'tests/test_patch_terrain_shadow.cpp','-o',helper]);run('patch-unit',[helper])
run('compile-gpu',[*compile,ROOT/'tests/terrain_shadow_disable_gpu.cpp','-luser32','-o',gpu])

programs={};vertex_count=0
header=fp.src('signatures.h').read_text().split('kTerrainVS[] = {',1)[1].split('};',1)[0]
known={int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',header)}
a=Assets(archives='stock')
try:
    for path in sorted(a.providers):
        if re.search(r'\\vertex\\vs_[23]_0\\terrain\.bls$',path):
            for _,_,code in variants(a.read(path)):
                assert fnv1a64(code) in known,(path,hashlib.sha256(code).hexdigest())
                vertex_count+=1
        if not re.search(r'\\pixel\\ps_[23]_0\\terrain[^\\]*\.bls$',path):continue
        for index,flags,code in variants(a.read(path)):
            digest=hashlib.sha256(code).hexdigest();p=out/(digest+'.bin');p.write_bytes(code)
            programs.setdefault(digest,[]).append({'path':path,'variant':index,'archive':a.origin(path)})
            if path.endswith('terrain0.bls'):
                (out/('terrain0-ps2.bin' if '\\ps_2_0\\' in path else 'terrain0-ps3.bin')).write_bytes(code)
finally:a.close()
paths=[out/(digest+'.bin') for digest in programs];lines=[]
for at in range(0,len(paths),60):lines.extend(run('stock-'+str(at),[helper,*paths[at:at+60]]).splitlines())
accepted=[];rejected=[]
for line in lines:
    parts=line.split(' ',5)
    if not parts or parts[0]!='TERRAIN_SHADOW':continue
    p=Path(parts[5]);digest=p.stem
    if int(parts[1])<0:
        assert all(s['path'].endswith('terrainsm.bls') for s in programs[digest]),programs[digest]
        # TerrainSM generates shadow distance; it never samples a terrain map.
        code=p.read_bytes()
        words=struct.unpack('<%dI'%(len(code)//4),code);at=1
        while words[at]!=65535:
            op=words[at]&65535;assert op not in {66,93,95}
            at+=1+(((words[at]>>16)&32767) if op==65534 else ((words[at]>>24)&15))
        rejected.append(digest)
    else:
        assert p.with_suffix('.bin.patched').stat().st_size==p.stat().st_size
        accepted.append(p.name)
assert len(programs)==394 and len(accepted)==392 and len(rejected)==2 and vertex_count>0
(out/'programs.txt').write_text('\n'.join(accepted)+'\n')
backends=[]
for i,path in enumerate(args.backend):
    windows_gpu_guard.processes();path=path.resolve();text=run('gpu-'+str(i),[gpu,path,str(out)+'/'])
    assert 'TERRAIN_SHADOW_DISABLE_GPU_PASS programs=392 cases=4' in text
    backends.append({'path':str(path),'sha256':sha(path),'log':'gpu-'+str(i)+'.log'})
report={'pass':True,'terrain_programs':len(programs),'shadow_consumers_patched':len(accepted),'shadow_generators_unchanged':len(rejected),'recognized_vertex_variants':vertex_count,'sources':programs,'backends':backends,'patch_sha256':sha(fp.src('patch_terrain_shadow.h')),'gpu_source_sha256':sha(ROOT/'tests/terrain_shadow_disable_gpu.cpp')}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('TERRAIN_SHADOW_ALL_CONSUMERS_PASS',len(accepted),'patched;',len(rejected),'non-sampling generators unchanged; backends',len(backends))
