#!/usr/bin/env python3
# northlight-test: requires=cxx
"""northlight-quality.ini parser, presets, legacy precedence and default-identity tests (native clang++, plain and ASan/UBSan)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-quality-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_quality_settings.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],cwd=fp.src('windows-package/northlight-quality.ini').parents[1],text=True),end='',flush=True)  # the test reads windows-package/northlight-quality.ini
