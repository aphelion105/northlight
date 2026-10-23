#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact culling masks against the preceding interval and independent corner oracle; no game/GPU."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import subprocess
import tempfile
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix="northlight-cull-batch-") as tmp:
    for label, flags in (("O2", ["-O2"]), ("debug", ["-O0"])):
        exe = Path(tmp) / ("unit-" + label)
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", *flags,
                        *fp.test_include_flags(), str(fp.TESTS / "test_shadow_culling_batch.cpp"),
                        str(fp.src("world_gi.cpp")), "-o", str(exe)], check=True)
        subprocess.run([str(exe), *(["--bench"] if label == "O2" else [])], check=True)
