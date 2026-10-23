#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Normal installation stages safe model generators, without external commands or game writes."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import native_model_lights as native
import build_packages


class NativeInstall(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=fp.output_dir())
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = SimpleNamespace(client=self.root, locale='enUS')
        self.work = self.root / 'work'

    def test_fresh_client_stages_equipment_and_safe_spells_without_wdb(self):
        equipment = 'spells\\enchant.m2'
        blobs = {'equipment-lights-staged.mpq': {equipment: b'equipment'},
                 'spell-lights-staged.mpq': {'spells\\missile.m2': b'missile'}}
        with patch.object(native.build_equipment_emitters, 'build', return_value={'counts': {'patched': 1}}) as items, \
             patch.object(native.build_spell_emitters, 'build', return_value={'counts': {'patched': 1}}) as spells, \
             patch.object(native.build_mount_emitters, 'build') as mounts:
            payload, steps = native.build(self.assets, self.work, lambda path: blobs[path.name])
        self.assertEqual(payload, {equipment: b'equipment', 'spells\\missile.m2': b'missile'})
        self.assertEqual(spells.call_args.kwargs, {'excluded': {equipment}})
        self.assertEqual(items.call_count, 1)
        mounts.assert_not_called()
        self.assertIn('live particle', steps[-1]['skipped'])
        self.assertFalse((self.root / 'Data').exists())

    def test_no_qualified_models_is_a_supported_client(self):
        with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('No supported equipment emitters')), \
             patch.object(native.build_spell_emitters, 'build', side_effect=ValueError('No supported spell emitters')):
            payload, steps = native.build(self.assets, self.work, lambda _: self.fail('No archive expected'))
        self.assertEqual(payload, {})
        self.assertEqual(len(steps), 3)

    def test_dbc_payload_and_conflicting_model_owners_are_refused(self):
        for spell in ({'dbfilesclient\\spell.dbc': b'bad'}, {'spells\\same.m2': b'different'}):
            with self.subTest(spell=spell), \
                 patch.object(native.build_equipment_emitters, 'build', return_value={'counts': {}}), \
                 patch.object(native.build_spell_emitters, 'build', return_value={'counts': {}}):
                def read(path):
                    return {'spells\\same.m2': b'equipment'} if 'equipment' in path.name else spell
                with self.assertRaises(ValueError):
                    native.build(self.assets, self.work, read)

    def test_supported_server_cache_adds_mounts_and_corrupt_cache_is_skipped(self):
        from test_mount_emitter_patch import cache
        source = self.root / 'Cache/WDB/enUS/creaturecache.wdb'
        source.parent.mkdir(parents=True)
        for blob in (cache(), b'BOMW' + bytes(21)):
            source.write_bytes(blob)
            with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('No supported equipment emitters')), \
                 patch.object(native.build_spell_emitters, 'build', side_effect=ValueError('No supported spell emitters')), \
                 patch.object(native.build_mount_emitters, 'build', return_value={'counts': {'patched': 1}}) as mounts:
                payload, steps = native.build(self.assets, self.work, lambda _: {'creature\\mount.m2': b'mount'})
            self.assertEqual(bool(payload), blob == cache())
            self.assertEqual(mounts.call_count, int(blob == cache()))
            self.assertEqual(source.read_bytes(), blob)

    def test_unexpected_generator_failure_is_not_silently_hidden(self):
        with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('bad archive')):
            with self.assertRaisesRegex(ValueError, 'bad archive'):
                native.build(self.assets, self.work, lambda _: {})

    def test_every_generator_is_packaged_on_every_platform(self):
        build_packages.check_closure(build_packages.APP_FILES)
        for module in ('native_model_lights', 'build_equipment_emitters', 'build_spell_emitters', 'build_mount_emitters', 'equipment_emitter_patch'):
            self.assertIn('renderer/' + module + '.py', build_packages.APP_FILES)


if __name__ == '__main__':
    unittest.main()
