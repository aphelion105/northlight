# northlight-test: requires=windows-gpu,zig,client
"""Exercise the production water capture on the packaged DXVK device."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

args = argparse.ArgumentParser(description=__doc__)
args.add_argument("--benchmark", action="store_true")
args = args.parse_args()
out = fp.output_dir() / "water-distance"
out.mkdir(parents=True, exist_ok=True)
exe = out / "water-distance.exe"
command = [fp.zig(), "c++", "-O2", "-UNDEBUG", "-Wno-return-type", "-Wno-microsoft-exception-spec", "-target",
           "x86-windows-gnu", "-std=c++17", *fp.test_include_flags(),
           fp.TESTS / "water_distance_gpu.cpp", "-luser32", "-o", exe]
subprocess.run(command, env=fp.zig_env(), check=True)
# Permit full-resolution 8x MSAA resources on the 32-bit client device.
binary = bytearray(exe.read_bytes())
flags = struct.unpack_from("<I", binary, 0x3c)[0] + 22
struct.pack_into("<H", binary, flags, struct.unpack_from("<H", binary, flags)[0] | 0x20)
exe.write_bytes(binary)
for name in ("dxvk2", "dxvk"):
    backend = fp.client_root() / "renderer-backends" / name / f"{name}_d3d9.dll"
    for option in ([[], ["timing"]] if args.benchmark and name=="dxvk" else [[]]):
        path = out / (name + ("-timing.log" if option else "-pixels.log"))
        with path.open("w") as log:
            result = subprocess.run([exe, backend, fp.SHADERS / "compiled/HdrCopy.bin", *option],
                                    cwd=out, env={**os.environ, "DXVK_LOG_PATH": str(out)},
                                    stdout=log, stderr=subprocess.STDOUT, timeout=240)
        if result.returncode:
            print(path, path.read_text()[-2500:])
        result.check_returncode()
print("PASS", out)
