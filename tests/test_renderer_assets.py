#!/usr/bin/env python3
"""External resources preserve every generated array and reject mismatched packages."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import ast
import hashlib
import re
import struct
import zlib
import build_renderer_assets as assets

out = fp.output_dir()
assert assets.built_path(out/'frd9.dll') == out/assets.FILENAME
assert assets.built_path(out/'frd9-dev-tuning.dll') != assets.built_path(out/'frd9.dll')
assert assets.built_path(out/'frd9-profile.dll') != assets.built_path(out/'frd9.dll')
manifest = assets.prepare(out/'first')
again = assets.prepare(out/'second')
packed = (out/'first'/assets.FILENAME).read_bytes()
assert manifest == again and packed == (out/'second'/assets.FILENAME).read_bytes()
raw = zlib.decompress(packed[8:])
assert len(raw) == manifest['raw_bytes'] == struct.unpack_from('<I', packed, 4)[0]
for entry in manifest['entries']:
    source = (fp.GENERATED/entry['header']).read_text()
    initializer = re.search(r'\b'+entry['name']+r'\[[^\]]*\]\s*=\s*\{([^}]+)\}', source, re.S)[1]
    words = ast.literal_eval('['+initializer+']')
    oracle = struct.pack('<' + ('B' if entry['type'] == 'unsigned char' else 'I') * len(words), *words)
    assert raw[entry['offset']:entry['offset']+entry['bytes']] == oracle, entry['name']
    assert entry['offset'] % 4 == 0
    assert entry['sha256'] == hashlib.sha256(oracle).hexdigest()
assert packed[8:] != raw and len(packed) < len(raw) // 2
dll = b'MZ\0' + assets.MARKER + manifest['sha256'].encode() + b'\0'
assert assets.verify_pair(dll, packed) == manifest['sha256']
for bad_dll, bad_pack in [(b'MZ', packed), (dll, packed[:-1]), (dll, packed+b'extra'),
                           (dll, packed[:10]+bytes([packed[10]^1])+packed[11:])]:
    try:
        assets.verify_pair(bad_dll, bad_pack)
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted damaged or mismatched resource pair')
print(f'PASS {len(manifest["entries"])} arrays: exact original bytes, aligned offsets, deterministic pack, corruption controls')
