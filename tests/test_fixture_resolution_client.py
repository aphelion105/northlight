#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Build independent stock and active-pack fixture catalogues without MPQ writes."""
from pathlib import Path
import hashlib
import json
import sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets
from native_fixture_catalog import build

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
reports={}
for view in ('stock','all'):
    assets=Assets(archives=view)
    before={p:(p.stat().st_size,p.stat().st_mtime_ns) for p in assets.paths}
    destination=out/view;destination.mkdir()
    try:report=build(assets,destination)
    finally:assets.close()
    assert before=={p:(p.stat().st_size,p.stat().st_mtime_ns) for p in before}, 'Archive inputs changed'
    assert report['fixtures'], 'No actual fixture identity exercised'
    assert hashlib.sha256((destination/'native-fixtures.bin').read_bytes()).hexdigest()==report['sha256']
    for fixture in report['fixtures']:
        assert fixture['dependencies'][fixture['asset']]==fixture['sha256']
        for index,resolved in fixture['resolution'].items():
            assert int(index) in fixture['sources'] and any(resolved['offset'])
            assert any(p.endswith('.skin') for p in fixture['dependencies'])
            assert any(p.endswith('.blp') for p in fixture['dependencies'])
    (destination/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    reports[view]={f['asset']:f for f in report['fixtures']}
    print(view,'fixtures',len(report['fixtures']),'resolved',sum(bool(f['resolution']) for f in report['fixtures']),'refused',len(report['errors']))
changed=[p for p in reports['stock'].keys()&reports['all'].keys()
         if reports['stock'][p]['dependencies']!=reports['all'][p]['dependencies']]
(out/'comparison.json').write_text(json.dumps({'changed_assets':changed,'same_path_is_not_identity':True},indent=2)+'\n')
print('PASS: independent content-derived catalogues; changed shared paths:',len(changed))
