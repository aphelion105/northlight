# northlight-test: requires=windows-gpu,zig,client
"""Thin gold receivers next to white sky: production depth hierarchy, full color/depth correspondence."""
from pathlib import Path
import argparse,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--baseline',type=Path,default=fp.COMPILED/'WaterSSR.bin');args=parser.parse_args()
folder=fp.out()/'ssr-thin-color';folder.mkdir(parents=True,exist_ok=True);exe=folder/'thin-color.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'water_ssr_thin_color_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
from shader_compiler import Compiler
oracle=folder/'oracle.hlsl'
oracle.write_text((fp.SHADERS/'surface_effects.hlsl').read_text()+r"""
float4 OracleReflection(float2 uv:TEXCOORD0):COLOR0 {
    uv=(floor(uv/Clip.xy)+.5)*Clip.xy;
    float3 p,world,normal;float z,coverage,crest;liquidGeometry(uv,p,world,normal,z,coverage,crest);
    float3 direction=reflect(normalize(p),normal);float distance=(20-p.z*Proj.z)/(direction.z*Proj.z);
    if(distance<.02)return 0;
    float2 hit=screen(p+direction*distance);if(any(hit<=0)||any(hit>=1))return 0;
    float receiver=linearDepth(tex2Dlod(FinalDepth,float4(hit,0,0)).r);if(abs(receiver-20)>.01)return 0;
    float confidence=edgeFade(hit);float2 colorUV=(floor(hit/Clip.xy)+.5)*Clip.xy;
    return float4(tex2Dlod(SourceColor,float4(colorUV,0,0)).rgb*confidence,confidence);
}
""")
Compiler(allow_compatible_binary=True).compile(oracle,'OracleReflection','ps_3_0',folder/'oracle.bin')
report={'game_launched':False,'runs':[]}
for name,backend in [('native',Path(r'C:\Windows\SysWOW64\d3d9.dll')),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    result=subprocess.run([str(exe),str(backend),str(args.baseline.resolve()),str(folder/'oracle.bin')],cwd=folder,capture_output=True,text=True,timeout=60)
    (folder/(name+'.txt')).write_text(result.stdout+result.stderr,encoding='utf-8');report['runs'].append({'backend':name,'returncode':result.returncode,'output':result.stdout});(folder/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,result.returncode,result.stdout,flush=True);assert result.returncode==0,result.stdout+result.stderr
