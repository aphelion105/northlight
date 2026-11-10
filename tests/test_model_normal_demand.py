# northlight-test: requires=cxx
"""Execute the production model-normal admission body, including a strict no-lookup check."""
from pathlib import Path
import json,os,runpy,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
method=runpy.run_path(str(ROOT/'tests/test_model_world_context.py'))['method']
body=method('src/proxy/renderer.cpp','template<class Draw> void captureModelNormals(')
cpp=r'''
#include <cassert>
#include <cstdio>
#include <map>
#include "m2_shader_families.h"
using IDirect3DVertexShader9=int;
namespace NorthlightAutoDisplacement {
struct Args {int draw=0,format=0;const void* indices=nullptr;const void* vertices=nullptr;unsigned stride=0;};
struct CaptureSource {int mesh=0,pose=0;void* declaration=nullptr;};
}
struct Normals {unsigned calls=0;template<class...T>void capture(T&&...){++calls;}};
struct Map:std::map<IDirect3DVertexShader9*,uint64_t>{unsigned lookups=0;auto find(IDirect3DVertexShader9* p){++lookups;return std::map<IDirect3DVertexShader9*,uint64_t>::find(p);}};
struct Device {
 Normals* modelNormals=nullptr;bool modelNormalsNeeded=false,enabled=false,applied=false,failed=false;Map vsHashes;
 __BODY__
};
int main(){unsigned cases=0;uint64_t model=0,particle=0;
 for(const auto& entry:kM2ShaderFamilies){if(entry.family==3)model=entry.hash;if(entry.family==1)particle=entry.hash;}
 assert(model&&particle);int shader=0;NorthlightAutoDisplacement::Args args;NorthlightAutoDisplacement::CaptureSource source;
 for(unsigned flags=0;flags<32;++flags)for(unsigned family=0;family<3;++family)for(bool handoff:{false,true}){
  Device d;Normals normals;d.modelNormals=flags&1?&normals:nullptr;d.modelNormalsNeeded=flags&2;d.enabled=flags&4;d.applied=flags&8;d.failed=flags&16;
  if(family)d.vsHashes[&shader]=family==1?particle:model;
  bool called=false;d.captureModelNormals(&shader,[&]{called=true;return 0;},false,args,handoff?&source:nullptr);
  const bool admitted=d.modelNormals&&d.modelNormalsNeeded&&d.enabled&&!d.applied&&!d.failed;
  assert(!called&&d.vsHashes.lookups==unsigned(admitted)&&normals.calls==unsigned(admitted&&family==2));++cases;
 }
 std::printf("MODEL_NORMAL_DEMAND_PASS cases=%u noLookupWithoutConsumer=1 acceptedModel=1 allGuards=1\n",cases);
}
'''.replace('__BODY__',body)
out=fp.output_dir()/'model-normal-demand';out.mkdir(parents=True,exist_ok=True);source=out/'test.cpp';source.write_text(cpp,encoding='utf-8');runs=[]
for mode,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
 exe=out/mode
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-UNDEBUG',*flags,*fp.test_include_flags(),str(source),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(mode+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');r.check_returncode();assert 'MODEL_NORMAL_DEMAND_PASS cases=192' in r.stdout
 runs.append(dict(mode=mode,pass_=True,stdout=r.stdout));print(r.stdout,end='')
(out/'validation.json').write_text(json.dumps(dict(pass_=True,runs=runs),indent=2)+'\n',encoding='utf-8')
