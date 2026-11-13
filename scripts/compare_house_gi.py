"""Compare linear house-fixture images, keeping camera, exposure and reference inputs fixed."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--prototype',type=Path,required=True)
    parser.add_argument('--day-reference',type=Path,required=True)
    parser.add_argument('--night-reference',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=fp.output_dir()/'house-gi-quality')
    args=parser.parse_args()
    import numpy as np
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    meta=json.loads((args.prototype/'render.json').read_text())
    hashes={str(Path(__file__).resolve()):sha(Path(__file__))}
    hashes[str((args.prototype/'render.json').resolve())]=sha(args.prototype/'render.json')
    for name,digest in meta['sources'].items():
        assert sha(Path(name))==digest,f'STALE prototype input: {name}'
    hashes.update(meta['sources'])
    w,h=meta['width'],meta['height']
    def load(path):
        hashes[str(path.resolve())]=sha(path)
        a=np.fromfile(path,dtype='<f4').reshape(h,w,3)
        assert np.isfinite(a).all() and (a>=0).all(),path
        return a
    records=[]
    luminance=np.array([.2126,.7152,.0722])
    for phase,root in [('day',args.day_reference),('night',args.night_reference)]:
        reference=json.loads((root/'render.json').read_text())
        hashes[str((root/'render.json').resolve())]=sha(root/'render.json')
        assert reference['reference_phase']==phase and reference['probe_interpolation'] is False
        assert reference['sources']==meta['sources'],'Reference and prototype used different sources'
        for name in ['width','height','samples_per_pixel','probe_spacing','bounces','exposure']:
            assert meta[name]==reference[name],f'Incompatible reference: {name}'
        for view in ['inside','doorway']:
            prefix=f'{phase}-{view}';ref=load(root/(prefix+'-gi.rgb32f'))
            before=load(args.prototype/(prefix+'-legacy.rgb32f'));after=load(args.prototype/(prefix+'-gi.rgb32f'))
            record={'phase':phase,'view':view,'colour_rays':meta['rays_per_probe'],'reference_paths_per_pixel':reference['receiver_paths_per_pixel']}
            for region,index in [('image',np.s_[:,:]),('wall',np.s_[140:330,620:950])]:
                r,b,a=ref[index],before[index],after[index]
                old=float(np.abs(b-r).mean());new=float(np.abs(a-r).mean())
                record[region]={'legacy_mae':old,'local_mae':new,'error_reduction_percent':100*(1-new/old),
                    'legacy_mean_luminance':float((b@luminance).mean()),'local_mean_luminance':float((a@luminance).mean()),
                    'reference_mean_luminance':float((r@luminance).mean())}
            # Fixture acceptance, not a claim about arbitrary game geometry.
            assert record['image']['local_mae']<record['image']['legacy_mae'],record
            if phase=='day':assert record['wall']['error_reduction_percent']>70,record
            records.append(record)
    assert all(sha(Path(p))==digest for p,digest in hashes.items()),'Inputs changed during comparison'
    report={'pass':True,'scope':'Offline house fixture; linear RGB MAE, not game captures or game performance',
        'wall_roi_xyxy':[620,140,950,330],'sources':hashes,'results':records}
    (out/'quality.json').write_text(json.dumps(report,indent=2)+'\n')
    for r in records:print(r['phase'],r['view'],'image error reduction %.1f%%'%r['image']['error_reduction_percent'],
        'wall error reduction %.1f%%'%r['wall']['error_reduction_percent'])

if __name__=='__main__':main()
