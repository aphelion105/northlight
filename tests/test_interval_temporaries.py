#!/usr/bin/env python3
# northlight-test: requires=cxx timing
"""Lazy interval temporaries: exact frozen-evaluator oracle and batch timings. No game or GPU."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ast
import subprocess
import tempfile
import northlight_paths as fp

tree = ast.parse((fp.TESTS / "test_terrain_snapshot.py").read_text())
stub = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
            and any(getattr(t, "id", "") == "stub" for t in n.targets))
with tempfile.TemporaryDirectory(prefix="northlight-interval-temporaries-") as tmp:
    root = Path(tmp)
    (root / "d3d9.h").write_text(stub)
    for label, flags in (("O2", ["-O2"]), ("debug", ["-O0"])):
        exe = root / ("unit-" + label)
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", *flags,
                        "-I", str(root), *fp.test_include_flags(),
                        str(fp.TESTS / "test_interval_temporaries.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe), *(["--bench"] if label == "O2" else [])], check=True)
