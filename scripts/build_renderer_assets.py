"""Pack validated generated arrays; production headers keep their existing array API."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

FILENAME = 'northlight-assets.bin'
MARKER = b'NORTHLIGHT_ASSETS_V1:'
ARRAY = re.compile(r'(?:alignas\(4\)\s*)?(?:static|inline)\s+(?:const|constexpr)\s+(DWORD|std::uint32_t|unsigned char)\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{([^}]+)\};', re.S)


def built_path(dll):
    dll = Path(dll)
    return dll.with_name(FILENAME) if dll.name == 'frd9.dll' else dll.with_suffix('.assets.bin')


def prepare(output, generated=fp.GENERATED):
    """Canonical headers remain the oracle; only this build's include directory is replaced."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    raw, offsets, headers, entries = bytearray(), {}, {}, []
    sources = sorted({*generated.glob('*compiled_shaders.h'), generated/'weather_texture_data.h',
                      generated/'overlay_font_roboto.h'})
    for path in sources:
        source = path.read_text(encoding='utf-8')

        def replace(match):
            kind, name, values = match.groups()
            tokens = re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', values)
            # Reject expressions/comments instead of silently packing a different array.
            if re.sub(r'0x[0-9a-fA-F]+|\b\d+\b|[\s,]', '', values):
                raise ValueError(f'Unsupported array initializer: {path.name}:{name}')
            width = 1 if kind == 'unsigned char' else 4
            data = b''.join(int(t, 16 if t.startswith('0x') else 10).to_bytes(width, 'little') for t in tokens)
            if not data:
                raise ValueError(f'Empty resource: {name}')
            if data not in offsets:
                raw.extend(b'\0' * (-len(raw) % 4))
                offsets[data] = len(raw)
                raw.extend(data)
            offset = offsets[data]
            entries.append({'header': path.name, 'name': name, 'offset': offset, 'bytes': len(data),
                            'sha256': hashlib.sha256(data).hexdigest(), 'type': kind, 'count': len(tokens)})
            return (f'inline const {kind} (&{name}Data())[{len(tokens)}] {{\n'
                    f'    return ::NorthlightAssets::array<{kind}, {len(tokens)}>({offset});\n'
                    f'}}\n#define {name} {name}Data()')

        header, count = ARRAY.subn(replace, source)
        if not count:
            raise ValueError(f'No resource arrays in {path}')
        headers[path.name] = '#include "renderer_assets.h"\n' + header
    raw.extend(b'\0' * (-len(raw) % 4))
    packed = b'NLA1' + struct.pack('<I', len(raw)) + zlib.compress(raw, 9)
    digest = hashlib.sha256(packed).hexdigest()
    for name, text in headers.items():
        (output/name).write_bytes(text.encode('utf-8'))
    (output/'renderer_asset_manifest.h').write_bytes((
        '#pragma once\nnamespace NorthlightAssets {\n'
        f'inline constexpr char PackIdentity[]="{MARKER.decode()}{digest}";\n'
        f'inline constexpr unsigned PackBytes={len(packed)}, RawBytes={len(raw)};\n'
        f'inline constexpr wchar_t PackFilename[]=L"{FILENAME}";\n'
        '}\n').encode('utf-8'))
    (output/FILENAME).write_bytes(packed)
    manifest = {'format': 'northlight-assets/1', 'sha256': digest, 'bytes': len(packed),
                'raw_bytes': len(raw), 'entries': entries}
    (output/'renderer-assets.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return manifest


def verify_pair(dll, assets):
    """Require the exact resource bundle declared by a production DLL."""
    identities = set(re.findall(re.escape(MARKER) + rb'([0-9a-f]{64})', dll))
    if len(identities) != 1:
        raise ValueError('Renderer has no unique resource bundle identity; rebuild the DLL')
    if hashlib.sha256(assets).hexdigest().encode() not in identities:
        raise ValueError('northlight-assets.bin does not match the renderer DLL')
    if not assets.startswith(b'NLA1') or len(assets) < 8:
        raise ValueError('Invalid renderer resource bundle')
    size = struct.unpack_from('<I', assets, 4)[0]
    if not 0 < size <= 16 * 1024 * 1024:
        raise ValueError('Invalid renderer resource size')
    stream = zlib.decompressobj()
    raw = stream.decompress(assets[8:], size + 1)
    if len(raw) != size or not stream.eof or stream.unused_data or stream.unconsumed_tail:
        raise ValueError('Invalid renderer resource stream')
    return identities.pop().decode()


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=fp.output_dir())
    args = parser.parse_args()
    result = prepare(args.out)
    print(f'{result["raw_bytes"]} resource bytes -> {result["bytes"]} bytes: {args.out/FILENAME}')
