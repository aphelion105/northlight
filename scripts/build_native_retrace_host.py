"""Build the private x86 apitrace host and capture DLL with pinned Zig on Windows."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from prepare_native_retrace_host import prepare, replace_once
from prepare_native_scene import digest, pe32


# Values from the Windows SDK d3d9.h/d3d9types.h omitted by Zig's MinGW headers.
D3D9_DEFINES = ('D3D9_RESOURCE_PRIORITY_MINIMUM=0x28000000 D3D9_RESOURCE_PRIORITY_LOW=0x50000000 '
                'D3D9_RESOURCE_PRIORITY_NORMAL=0x78000000 D3D9_RESOURCE_PRIORITY_HIGH=0xa0000000 '
                'D3D9_RESOURCE_PRIORITY_MAXIMUM=0xc8000000 D3DPRESENT_DONOTFLIP=4 D3DPRESENT_FLIPRESTART=8 '
                'D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR=16 D3DBASIS_CATMULL_ROM=D3DBASIS_INTERPOLATE').split()


def main():
    parser = argparse.ArgumentParser(__doc__)
    for option in ('upstream', 'cmake', 'ninja', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if os.name != 'nt' or args.jobs < 1:
        raise ValueError('Build on Windows with at least one compiler job')
    out = args.output.resolve()
    upstream = args.upstream.resolve()
    if any(path.is_relative_to(out) for path in (upstream, args.cmake.resolve(), args.ninja.resolve())) or out.is_relative_to(upstream):
        raise ValueError('Build output must be separate from the vendor source and tools')
    out.mkdir(parents=True, exist_ok=False)
    src, build = out / 'source', out / 'build'
    report = prepare(argparse.Namespace(upstream=upstream, output_source=src, private_wrapper=True))
    for name, old, new in (
        ('CMakeLists.txt', 'add_linker_flags (-Wl,--large-address-aware)', '# Set PE large address awareness after the Zig link.'),
        ('wrappers/CMakeLists.txt', 'set (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--enable-stdcall-fixup")',
         '# Zig/lld uses stdcall fixups without a GNU-ld option.'),
    ):
        path = src / name
        path.write_text(replace_once(path.read_text(encoding='utf-8'), old, new), encoding='utf-8', newline='\n')
    zig = fp.zig().resolve()
    include = zig.parent / 'lib/libc/include/any-windows-any'
    if not (include / 'd3d9.h').is_file():
        raise ValueError('The Zig Windows SDK headers are required')
    toolchain = ['set(CMAKE_SYSTEM_NAME Windows)', 'set(CMAKE_SYSTEM_PROCESSOR x86)']
    defines = ' '.join('-D' + value for value in D3D9_DEFINES)
    for cmake_key, action, flags in (('C_COMPILER', 'cc', '-target x86-windows-gnu'),
                                     ('CXX_COMPILER', 'c++', '-target x86-windows-gnu ' + defines),
                                     ('AR', 'ar', ''), ('RANLIB', 'ranlib', '')):
        shim = out / ('zig-' + action.replace('+', 'x') + '.cmd')
        shim.write_text(f'@echo off\n"{zig}" {action} {flags} %*\n', encoding='utf-8')
        toolchain.append(f'set(CMAKE_{cmake_key} [=[{shim.as_posix()}]=] CACHE FILEPATH "Pinned Zig {action}" FORCE)')
    # Preserve the upstream identity even when the private source is below the
    # Northlight checkout and has no Git metadata of its own.
    version = src / 'version.h.in'
    template = version.read_text(encoding='utf-8')
    version.write_text(template.replace('@APITRACE_VERSION@', '14.0-northlight'), encoding='utf-8', newline='\n')
    toolchain_path = out / 'zig-toolchain.cmake'
    toolchain_path.write_text('\n'.join(toolchain) + '\n', encoding='utf-8')
    cmake, ninja = args.cmake.resolve(), args.ninja.resolve()
    commands = [[cmake, '-S', src, '-B', build, '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={toolchain_path}',
                 f'-DCMAKE_MAKE_PROGRAM={ninja}', f'-DDirectX_D3D_INCLUDE_DIR={include}', f'-DDirectX_D3D9_INCLUDE_DIR={include}',
                 '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DENABLE_GUI=OFF'],
                [cmake, '--build', build, '--target', 'd3dretrace', 'd3d9trace', '--parallel', args.jobs]]
    for command, name in zip(commands, ('configure.log', 'build.log')):
        with (out / name).open('xb') as log:
            subprocess.run([str(arg) for arg in command], env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)
    host, wrapper = build / 'northlight-native-retrace.exe', build / 'wrappers/d3d9.dll'
    pe32(host)
    pe32(wrapper)
    image = bytearray(host.read_bytes())
    flags = struct.unpack_from('<I', image, 60)[0] + 22
    struct.pack_into('<H', image, flags, struct.unpack_from('<H', image, flags)[0] | 0x20)
    host.write_bytes(image)
    if b'NORTHLIGHT_APITRACE_D3D9_TARGET\0' not in wrapper.read_bytes():
        raise ValueError('Private wrapper was built without the unique target route')
    builds = {str(artifact): digest(artifact) for artifact in (host, wrapper, zig, cmake, ninja)}
    result = {'format': 1, 'source': report, 'artifacts_sha256': builds, 'laa': True,
              'host': str(host), 'capture_wrapper': str(wrapper), 'commands': [[str(x) for x in cmd] for cmd in commands]}
    (out / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
