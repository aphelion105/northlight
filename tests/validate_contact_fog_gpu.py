#!/usr/bin/env python3
"""Manual hidden-device native-fog/AO oracle; never launches WoW."""
import os, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

backend=os.environ.get('NORTHLIGHT_DXVK_D3D9')
if os.name!='nt' or not backend or not Path(backend).is_file():
    raise SystemExit('Requires Windows and NORTHLIGHT_DXVK_D3D9 pointing to the test backend')
with tempfile.TemporaryDirectory(prefix='northlight-contact-fog-') as temp:
    root=Path(temp);binary=root/'test.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
                    *fp.test_include_flags(),str(fp.TESTS/'contact_fog_gpu.cpp'),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
    subprocess.run([str(binary),str(fp.COMPILED/'WorldComposite.bin'),str(fp.COMPILED/'Composite.bin'),backend],cwd=root,check=True,timeout=60)
