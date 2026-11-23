#!/usr/bin/env python3
# northlight-test: requires=release-inputs,zig timing
"""Named-runner adapter for the trusted Windows package live gate."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def main():
    package = os.environ.get('NORTHLIGHT_RELEASE_PACKAGE')
    stage = os.environ.get('NORTHLIGHT_LIVE_STAGE')
    out = fp.output_dir()
    if not package or not stage or not out.resolve().is_relative_to(Path(stage).resolve()):
        (out / 'validation.json').write_text(
            '{"status":"INCOMPLETE","error":"Set NORTHLIGHT_RELEASE_PACKAGE and NORTHLIGHT_LIVE_STAGE '
            'and put --output below the private stage"}\n', encoding='utf-8')
        print('INCOMPLETE: supply the private Windows stage, ZIP and a run directory under the stage', flush=True)
        return 1
    command = [sys.executable, '-u', str(fp.SCRIPTS / 'run_release_live_gate.py'),
               '--package', package, '--stage', stage, '--output', str(out / 'gate')]
    if os.environ.get('NORTHLIGHT_RELEASE_BACKGROUND') == '1':
        command.append('--background')
    return subprocess.call(command)


if __name__ == '__main__':
    sys.exit(main())
