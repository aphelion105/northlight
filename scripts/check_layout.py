#!/usr/bin/env python3
"""Layout lint, run by scripts/run_tests.py, scripts/build_renderer.py (checks 1-3) and githooks/pre-commit.

1. No test files outside tests/, and no source, shader or build script directly in renderer/
   (a stale edit to an old path would silently fork a file).
2. Every source sits in a src/<group>/ directory, not in src/ itself.
3. Basenames are unique across src/, shaders/, scripts/, renderer/ and tests/ (they are the
   include namespace).
4. No machine paths in code. Docs, records and vendored third-party sources (src/imgui/) are not
   checked (docs and records keep historical paths as evidence); a code line may opt out with the
   marker `layout: allow-machine-path`.
"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402

# Absolute paths of one machine: home folders (macOS, Linux, Windows), temporary and app folders.
MACHINE = re.compile(r'/Users/[^/\s]+|/home/[^/\s]+|/private/|/var/folders/|/tmp/|/Applications/|'  # layout: allow-machine-path
                     r'[A-Za-z]:\\+(?:Users|Documents and Settings)\\+[^\\\s]+', re.I)
CODE_SUFFIXES = {'.py', '.cpp', '.h', '.inl', '.hlsl', '.inc', '.def', '.sh', '.ini', '.bat', '.ps1'}
NOT_CODE = ('renderer/records/', 'renderer/docs/', 'docs/', 'releases/',
            'src/imgui/')   # vendored, unmodified Dear ImGui (src/imgui/README.md): its comments are not ours to edit
ALLOW = 'layout: allow-machine-path'
# Sources, shaders and build scripts live in src/, shaders/ and scripts/, never in renderer/.
LEGACY_SOURCE = re.compile(r'.*\.(h|inl|inc|def|hlsl|bin|asm|cpp)$|.*-shader-build\.json$|'
                           r'(build_renderer|build_environment|generate_\w+|compile_\w+)\.py$')
LEGACY_TOOLS = {'probe_loader.cpp', 'tirisfal-light-probe-0.3.55.cpp'}   # not DLL sources (private probes)


def git_files():
    r = subprocess.run(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
                       cwd=fp.REPO, capture_output=True)
    return [n for n in r.stdout.decode('utf-8', 'surrogateescape').split('\0') if n] if r.returncode == 0 else []


def top_level(files, d):
    """(path, name) of the files directly in d."""
    rel = d.relative_to(fp.REPO).as_posix() + '/'
    return [(n, n[len(rel):]) for n in files if n.startswith(rel) and '/' not in n[len(rel):]]


def layout_problems(files=None):
    """Checks 1-3: nothing at a legacy path, every source in a group, unique basenames."""
    problems = []
    files = git_files() if files is None else files
    for d in [*fp.SRC_DIRS, *fp.SHADER_DIRS, *fp.SCRIPT_DIRS, *fp.LEGACY_DIRS]:
        for n, name in top_level(files, d):
            if re.match(r'(test_|validate_|verify_)', name):
                problems.append(f'{n}: tests live in tests/, not in {d.relative_to(fp.REPO).as_posix()}/')
    for d in fp.LEGACY_DIRS:
        for n, name in top_level(files, d):
            if LEGACY_SOURCE.match(name) and name not in LEGACY_TOOLS:
                problems.append(f'{n}: sources, shaders and build scripts live in src/, shaders/ and scripts/')
    for n, name in top_level(files, fp.SRC):
        problems.append(f'{n}: sources live in a src/<group>/ directory')
    try:
        fp._build_index()
    except RuntimeError as e:
        problems.append(str(e))
    return problems


def check():
    files = git_files()
    problems = layout_problems(files)
    for n in files:
        p = fp.REPO / n
        if n.startswith(NOT_CODE) or not p.is_file():
            continue
        if p.suffix not in CODE_SUFFIXES and not n.startswith(('githooks/', 'renderer/windows-package/')):
            continue
        for i, line in enumerate(p.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
            if MACHINE.search(line) and ALLOW not in line:
                problems.append(f'{n}:{i}: machine path: {line.strip()[:120]}')
    return problems


if __name__ == '__main__':
    found = check()
    print('\n'.join(found) or 'layout ok')
    sys.exit(1 if found else 0)
