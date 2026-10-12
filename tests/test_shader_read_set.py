#!/usr/bin/env python3
# northlight-test: requires=cxx
"""task 34: shader_read_set.h, the bytecode scanner behind the spell layer's per-shader read sets, on hand-assembled token streams
(def, dcl, relative addressing, m4x4-family matrix reads, predicated instructions, comments, loops and branches, unknown-opcode fallbacks)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-read-set-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_shader_read_set.cpp'),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120)
        print(label,run.stdout,run.stderr,end='',flush=True);run.check_returncode()
        assert 'tests passed' in run.stdout
