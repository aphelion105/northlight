# northlight-test: requires=windows-exe,zig,client,stormlib timing
"""Windows O2/UBSan worker equivalence and lifetime scenarios; no ASan/TSan claim."""
from pathlib import Path
import subprocess
import sys
subprocess.run([sys.executable, str(Path(__file__).with_name('test_prepare_worker.py'))], check=True)
