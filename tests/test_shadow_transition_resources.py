#!/usr/bin/env python3
# northlight-test: requires=zig
"""Actual production transition resource helpers against a reference-counted fake device; CPU only."""
import subprocess
import tempfile
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
manifest=json.loads((fp.SHADERS/'shadow-transition-shader-build.json').read_text())['shaders']
entries=[name for name in manifest if name.startswith(('DirectionalShadow', 'WorldFog', 'FogClouds'))]
assert len(entries)==8
limits=['-DNORTHLIGHT_TEST_'+name.upper()+'_SLOTS='+str(manifest[name]['static_instruction_slots']) for name in entries]
limits+=['-DNORTHLIGHT_TEST_REQUIRED_SLOTS='+str(max(manifest[name]['static_instruction_slots'] for name in entries))]
with tempfile.TemporaryDirectory(prefix="northlight-shadow-transition-resources-") as tmp:
    for label, flags in ([("O2", ["-O2"])] if os.name=="nt" else [("O2", ["-O2"]),
                         ("ASan+UBSan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]):
        exe = Path(tmp) / (label+".exe" if os.name=="nt" else label)
        compiler=([str(fp.zig()),"c++","-target","x86-windows-gnu"] if os.name=="nt" else ["clang++"])
        subprocess.run([*compiler, "-std=c++17", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", *flags, *limits,
                        *fp.test_include_flags(),
                        str(ROOT / "tests/test_shadow_transition_resources.cpp"), str(ROOT / "src/gi/world_gi.cpp"),
                        "-o", str(exe)], env=fp.zig_env(),check=True)
        print(label, subprocess.check_output([str(exe)], text=True), end="", flush=True)
