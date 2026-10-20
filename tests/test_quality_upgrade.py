#!/usr/bin/env python3
# northlight-test:
"""New Quality effects migrate once, including old explicit zeros, through normal rollback."""
from pathlib import Path
import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
spec = importlib.util.spec_from_file_location('quality_installer', fp.RENDERER / 'windows-package/install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

TEMPLATE = (b'[Quality]\nPreset=Quality\n;EquipmentLights=1\n;CommandStream=1\n'
            b'; Northlight Quality upgrade: equipment-lights EquipmentLights=1 CommandStream=1\n')


class QualityUpgrade(unittest.TestCase):
    def test_existing_quality_enables_new_effect_even_if_old_key_was_zero(self):
        user = b'[Quality]\nPreset=QUALITY\nEquipmentLights=0\nCommandStream=0\nGpuBudgetMs=7\n'
        upgraded = installer.merge_quality_ini(user, TEMPLATE)
        self.assertTrue(upgraded.startswith(user))
        self.assertIn(b'EquipmentLights=1\nCommandStream=1', upgraded)
        self.assertIn(b'GpuBudgetMs=7', upgraded)
        self.assertIsNone(installer.merge_quality_ini(upgraded, TEMPLATE))
        edited = upgraded + b'EquipmentLights=0\n'
        self.assertIsNone(installer.merge_quality_ini(edited, TEMPLATE))

    def test_other_presets_are_unchanged_until_quality_is_selected(self):
        for preset in ('Balanced', 'Performance'):
            user = ('[Quality]\nPreset='+preset+'\nEquipmentLights=0\nCommandStream=0\n').encode()
            self.assertIsNone(installer.merge_quality_ini(user, TEMPLATE))
            selected = user.replace(preset.encode(), b'Quality')
            self.assertIn(b'EquipmentLights=1', installer.merge_quality_ini(selected, TEMPLATE))

    def test_scope_bom_crlf_and_multiple_features(self):
        user = b'\xef\xbb\xbf[other]\r\nPreset=Performance\r\n[Quality]\r\n;Preset=Performance\r\n'
        template = TEMPLATE + b';GrassShadows=1\n; Northlight Quality upgrade: grass-shadows GrassShadows=1 ActorShadows=1\n'
        upgraded = installer.merge_quality_ini(user, template)
        self.assertTrue(upgraded.startswith(user))
        self.assertIn(b'GrassShadows=1\r\nActorShadows=1', upgraded)
        self.assertNotIn(b'\n', upgraded.replace(b'\r\n', b''))
        self.assertIsNone(installer.merge_quality_ini(upgraded, template))

    def test_repeated_and_invalid_preset_matches_renderer_rules(self):
        user = b'[Quality]\nPreset=Quality\nPreset=Performance\nPreset=unknown\nEquipmentLights=0\nCommandStream=0\n'
        self.assertIsNone(installer.merge_quality_ini(user, TEMPLATE))
        self.assertIn(b'EquipmentLights=1', installer.merge_quality_ini(user+b'Preset=Quality\n', TEMPLATE))

    def test_new_install_update_and_restore_use_the_same_transaction(self):
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as directory:
            base = Path(directory)
            package = base/'package'
            (package/'payload').mkdir(parents=True)
            files = {'d3d9.dll': b'MZ Northlight renderer 0.3.216; PROXY module=%ls root=%ls',
                     'northlight-quality.ini': TEMPLATE}
            manifest = []
            for name, blob in files.items():
                (package/'payload'/name).write_bytes(blob)
                manifest.append({'path':name, 'sha256':hashlib.sha256(blob).hexdigest(),
                                 **({'preserve':True} if name.endswith('.ini') else {})})
            (package/'payload-manifest.json').write_text(json.dumps(manifest))
            for old in (None, b'[Quality]\nPreset=Quality\nEquipmentLights=0\nCommandStream=0\n'):
                client = base/('fresh' if old is None else 'upgrade')
                client.mkdir()
                (client/'wow.exe').write_bytes(b'untouched fixture')
                quality = client/'northlight-quality.ini'
                if old is not None:
                    quality.write_bytes(old)
                backup = installer.install(client, package, backend='native')
                self.assertIn(b'EquipmentLights=1\nCommandStream=1', quality.read_bytes())
                plan, _, _ = installer.payload_plan(client, package, backend='native')
                self.assertFalse(plan)
                installer.restore(client, backup)
                self.assertEqual(quality.read_bytes() if quality.exists() else None, old)


if __name__ == '__main__':
    unittest.main()
