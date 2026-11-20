#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""Native fog math/bytecode validation; no Wine, graphics device or game.

Builds the C++ test in a temporary directory, checks every original PS3 Terrain
permutation in enabled existing archives, and writes legacy-fog-validation.json.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
from scripts.shaders.shader_sources import world_source_bytes
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
WORK=HERE.parent
CLIENT=fp.client_root()
sys.path.insert(0,str(WORK))
from mpq import Archive
from extract_signatures import variants


def main():
    inventory=json.loads((WORK/'inspection/inventory.json').read_text())
    codes={}
    sources=[]
    for path,record in sorted(inventory.items()):
        if not record['enabled'] or not (CLIENT/path).exists():continue
        listing=WORK/'inspection'/(Path(path).name+'.list')
        if not listing.exists():continue
        names=[name for name in listing.read_text().splitlines()
               if '\\pixel\\ps_3_0\\terrain' in name.lower() and name.lower().endswith('.bls')]
        if not names:continue
        with Archive(CLIENT/path) as archive:
            for name in names:
                data=archive.read(name)
                if not data:continue
                for index,flags,code in variants(data):
                    digest=hashlib.sha256(code).hexdigest()
                    codes[digest]=code
                    sources.append({'archive':path,'path':name,'variant':index,'sha256':digest})
    accepted={}
    with tempfile.TemporaryDirectory(prefix='northlight-fog-validation-') as temporary:
        folder=Path(temporary);binary=folder/'test-legacy-fog'
        subprocess.run(['clang++','-std=c++17','-O2',*fp.test_include_flags(),str(HERE/'test_legacy_fog.cpp'),'-o',str(binary)],check=True)
        tests=subprocess.check_output([str(binary)],text=True).strip()
        paths=[]
        for digest,code in codes.items():
            p=folder/(digest+'.bin');p.write_bytes(code);paths.append(p)
        for start in range(0,len(paths),100):
            output=subprocess.check_output([str(binary),*map(str,paths[start:start+100])],text=True)
            for line in output.splitlines():
                if line.startswith('PS3_FOG_REGISTER '):
                    _,register,path=line.split(' ',2);accepted[Path(path).stem]=int(register)
    summary={}
    for source in sources:
        key=source['archive']+':'+source['path']
        row=summary.setdefault(key,{'recognized':0,'rejected':0})
        row['recognized' if accepted[source['sha256']]>=0 else 'rejected']+=1
    report={'result':'pass','game_launched':False,'gpu_test':False,'native_tests':tests,
            'helper_sha256':hashlib.sha256(fp.src('legacy_fog.h').read_bytes()).hexdigest(),
            'shader_sha256':hashlib.sha256(world_source_bytes(fp.src('world_effects.hlsl'))).hexdigest(),
            'unique_original_shaders':len(codes),'source_variants':len(sources),
            'recognized_fog_shaders':sum(v>=0 for v in accepted.values()),
            'fog_constant_register_counts':{str(r):sum(v==r for v in accepted.values()) for r in sorted(set(accepted.values()))},
            'rejected_other_shaders':sum(v<0 for v in accepted.values()),
            'sources':summary,
            'mapping_policy':'Exact input-fog / subtract-constant / multiply-add-constant epilogue proof at runtime; other shader mappings disable correction.',
            'limitations':['Fog curve from reconstructed depth approximates interpolated per-vertex fog.',
                           'Terrain fog parameters are a scene-level approximation for other material types.',
                           'Surface darkening bound precedes additional physical volume extinction.',
                           'CPU checks do not prove runtime capture, visual appearance or performance.']}
    output=fp.output_dir()/'legacy-fog-validation.json';output.write_text(json.dumps(report,indent=2)+'\n')
    print(tests)
    print(f'{len(codes)} original unique shaders: {sum(v>=0 for v in accepted.values())} recognized fog mappings; {output}')


if __name__=='__main__':main()
