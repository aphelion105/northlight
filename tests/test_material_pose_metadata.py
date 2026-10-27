#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""CPU-only differential bounds/ownership checks and paired immutable-key timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]
import northlight_paths as fp
import client_fixtures

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path)
parser.add_argument('--pairs',type=int,default=9)
args=parser.parse_args()
out=args.output or fp.output_dir()/'material-pose-metadata'
out.mkdir(parents=True,exist_ok=True)
reference=(fp.TESTS/'displacement_state_reuse.cpp').read_text(encoding='utf-8').split('int main(){')[0]
reference=reference.replace('struct DisplacementStateTestAccess {','struct DisplacementStateTestAccess {\n    static void poseMetadataChecks(const char* path);\n    template<class G> static double poseMetadataTiming(const NorthlightActorDeformation::Program& p,unsigned draws,bool rotating);')
# The same production evaluator, with only immutable-key preparation disabled.
source=fp.src('auto_displacement_renderer.h').read_text(encoding='utf-8')
begin=source.index('        const auto prepared=poseMetadata.get(')
end=source.index('\n    }',begin)
control=source[:begin]+'''        return poseEnvelopes.calculate(program,*source,source,elements,count,bank,identity,envelopeBudget,result);'''+source[end:]
(out/'pose_metadata_control.h').write_bytes(control.encode())
(out/'pose_metadata_reference.h').write_bytes(reference.encode())
exe=out/'material-pose-metadata.exe'
environment=dict(os.environ,NORTHLIGHT_OUT=str(out/'runtime'),ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'),TEMP=str(out/'temp'),TMP=str(out/'temp'))
Path(environment['TEMP']).mkdir(exist_ok=True)
command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_material_pose_metadata.cpp'),'-luser32','-luuid','-o',str(exe)]
subprocess.run(command,env=environment,check=True)
fixture=client_fixtures.four_bone_vs3()
subprocess.run([str(exe),'checks',str(fixture)],env=environment,check=True)
rows=[]
for block in range(args.pairs+2):
    for workload,draws in (('warm',24),('warm',192),('warm',576),('rotating',192)):
        for label in (('prepared','uncached') if block%2 else ('uncached','prepared')):
            line=subprocess.check_output([str(exe),label,str(fixture),str(draws),workload],env=environment,text=True)
            row=json.loads(line)
            row.update(block=block-2,warmup=block<2)
            rows.append(row)
summary={}
for workload,draws in (('warm',24),('warm',192),('warm',576),('rotating',192)):
    pair={label:statistics.median(r['ms_per_frame'] for r in rows if not r['warmup'] and r['draws']==draws and r['workload']==workload and r['variant']==label) for label in ('prepared','uncached')}
    summary[workload+'-'+str(draws)]={**pair,'ratio':pair['prepared']/pair['uncached'],'saved_ms':pair['uncached']-pair['prepared']}
report={'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'control_sha256':hashlib.sha256(control.encode()).hexdigest(),'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'command':command,'game_launched':False,'GPU_used':False,'quality_parameters_changed':False,'pairs':args.pairs,'rows':rows,'summary':summary}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
