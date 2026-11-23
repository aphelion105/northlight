# northlight-test: requires=windows-gpu,zig,client
"""Production empty-sky admission/replay, exact GPU outputs and no-work checks."""

from source_units import source_text
import json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes
out=fp.output_dir()/'empty-sky';out.mkdir(parents=True,exist_ok=True)
source=source_text(fp.src('renderer.cpp'), encoding='utf-8')
methods=source[source.index('    bool hdrSkyHasEmission()const'):source.index('    template<class Draw> void hdrReplayWorldAlpha')]
methods+=source[source.index('    template<class Draw> void hdrReplaySky'):source.index('    template<class Capture,class Draw> HRESULT drawHook')]
(out/'empty_sky_device_methods.h').write_text(methods,encoding='utf-8')
disc=fp.src('celestial_disc_renderer.h').read_text(encoding='utf-8')
methods='\n'.join(l for l in disc.splitlines() if 'void setHdrSkyTarget(' in l or 'bool hasHdrSkyEmission()' in l)
(out/'empty_sky_disc_methods.h').write_text(methods,encoding='utf-8')
assert 'if(surfaceVisible&&hdrSkyTarget){hdrSkyWritten=true;drawDisc(' in disc
assert 'sky&&skyCaptured&&hdrSkyHasEmission()&&hdr.logical.MultiSampleType==' in source
assert 'hdrSkyEmission=false;hdrSkyValid=true;' in source
exe=out/'empty-sky.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-Wno-microsoft-exception-spec','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/'hdr_empty_sky_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
results=[]
for name,backend in [('native',Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
 processes();folder=out/name;folder.mkdir(exist_ok=True)
 timing=bool(os.environ.get('NORTHLIGHT_EMPTY_SKY_TIMING')) and name=='dxvk3'
 r=subprocess.run([str(exe),str(backend),*(['--timing'] if timing else [])],cwd=folder,capture_output=True,text=True,timeout=180,env={**os.environ,'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(folder)})
 (folder/'gpu.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(name,r.stdout+r.stderr,flush=True);r.check_returncode()
 assert r.stdout.count('EMPTY_SKY_GPU_PASS')==3
 results.append({'backend':name,'timing':timing,'stdout':r.stdout})
(out/'validation.json').write_text(json.dumps({'pass':True,'game_fps_measured':False,'results':results},indent=2)+'\n')
