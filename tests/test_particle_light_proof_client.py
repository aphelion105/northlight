#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""Audit stock WotLK particle/model COLOR0 and native single-texture combiners; no device or game."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import struct
import northlight_paths as fp
from test_particle_light_proof import run

fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants

def main():
    records = []
    assets = Assets(archives="stock")
    try:
        for model in ("vs_2_0", "vs_3_0"):
            for family, name in ((1, "CDiffuse_T1"), (2, "Color_T1"), (3, "Diffuse_T1")):
                data = assets.read(f"shaders\\vertex\\{model}\\{name}.bls")
                records.extend((1, family, code) for _, _, code in variants(data))
        for model in ("ps_2_0", "ps_3_0"):
            for name in ("Combiners_Mod", "Combiners_Opaque", "Combiners_Add", "Combiners_Mod2x"):
                data = assets.read(f"shaders\\pixel\\{model}\\{name}.bls")
                records.extend((0, 0, code) for _, _, code in variants(data))
    finally:
        assets.close()
    payload = bytearray(struct.pack("<I", len(records)))
    for vertex, family, code in records:
        payload.extend(struct.pack("<III", vertex, family, len(code) // 4))
        payload.extend(code)
    run(payload)

if __name__ == "__main__":
    main()
