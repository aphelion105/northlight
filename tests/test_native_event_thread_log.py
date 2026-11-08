# northlight-test: requires=zig,windows-exe,windows-gpu,client
"""Actual StreamDevice logging options and EVENT lifecycle; hidden test window.

The query visibility gate is synthetic. Completion still comes from the real
DXVK EVENT. This verifies wiring and lifecycle, not GPU speed or game stutters.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='event-thread-log-') as folder:
    for mode in ['current', 'null-sink-negative']:
        overlay = Path(folder) / mode
        overlay.mkdir()
        if mode == 'null-sink-negative':
            for header in fp.src('stream_device.h').parent.glob('*.h'):
                shutil.copyfile(header, overlay / header.name)
            header = overlay / 'stream_device.h'
            source = header.read_text()
            assert source.count('core.queryThreadLog=opt.log;') == 1
            header.write_text(source.replace('core.queryThreadLog=opt.log;', 'core.queryThreadLog=nullptr;'))
        exe = overlay / 'test.exe'
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                        '-O2', '-UNDEBUG', '-Wno-microsoft-exception-spec', '-I', str(overlay),
                        *fp.test_include_flags(), str(Path(__file__).with_name('native_event_thread_log.cpp')),
                        '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
        env = os.environ.copy()
        env['DXVK_LOG_PATH'] = str(overlay)
        env['DISABLE_VULKAN_OBS_CAPTURE'] = '1'
        result = subprocess.run([str(exe), str(fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll')],
                                cwd=overlay, env=env, text=True, capture_output=True, timeout=60)
        print(mode, result.returncode, result.stdout, result.stderr, flush=True)
        if mode == 'current':
            result.check_returncode()
            assert 'NATIVE_EVENT_THREAD_LOG_PASS' in result.stdout
        else:
            assert result.returncode != 0 and 'reports==generation' in result.stderr
print('PASS actual StreamDevice EVENT diagnostic wiring and old-sink negative control')
