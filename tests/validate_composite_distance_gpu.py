#!/usr/bin/env python3
"""Manual DXVK 3 regression and timing; hidden standalone window, no game.
Full-resolution synthetic depth checks odd sizes, discontinuities, sky, water
and frames with no directional source. A capture adds real-scene coverage.
"""
import argparse, json, os, shutil, struct, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from validate_removal_distance_gpu import fixture, image

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--capture-prefix');ap.add_argument('--output',type=Path)
 args=ap.parse_args()
 backend=os.environ['NORTHLIGHT_DXVK_D3D9']
 with tempfile.TemporaryDirectory(prefix='composite-distance-') as temp:
  folder=Path(temp);binary=folder/'test.exe'
  subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(fp.TESTS/'composite_distance_gpu.cpp'),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
  prefixes=[]
  for name in ('flat','edges','odd','sky','no-source','fog','near-far','water'):
   w,h=(137,79) if name in ('odd','water','near-far','no-source') else (128,96)
   prefix=fixture(folder,name,w,h)
   c=list(struct.unpack('<272f',Path(prefix+'constants.f32').read_bytes()))
   c[25*4:26*4]=[0,1,1,0]
   c[30*4]=float(name=='water')
   Path(prefix+'constants.f32').write_bytes(struct.pack('<272f',*c))
   shutil.copyfile(prefix+'light.fgr',prefix+'temporal-light.fgr')
   image(Path(prefix+'fog.fgr'),w//2,h//2,113,'4e',
         ((.1*(x%4),.05*(y%5),.08,.6+.08*(x%3)) for y in range(h//2) for x in range(w//2)))
   image(Path(prefix+'ao.fgr'),w//2,h//2,113,'4e',
         ((.02*(x%4),.01*(y%5),.03,.2+.1*((x+y)%8)) for y in range(h//2) for x in range(w//2)))
   image(Path(prefix+'baseline.fgr'),w//2,h//2,113,'4e',
         ((.1+.1*(x%8),.2+.1*(y%7),.3+.05*((x+y)%9),1.) for y in range(h//2) for x in range(w//2)))
   if name=='water':
    image(Path(prefix+'water.fgr'),w,h,113,'4e',
          ((12. if x<w//2 else 100.,1. if (x//9+y//11)%2 else 0.,0.,0.) for y in range(h) for x in range(w)))
   prefixes.append(prefix)
  if args.capture_prefix:prefixes.append(args.capture_prefix)
  rows=[]
  for prefix in prefixes:
   result=subprocess.run([str(binary),prefix,str(fp.TESTS/'support/composite_distance/WorldComposite.bin'),str(fp.COMPILED/'WorldComposite.bin'),'composite-'+Path(prefix).name,str(fp.COMPILED/'WorldNormals.bin'),backend],cwd=folder,capture_output=True,text=True,check=True,timeout=60)
   print(result.stdout,end='',flush=True)
   rows.extend(json.loads(line) for line in result.stdout.splitlines() if line.startswith('{'))
  if args.output:args.output.write_text(json.dumps(rows,indent=2)+'\n')
  print('PASS composite distance: finite, alpha exact, component error <= 1/1024')
if __name__=='__main__':main()
