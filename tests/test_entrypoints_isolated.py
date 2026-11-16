#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""The world-cache pipeline's entry points run isolated, as the installer starts them
(`python -I -X utf8 <script>`: no script folder, no PYTHONPATH on sys.path, as under the Windows
embeddable Python's ._pth): each one's --help exits 0 from an unrelated working directory.
The git guard: northlight_paths in a package tree without .git finds its files by walking the tree
and never runs git (on a Mac without the command line tools /usr/bin/git opens an installer
dialog); a fake git on PATH proves it would have been reachable."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import os, shutil, subprocess

ENTRY_POINTS = ['scripts/capture_files.py', 'scripts/install_world_cache.py', 'renderer/world_scene_builder.py', 'renderer/world_lights_builder.py',
                'renderer/regional_fog_builder.py', 'renderer/build_celestial_disc_assets.py',
                'tests/validate_world_cache.py', 'build_art_layer.py', 'scripts/client_identity.py',
                'scripts/build_cache_variant.py']
out = fp.output_dir()
env = dict(os.environ, NORTHLIGHT_STORMLIB=str(fp.stormlib()))

# 1. Every entry point under -I -X utf8.
for rel in ENTRY_POINTS:
    r = subprocess.run([sys.executable, '-I', '-X', 'utf8', str(fp.REPO / rel), '--help'], cwd=out, env=env,
                       capture_output=True, encoding='utf-8', errors='replace', timeout=120)
    assert r.returncode == 0 and 'usage' in r.stdout, (rel, r.returncode, r.stderr[-1500:])

# 2. The git guard, in a package tree (the repository layout, no .git).
if os.name != 'nt':
    pkg, fake, marker = out / 'package', out / 'fakebin', out / 'git-was-called'
    for d in (pkg, fake):
        shutil.rmtree(d, ignore_errors=True)
    if marker.exists():
        marker.unlink()
    for name in ['src/sky/forest_regions.inc', 'renderer/world_scene_builder.py', 'scripts/run_tests.py', 'tests/validate_world_cache.py']:
        (pkg / name).parent.mkdir(parents=True, exist_ok=True)
        (pkg / name).write_text('# copy\n')
    shutil.copyfile(fp.REPO / 'northlight_paths.py', pkg / 'northlight_paths.py')
    fake.mkdir()
    (fake / 'git').write_text(f'#!/bin/sh\ntouch "{marker}"\nexit 1\n')
    (fake / 'git').chmod(0o755)
    probe = (f'import sys; sys.path.insert(0, {str(pkg)!r}); import northlight_paths as fp; '
             "print(fp.src('forest_regions.inc')); print(fp.tracked('validate_world_cache.py'))")
    fake_env = dict(env, PATH=f'{fake}{os.pathsep}{os.environ.get("PATH", "")}')
    r = subprocess.run([sys.executable, '-I', '-c', probe], cwd=out, env=fake_env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    assert r.stdout.splitlines() == [str(pkg / 'src/sky/forest_regions.inc'), str(pkg / 'tests/validate_world_cache.py')], r.stdout
    assert not marker.exists(), 'northlight_paths ran git in a package without .git'
    (pkg / '.git').mkdir()
    r = subprocess.run([sys.executable, '-I', '-c', probe], cwd=out, env=fake_env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 0 and marker.exists(), 'the fake git was not reachable, so the check proved nothing'
    shutil.rmtree(pkg)
print(f'entry points: {len(ENTRY_POINTS)} run under python -I -X utf8; git guard OK')
