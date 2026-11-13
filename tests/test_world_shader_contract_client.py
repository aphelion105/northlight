#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-exe
"""Active client materials, including modified WMO shaders absent from identities."""
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants,fnv1a64
from extract_wmo_context import lit_wmo

out=fp.output_dir();assets=Assets()
try:
    names=sorted(n for n in assets.providers if n.startswith('shaders\\vertex\\vs_3_0\\') and
                 (n.rsplit('\\',1)[-1].startswith('mapobj') or n.rsplit('\\',1)[-1] in ('cdiffuse_t1.bls','diffuse_t1.bls','shadowmap.bls')) and n.endswith('.bls'))
    assert names
    shaders={}
    for name in names:
        for _,_,code in variants(assets.read(name)):shaders.setdefault(code,name)
finally:assets.close()
source=out/'contract.cpp';exe=out/'contract.exe'
source.write_text(r'''
#include "shader_tags.h"
#include "equipment_light_proof.h"
#include "spell_fog_proof.h"
#include "actor_deformation.h"
#include <cstdio>
#include <vector>
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* f=std::fopen(argv[1],"rb");if(!f)return 2;
    unsigned n;while(std::fread(&n,4,1,f)==1){
        std::vector<std::uint32_t> w(n);if(std::fread(w.data(),4,n,f)!=n)return 2;
        bool rgb=false;auto lights=NorthlightEquipmentLightProof::analyze(w.data(),n,&rgb);
        auto h=NorthlightShaderTags::fnv1a(w.data(),n*4);
        NorthlightShadowShader::PatchInfo info;std::vector<std::uint32_t> patched;NorthlightActorDeformation::Program program;
        const bool replay=NorthlightShadowShader::patch(w.data(),n,patched,&info)&&info.texcoord0XY&&
            NorthlightActorDeformation::compile(w.data(),n,program,false,2)&&!program.skinned;
        std::printf("%u %u %u %u %u %u %u\n",unsigned(NorthlightWorldShaderContract::rigidWmo(w.data(),n)),
            NorthlightShaderTags::triggerTags(h),NorthlightShaderTags::triggerTags(h,w.data(),n*4),lights,unsigned(rgb),unsigned(NorthlightSpellFog::vsFogProven(w.data(),n)),unsigned(replay));
    }std::fclose(f);
}
''')
subprocess.run([str(fp.zig()),'c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(source),'-o',str(exe)],env=fp.zig_env(),check=True)
def check(codes):
    data=out/'programs.bin';data.write_bytes(b''.join(struct.pack('<I',len(c)//4)+c for c in codes))
    return [list(map(int,line.split())) for line in subprocess.check_output([str(exe),str(data)],text=True).splitlines()]
rows=check(shaders)
# These active programs also satisfy the coordinate proof. It is deliberately
# not semantic WMO identity; draw-domain admission must guard camera snapshots.
for family in ('cdiffuse_t1.bls','diffuse_t1.bls','shadowmap.bls'):
    assert any(name.endswith('\\'+family) and row[0] for name,row in zip(shaders.values(),rows)),family
new=[]
for (code,name),(rigid,old,tags,lights,rgb,fog,replay) in zip(shaders.items(),rows):
    if '\\mapobj' in name and lit_wmo(code):assert rigid,(name,hex(fnv1a64(code)))
    if rigid:
        assert tags&24==24
        if old&24!=24:new.append(dict(path=name,hash=f'{fnv1a64(code):016x}',lights=lights,rgb=bool(rgb),fog=bool(fog)))
    if fnv1a64(code)==0x66fdafb957c26432:assert (rigid,tags,lights,rgb,fog,replay)==(1,24,4,1,1,1)
assert any(r['path'].endswith('mapobjspecular_t1.bls') and r['lights']==4 and r['rgb'] for r in new), 'active modified ground program must be exercised'
# Lighting edits retain world coordinates; projection/model-view edits do not.
negatives=[];positives=[]
for code,row in zip(shaders,rows):
    if not row[0]:continue
    words=struct.unpack('<%dI'%(len(code)//4),code)
    for r in (2,31):
        changed=[(w&~2047)|80 if w&0x80000000 and ((w>>28)&7)|((w>>8)&24)==2 and w&2047==r else w for w in words]
        negatives.append(struct.pack('<%dI'%len(changed),*changed))
    changed=[(w&~2047)|100 if w&0x80000000 and ((w>>28)&7)|((w>>8)&24)==2 and w&2047==17 else w for w in words]
    positives.append(struct.pack('<%dI'%len(changed),*changed))
assert not any(r[0] for r in check(negatives))
assert all(r[0] for r in check(positives))
report=dict(programs=len(rows),coordinate_proven=sum(r[0] for r in rows),newly_admitted=new,negative_cases=len(negatives),material_edits=len(positives))
(out/'contract-client.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:len(v) if k=='newly_admitted' else v for k,v in report.items()}))
