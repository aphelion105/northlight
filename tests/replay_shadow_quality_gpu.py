# northlight-test: requires=windows-gpu,zig manual
"""Compare visibility on identical F12 inputs; no claim about enlarged-map cost."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.TESTS))
import windows_gpu_guard

p = argparse.ArgumentParser(__doc__)
p.add_argument('--session', type=Path, required=True)
p.add_argument('--capture', type=int, required=True)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--backend', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
windows_gpu_guard.processes()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
support = (fp.TESTS/'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
source = out/'replay.cpp'
source.write_text(support + r'''
int main(int argc,char** argv){
    assert(argc==6);GPU gpu(argv[1]);const std::string prefix=argv[2],output=argv[5];
    auto raw=read(prefix+"constants.f32");Constants c{};assert(raw.size()<=sizeof c);std::memcpy(c[0].data(),raw.data(),raw.size());
    const auto depth=image(prefix+"depth.fgr");const unsigned w=depth.width/2,h=depth.height/2;
    gpu.bind(1,gpu.tex(depth));gpu.bind(2,gpu.tex(image(prefix+"shadow-near.fgr")));gpu.bind(3,gpu.tex(image(prefix+"shadow-far.fgr")));
    gpu.bind(14,gpu.tex(image(prefix+"normals.fgr")));gpu.bind(11,gpu.tex(rgbaImage(1,1,[](unsigned,unsigned){return std::array<float,4>{0,0,0,0};})));
    auto* old=gpu.shader(argv[3]);auto* current=gpu.shader(argv[4]);
    auto median=[](std::vector<double> t){std::sort(t.begin(),t.end());return t[t.size()/2];};
    for(unsigned pass=0;pass<2;++pass){
        c[115]={1.25f,.5f,1,1};auto* shader=pass?current:old;
        const auto values=gpu.capture(shader,c,w,h);std::vector<float> channel(w*h);
        for(unsigned i=0;i<channel.size();++i){channel[i]=values[4*i];if(!std::isfinite(channel[i])||channel[i]<-1e-5f||channel[i]>1.00001f){std::fprintf(stderr,"invalid visibility pass=%u pixel=%u value=%g\n",pass,i,channel[i]);std::abort();}channel[i]=std::clamp(channel[i],0.f,1.f);}
        std::ofstream file(output+(pass?"/new.f32":"/old.f32"),std::ios::binary);file.write(reinterpret_cast<const char*>(channel.data()),channel.size()*sizeof(float));
        std::printf("SHADOW_REPLAY variant=%s width=%u height=%u medianMs=%.6f\n",pass?"new":"old",w,h,median(gpu.time(shader,c,w,h)));
    }
}
''', encoding='utf-8')
exe = out/'replay.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           '-Wno-return-type', *fp.test_include_flags(), '-I'+str(fp.TESTS), str(source),
           str(fp.src('world_gi.cpp')), '-luser32', '-o', str(exe)]
subprocess.run(command, env=fp.zig_env(), check=True)
prefix = a.session.resolve()/f'capture-{a.capture}-'
result = subprocess.run([str(exe), str(a.backend.resolve()), str(prefix), str(a.baseline.resolve()),
                         str(fp.COMPILED/'DirectionalShadow.bin'), str(out)],
                        cwd=out, env=fp.zig_env(), capture_output=True, text=True, timeout=120)
(out/'gpu.log').write_text(result.stdout+result.stderr)
result.check_returncode()
print(result.stdout)
(out/'validation.json').write_text(json.dumps({'pass': True, 'scope': 'Identical captured maps/depth/normals; directional visibility only',
    'whole_scene_replay': False, 'water_mask': 'authored-neutral', 'source_constants': 'unversioned-final-bank', 'enlarged_geometry_measured': False, 'result': result.stdout}, indent=2))
