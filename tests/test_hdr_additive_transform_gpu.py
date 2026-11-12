# northlight-test: requires=windows-gpu,zig manual
"""Compare additive COUNT2 sky against native UVs, then optionally time it at 4K."""
import argparse, hashlib, json, os, struct, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

def game_open():
    try:processes();return False
    except RuntimeError as exc:
        if str(exc)!='Game process exists; defer this stand without changing the game':raise
        return True  # Open-game correctness checks are explicitly authorized.

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--backend',type=Path,action='append',required=True)
    parser.add_argument('--benchmark',action='store_true')
    args=parser.parse_args();out=args.output.resolve()
    if fp.is_client(out):parser.error('Use an isolated test output directory.')
    out.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).with_name('hdr_additive_transform_gpu.cpp');exe=out/'hdr-additive.exe'
    run=subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*fp.test_include_flags(),str(source),'-luser32','-o',str(exe)],env=fp.zig_env(),capture_output=True,text=True)
    (out/'compile.log').write_text(run.stdout+run.stderr);assert run.returncode==0,'\n'.join(run.stderr.splitlines()[-12:])
    # 4K legacy layers plus readback must fit the same PE32 address space as WoW.
    data=bytearray(exe.read_bytes());pe=struct.unpack_from('<I',data,0x3c)[0]
    struct.pack_into('<H',data,pe+22,struct.unpack_from('<H',data,pe+22)[0]|0x20);exe.write_bytes(data)
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    report={'source_sha256':sha(source),'hdr_color_sha256':sha(fp.src('hdr_color.h')),'game_launched':False,'game_observed_open':game_open(),'runs':[]}
    benchmark=args.benchmark and not report['game_observed_open']
    for i,backend in enumerate(args.backend):
        backend=backend.resolve()
        run=subprocess.run([str(exe),str(backend)]+(['benchmark'] if benchmark else []),cwd=out,env=dict(os.environ,DXVK_LOG_PATH=str(out)),capture_output=True,text=True,timeout=180)
        (out/f'{i}.log').write_text(run.stdout+run.stderr)
        report['game_observed_open']|=game_open()
        assert run.returncode==0 and 'HDR_ADDITIVE_PASS' in run.stdout,run.stdout+run.stderr
        result=[line for line in run.stdout.splitlines() if line.startswith('HDR_ADDITIVE')]
        report['runs'].append({'backend':str(backend),'sha256':sha(backend),'result':result});print(backend,*result,sep='\n',flush=True)
    report.update({'pass':True,'timings_usable_for_comparison':benchmark and not report['game_observed_open']})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
