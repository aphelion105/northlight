#!/usr/bin/env python3
# northlight-test: requires=cxx,world-cache
"""Dalaran: release packed shadow-only triangles before paging; exact geometry/alpha."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix="shadow-paging-memory-") as temp:
    binary = Path(temp) / "test"
    subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-O2",
                    *fp.test_include_flags(), str(Path(__file__).with_suffix(".cpp")),
                    str(fp.src("world_gi.cpp")), str(fp.src("world_mesh_plan.cpp")), "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(fp.world_cache())], check=True)
