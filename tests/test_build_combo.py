#!/usr/bin/env python3
"""The combo build configures from a clean cache with the triple-named tools and lists the files it produced."""

from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import sys
import tempfile
from unittest import mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_combo
from build_renderer_assets import FILENAME, built_path


def toolchain(folder):
    """llvm-mingw names its tools by triple; on hosts that allow it they are links to shared wrappers."""
    wrapper = folder / 'clang-target-wrapper.exe'
    wrapper.write_bytes(b'wrapper')
    try:
        os.symlink(wrapper, folder / 'link-probe.exe')
    except OSError:
        return False
    return True


def run(argv, out, tools, links, missing=()):
    calls, copies = [], []
    cache_present = []

    def which(name):
        if name in missing:
            return None
        path = tools / f'{name}.exe'
        if links and '-w64-mingw32-' in name and not path.exists():
            os.symlink(tools / 'clang-target-wrapper.exe', path)
        return str(path)

    def record(command, check):
        assert check
        command = [str(part) for part in command]
        if command[:2] == ['cmake', '-S']:
            build = Path(command[command.index('-B') + 1])
            cache_present.append((build / 'CMakeCache.txt').exists())
        calls.append(command)

    stdout = io.StringIO()
    with mock.patch.object(sys, 'argv', ['build_combo.py', *argv]), \
         mock.patch.object(build_combo.tideglass_source, 'verify'), \
         mock.patch.object(build_combo.fp, 'out', return_value=out), \
         mock.patch.object(build_combo.shutil, 'which', side_effect=which), \
         mock.patch.object(build_combo.shutil, 'copyfile', side_effect=lambda a, b: copies.append((Path(a), Path(b)))), \
         mock.patch.object(build_combo.subprocess, 'run', side_effect=record), \
         redirect_stdout(stdout), redirect_stderr(io.StringIO()):
        build_combo.main()
    return calls, copies, cache_present, [Path(line) for line in stdout.getvalue().splitlines()]


with tempfile.TemporaryDirectory() as folder:
    out = Path(folder) / 'out'
    tools = Path(folder) / 'bin'
    out.mkdir()
    tools.mkdir()
    links = toolchain(tools)
    if not links:  # Windows without the symlink privilege: the wrapper names cannot be exercised.
        assert 'Path(tool).resolve()' not in (fp.SCRIPTS / 'build_combo.py').read_text()
    for x64, build_name, triple in ((False, 'tideglass', 'i686'), (True, 'tideglass-x64', 'x86_64')):
        build = out / build_name
        build.mkdir()
        (build / 'CMakeCache.txt').write_text('CMAKE_CXX_FLAGS:STRING=-DWATER_VISUAL_CLOCK\n')
        calls, copies, cache_present, products = run(['--x64'] if x64 else [], out, tools, links)
        configure, core, renderer = calls
        assert cache_present == [False], 'the combo configure reused a stale CMake cache'
        for key, binary in (('C', 'clang'), ('CXX', 'clang++'), ('RC', 'windres')):
            name = (tools / f'{triple}-w64-mingw32-{binary}.exe').as_posix()
            assert f'-DCMAKE_{key}_COMPILER={name}' in configure, configure
        assert core[-3:] == ['water-core', '--parallel', '2'] and core[2] == str(build)
        assert renderer[1].endswith('build_renderer.py') and (renderer[2:] == ['--x64']) == x64
        if x64:
            proxy = out / 'x64-proxy'
            assert copies == [(build / 'water-core.dll', proxy / 'water-core.dll')]
            assert products == [proxy / 'd3d9.dll', proxy / FILENAME, proxy / 'water-core.dll']
        else:
            dll = fp.RENDERER / 'frd9.dll'
            assert copies == [] and products == [dll, built_path(dll), build / 'water-core.dll']

    for tool in ('cmake', 'ninja'):
        try:
            run([], out, tools, links, missing={tool})
        except SystemExit as error:
            assert error.code == 2
        else:
            raise AssertionError(f'a missing {tool} was not reported')

# The x64 products must sit where build_renderer.py writes the standalone proxy.
renderer_source = (fp.SCRIPTS / 'build_renderer.py').read_text()
assert "'x64-proxy' if x64" in renderer_source and 'output_dll.with_name(FILENAME) if x64' in renderer_source
print('PASS build combo: clean configure cache, triple-named tools, missing tools, x86/x64 product paths')
