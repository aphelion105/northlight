#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Independent native-track evaluation, strict emission selection and M2/MPQ preservation."""
from pathlib import Path
import hashlib
import json
import math
import struct
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
from build_spell_emitters import append_spell_lights, build, effect_models, lifecycle_track, spell_sources


def append(blob, data):
    offset = len(blob)
    blob.extend(data)
    return offset


def raw_track(blob, curves, fmt='<f', interpolation=1, sequence=-1):
    times, keys = [], []
    for stamps, values in curves:
        times.append((len(stamps), append(blob, struct.pack('<%dI' % len(stamps), *stamps))))
        keys.append((len(values), append(blob, b''.join(struct.pack(fmt, v) for v in values))))
    ot = append(blob, b''.join(struct.pack('<II', *v) for v in times))
    ok = append(blob, b''.join(struct.pack('<II', *v) for v in keys))
    return struct.pack('<hh4I', interpolation, sequence, len(times), ot, len(keys), ok)


def model(rate=None, enabled=None, sequence=-1):
    blob = bytearray(512)
    blob[:4] = b'MD20'
    struct.pack_into('<I', blob, 4, 264)
    struct.pack_into('<II', blob, 44, 1, 304)
    struct.pack_into('<iIh', blob, 304, -1, 0x200, -1)
    global_offset = append(blob, struct.pack('<I', 1000))
    struct.pack_into('<II', blob, 20, 1, global_offset)
    particle = append(blob, bytes(476))
    struct.pack_into('<II', blob, 296, 1, particle)
    struct.pack_into('<3f', blob, particle+8, 1, 2, 3)
    struct.pack_into('<BBh', blob, particle+40, 4, 1, -1)
    name = b'particles\\real-blue.blp\0'
    name_offset = append(blob, name)
    texture = append(blob, struct.pack('<4I', 0, 0, len(name), name_offset))
    struct.pack_into('<II', blob, 80, 1, texture)
    blob[particle+176:particle+196] = raw_track(blob, rate or [([0, 100, 200, 300], [0, 10, 10, 0])], sequence=sequence)
    blob[particle+456:particle+476] = raw_track(blob, enabled or [([0, 150, 250], [1, 0, 1])], '<B', interpolation=0, sequence=sequence)
    stamps = append(blob, struct.pack('<H', 0))
    colour = append(blob, struct.pack('<3f', 255, 255, 255))
    alpha = append(blob, struct.pack('<h', 32767))
    struct.pack_into('<4I', blob, particle+260, 1, stamps, 1, colour)
    struct.pack_into('<4I', blob, particle+276, 1, stamps, 1, alpha)
    return bytes(blob), particle


def native_sample(blob, offset, fmt, time, animation=0, default=0):
    """12340 FindKey/M2AnimateTrack semantics, without production parser/writer."""
    mode, loop, nt, ot, nk, ok = struct.unpack_from('<hh4I', blob, offset)
    if not nt or not nk:
        return default
    slot = animation if animation < nk else 0
    key_count, key_offset = struct.unpack_from('<II', blob, ok+slot*8)
    if not key_count:
        return default
    if loop >= 0:
        globals_offset = struct.unpack_from('<I', blob, 24)[0]
        time %= struct.unpack_from('<I', blob, globals_offset+loop*4)[0]
        slot = 0
    count, times_offset = struct.unpack_from('<II', blob, ot+slot*8)
    stamps = struct.unpack_from('<%dI' % count, blob, times_offset)
    index = max((i for i,t in enumerate(stamps) if t <= time), default=0)
    size = struct.calcsize(fmt)
    a = struct.unpack_from(fmt, blob, key_offset+index*size)[0]
    if mode == 0 or index == count-1 or fmt == '<B':
        return a
    b = struct.unpack_from(fmt, blob, key_offset+(index+1)*size)[0]
    return a + (b-a)*(time-stamps[index])/(stamps[index+1]-stamps[index])


class Assets:
    def __init__(self, blob, extra=None):
        self.blob, self.extra = blob, extra or {}
    def read(self, path):
        if path in self.extra:
            return self.extra[path]
        if path.endswith('.m2'):
            return self.blob
        if not path.endswith('.blp'):
            raise FileNotFoundError(path)
        blp = bytearray(152)
        blp[:4] = b'BLP2'
        struct.pack_into('<4BII', blp, 8, 3, 8, 0, 0, 1, 1)
        struct.pack_into('<I', blp, 20, 148)
        struct.pack_into('<I', blp, 84, 4)
        blp[148:] = bytes((255, 0, 0, 255))  # BGRA: blue.
        return bytes(blp)
    def fingerprint(self):
        return {'test': True}
    def origin(self, path):
        return 'synthetic.mpq'


