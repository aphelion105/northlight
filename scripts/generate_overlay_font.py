"""Embed the settings overlay's font: src/generated/overlay_font_roboto.h from Roboto-Medium.ttf.

Usage: python3 scripts/generate_overlay_font.py <path to Roboto-Medium.ttf>

The font is Dear ImGui's misc/fonts/Roboto-Medium.ttf at the vendored tag (src/imgui/README.md records the URL and
checksum; Apache-2.0, licenses/roboto.txt). It is stored in the stb_compress stream format that
ImFontAtlas::AddFontFromMemoryCompressedTTF decompresses (stb_decompress in imgui_draw.cpp), produced here by a
plain greedy LZ encoder. The stream is decoded again before the header is written and must reproduce the file.
Not run by build_renderer.py: the TTF is not kept in the repository, the generated header is.
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
import northlight_paths  # noqa: E402

OUT = northlight_paths.GENERATED / 'overlay_font_roboto.h'
EXPECTED_SHA256 = '8559132c89ad51d8a2ba5b171887a44a7ba93776e205f553573de228e64b45f8'   # imgui v1.91.9 misc/fonts/Roboto-Medium.ttf
WINDOW = 0x80000          # farthest match the stream format can address
MIN_MATCH, CHAIN = 4, 48  # shortest match worth a token; candidates tried per position


def adler32(data):
    s1, s2 = 1, 0
    for b in data:
        s1 = (s1 + b) % 65521
        s2 = (s2 + s1) % 65521
    return (s2 << 16) | s1


def literal(out, chunk):
    n = len(chunk)
    if n <= 32:
        out.append(0x20 + n - 1)
    elif n <= 2048:
        v = 0x0800 + n - 1
        out += bytes([v >> 8, v & 0xFF])
    else:
        assert n <= 65536
        out += bytes([0x07, (n - 1) >> 8, (n - 1) & 0xFF])
    out += chunk


def match(out, dist, length):
    if dist <= 256 and length <= 128:
        out += bytes([0x80 + length - 1, dist - 1])
    elif dist <= 16384 and length <= 256:
        v = 0x4000 + dist - 1
        out += bytes([v >> 8, v & 0xFF, length - 1])
    elif length <= 256:
        v = 0x180000 + dist - 1
        out += bytes([v >> 16, (v >> 8) & 0xFF, v & 0xFF, length - 1])
    else:
        v = 0x100000 + dist - 1
        out += bytes([v >> 16, (v >> 8) & 0xFF, v & 0xFF, (length - 1) >> 8, (length - 1) & 0xFF])


def compress(data):
    out = bytearray()
    for word in (0x57BC0000, 0, len(data), WINDOW):
        out += word.to_bytes(4, 'big')
    heads, i, pending = {}, 0, bytearray()
    n = len(data)

    def flush():
        while pending:
            literal(out, bytes(pending[:65536]))
            del pending[:65536]

    def remember(at):
        if at + 3 <= n:
            heads.setdefault(data[at:at + 3], []).append(at)

    while i < n:
        best_len, best_dist = 0, 0
        if i + MIN_MATCH <= n:
            for at in reversed(heads.get(data[i:i + 3], [])[-CHAIN:]):
                if i - at > WINDOW:
                    break
                length = 0
                limit = min(65536, n - i)
                while length < limit and data[at + length] == data[i + length]:
                    length += 1
                if length > best_len:
                    best_len, best_dist = length, i - at
        if best_len >= MIN_MATCH:
            flush()
            match(out, best_dist, best_len)
            for k in range(best_len):
                remember(i + k)
            i += best_len
        else:
            pending.append(data[i])
            remember(i)
            i += 1
    flush()
    out += bytes([0x05, 0xFA]) + adler32(data).to_bytes(4, 'big')
    return bytes(out)


def decompress(stream):
    """stb_decompress (imgui_draw.cpp), for the self-check."""
    assert int.from_bytes(stream[0:4], 'big') == 0x57BC0000 and int.from_bytes(stream[4:8], 'big') == 0
    size = int.from_bytes(stream[8:12], 'big')
    out, i = bytearray(), 16

    def copy(dist, length):
        start = len(out) - dist
        assert start >= 0
        for k in range(length):
            out.append(out[start + k])
    while True:
        op = stream[i]
        if op >= 0x80:
            copy(stream[i + 1] + 1, op - 0x80 + 1); i += 2
        elif op >= 0x40:
            copy(int.from_bytes(stream[i:i + 2], 'big') - 0x4000 + 1, stream[i + 2] + 1); i += 3
        elif op >= 0x20:
            n = op - 0x20 + 1; out += stream[i + 1:i + 1 + n]; i += 1 + n
        elif op >= 0x18:
            copy(int.from_bytes(stream[i:i + 3], 'big') - 0x180000 + 1, stream[i + 3] + 1); i += 4
        elif op >= 0x10:
            copy(int.from_bytes(stream[i:i + 3], 'big') - 0x100000 + 1, int.from_bytes(stream[i + 3:i + 5], 'big') + 1); i += 5
        elif op >= 0x08:
            n = int.from_bytes(stream[i:i + 2], 'big') - 0x0800 + 1; out += stream[i + 2:i + 2 + n]; i += 2 + n
        elif op == 0x07:
            n = int.from_bytes(stream[i + 1:i + 3], 'big') + 1; out += stream[i + 3:i + 3 + n]; i += 3 + n
        elif op == 0x05 and stream[i + 1] == 0xFA:
            assert len(out) == size and int.from_bytes(stream[i + 2:i + 6], 'big') == adler32(out)
            return bytes(out)
        else:
            raise ValueError(f'unexpected token 0x{op:02x} at {i}')


def main(argv):
    if len(argv) != 1:
        raise SystemExit(__doc__)
    data = Path(argv[0]).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f'{argv[0]}: sha256 {digest}, expected {EXPECTED_SHA256} (Roboto-Medium.ttf of imgui v1.91.9)')
    stream = compress(data)
    if decompress(stream) != data:
        raise SystemExit('generate_overlay_font.py: the compressed stream does not decode to the font')
    rows = [','.join(str(b) for b in stream[k:k + 40]) for k in range(0, len(stream), 40)]
    OUT.write_text(
        '// Generated by scripts/generate_overlay_font.py; do not edit.\n'
        f'// Roboto-Medium.ttf (Apache-2.0, licenses/roboto.txt) from Dear ImGui v1.91.9 misc/fonts, sha256 {digest},\n'
        f'// {len(data)} bytes, stored as an stb_compress stream of {len(stream)} bytes for ImFontAtlas::AddFontFromMemoryCompressedTTF.\n'
        '#pragma once\n'
        'namespace NorthlightOverlayFont {\n'
        f'inline constexpr unsigned RobotoMediumCompressedSize={len(stream)};\n'
        'alignas(4) inline const unsigned char RobotoMediumCompressed[RobotoMediumCompressedSize]={\n'
        + ',\n'.join(rows) + '\n};\n'
        '} // namespace NorthlightOverlayFont\n')
    print(f'Wrote {OUT} ({len(data)} -> {len(stream)} bytes)')


if __name__ == '__main__':
    main(sys.argv[1:])
