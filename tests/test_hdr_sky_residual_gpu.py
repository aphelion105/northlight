# northlight-test: requires=windows-gpu,zig,client
"""Native sky hue/brightness, linear emission, transmission and MSAA oracle."""
import argparse,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes
processes()
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--capture',type=Path);args=parser.parse_args()
out=fp.output_dir()/'hdr-sky-residual';out.mkdir(parents=True,exist_ok=True)
exe=out/'hdr-sky-residual.exe'
command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'hdr_sky_residual_gpu.cpp'),'-luser32','-o',str(exe)]
run=subprocess.run(command,env=fp.zig_env(),capture_output=True,text=True)
(out/'compile.log').write_text(run.stdout+run.stderr);run.check_returncode()
results=[]
for name,backend in [('native',Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    folder=out/name;folder.mkdir(exist_ok=True)
    capture=[str(args.capture.resolve()),str(folder/'captured-sky-linear.fgr')] if args.capture and name=='dxvk3' else []
    run=subprocess.run([str(exe),str(backend),*capture],cwd=folder,capture_output=True,text=True,timeout=90,env=dict(os.environ,DXVK_LOG_PATH=str(folder),DISABLE_VULKAN_OBS_CAPTURE='1'))
    (folder/'run.log').write_text(run.stdout+run.stderr);print(name,run.stdout+run.stderr,flush=True);run.check_returncode()
    assert 'HDR_SKY_RESIDUAL_GPU_PASS' in run.stdout
    results.append(dict(backend=name,result=run.stdout))
(out/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
