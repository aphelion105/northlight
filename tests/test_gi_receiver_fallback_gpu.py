# northlight-test: requires=windows-gpu,zig manual
"""Missing/occluded GI, sky orientation and normal confidence on real shaders."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path[:0] = [str(fp.SCRIPTS/'shaders'), str(fp.TESTS)]
from shader_compiler import Compiler
import windows_gpu_guard

p = argparse.ArgumentParser(__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--baseline-source', type=Path, required=True)
p.add_argument('--backend', type=Path, action='append', required=True)
a = p.parse_args()
windows_gpu_guard.processes(allow_game=True)  # Correctness only; no timing claim.
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
compiler = Compiler(allow_compatible_binary=True)
for name, source in [('old', a.baseline_source), ('new', fp.SHADERS/'world_effects.hlsl')]:
    text = source.read_text().replace('#include "auto_bump_normal.hlsl"', (fp.SHADERS/'auto_bump_normal.hlsl').read_text())
    shader = out/(name+'.hlsl')
    shader.write_text(text+'''
float4 ProbeNormal:register(c116);
float4 ReceiverProbe(float2 uv:TEXCOORD0):COLOR0 {
 return float4(worldGIValue(float3(4,4,4),ProbeNormal),probeIrradiance(float3(4,4,4)+ProbeNormal.xyz*.25,ProbeNormal.xyz).a);
}
''')
    compiler.compile(shader, 'ReceiverProbe', 'ps_3_0', out/(name+'.bin'))

support = (fp.TESTS/'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
source = out/'receiver.cpp'
source.write_text(support+r'''
int main(int argc,char** argv){
 assert(argc==4);GPU gpu(argv[1]);auto* old=gpu.shader(argv[2]);auto* now=gpu.shader(argv[3]);unsigned cases=0,oldFailures=0;double maxError=0;
 const float ambient[3]={.2f,.3f,.4f};
 for(unsigned mode=0;mode<4;++mode){
  for(unsigned ch=0;ch<3;++ch)gpu.bind(4+ch,gpu.tex(rgbaImage(4,2,[&](unsigned,unsigned){return std::array<float,4>{mode==1?0.f:ambient[ch]/.28209479f,0,0,0};})));
  gpu.bind(7,gpu.tex(rgbaImage(4,12,[&](unsigned,unsigned){return mode==2?std::array<float,4>{0,0,1,-1000}:std::array<float,4>{32,1024,1,-1000};})));
  gpu.bind(10,gpu.tex(rgbaImage(4,2,[&](unsigned x,unsigned y){return std::array<float,4>{float(x%2),float(y),float(x/2),mode?0.f:-1.f};})));
  for(float z:{-1.f,0.f,.5f,1.f})for(float confidence:{0.f,1.f})for(float strength:{0.f,1.f,2.f})for(float brightness:{-1.f,0.f,1.f}){
   Constants c{};c[19][3]=8;c[20]={2,strength,1,1};c[24][3]=1;c[66][1]=brightness;c[116]={std::sqrt(1-z*z),0,z,confidence};
   for(unsigned ch=0;ch<3;++ch)c[18][ch]=ambient[ch];
   // The eight receiver-to-probe distances vary after normal offset. Compute
   // the scalar moment probability independently for those fixed corners.
   double total=0;
   const double q[3]={4+.25*c[116][0],4,4+.25*z};
   for(unsigned bits=0;bits<8;++bits){double d2=0,weight=1;for(unsigned axis=0;axis<3;++axis){double v=(bits&(1u<<axis))?8:0;d2+=(q[axis]-v)*(q[axis]-v);weight*=v?q[axis]/8:1-q[axis]/8;}double visibility=.02/(.02+d2);total+=weight*visibility*visibility;}
   auto actual=gpu.capture(now,c,4,4),before=gpu.capture(old,c,4,4);
   for(unsigned ch=0;ch<3;++ch){double sky=confidence?(.5+.5*z):1;
    double ratio=mode==0?sky:mode==1?0:mode==2?total/std::max(total,.00001):1;
    if(mode)ratio*=std::max(1.f+brightness,0.f);
    double expected=std::max((ratio-1)*ambient[ch]*strength,-double(ambient[ch]));if(expected>0)expected*=confidence;
    const double error=std::fabs(actual[ch]-expected);maxError=std::max(maxError,error);assert(error<3e-5);
    oldFailures+=std::fabs(before[ch]-expected)>3e-5;
   }
   assert(std::fabs(actual[3]-(mode?1:0))<1e-6);++cases;
  }
 }
 assert(cases==288&&oldFailures>0);std::printf("GI_RECEIVER_PASS cases=%u baselineFailedChannels=%u maxError=%.9g missingSky=1 occludedCoverage=1 confidence=1 strength=1 brightness=1\n",cases,oldFailures,maxError);
}
''')
exe = out/'receiver.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
               '-Wno-return-type', *fp.test_include_flags(), '-I'+str(fp.TESTS), str(source),
               str(fp.src('world_gi.cpp')), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
records = []
for index, backend in enumerate(a.backend):
    run = subprocess.run([str(exe), str(backend.resolve()), str(out/'old.bin'), str(out/'new.bin')],
                         cwd=out, env=fp.zig_env(), capture_output=True, text=True, timeout=120)
    (out/f'gpu-{index}.log').write_text(run.stdout+run.stderr)
    run.check_returncode()
    print(run.stdout)
    records.append({'backend': str(backend), 'output': run.stdout})
(out/'validation.json').write_text(json.dumps({'pass': True, 'timing_valid': False, 'records': records}, indent=2))
