# northlight-test: requires=windows-exe,zig
"""Windows CPU upload ownership and static shadow cache lifecycle contracts."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
for name in ('test_world_mesh_plan', 'test_world_mesh_pages', 'test_world_mesh_page_gpu'):
    exe = out / (name + '.exe')
    sources = [] if name.endswith('page_gpu') else [fp.src('world_gi.cpp'), fp.src('world_mesh_plan.cpp')]
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                    str(fp.TESTS / (name + '.cpp')), *map(str, sources), '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)

for name in ('test_upload_payload_release.py', 'test_mesh_buffer_store.py'):
    subprocess.run([sys.executable, str(fp.TESTS/name)], check=True)

for name in ('test_static_shadow_gpu', 'test_static_plan_job'):
    exe = out / (name + '.exe')
    sources = [fp.src('world_gi.cpp')]
    if name == 'test_static_plan_job':
        sources.append(fp.src('static_shadow_scene.cpp'))
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                    str(fp.TESTS / (name + '.cpp')), *map(str, sources), '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
