"""Decode copied shader evidence without a client, CASC library or GPU."""
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_forever_shadows import dxbc_programs


class ShaderEvidence(unittest.TestCase):
    def test_container_across_compressed_blocks(self):
        code = b'DXBC' + bytes(20) + struct.pack('<I', 48) + bytes(20)
        raw = b'permutation-table' + code + code
        package = b'HSXG' + b'\x78\x00invalid' + zlib.compress(raw[:40]) + zlib.compress(raw[40:])
        self.assertEqual(list(dxbc_programs(package)), [code, code])

    def test_invalid_size_and_truncation_are_not_programs(self):
        invalid = b'DXBC' + bytes(20) + struct.pack('<I', 1000) + bytes(4)
        self.assertEqual(list(dxbc_programs(b'HSXG' + zlib.compress(invalid))), [])
        self.assertEqual(list(dxbc_programs(b'HSXG' + zlib.compress(invalid)[:-2])), [])

    def test_unknown_package_is_rejected(self):
        with self.assertRaises(ValueError):
            list(dxbc_programs(b'not-a-shader'))


if __name__ == '__main__':
    unittest.main()