class SpellPatch(unittest.TestCase):
    def test_native_time_and_enable_semantics(self):
        for sequence, curves in ((-1, [([0,100,200,300], [0,10,10,0]), ([], [])]), (0, [([0,100,200,300], [0,10,10,0])])):
            original, particle = model(rate=curves, sequence=sequence)
            emitters, _ = spell_sources(Assets(original), original)
            patched = append_spell_lights(original, emitters)
            _, light = struct.unpack_from('<II', patched, 264)
            self.assertEqual(emitters[0]['rgb'], (0,0,1))
            for animation in (0,1,8):
                for time in (0, 50, 100, 149, 150, 199, 200, 249, 250, 275, 300, 999, 1000, 1150):
                    rate = native_sample(original, particle+176, '<f', time, animation)
                    enabled = native_sample(original, particle+456, '<B', time, animation, 1)
                    intensity = native_sample(patched, light+76, '<f', time, animation)
                    visible = native_sample(patched, light+136, '<B', time, animation, 1)
                    self.assertEqual(enabled, visible)
                    self.assertAlmostEqual(intensity, rate/10*.35, places=6)
                    self.assertAlmostEqual(visible*intensity, enabled*rate/10*.35, places=6)
            # Native AnimateMT only commits intensity * RGB in this branch.
            # Verify it still runs after the initial frame, including rate=0.
            _, _, nt, ot, _, _ = struct.unpack_from('<hh4I', patched, light+56)
            color_keys = struct.unpack_from('<I', patched, ot)[0]
            self.assertTrue(nt > 1 or color_keys > 1)
            previous = None
            for frame, time in enumerate((100, 275, 300)):
                if nt > 1 or color_keys > bool(frame):
                    previous = native_sample(patched, light+76, '<f', time)
                self.assertAlmostEqual(previous, native_sample(original, particle+176, '<f', time)/10*.35, places=6)
            preserved = bytearray(patched[:len(original)])
            for a,b in ((20,28), (264,272)):
                preserved[a:b] = original[a:b]
            self.assertEqual(preserved, original)

    def test_strict_tracks_and_materials(self):
        original, particle = model()
        for field, fmt, value in ((40, '<B', 2), (42, '<h', 11), (4, '<I', 0x8000), (176, '<h', 2)):
            bad = bytearray(original)
            struct.pack_into(fmt, bad, particle+field, value)
            with self.assertRaisesRegex(ValueError, 'No proven additive'):
                spell_sources(Assets(bad), bad)
        for curves in ([([0,100],[0,0])], [([0,100],[10,-1])], [([0,100],[10,float('nan')])]):
            bad, _ = model(rate=curves)
            with self.assertRaises(ValueError):
                spell_sources(Assets(bad), bad)
        bad = bytearray(original)
        struct.pack_into('<I', bad, 264, 1)
        with self.assertRaisesRegex(ValueError, 'Existing native'):
            spell_sources(Assets(bad), bad)
        with self.assertRaises(ValueError):
            lifecycle_track(original[:particle+180], particle+176, '<f', 0.)

    def test_dbc_read_only_scope_and_stormlib_roundtrip(self):
        original, _ = model()
        strings = b'\0Spells\\actual.mdx\0Spells\\owned.mdx\0Creature\\mount.mdx\0'
        rows = b''.join(struct.pack('<7I', i, 0, strings.index(name), 0, 0, 0, 0) for i,name in enumerate((b'Spells\\actual',b'Spells\\owned',b'Creature\\mount'), 1))
        dbc = struct.pack('<4s4I', b'WDBC', 3, 7, 28, len(strings)) + rows + strings
        assets = Assets(original, {'dbfilesclient\\spellvisualeffectname.dbc': dbc})
        self.assertEqual(effect_models(assets), {'spells\\actual.m2': [1], 'spells\\owned.m2': [2]})
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            result = build(assets, Path(folder), {'spells\\owned.m2'})
            self.assertEqual(list(result['models']), ['spells\\actual.m2'])
            self.assertEqual(result['counts']['mpq_byte_roundtrips'], 1)
            self.assertEqual(result['counts']['dbc_files'], 0)
            self.assertEqual(assets.extra['dbfilesclient\\spellvisualeffectname.dbc'], dbc)


def audit_stage(folder):
    """Read every real client model and independently sample its staged lifecycle."""
    from world_scene_builder import Assets as ClientAssets
    from mpq import Archive
    folder = Path(folder)
    report = json.loads((folder/'spell-lights-report.json').read_text())
    assets, samples, zero = ClientAssets(), 0, 0
    try:
        with Archive(folder/'spell-lights-staged.mpq') as mpq:
            for path, model_report in report['models'].items():
                before, after = assets.read(path), mpq.read(path)
                assert hashlib.sha256(before).hexdigest() == model_report['before_sha256'], path
                assert hashlib.sha256(after).hexdigest() == model_report['after_sha256'], path
                particles = struct.unpack_from('<I', before, 300)[0]
                stride = 492 if struct.unpack_from('<I', before, 16)[0] & 512 else 476
                lights = struct.unpack_from('<I', after, 268)[0]
                for index, source in enumerate(model_report['emitters']):
                    particle, light = particles+source['particles'][0]*stride, lights+index*156
                    animations = set(range(max(len(source['rate_track']['curves']), len(source['enable_track']['curves'])))) | {999}
                    times = {0, 1, 2, 1000000}
                    for name in ('rate_track', 'enable_track'):
                        for curve in source[name]['curves']:
                            for t in curve['times']:
                                times |= {max(0,t-1), t, t+1}
                            times |= {(a+b)//2 for a,b in zip(curve['times'],curve['times'][1:])}
                    for animation in animations:
                        for time in times:
                            rate = native_sample(before, particle+176, '<f', time, animation)
                            enabled = native_sample(before, particle+456, '<B', time, animation, 1)
                            intensity = native_sample(after, light+76, '<f', time, animation)
                            visible = native_sample(after, light+136, '<B', time, animation, 1)
                            assert enabled == visible, (path, animation, time, 'enabled')
                            assert math.isclose(intensity, rate/source['rate']*source['intensity'], rel_tol=1e-5, abs_tol=1e-6), (path, animation, time, 'rate')
                            samples += 1
                            zero += intensity == 0 or visible == 0
    finally:
        assets.close()
    result = {'models': len(report['models']), 'independent_lifecycle_samples': samples,
              'zero_or_invisible_samples': zero, 'passed': True}
    (folder/'semantic-audit.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--audit-stage':
        audit_stage(sys.argv[2])
    else:
        unittest.main()
