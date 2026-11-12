#!/usr/bin/env python3
# northlight-test: requires=client
"""Every active sky/skin/material must have a current, unambiguous renderer role."""
from pathlib import Path
import json
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from scripts import build_sky_material_catalog as catalog

catalog.check_generated()
subprocess.run([sys.executable,str(fp.REPO/'scripts/build_sky_material_catalog.py'),'--check'],check=True)
roles=json.loads(catalog.ROLES.read_text())['materials']
assert catalog.role(roles,'environments\\stars\\icecrown_skybowla_night.blp',320)=='background'
assert catalog.role(roles,'environments\\stars\\icecrown_skybowla_night.blp',192)=='foreground'
assert catalog.role(roles,'environments\\stars\\dalaranday_dome02.blp',17)=='background'
assert catalog.role(roles,'environments\\stars\\dalaran_night_clouds02.blp',37)=='foreground'
assert catalog.role(roles,'pogost_sun.blp',2)=='celestial'
assert catalog.role(roles,'deathsky_skybowl01.blp',320)=='native'
assert catalog.role(roles,'arathi_sunray01.blp',64)=='celestial'
try:
    catalog.role(roles,'new_material.blp',1)
except KeyError:
    pass
else:
    raise AssertionError('Unreviewed material must fail catalog generation')
assert catalog.role(roles,'hellfireplanet03.blp',2)=='planet'
assert catalog.role(roles,'hellfireplanetring.blp',4)=='planet'
assert catalog.role(roles,'shadowmoon_meteor.blp',24)=='foreground'
assert catalog.role(roles,'saturnplanet.blp',2)=='foreground' # Caverns of Time stays native
assert json.loads(catalog.MANIFEST.read_text())['planet_meshes'] >= 15
print('PASS all active sky materials, exact planet geometry and ring anchors; no asset writes')
