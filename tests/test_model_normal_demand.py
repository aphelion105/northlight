# northlight-test: requires=zig
"""Execute the production model-normal admission body, including a strict no-lookup check."""
from pathlib import Path
import json,os,runpy,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
method=runpy.run_path(str(ROOT/'tests/test_model_world_context.py'))['method']
body=method('src/proxy/renderer.cpp','template<class Draw> void captureModelNormals(')
cpp=r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
using IDirect3DVertexShader9=int;
namespace NorthlightAutoDisplacement {
struct Args {int draw=0,format=0;const void* indices=nullptr;const void* vertices=nullptr;unsigned stride=0;};
struct CaptureSource {int mesh=0,pose=0,bank=0;void* declaration=nullptr;};
}
struct Normals {unsigned calls=0;template<class...T>void capture(T&&...){++calls;}};
struct Device {
 struct CpuScope {explicit CpuScope(unsigned* count){if(count)++*count;}};
 unsigned cpuNormalCapture=0;
 bool sampledDrawTimers()const{return true;}
 struct ShaderClass {bool terrain=false;unsigned family=0;} shader;
 unsigned classifications=0;
 const ShaderClass& classifyVs(IDirect3DVertexShader9*){++classifications;return shader;}
 Normals* modelNormals=nullptr;bool modelNormalsNeeded=false,enabled=false,applied=false,failed=false;
 __BODY__
};
int main(){unsigned cases=0;int shader=0;NorthlightAutoDisplacement::Args args;NorthlightAutoDisplacement::CaptureSource source;
 for(unsigned flags=0;flags<32;++flags)for(unsigned family=0;family<3;++family)for(bool handoff:{false,true}){
  Device d;Normals normals;d.modelNormals=flags&1?&normals:nullptr;d.modelNormalsNeeded=flags&2;d.enabled=flags&4;d.applied=flags&8;d.failed=flags&16;
  d.shader.family=family==2?3:family;
  bool called=false;d.captureModelNormals(&shader,[&]{called=true;return 0;},false,args,handoff?&source:nullptr);
  const bool admitted=d.modelNormals&&d.modelNormalsNeeded&&d.enabled&&!d.applied&&!d.failed;
  assert(!called&&d.classifications==unsigned(admitted)&&normals.calls==unsigned(admitted&&family==2));assert(d.cpuNormalCapture==normals.calls);++cases;
 }
 std::printf("MODEL_NORMAL_DEMAND_PASS cases=%u noLookupWithoutConsumer=1 acceptedModel=1 allGuards=1\n",cases);
}
'''.replace('__BODY__',body)
out=fp.output_dir()/'model-normal-demand';out.mkdir(parents=True,exist_ok=True);source=out/'test.cpp';source.write_text(cpp,encoding='utf-8');runs=[]
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else [os.environ.get('CXX','clang++')]
modes=[('O2',['-O2']),('O0',['-O0'])] if os.name=='nt' else [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]
for mode,flags in modes:
 exe=out/(mode+('.exe' if os.name=='nt' else ''))
 subprocess.run([*compiler,'-std=c++17','-UNDEBUG',*flags,*fp.test_include_flags(),str(source),'-o',str(exe)],env=fp.zig_env(),check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(mode+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');r.check_returncode();assert 'MODEL_NORMAL_DEMAND_PASS cases=192' in r.stdout
 runs.append(dict(mode=mode,pass_=True,stdout=r.stdout));print(r.stdout,end='')
(out/'validation.json').write_text(json.dumps(dict(pass_=True,runs=runs),indent=2)+'\n',encoding='utf-8')
