#!/usr/bin/env python3
# northlight-test:
"""Bound custom BLP tables and reject mismatched M2/SKIN without archive rewrites."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import blp_mips,decode_blp,skin_vertex_indices
from build_spell_asset_manifest import texture_manifest
import emitter_pin_report
import world_scene_builder as scene
from collections import Counter


def bc_texture(width=64,height=32,padding=0):
    data=bytearray(148);data[:4]=b'BLP2';struct.pack_into('<I',data,4,1)
    struct.pack_into('<4B2I',data,8,2,0,0,1,width,height)
    for level in range(max(width,height).bit_length()):
        w,h=max(1,width>>level),max(1,height>>level)
        block=struct.pack('<HHI',0xf800,0x001f,0)
        raw=block*(((w+3)//4)*((h+3)//4))+bytes(padding)
        struct.pack_into('<I',data,20+4*level,len(data));struct.pack_into('<I',data,84+4*level,len(raw));data.extend(raw)
    return data


class Assets:
    def __init__(self,blob):self.blob=blob;self.providers={'world\\lamp.m2':0}
    def read(self,path):return self.blob
    def origin(self,path):return 'memory/custom.mpq'
    def fingerprint(self):return {}


class CustomInputs(unittest.TestCase):
    def test_bc_rows_keep_every_colour_alpha_and_partial_edge(self):
        for depth, encoding in ((0, 0), (1, 0), (8, 1), (8, 7)):
            for width, height in ((1, 1), (3, 7), (7, 5), (8, 8)):
                raw = bytearray()
                expected = bytearray(width * height * 4)
                for by in range((height + 3) // 4):
                    for bx in range((width + 3) // 4):
                        reverse = (bx + by) % 2
                        ca, cb = (0x001f, 0xf800) if reverse else (0xf800, 0x001f)
                        a, b = ((0, 0, 255), (255, 0, 0)) if reverse else ((255, 0, 0), (0, 0, 255))
                        if depth in (0, 1) and reverse:
                            colours = [(*a, 255), (*b, 255), (127, 0, 127, 255), (0, 0, 0, 0 if depth else 255)]
                        else:
                            colours = [(*a, 255), (*b, 255),
                                       (*( (2*x+y)//3 for x, y in zip(a, b)), 255),
                                       (*( (x+2*y)//3 for x, y in zip(a, b)), 255)]
                        alpha = [i * 17 for i in range(16)]
                        if depth == 8 and encoding != 1:
                            endpoints = [10, 240] if reverse else [240, 10]
                            n = 5 if reverse else 7
                            table = endpoints + [((n-i)*endpoints[0]+i*endpoints[1])//n for i in range(1, n)]
                            if reverse: table += [0, 255]
                            alpha = [table[i % 8] for i in range(16)]
                            raw.extend(bytes(endpoints) + sum((i % 8) << (3*i) for i in range(16)).to_bytes(6, 'little'))
                        elif depth == 8:
                            raw.extend(sum(i << (4*i) for i in range(16)).to_bytes(8, 'little'))
                        raw.extend(struct.pack('<HHI', ca, cb, sum((i % 4) << (2*i) for i in range(16))))
                        for i in range(16):
                            x, y = bx*4+i%4, by*4+i//4
                            if x < width and y < height:
                                color = colours[i % 4]
                                if depth == 8: color = (*color[:3], alpha[i])
                                expected[(y*width+x)*4:(y*width+x+1)*4] = bytes(color)
                blob = bytearray(148)
                blob[:4] = b'BLP2'
                struct.pack_into('<4B2I', blob, 8, 2, depth, encoding, 1, width, height)
                struct.pack_into('<I', blob, 20, 148)
                struct.pack_into('<I', blob, 84, len(raw))
                self.assertEqual(decode_blp(blob + raw), (width, height, bytes(expected)))

    def test_palette_channels_and_all_alpha_depths(self):
        palette = bytes(v for i in range(256) for v in (i, 255-i, i//2, 77))
        indices = bytes((255, 0, 128, 17, 1, 192, 54, 7, 11))
        for depth in (0, 1, 4, 8):
            alpha = [255 if depth == 0 else (i % (1 << depth)) * (255 // ((1 << depth)-1)) for i in range(9)]
            packed = sum((i % (1 << depth)) << (i*depth) for i in range(9)).to_bytes((9*depth+7)//8, 'little') if depth else b''
            raw = indices + packed
            blob = bytearray(148) + palette
            blob[:4] = b'BLP2'
            struct.pack_into('<4B2I', blob, 8, 1, depth, 0, 1, 3, 3)
            struct.pack_into('<I', blob, 20, 1172)
            struct.pack_into('<I', blob, 84, len(raw))
            expected = bytes(v for i, index in enumerate(indices) for v in (index//2, 255-index, index, alpha[i]))
            self.assertEqual(decode_blp(blob + raw), (3, 3, expected))

    def test_flat_texture_colour_is_exact_across_tile_resets(self):
        builder = scene.Builder(type('Assets', (), {'unsupported': set()})())
        pixels = bytes(range(256)) * 4
        builder.textures['terrain'] = (16, 16, pixels)
        expected = tuple(sum(scene.srgb_to_linear(v/255) for v in pixels[c::4])/256 for c in range(3))
        first = builder.material('terrain', flat=True)
        self.assertEqual(builder.materials[first][0], expected)
        builder.reset()
        with patch.object(scene, 'SRGB8_TO_LINEAR', None):
            again = builder.material('terrain', flat=True)
        self.assertEqual(builder.materials[again], (expected, 0, 0, 0., b''))

    def test_object_space_export_preserves_normalization_and_signed_zero(self):
        builder = scene.Builder(type('Assets', (), {'stats': Counter()})())
        vertices = [(-0., 2., -3., -0., 3., 4., .2, .7), (1., 0., 2., 0., 0., 0., 0., 1.),
                    (1., 2., 3., 2., -3., 7., 1., 0.)]
        matrix = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
        builder.add_mesh((vertices, [(0, 1, 2, 0)], [('', 0, False)]), matrix, (0, 0, 0), 'model')
        for source, actual in zip(vertices, builder.vertices):
            expected = tuple(v+0 for v in scene.matvec(matrix, source[:3])) + scene.normalize(scene.matvec(matrix, source[3:6])) + source[6:8]
            self.assertEqual(struct.pack('<8f', *actual), struct.pack('<8f', *expected))

    def test_spare_mip_slots_are_ignored_without_changing_the_payload(self):
        for width,height in ((1,1),(17,3),(64,32),(1024,1024)):
            blob=bc_texture(width,height);levels=max(width,height).bit_length()
            for level in range(levels,16):
                struct.pack_into('<I',blob,20+4*level,0xfffffff0)
                struct.pack_into('<I',blob,84+4*level,0xfffffff0)
            before=bytes(blob);mips=blp_mips(blob)
            self.assertEqual(len(mips),levels)
            w,h,rgba=decode_blp(blob,1);self.assertEqual((w,h,rgba),(1,1,bytes((255,0,0,255))))
            manifest=texture_manifest(Assets(blob),'custom.blp')
            self.assertEqual(len(manifest['mips']),levels)
            self.assertEqual(manifest['sha256'],hashlib.sha256(before).hexdigest())
            self.assertEqual(bytes(blob),before)

    def test_padding_is_not_uploaded_as_extra_blocks(self):
        blob=bc_texture(8,4,padding=16);manifest=texture_manifest(Assets(blob),'custom.blp')
        for level,w,h,offset,size in blp_mips(blob):
            self.assertEqual(size,((w+3)//4)*((h+3)//4)*8)
            self.assertEqual(manifest['mips'][level]['raw_sha256'],hashlib.sha256(blob[offset:offset+size]).hexdigest())

    def test_real_mip_corruption_still_fails(self):
        for field,value in ((12,0),(20,100),(20,0xfffffff0),(84,1),(24,0xfffffff0)):
            blob=bc_texture();struct.pack_into('<I',blob,field,value)
            with self.assertRaises(ValueError):decode_blp(blob)
        for length in (0,20,147):
            with self.assertRaises(ValueError):blp_mips(bc_texture()[:length])

    def test_partial_chain_with_repeated_eof_offsets(self):
        for size in (0,8,0xfffffff0):
            blob=bc_texture(8,4);last=blp_mips(blob)[-1];del blob[last[3]:]
            struct.pack_into('<I',blob,20+4*last[0],len(blob));struct.pack_into('<I',blob,84+4*last[0],size)
            self.assertEqual(len(blp_mips(blob)),3)
            w,h,rgba=decode_blp(blob,1);self.assertEqual((w,h),(2,1));self.assertEqual(rgba,bytes((255,0,0,255))*2)
        blob=bc_texture();struct.pack_into('<I',blob,88,0)
        with self.assertRaisesRegex(ValueError,'Noncontiguous'):blp_mips(blob)

    def test_palette_alpha_extent_and_uncompressed_mips(self):
        blob=bytearray(1172);blob[:4]=b'BLP2';struct.pack_into('<4B2I',blob,8,1,4,0,1,3,1)
        blob[152:156]=bytes((10,20,30,0));struct.pack_into('<I',blob,20,1172);struct.pack_into('<I',blob,84,5)
        blob.extend(bytes((1,1,1,0x80,0x0f)))
        self.assertEqual(decode_blp(blob),(3,1,bytes((30,20,10,0,30,20,10,136,30,20,10,255))))
        struct.pack_into('<I',blob,84,4)
        with self.assertRaises(ValueError):decode_blp(blob)
        blob=bytearray(148);blob[:4]=b'BLP2';struct.pack_into('<4B2I',blob,8,3,8,0,1,1,1)
        struct.pack_into('<I',blob,20,148);struct.pack_into('<I',blob,84,4);blob.extend(bytes((10,20,30,40)))
        self.assertEqual(decode_blp(blob),(1,1,bytes((30,20,10,40))))

    def test_sub_byte_alpha_tail_preserves_the_last_complete_level(self):
        for bits,last_width in ((1,4),(4,2)):
            blob=bytearray(1172);blob[:4]=b'BLP2';blob[148:152]=bytes((0,0,255,0))
            struct.pack_into('<4B2I',blob,8,1,bits,0,1,32,16)
            for level in range(6):
                w,h=max(1,32>>level),max(1,16>>level);pixels=w*h
                raw=bytes(pixels)+bytes([255])*(pixels*bits//8)
                struct.pack_into('<I',blob,20+4*level,len(blob));struct.pack_into('<I',blob,84+4*level,len(raw));blob.extend(raw)
            self.assertEqual(blp_mips(blob)[-1][1],last_width)
            w,h,rgba=decode_blp(blob,1)
            self.assertEqual(w,last_width);self.assertEqual(rgba,bytes((255,0,0,255))*(w*h))

    def test_rectangular_bc_tail_does_not_invent_missing_blocks(self):
        blob=bc_texture(768,128);mip=blp_mips(blob)[6]
        struct.pack_into('<I',blob,84+4*mip[0],8)
        self.assertEqual(len(blp_mips(blob)),6)
        w,h,rgba=decode_blp(blob,1)
        self.assertEqual((w,h),(24,4));self.assertEqual(rgba,bytes((255,0,0,255))*(w*h))

    def test_skin_lookup_is_bounded_by_the_winning_model(self):
        model=bytearray(64);struct.pack_into('<I',model,60,3)
        skin=bytearray(44);skin[:4]=b'SKIN';struct.pack_into('<4I',skin,4,3,44,3,50)
        skin.extend(struct.pack('<6H',2,0,1,0,1,2))
        self.assertEqual(skin_vertex_indices(model,skin),[2,0,1])
        for offset in (44,50):
            bad=bytearray(skin);struct.pack_into('<H',bad,offset,3)
            with self.assertRaises(ValueError):skin_vertex_indices(model,bad)

    def test_one_incompatible_skin_does_not_abort_the_emitter_report(self):
        assets=Assets(b'model');path='world\\lamp.m2'
        profile={'asset_sha256':hashlib.sha256(assets.blob).hexdigest(),'position':[0,0,1],
                 'rgb':[1,1,1],'intensity':1,'start':0,'end':4}
        with patch.object(emitter_pin_report,'batches',side_effect=ValueError('SKIN vertex lookup exceeds winning M2')):
            record=emitter_pin_report.report(assets,{path:profile},with_batches=True)['profiles'][path]
        self.assertEqual(record['status'],'matched')
        self.assertIn('geometry_error',record);self.assertNotIn('batches',record)


if __name__=='__main__':unittest.main()
