# northlight-test: requires=windows-exe,zig,client,stormlib
"""Single-bone interval bounds must retain the interpreter's exact numeric enclosure."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
client_fixtures.actor_client_programs(out)
for tag, flags in [('O2', ['-O2']), ('O0', ['-O0']),
                   ('sanitized', ['-O2', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
    exe = out / ('single-skin-intervals-' + tag + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', *flags,
                    '-UNDEBUG', '-I' + str(out), *fp.test_include_flags(),
                    str(fp.TESTS / 'single_skin_intervals.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
