#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Renderer-owned metadata is bounded, source-pinned and separate from native M2 limits."""
import copy
from collections import Counter
import hashlib
import json
import struct
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import world_emitter_index as index
from light_attachments import REQUIRES_NATIVE
from m2_emitter_patch import append_lights, verify_lights
from test_world_emitter_coverage import fixture
from world_lights_builder import (
    instance_lights, m2_sources, reviewed_native_range, reviewed_placement_sources,
    reviewed_world_range,
)
from outdoor_light_profiles import merge_drak_bonfire


PATH = 'world\\anonymous.m2'


def source():
    return dict(position=[0., 0., 2.], rgb=[.25, .5, 1.], intensity=.175,
                start=1., end=8., emission='energy')


def document(blob, emitters=None):
    return {'version': 1, 'owner': 'northlight-local-direct', 'models': {
        PATH: {'asset_sha256': hashlib.sha256(blob).hexdigest(), 'basis': 'measured',
               'emitters': emitters if emitters is not None else [source()]}}}


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


class WorldEmitterIndex(unittest.TestCase):
    def setUp(self):
        self.assets = fixture()
        self.blob = self.assets.read(PATH)
        self.document = document(self.blob)

    def read(self, value=None, payload=None):
        self.assets.files[index.ARCHIVE_PATH] = payload if payload is not None else encoded(
            self.document if value is None else value)
        return index.Index(self.assets)

    def test_canonical_payload_digest_flags_and_absolute_energy(self):
        value = self.read()
        self.assertEqual(value.origin, 'mpq')
        self.assertEqual(value.payload(), encoded(self.document))
        self.assertEqual(value.digest, hashlib.sha256(value.payload()).hexdigest())
        entry = value.get(PATH, self.blob)
        position, rgb, start, end, identity, kind, flags = index.records(entry)[0]
        self.assertEqual(position, (0., 0., 2.))
        self.assertEqual(rgb, (.25*.175, .5*.175, .175))
        self.assertEqual((start, end, identity, kind, flags), (1., 8., 0x80000000, 3, 0))
        for emission, expected in (('fire', index.CAST_SHADOW|index.FIRE), ('candle', index.CANDLE), ('energy', 0)):
            self.assertEqual(index.flags({'emission': emission}), expected)

    def test_reviewed_fixture_replaces_old_measured_index_without_a_second_source(self):
        value = self.read()
        measured = index.records(self.document['models'][PATH])[0]
        value.catalog[PATH] = dict(source(), family='wotlk_compact_source',
                                   asset_sha256=hashlib.sha256(self.blob).hexdigest(),
                                   intensity=1.15, end=10., casts_shadows=True)
        result = value.get(PATH, self.blob)
        records = index.records(result)
        self.assertEqual(result['basis'], 'reviewed')
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0][4:6], measured[4:6])
        self.assertEqual(records[0][6], index.CAST_SHADOW)
        self.assertEqual((records[0][1][2], records[0][3]), (1.15, 10.))
        self.assertEqual(json.loads(value.payload())['models'][PATH]['basis'], 'reviewed')

        updated = self.read(payload=value.payload())
        updated.catalog[PATH] = dict(value.catalog[PATH], position=[0., 0., 3.], casts_shadows=False)
        refreshed, = index.records(updated.get(PATH, self.blob))
        self.assertEqual(refreshed[0], (0., 0., 3.))
        self.assertEqual(refreshed[4:6], records[0][4:6])
        self.assertEqual(refreshed[6], 0)

    def test_explicit_shadow_intent_survives_compact_build_and_roundtrip(self):
        for emission in ('energy','fire','candle'):
            for shadow in (False,True):
                emitter=dict(source(),emission=emission,casts_shadows=shadow)
                measured={'asset_sha256':hashlib.sha256(self.blob).hexdigest(),'basis':'measured','emitters':[emitter]}
                with patch.object(index,'profile',return_value=measured):payload,_=index.build(self.assets)
                value=self.read(payload=payload);entry=value.get(PATH,self.blob)
                self.assertEqual(entry['emitters'][0]['casts_shadows'],shadow)
                self.assertEqual(bool(index.records(entry)[0][-1]&index.CAST_SHADOW),shadow and emission!='candle')
                self.assertEqual(value.payload(),payload)
        for bad in (0,1,None,'false'):
            value=copy.deepcopy(self.document);value['models'][PATH]['emitters'][0]['casts_shadows']=bad
            with self.assertRaises(ValueError):self.read(value)

    def test_reviewed_mixed_shadow_fixtures_keep_their_energy_and_policies(self):
        sources=[dict(source(),casts_shadows=False),dict(source(),position=[.5,0.,2.],casts_shadows=True)]
        reviewed={'emitters':sources}
        with patch.object(index,'pinned',return_value={hashlib.sha256(self.blob).hexdigest():reviewed}):
            value=index.profile(self.assets,PATH,self.blob,{PATH:{'family':'energy'}})
        self.assertEqual(len(value['emitters']),2)
        for actual,expected in zip(value['emitters'],sources):
            for field in ('position','rgb','intensity','start','end','casts_shadows'):
                self.assertEqual(actual[field],expected[field])

    def test_source_hash_changes_are_refused_without_changing_the_pin(self):
        value = self.read()
        with self.assertRaisesRegex(ValueError, 'hash changed'):
            value.get(PATH, self.blob + b'changed')
        self.assertEqual(value.get(PATH, self.blob), self.document['models'][PATH])

    def test_native_candles_keep_energy_without_shadow_intent(self):
        blob=bytearray(self.blob);struct.pack_into('<I',blob,264,1)
        steady=((0.,0.,2.),(1.,.5,.2),1.,12.,0,2,index.CAST_SHADOW)
        animated=((0.,1.,2.),(1.,.5,.2),1.,12.,1,2,index.CAST_SHADOW|REQUIRES_NATIVE)
        native=[steady,animated]
        value=self.read();value.catalog={}
        for path,candle in (('world\\custom_candle.m2',True),('world\\custom_candelabra.m2',True),
                            ('world\\custom_lantern.m2',False),('world\\anonymous.m2',False)):
            with self.subTest(path=path):
                actual=m2_sources(path,blob,native,value)
                self.assertEqual(actual[0][:-1],steady[:-1])
                self.assertEqual(bool(actual[0][-1]&index.CAST_SHADOW),not candle)
                self.assertEqual(actual[1],animated)
        self.assertEqual(native,[steady,animated])

    def test_authored_range_limit_preserves_world_position_and_falloff_ratio(self):
        authored, = index.records({'emitters': [source()]})
        native = (*authored[:4],0,2,authored[-1])
        small = (bytes(32),1,3,*([0.]*6),1,0,0,0,1,0,0,0,1,5,6,7)
        large = (*small[:9],8,0,0,0,8,0,0,0,8,*small[-3:])
        self.assertEqual(instance_lights([authored],small,PATH,max_world_end=24)[0][6:8],(1.,8.))
        unbounded, native_original = instance_lights([authored,native],large,PATH)
        bounded, native_after = instance_lights([authored,native],large,PATH,max_world_end=24)
        self.assertEqual(bounded[:6],unbounded[:6])
        self.assertEqual(bounded[6:8],(3.,24.))
        self.assertEqual(bounded[8:],unbounded[8:])
        self.assertEqual(native_after,native_original)

    def test_malformed_schema_is_refused(self):
        malformed = [[], None, {}, {'version': 1, 'owner': 'northlight-local-direct'},
                     {**self.document, 'models': []}, {**self.document, 'models': None}]
        for value in (None, [], {}, {'asset_sha256': '0'*64},
                      {**self.document['models'][PATH], 'emitters': None},
                      {**self.document['models'][PATH], 'emitters': {}},
                      {**self.document['models'][PATH], 'emitters': [None]}):
            malformed.append({**self.document, 'models': {PATH: value}})
        for value in malformed:
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.read(payload=json.dumps(value).encode())

    def test_owner_and_version_are_explicit(self):
        for field, invalid in (('version', 0), ('version', 2), ('version', True),
                               ('owner', 'client-native'), ('owner', ''), ('owner', None)):
            with self.subTest(field=field, value=invalid), self.assertRaises(ValueError):
                self.read({**self.document, field: invalid})

    def test_paths_must_be_canonical_world_or_spell_models(self):
        invalid = ('item\\torch.m2', 'creature\\torch.m2', 'world\\torch.mdx',
                   'World\\torch.m2', 'world/torch.m2', 'world\\..\\torch.m2',
                   'spells\\.\\torch.m2', 'world\\\\torch.m2', 'world\\bad/../torch.m2',
                   'world\\bad\x00.m2', 'world\\C:torch.m2', 'world\\.m2')
        for path in invalid:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.read({**self.document, 'models': {path: self.document['models'][PATH]}})

    def test_hash_shape_is_strict(self):
        for digest in ('', '0'*63, '0'*65, 'G'*64, 'A'*64, 0, None):
            value = copy.deepcopy(self.document)
            value['models'][PATH]['asset_sha256'] = digest
            with self.subTest(digest=digest), self.assertRaises(ValueError):
                self.read(value)

    def test_invalid_vectors_energy_and_ranges_are_refused(self):
        invalid = [('position', [0, 1]), ('position', [0, 0, 1024.001]),
                   ('position', [0, 0, float('nan')]), ('position', [0, 0, float('inf')]),
                   ('rgb', [0, 0]), ('rgb', [0, 0, 0]), ('rgb', [-.001, .5, 1]),
                   ('rgb', [0, .5, 1.001]), ('intensity', 0), ('intensity', -.1),
                   ('intensity', 4.001), ('intensity', float('inf')),
                   ('intensity', '1'), ('intensity', True), ('start', -1),
                   ('start', 8), ('end', 1), ('end', 32.001), ('emission', 'white')]
        for field, invalid_value in invalid:
            value = copy.deepcopy(self.document)
            value['models'][PATH]['emitters'][0][field] = invalid_value
            with self.subTest(field=field, value=invalid_value), self.assertRaises(ValueError):
                self.read(payload=json.dumps(value).encode())

    def test_metadata_accepts_64_sources_and_1024_anchors_without_weakening_native_limits(self):
        sources = [source() for _ in range(64)]
        sources[-1]['position'] = [-1024., 1024., 1024.]
        value = self.read(document(self.blob, sources))
        self.assertEqual(len(value.get(PATH, self.blob)['emitters']), 64)
        for emitters in ([], sources + [source()]):
            with self.subTest(count=len(emitters)), self.assertRaises(ValueError):
                self.read(document(self.blob, emitters))
        native = [source() for _ in range(16)]
        native[-1]['position'] = [100., -100., 100.]
        patched = append_lights(self.blob, native)
        verify_lights(self.blob, patched, native)
        for emitters in (native + [source()], [dict(source(), position=[100.001, 0, 0])], sources):
            with self.subTest(native_count=len(emitters)), self.assertRaises(ValueError):
                append_lights(self.blob, emitters)

    def test_metadata_byte_and_model_count_limits(self):
        with self.assertRaisesRegex(ValueError, 'size'):
            self.read(payload=b' '*(32*1024*1024+1))
        with self.assertRaisesRegex(ValueError, 'count'):
            self.read({**self.document, 'models': dict.fromkeys('world\\%d.m2' % i for i in range(20001))})

    def test_build_is_deterministic_compact_and_does_not_scan_unreferenced_spells(self):
        self.assets.files.update({'spells\\referenced.m2': self.blob, 'spells\\unused.m2': self.blob,
                                  'creature\\unused.m2': self.blob, 'world\\z.m2': self.blob})
        self.assets.providers = dict.fromkeys(reversed(self.assets.files))
        measured = {'asset_sha256': hashlib.sha256(self.blob).hexdigest(), 'basis': 'measured',
                    'partial': False, 'emitters': [dict(source(), evidence=['private audit'], raw_intensity=2)]}
        with patch.object(index, 'profile', return_value=measured) as profile:
            payload, counts = index.build(self.assets, ['spells\\referenced.m2', 'spells\\..\\bad.m2'])
            called = [call.args[1] for call in profile.call_args_list]
        self.assertEqual(called, ['spells\\referenced.m2', PATH, 'world\\z.m2'])
        self.assertEqual(counts, {'scanned': 3, 'measured': 3, 'emitters': 3})
        self.assertEqual(payload, encoded(json.loads(payload)))
        self.assertEqual(set(json.loads(payload)['models'][PATH]['emitters'][0]),
                         {'position', 'rgb', 'intensity', 'start', 'end', 'emission'})
        self.assets.providers = dict.fromkeys(sorted(self.assets.files))
        with patch.object(index, 'profile', return_value=measured):
            self.assertEqual(index.build(self.assets, ['spells\\referenced.m2'])[0], payload)

    def test_only_referenced_missing_spells_are_discovered_lazily(self):
        value = self.read()
        spell = 'spells\\referenced.m2'
        measured = {'asset_sha256': hashlib.sha256(self.blob).hexdigest(), 'basis': 'measured',
                    'emitters': [dict(source(), evidence=['private audit'])]}
        with patch.object(index, 'profile', return_value=measured) as discover:
            self.assertIsNone(value.get('world\\missing.m2', self.blob))
            self.assertIsNone(value.get('item\\missing.m2', self.blob))
            discover.assert_not_called()
            self.assertEqual(value.get(spell, self.blob), measured)
            self.assertEqual(value.get(spell, self.blob), measured)
            self.assertEqual(discover.call_count, 1)
        payload = value.payload()
        self.assertEqual(set(json.loads(payload)['models']), {PATH, spell})
        self.assertNotIn(b'evidence', payload)
        self.assertEqual(value.digest, hashlib.sha256(payload).hexdigest())
        self.assertEqual(self.read(payload=payload).payload(), payload)
        with self.assertRaises(ValueError):
            value.get(spell, self.blob+b'changed')

    def test_absent_mpq_metadata_is_generated_without_native_model_mutation(self):
        original = dict(self.assets.files)
        value = index.Index(self.assets)
        self.assertEqual(value.origin, 'generated-for-mpq')
        self.assertEqual(set(value.models), {PATH})
        self.assertEqual(value.payload(), index.build(self.assets)[0])
        self.assertEqual(self.assets.files, original)


