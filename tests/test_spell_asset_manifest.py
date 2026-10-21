#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=stormlib
"""Independent DBC graph fixtures and exact UNORM/FNV arithmetic."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
from build_spell_asset_manifest import fnv1a64, resolve_spell, rgba_moments


def table(rows, strings=b'\0'):
    fields = len(rows[0])
    return struct.pack('<4s4I', b'WDBC', len(rows), fields, fields*4, len(strings)) + b''.join(struct.pack('<%dI' % fields, *r) for r in rows) + strings


class Assets:
    def __init__(self):
        spell, visual, kit, effect, attach = [0]*234, [0]*32, [0]*38, [0]*7, [0]*10
        spell[0], spell[131], spell[136] = 14177, 4371, 1
        visual[0], visual[4] = 4371, 3771
        kit[0], kit[7] = 3771, 121
        effect[0], effect[1], effect[2] = 121, 1, 6
        attach[0], attach[1], attach[2], attach[3] = 999, 3771, 121, 37
        self.blobs = {'spell': table([spell], b'\0Cold Blood\0'), 'spellvisual': table([visual]),
                      'spellvisualkit': table([kit]), 'spellvisualeffectname': table([effect], b'\0ice!\0Spells\\Actual.MDX\0'),
                      'spellvisualkitmodelattach': table([attach])}
    def read(self, path):
        return self.blobs[Path(path.replace('\\', '/')).stem]
    def origin(self, path):
        return 'synthetic'


class ManifestTest(unittest.TestCase):
    def test_column_graph_and_attachment_are_not_bone_indices(self):
        spell, metadata = resolve_spell(Assets(), 14177)
        self.assertEqual(spell['name'], 'cold blood')
        self.assertEqual(len(metadata), 5)
        hand, attach = spell['graph']
        self.assertEqual((hand['visual_id'], hand['stage'], hand['kit_id'], hand['effect_id']), (4371, 'state', 3771, 121))
        self.assertEqual((hand['slot'], hand['attachment_id'], hand['model']), ('rightHand', 22, 'spells\\actual.m2'))
        self.assertEqual((attach['row_id'], attach['attachment_id']), (999, 37))

    def test_schema_drift_fails(self):
        assets = Assets()
        assets.blobs['spell'] = table([[0]*233])
        with self.assertRaisesRegex(ValueError, 'schema'):
            resolve_spell(assets, 14177)

    def test_fnv_standard_vectors_and_alpha_correlation(self):
        self.assertEqual(fnv1a64(b''), 'cbf29ce484222325')
        self.assertEqual(fnv1a64(b'hello'), 'a430d84680aabd0b')
        moments = rgba_moments(bytes((255,0,0,0, 0,0,255,255)))
        self.assertEqual(moments, {'rgb_mean': [.5,0.,.5], 'rgb_alpha_mean': [0.,0.,.5], 'alpha_mean': .5})
        for bad in (b'', b'123'):
            with self.assertRaises(ValueError):
                rgba_moments(bad)


if __name__ == '__main__':
    unittest.main()
