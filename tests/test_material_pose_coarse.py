# northlight-test: requires=windows-exe,zig,client,stormlib timing
"""Conservative M2 broad phase against exact skin bounds and original budgets."""
from pathlib import Path
import struct, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
header = client_fixtures.actor_client_programs(out)
code = client_fixtures.four_bone_vs3().read_bytes()
words = struct.unpack(f'<{len(code)//4}I', code)
with header.open('a', encoding='utf-8') as f:
    f.write('\nnamespace ClientShaders { static const uint32_t FourBoneVs3[]={' + ','.join(map(str, words)) + '}; }\n')
for opt in ('-O2', '-O0'):
    exe = out / ('material-pose-coarse' + opt + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', opt,
                    '-UNDEBUG', *(['-DNORTHLIGHT_COARSE_BENCH'] if opt == '-O2' else []), '-I' + str(out), *fp.test_include_flags(),
                    str(fp.TESTS / 'material_pose_coarse.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
