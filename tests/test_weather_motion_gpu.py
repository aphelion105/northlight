#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Production rain/snow direction and foreground/name occlusion on the GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import runpy

runpy.run_path(str(fp.TESTS / 'validate_weather_occlusion_gpu.py'), run_name='__main__')
