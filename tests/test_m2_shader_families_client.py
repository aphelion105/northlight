#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""task 34: m2_shader_families.h is exactly what renderer/extract_m2_shader_families.py generates from the
configured client's archives (byte for byte). Regenerate with `python3 renderer/extract_m2_shader_families.py`."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
from extract_m2_shader_families import generate
text,lines,_=generate([fp.client_root()])
print('\n'.join(lines))
assert text==(fp.GAMEDATA/'m2_shader_families.h').read_text(),'m2_shader_families.h is stale: rerun renderer/extract_m2_shader_families.py'
print('ok')
