"""Build the 32-bit Northlight host and cooperative Tideglass core from this checkout."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

import tideglass_source

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--storm-source', type=Path, help='existing clean checkout of the pinned StormLib revision')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    tideglass_source.verify()
    build = fp.out() / 'tideglass'
    configure = ['cmake', '-S', ROOT / 'tideglass', '-B', build, '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_SYSTEM_NAME=Windows']
    for key, binary in [('C', 'clang'), ('CXX', 'clang++'), ('RC', 'windres')]:
        tool = shutil.which('i686-w64-mingw32-' + binary)
        if not tool:
            parser.error('Put the llvm-mingw i686 cross-toolchain on PATH')
        configure.append(f'-DCMAKE_{key}_COMPILER={Path(tool).resolve().as_posix()}')
    configure.append('-DSTORM_SOURCE=' + (args.storm_source.resolve().as_posix() if args.storm_source else ''))
    subprocess.run(configure, check=True)
    subprocess.run(['cmake', '--build', build, '--target', 'water-core', '--parallel', str(args.jobs)], check=True)
    subprocess.run([sys.executable, ROOT / 'scripts/build_renderer.py'], check=True)
    for product in (ROOT / 'renderer/frd9.dll', build / 'water-core.dll'):
        print(product)


if __name__ == '__main__':
    main()
