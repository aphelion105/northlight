#!/usr/bin/env python3
"""Render the house GI prototype with matched legacy and receiver-path references."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--output',type=Path,default=fp.output_dir()/'house-gi')
    parser.add_argument('--reference',action='store_true',help='Trace receiver paths without probe interpolation.')
    parser.add_argument('--reference-phase',choices=('day','night'),default='night')
    parser.add_argument('--rays',type=int,default=128,help='Colour transport rays per probe (8..4096).')
    parser.add_argument('--benchmark',action='store_true',help='Measure cold/warm CPU batches, without rendering images.')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if not 8<=args.rays<=4096:parser.error('--rays must be in 8..4096')
    inputs=[fp.SCRIPTS/'render_house_gi.cpp',fp.SCRIPTS/'render_house_gi.py',fp.src('probe_local_visibility.h'),fp.src('world_gi.h'),fp.src('world_gi.cpp'),fp.SHADERS/'world_effects.hlsl']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    hashes={str(p):sha(p) for p in inputs}
    exe=out/'house-gi.exe'
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
             *fp.include_flags(),str(fp.SCRIPTS/'render_house_gi.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)]
    subprocess.run(command,check=True,env=fp.zig_env())
    with (out/'render.log').open('w') as log:
        reference_flag='--reference-day' if args.reference_phase=='day' else '--reference'
        subprocess.run([str(exe),str(out),'--rays',str(args.rays),*(['--benchmark'] if args.benchmark else [reference_flag] if args.reference else [])],check=True,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT)
    assert all(sha(Path(p))==value for p,value in hashes.items()),'Inputs changed during render'
    if args.benchmark:
        import statistics
        meta=json.loads((out/'cpu-timing.json').read_text())
        meta['sources']=hashes
        meta['median_ms_after_warmup']={k:statistics.median(s[k] for s in meta['samples'][1:]) for k in meta['samples'][0]}
        (out/'cpu-timing.json').write_text(json.dumps(meta,indent=2)+'\n')
        print(json.dumps(meta['median_ms_after_warmup']));return
    import numpy as np
    from PIL import Image,ImageDraw,ImageFont
    meta=json.loads((out/'render.json').read_text());w,h=meta['width'],meta['height']
    font_path=Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts/arial.ttf'
    font=ImageFont.truetype(str(font_path),26)
    small=ImageFont.truetype(str(font_path),19)
    def image(path):
        linear=np.fromfile(path,dtype='<f4').reshape(h,w,3)
        assert np.isfinite(linear).all() and (linear>=0).all(),path
        x=linear*1.15
        mapped=np.clip((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0,1)
        srgb=np.where(mapped<=.0031308,mapped*12.92,1.055*np.power(mapped,1/2.4)-.055)
        return Image.fromarray(np.rint(np.clip(srgb,0,1)*255).astype('u1'))
    titles={'outside':'Снаружи — взгляд в дверной проём','inside':'Внутри комнаты','doorway':'Из комнаты — наружу'}
    phases={'day':'ДЕНЬ','night':'НОЧЬ'}
    meta.update(exposure=1.15,display_transform='ACES fitted, then sRGB',night_sources='Moon and two warm lamps',GI_strength=1)
    meta.update(GI='Production colour transport and SH4; experimental cached 9x9 FP16 blocker planes, bilinear visibility^4 and receiver facing',production_renderer_integration=False)
    meta['sources']=hashes
    if args.reference:
        meta.update(GI='Independent cosine-weighted receiver paths, production BVH and light equations',receiver_paths_per_pixel=256,probe_interpolation=False,reference_phase=args.reference_phase)
        for view in ('inside','doorway'):
            prefix=args.reference_phase+'-'+view
            comparison=Image.new('RGB',(w*2,h+48),(18,21,25));label=ImageDraw.Draw(comparison)
            comparison.paste(image(out/(prefix+'-probes.rgb32f')),(0,48));comparison.paste(image(out/(prefix+'-gi.rgb32f')),(w,48))
            label.text((20,10),'GI: прототип, локальная видимость',font=font,fill=(235,237,240))
            label.text((w+20,10),'Эталон: лучи от поверхности, без проб',font=font,fill=(235,237,240))
            comparison.save(out/(prefix+'-reference-comparison.png'))
        (out/'render.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
        print(out/(args.reference_phase+'-inside-reference-comparison.png'))
        return
    sheet=Image.new('RGB',(w*2,h*3+54*3+80),(18,21,25));draw=ImageDraw.Draw(sheet)
    draw.text((20,16),f'Прототип GI · 3 отражения · сетка 8 ед. · {args.rays} лучей / пробу · одна экспозиция',font=small,fill=(221,225,230))
    for row,view in enumerate(titles):
        for col,phase in enumerate(phases):
            prefix=phase+'-'+view;im=image(out/(prefix+'-gi.rgb32f'));im.save(out/(prefix+'.png'))
            y=70+row*(h+54);sheet.paste(im,(col*w,y+44));draw.text((col*w+20,y+5),phases[phase]+'  ·  '+titles[view],font=font,fill=(235,237,240))
            comparison=Image.new('RGB',(w*2,h+48),(18,21,25));label=ImageDraw.Draw(comparison)
            comparison.paste(image(out/(prefix+'-native-ambient.rgb32f')),(0,48));comparison.paste(im,(w,48))
            label.text((20,10),'Без GI: общий ambient',font=font,fill=(235,237,240));label.text((w+20,10),'GI: видимость неба и отражённый свет',font=font,fill=(235,237,240))
            comparison.save(out/(prefix+'-comparison.png'))
            comparison.paste(image(out/(prefix+'-legacy.rgb32f')),(0,48))
            label.rectangle((0,0,w*2,47),fill=(18,21,25))
            label.text((20,10),'До: видимость по 6 направлениям',font=font,fill=(235,237,240))
            label.text((w+20,10),'После: локальные преграды 9×9',font=font,fill=(235,237,240))
            comparison.save(out/(prefix+'-before-after.png'))
            image(out/(prefix+'-indirect.rgb32f')).save(out/(prefix+'-indirect.png'))
    sheet.save(out/'house-day-night.png')
    (out/'render.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    print(out/'house-day-night.png')

if __name__=='__main__':main()