class ReviewedPlacementTests(unittest.TestCase):
    def test_native_range_preserves_identity_and_shadow_flags(self):
        base=(1,2,3,1,.5,.1,1,12,15,2,0)
        shadowed=(*base[:-2],2,2)
        attached=(*base[:-2],2,2|REQUIRES_NATIVE)
        stats=Counter()
        reviewed=('a'*64,4.5)
        result=reviewed_native_range([base,shadowed,attached],reviewed,'a'*64,stats)
        self.assertEqual(result,[(*base[:7],4.5,*base[8:]),shadowed,attached])
        self.assertEqual(stats['reviewed_native_ranges_applied'],1)
        self.assertEqual(reviewed_native_range([shadowed,attached],reviewed,'a'*64,stats),[shadowed,attached])
        self.assertEqual(stats['reviewed_native_ranges_no_match'],1)
        self.assertEqual(reviewed_native_range([base],reviewed,'b'*64,stats),[base])

    def test_wmo_omission_checks_scope_and_source_kind(self):
        model='world\\fixture.wmo'
        stock='a'*64
        molt=((0,0,1),(1,1,1),1,8,0,1,3)
        authored=(*molt[:5],3,2)
        other=(*molt[:4],1,1,3)
        sources=[molt,authored,other]
        omissions={('Map',model,42,stock):{0}}
        stats=Counter()
        self.assertEqual(reviewed_placement_sources(sources,'Map',model,42,stock,omissions,stats),[authored,other])
        self.assertEqual(stats['reviewed_wmo_points_omitted'],1)
        self.assertEqual(reviewed_placement_sources(sources,'Map',model,43,stock,omissions,stats),sources)
        self.assertEqual(reviewed_placement_sources(sources,'Map',model,42,'b'*64,omissions,stats),sources)

    def test_world_cap_covers_reviewed_variants_only(self):
        profile={'asset_sha256':'a'*64,'max_world_end':12,'variants':{'b'*64:{}}}
        self.assertEqual(reviewed_world_range(profile,'a'*64),12)
        self.assertEqual(reviewed_world_range(profile,'b'*64),12)
        self.assertIsNone(reviewed_world_range(profile,'c'*64))

    def test_bonfire_merge_keeps_one_pulsed_shadow_origin(self):
        provenance={
            r'world\exteriordesigners\zuldrak\ed_zuldrakbridge_fire.m2':{
                'asset_sha256':'7ee708d3d9b4393b65f39fcfa4cb0682219516ae106c5c3d90c8394169844ad1'},
            r'world\expansion01\doodads\generic\draenei\glow\dr_glow.m2':{
                'asset_sha256':'fa3ed7e3632bd04b5936e472023577dd0d234d9d136bb73a8a3191b2c393fa31'},
        }
        proxy=[(0,0,2,i+1,.5,1,1,10,i,3,0) for i in range(9)]
        flame=(0,0,0,.002,.001,.001,1,12,9,2,2)
        other=(4,4,0,.6,.5,.2,1,12,10,2,2)
        owners={i:(3,9480539010499056 if i<9 else 9480539010499055 if i==9 else 1) for i in range(11)}
        records=proxy+[flame,other]
        self.assertEqual(merge_drak_bonfire('OtherMap',records,owners,provenance),records)
        self.assertEqual(merge_drak_bonfire('DrakTheronKeep',records,owners,{}),records)
        bright=proxy+[(*flame[:3],.5,*flame[4:]),other]
        self.assertEqual(merge_drak_bonfire('DrakTheronKeep',bright,owners,provenance),bright)
        merged=merge_drak_bonfire('DrakTheronKeep',records,owners,provenance)
        self.assertEqual(merged,[other,(0,0,2,1.70,.48,1.92,.7,15.,8,3,index.CAST_SHADOW|index.FIRE)])
        self.assertEqual(merge_drak_bonfire('DrakTheronKeep',merged,owners,provenance),merged)


if __name__ == '__main__':
    unittest.main()
