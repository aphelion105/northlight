#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Shader-only COLOR0/UV0 and single-texture RGB/alpha proof; never creates a device."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ast
import subprocess
import tempfile
import northlight_paths as fp

def run(native=None):
    tree = ast.parse((fp.TESTS / "test_terrain_snapshot.py").read_text())
    stub = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                and any(getattr(t, "id", "") == "stub" for t in n.targets))
    with tempfile.TemporaryDirectory(prefix="northlight-particle-proof-") as tmp:
        root = Path(tmp)
        (root / "d3d9.h").write_text(stub)
        if native is not None:
            (root / "native.bin").write_bytes(native)
        for label, flags in (("O2", ["-O2"]), ("debug", ["-O0"])):
            exe = root / ("unit-" + label)
            subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", *flags,
                            "-I", str(root), *fp.test_include_flags(),
                            str(fp.TESTS / "test_particle_light_proof.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe), *([str(root / "native.bin")] if native is not None else [])], check=True)

if __name__ == "__main__":
    run()
