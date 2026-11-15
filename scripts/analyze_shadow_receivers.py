"""Replay directional receiver shaders on exported depth/maps, not a whole scene."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-prefix', required=True)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--shader', type=Path, action='append', required=True)
    args = parser.parse_args()
    assert len({p.stem for p in args.shader}) == len(args.shader), 'Shader names must be unique'
    out = fp.output_dir()
    support = (fp.TESTS/'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
    source = out/'receiver-replay.cpp'
    source.write_text(support+r'''
int main(int argc,char** argv) {
 assert(argc>=6);GPU gpu(argv[1]);std::string prefix=argv[2],dir=argv[3];
 Constants c{};auto bank=read(prefix+"constants.f32");assert(bank.size()<=sizeof(c));
 std::memcpy(c.data(),bank.data(),bank.size());
 auto tuning=read(prefix+"shadow-tuning.f32");assert(tuning.size()==16);std::memcpy(c[115].data(),tuning.data(),16);
 for(auto binding:std::vector<std::pair<unsigned,std::string>>{{1,"depth"},{2,"shadow-near"},{3,"shadow-far"},{14,"normals"}})
  gpu.bind(binding.first,gpu.tex(image(prefix+binding.second+".fgr")));
 for(int i=4;i<argc;i+=2) {
  auto data=gpu.capture(gpu.shader(argv[i]),c,unsigned(c[33][2]),unsigned(c[33][3]));
  std::ofstream file(dir+"/"+argv[i+1]+".f32",std::ios::binary);file.write(reinterpret_cast<char*>(data.data()),data.size()*sizeof(float));
 }
}
''')
    exe = out/'receiver-replay.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-O2', '-UNDEBUG', *fp.test_include_flags(), str(source),
                    '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    command = [str(exe), str(args.backend.resolve()), args.capture_prefix, str(out)]
    for shader in args.shader:
        command += [str(shader.resolve()), shader.stem]
    subprocess.run(command, cwd=out,
                   env={**os.environ, 'DXVK_LOG_PATH': str(out), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
                   check=True, timeout=120)
    inputs = [*args.shader, args.backend, Path(__file__), fp.TESTS/'shadow_transition_gpu.cpp']
    inputs += [Path(args.capture_prefix+suffix) for suffix in
               ['constants.f32', 'shadow-tuning.f32', 'depth.fgr', 'shadow-near.fgr', 'shadow-far.fgr', 'normals.fgr']]
    (out/'replay-scope.json').write_text(json.dumps(dict(
        capture_prefix=args.capture_prefix, backend=str(args.backend),
        sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        scope='Directional receiver queries only; dry surfaces, actor visibility, temporal filtering and final composition excluded.'), indent=2)+'\n')
    print(out)


if __name__ == '__main__':
    main()
