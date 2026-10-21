#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Emitter pin report (renderer/emitter_pin_report.py) on the configured client: every reviewed
profile matches the client's own models, and in the stock view (Blizzard's 3.3.5a archives only)
the six lamp models that differ from the HD pack match their stock variant pins. Reads MPQs only."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import hashlib,json,unittest
import emitter_pin_report
import outdoor_light_profiles
from world_scene_builder import Assets

# The six profiles whose stock 3.3.5a model (common-2.mpq) differs from the HD pack's model.
STOCK_DIFFERENT={'world\\generic\\human\\passive doodads\\lamps\\stormwindstreetlamp01.m2',
    'world\\generic\\human\\passive doodads\\lamps\\tirisfallstreetlamp01.m2',
    'world\\azeroth\\westfall\\passivedoodads\\lamppost\\westfalllamppost.m2',
    'world\\azeroth\\westfall\\passivedoodads\\lamppost\\westfalllamppost01.m2',
    'world\\azeroth\\westfall\\passivedoodads\\lamppost\\westfalllamppost02.m2',
    'world\\azeroth\\elwynn\\passivedoodads\\campfire\\elwynncampfire.m2'}

class PinReport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets={view:Assets(fp.client_root(),view) for view in ('all','stock')}
        cls.baseline=Assets(fp.client_root(),without='y')
        cls.reports={view:emitter_pin_report.report(a) for view,a in cls.assets.items()}
        (fp.output_dir()/'emitter-pins.json').write_text(json.dumps(cls.reports,indent=2)+'\n')
    @classmethod
    def tearDownClass(cls):
        for a in cls.assets.values():a.close()
        cls.baseline.close()
    def test_shape(self):
        catalog=outdoor_light_profiles.load()
        for view,r in self.reports.items():
            self.assertEqual(set(r['profiles']),set(catalog))
            self.assertEqual(sum(r['summary'].values()),len(catalog))
            self.assertEqual(r['archive_fingerprint']['view'],view)
            for path,x in r['profiles'].items():
                self.assertEqual(x['status']=='matched',x.get('asset_sha256') in x['pinned'])
    def test_client_matches(self):
        # The configured (dev) client resolves every profile's own model.
        r=self.reports['all']
        self.assertEqual(r['summary']['matched'],r['profiles_total'])
    def test_batches_reproduce_0_3_110_evidence(self):
        # batches() is the method the reviewed positions were measured with. The 0.3.110 evidence is kept
        # as digests (tests/fixtures/lamp-batches-0.3.110.sha256.json): the path, the model file and the
        # batches of each of its 269 models, compared on the client's own models.
        expected=json.loads((fp.FIXTURES/'lamp-batches-0.3.110.sha256.json').read_text())['models']
        digest=lambda text:hashlib.sha256(text.encode()).hexdigest()
        compared=0
        for path in sorted(self.assets['all'].providers):
            entry=expected.get(digest(path)[:32])
            if entry is None:continue
            sha=hashlib.sha256(self.baseline.read(path)).hexdigest()
            if sha!=entry['model_sha256']:continue
            batches=json.loads(json.dumps(emitter_pin_report.batches(self.assets['all'],path)))
            self.assertEqual(digest(json.dumps(batches,sort_keys=True,separators=(',',':'))),entry['batches_sha256'],path);compared+=1
        self.assertGreaterEqual(compared,74)
    def test_luminous_positions_are_batch_centers(self):
        # Every profile or variant whose basis names a luminous batch sits at that batch's center
        # in the model it pins: HD profiles on the configured client, stock variants in the stock view.
        catalog=outdoor_light_profiles.load();checked=0
        for view,r in self.reports.items():
            for path,x in r['profiles'].items():
                p=catalog[path];pin=p if x['pin']=='profile' else p['variants'][x['asset_sha256']]
                prefix='installed luminous skin-batch bounds center: '
                if not pin['position_basis'].startswith(prefix):continue
                texture=pin['position_basis'][len(prefix):].split(' (')[0]
                centers={tuple(b['center']) for b in emitter_pin_report.batches(self.assets[view],path) if b['texture']==texture}
                self.assertEqual(centers,{tuple(pin['position'])},(view,path));checked+=1
        self.assertGreaterEqual(checked,18*2+5)
    def test_stock_view(self):
        r=self.reports['stock']
        self.assertEqual(r['summary']['matched'],r['profiles_total'])
        by_variant={p for p,x in r['profiles'].items() if x['pin']!='profile'}
        self.assertEqual(by_variant,STOCK_DIFFERENT)
        for path in STOCK_DIFFERENT:self.assertEqual(r['profiles'][path]['archive'].lower(),'data/common-2.mpq')

if __name__=='__main__':unittest.main()
