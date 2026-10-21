#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Installer model staging and release-cache freshness, without changing a client."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_art_layer as art
import build_packages as packages
import test_emitter_patch


class Delivery(unittest.TestCase):
    def test_release_rejects_stale_or_missing_builder_sources(self):
        manifest = {'variant': 'stock', 'build': {'sources': packages.install_world_cache.source_hashes()}}
        packages.check_variant_sources(manifest)
        for changed in ('outdoor-light-profiles.json', 'wotlk-emitter-profiles.json', 'world_scene_builder.py'):
            stale = copy.deepcopy(manifest)
            stale['build']['sources'][changed] = 'old'
            with self.assertRaisesRegex(SystemExit, changed):
                packages.check_variant_sources(stale)
        with self.assertRaisesRegex(SystemExit, 'regenerate'):
            packages.check_variant_sources({'variant': 'stock'})

    def test_art_stages_only_reviewed_models(self):
        assets = object()
        work = Path('staging')
        models = {'world\\test\\lamp.m2': b'model'}
        with patch.object(art.build_emitter_patch, 'build', return_value={'counts': {'patched_models': 1}}) as generate, \
             patch.object(art, 'read_archive', return_value=models):
            payload, report = art.model_lights(assets, work)
        self.assertEqual(payload, models)
        self.assertEqual(report['models'], 1)
        self.assertTrue(generate.call_args.kwargs['reviewed_only'])
        self.assertIs(generate.call_args.args[0], assets)

    def test_art_refuses_non_model_payload_and_unexpected_failure(self):
        with patch.object(art.build_emitter_patch, 'build', return_value={'counts': {}}), \
             patch.object(art, 'read_archive', return_value={'DBFilesClient\\Spell.dbc': b'bad'}):
            with self.assertRaisesRegex(ValueError, 'non-model'):
                art.model_lights(object(), Path('staging'))
        with patch.object(art.build_emitter_patch, 'build', side_effect=ValueError('broken model archive')):
            with self.assertRaisesRegex(ValueError, 'broken model archive'):
                art.model_lights(object(), Path('staging'))

    def test_client_without_supported_models_keeps_normal_art(self):
        with patch.object(art.build_emitter_patch, 'build', side_effect=ValueError('No supported model changes')):
            payload, report = art.model_lights(object(), Path('staging'))
        self.assertEqual(payload, {})
        self.assertIn('skipped', report)

    def test_packaged_imports_are_complete(self):
        packages.check_closure(packages.APP_FILES)

    def test_automatic_generation_never_guesses_unknown_models_or_revisions(self):
        original = test_emitter_patch.LightPatch().model()
        name = 'world\\torch.m2'
        class Assets:
            providers = {name: None, 'world\\unknown_lantern.m2': None}
            def read(self, path):
                return original
            def fingerprint(self):
                return {}
        profile = {'position': [0, 0, 1], 'rgb': [1, .5, .1], 'intensity': 1,
                   'start': 1, 'end': 10, 'asset_sha256': '0' * 64,
                   'family': 'test', 'position_basis': 'reviewed fixture'}
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
            path = Path(folder) / 'profiles.json'
            path.write_text(json.dumps({'version': 1, 'profiles': {name: profile}}))
            with patch.object(art.build_emitter_patch, 'profile', side_effect=AssertionError('must not guess')):
                with self.assertRaisesRegex(ValueError, 'No supported model changes'):
                    art.build_emitter_patch.build(Assets(), Path(folder) / 'out', path, reviewed_only=True)
            self.assertFalse((Path(folder) / 'out/patch-y.mpq').exists())

    def test_generated_model_matches_the_reviewed_light_cache_source(self):
        from outdoor_light_profiles import fallback, load
        from world_lights_builder import extract_m2
        from collections import Counter
        original = test_emitter_patch.LightPatch().model()
        name = 'world\\torch.m2'
        class Assets:
            providers = {name: None}
            def read(self, path):
                return original
            def fingerprint(self):
                return {}
            def origin(self, path):
                return 'fixture.mpq'
        profile = {'position': [0, 0, 1], 'rgb': [1, .5, .1], 'intensity': 1,
                   'start': 1, 'end': 10, 'asset_sha256': hashlib.sha256(original).hexdigest(),
                   'family': 'test', 'position_basis': 'reviewed fixture'}
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
            folder = Path(folder)
            path = folder / 'profiles.json'
            path.write_text(json.dumps({'version': 1, 'profiles': {name: profile}}))
            art.build_emitter_patch.build(Assets(), folder / 'out', path, reviewed_only=True)
            payload = art.read_archive(folder / 'out/patch-y.mpq')
            self.assertEqual(set(payload), {name})
            catalog = load(folder / 'out/outdoor-light-profiles.json')
            self.assertEqual(fallback(catalog, name, original, []),
                             fallback(catalog, name, payload[name], extract_m2(payload[name], Counter())))


if __name__ == '__main__':
    unittest.main()
