#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Rain/snow/frost image oracle using the production external resource bundle."""
import runpy
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.argv = [str(fp.TESTS/'validate_weather_occlusion_gpu.py'), '--external-assets']
runpy.run_path(sys.argv[0], run_name='__main__')
