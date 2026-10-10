#!/usr/bin/env python3
# northlight-test:
"""0.3.208 round 3: the 1-pixel light line that remained along a wall foot after the contact pull (2D lattice emulation).

A 1D column cannot show it. Here a pinhole camera (1920x1080, half-res lattice texel i = full pixel 2i+1, WorldNormals port with confidence and
the one-sided choice, a night WorldLighting correction from that normal, smoothRemoval + contactPull as in RemovalSmooth, the exact WorldComposite
tent with the .02 fallback, baseline mixing, albedoT clamp and the -45% floor) looks at flat ground and a vertical wall seen obliquely
(wall yaw 78-85 degrees: the foot line then crosses the screen at ~1 row per 8 pixels and staircases over the lattice), the ground and the
wall both in shadow, the wall facing away from the source. MSAA 4x is modelled (colour = mean of the D3D 4x pattern samples, depth = sample 0,
what RESZ copies) and switched off for comparison. Error = composite - ideal (every sample relit by its own surface), /255.

Mechanism (not MSAA specific: 12 bright pixels with MSAA 4x and 12 without in the first scene): the ground texel at the contact has a
confidence-0 one-sided normal that blends ground and wall (about 55 degrees off the ground: N.L<=0, correction ~0), and the composite weights it
at .56 for the top ground row. The 50-70 degree normal gate left the pull at about .24, and a depth step allowance of 1.0 x the neighbour's step
(the step toward the camera shrinks) left it at .55 of that. A texel next to it then also loses removal in smoothRemoval (its disc takes in
the tilted contact texel, which passes the 35-50 degree normal weight). Fix: a confidence-0 centre skips the normal gate, the allowance is
1.5 x the step. No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0,str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import json,math
hlsl=fp.src('world_effects.hlsl').read_text()
W,H=1920,1080;F=1217.;HW,HH=W//2,H//2
CH=1.7;AMB=.3;LEGACY=.9;ALB_G=.94;ALB_W=.6
def norm(v):
    m=math.sqrt(sum(a*a for a in v));return tuple(a/m for a in v)
LG=norm((.2,.5,.85))  # toward the source, world (X right, Y up, S forward)
class Scene:
    def __init__(s,S0,pitch_deg,yaw_deg,Hw=60.,pitch_extra=0.,ao=False,pull=True,gates=True):
        s.cp=math.cos(math.radians(pitch_deg));s.sp=math.sin(math.radians(pitch_deg))
        s.wx=math.sin(math.radians(yaw_deg));s.ws=math.cos(math.radians(yaw_deg));s.d=S0*s.ws;s.Hw=Hw
        s.zc={};s.nc={};s.lc={};s.rc={};s.pull=pull
    def cast(s,px,py):
        key=(px,py)
        if key in s.zc:return s.zc[key]
        px_=min(max(px,0),W-1);py_=min(max(py,0),H-1)
        rx=(px_+.5-W/2)/F;ry=-(py_+.5-H/2)/F
        dY=ry*s.cp-s.sp;dX=rx;dS=ry*s.sp+s.cp
        tg=CH/-dY if dY<0 else None
        den=s.wx*dX+s.ws*dS;tw=s.d/den if den>1e-9 else None
        hit=None
        if tw is not None and 0<=CH+tw*dY<=s.Hw and (tg is None or tw<tg):hit=('w',tw)
        elif tg is not None:hit=('g',tg)
        if hit is None:r=('s',1e4)
        else:r=hit
        s.zc[key]=r;return r
    def pos(s,px,py):
        k,t=s.cast(px,py);px_=min(max(px,0),W-1);py_=min(max(py,0),H-1)
        return ((px_+.5-W/2)/F*t,-(py_+.5-H/2)/F*t,t)
    def true_normal(s,k):
        return (0.,1.,0.) if k=='g' else (-s.wx,0.,-s.ws)
    def orig(s,px,py):
        k,t=s.cast(px,py)
        if k=='s':return 0.
        n=s.true_normal(k);old=LEGACY*max(sum(a*b for a,b in zip(n,LG)),0)
        return (ALB_G if k=='g' else ALB_W)*(AMB+old)

# ---------------- MSAA, normals, lighting, removal, contact pull, composite ----------------
SAMPLES=((-2,-6),(6,-2),(-6,2),(2,6))   # D3D 4x standard pattern, 1/16 px, y down; sample 0 is the depth the effects read
def cast_s(sc,px,py,si):
    """surface hit of sample si of full-res pixel (px,py) (no MSAA: si=None -> pixel centre)."""
    ox,oy=(0,0) if si is None else SAMPLES[si]
    rx=(px+.5+ox/16-W/2)/F;ry=-(py+.5+oy/16-H/2)/F
    dY=ry*sc.cp-sc.sp;dX=rx;dS=ry*sc.sp+sc.cp
    tg=CH/-dY if dY<0 else None
    den=sc.wx*dX+sc.ws*dS;tw=s_div(sc.d,den)
    if tw is not None and 0<=CH+tw*dY<=sc.Hw and (tg is None or tw<tg):return 'w',tw,(rx,ry)
    if tg is not None:return 'g',tg,(rx,ry)
    return 's',1e4,(rx,ry)
def s_div(a,b):return a/b if b>1e-9 else None
class Emu:
    def __init__(s,sc,msaa=True,pull=True,gates=True,opt=()):
        s.opt=opt;s.sc=sc;s.msaa=msaa;s.pull_on=pull;s.d0={};s.nrm={};s.light={};s.out={};s.col={};s.gates=gates
    # depth read by every effect: sample 0 (or the centre without MSAA)
    def depth(s,px,py):
        px=min(max(px,0),W-1);py=min(max(py,0),H-1)
        k=(px,py)
        if k not in s.d0:
            kind,t,r=cast_s(s.sc,px,py,0 if s.msaa else None);s.d0[k]=(kind,t,(r[0]*t,r[1]*t,t))
        return s.d0[k]
    def colour(s,px,py):
        """resolve: mean of the sample colours (each sample its own surface's painted colour)."""
        k=(px,py)
        if k not in s.col:
            tot=0.;n=4 if s.msaa else 1
            for si in (range(4) if s.msaa else (None,)):
                kind,t,r=cast_s(s.sc,px,py,si)
                tot+=0. if kind=='s' else (ALB_G*(AMB+LEGACY*max(sum(a*b for a,b in zip((0,1,0),LG)),0)) if kind=='g' else ALB_W*(AMB+LEGACY*max(sum(a*b for a,b in zip(s.sc.true_normal('w'),LG)),0)))
            s.col[k]=tot/n
        return s.col[k]
    # ---- WorldNormals port (half-res texel (i,j) reads full pixel (2i+1,2j+1)) ----
    def normal(s,i,j):
        k=(i,j)
        if k in s.nrm:return s.nrm[k]
        px,py=2*i+1,2*j+1;kind,z,p=s.depth(px,py)
        if kind=='s':s.nrm[k]=(0,0,1,1.);return s.nrm[k]
        sub=lambda a,b:[a[t]-b[t] for t in range(3)];dot=lambda a,b:sum(u*v for u,v in zip(a,b))
        cosine=lambda a,b:dot(a,b)/math.sqrt(max(dot(a,a)*dot(b,b),1e-12))
        P=lambda x,y:s.depth(min(max(x,0),W-1),min(max(y,0),H-1))[2]
        pixels=min(max(1.5*F/z,2),40)
        def wide(dx,dy,near):
            qx=min(max(px+int(math.floor(.5+dx*pixels)),0),W-1);qy=min(max(py+int(math.floor(.5+dy*pixels)),0),H-1)
            w=P(qx,qy);a=sub(near,p);b=sub(w,p)
            inner=P(min(max(qx-dx,0),W-1),min(max(qy-dy,0),H-1));end=sub(w,inner)
            bad=abs(w[2]-z)>max(.35,z*.04) or min(cosine(a,b),cosine(a,end))<.95
            return near if bad else w
        def axis(dx,dy):
            a1=P(px-dx,py-dy);b1=P(px+dx,py+dy)
            a=wide(-dx,-dy,a1);b=wide(dx,dy,b1)
            if a==a1 or b==b1:a,b=a1,b1
            da=sub(p,a);db=sub(b,p)
            if cosine(da,db)>=.85:return sub(b,a),1.
            da=sub(p,a1);db=sub(b1,p)
            if cosine(da,db)>=.85:return sub(b1,a1),1.
            return (db if abs(b1[2]-p[2])<abs(a1[2]-p[2]) else da),0.
        tx,c1=axis(1,0);ty,c2=axis(0,1)
        n=[tx[1]*ty[2]-tx[2]*ty[1],tx[2]*ty[0]-tx[0]*ty[2],tx[0]*ty[1]-tx[1]*ty[0]]
        m=math.sqrt(max(dot(n,n),1e-12));n=[v/m for v in n]
        if dot(n,[-v for v in p])<0:n=[-v for v in n]
        # view -> world: view x right, y up, z forward
        sc=s.sc;fwd=(0,-sc.sp,sc.cp);up=(0,sc.cp,sc.sp)
        nw=tuple(n[0]*(1,0,0)[t]+n[1]*up[t]+n[2]*fwd[t] for t in range(3))
        s.nrm[k]=(*nw,min(c1,c2));return s.nrm[k]
    # ---- WorldLighting at night (both surfaces in shadow: correction = -painted): (corr, baseline, alpha) ----
    def lighting(s,i,j):
        n=s.normal(i,j);kind=s.depth(2*i+1,2*j+1)[0]
        if kind=='s':return (0.,max(AMB,.15),1.)
        old=LEGACY*max(sum(a*b for a,b in zip(n[:3],LG)),0)
        return (-old,max(old+AMB,.15),0.)
    def scene_tex(s,i,j):
        return (s.colour(2*i,2*j)+s.colour(2*i+1,2*j)+s.colour(2*i,2*j+1)+s.colour(2*i+1,2*j+1))/4
    def z(s,i,j):return s.depth(2*i+1,2*j+1)[1]
    def rawL(s,i,j):
        i=min(max(i,0),HW-1);j=min(max(j,0),HH-1)
        if (i,j) not in s.light:s.light[(i,j)]=s.lighting(i,j)
        return s.light[(i,j)]
    def scale(s,i,j):return max(s.rawL(i,j)[1],s.scene_tex(i,j))   # removalScale, T=1, no fog
    def smooth_removal(s,i,j):
        cur=s.rawL(i,j);z=s.z(i,j);amount=min(max((1-cur[2]*1.)/.85,0),1)
        if amount<=1/256:return cur[0]
        ratio=cur[0]/s.scale(i,j);radius=min(max(.375*F/z,3),16);ds=-1.442695/max(.25,z*.03);vs=2/.85
        n0=s.normal(i,j);tot=1.;sm=max(ratio,-.45);t=.5;dx,dy=1.,0.
        for _ in range(24):
            r=math.sqrt(t/24)*radius
            tx=min(max(math.floor(i+.5+dx*r),0),HW-1);ty=min(max(math.floor(j+.5+dy*r),0),HH-1)
            l=s.rawL(tx,ty);nt=s.normal(tx,ty)
            agree=min(max((sum(a*b for a,b in zip(n0[:3],nt[:3]))-.6428)*5.67,0),1)
            w=2**(abs(s.z(tx,ty)-z)*ds)*min(max(1-abs(l[2]-cur[2])*vs,0),1)*(agree if n0[3]>.5 else 1)
            sm+=max(l[0]/s.scale(tx,ty),-.45)*w;tot+=w
            dx,dy=dx*-.7373688-dy*.6754903,dx*.6754903-dy*.7373688;t+=1
        return (ratio+(sm/tot-ratio)*amount)*s.scale(i,j)
    def smoothed(s,i,j):
        i=min(max(i,0),HW-1);j=min(max(j,0),HH-1)
        if (i,j) not in s.out:
            if s.depth(2*i+1,2*j+1)[0]=='s':s.out[(i,j)]=s.rawL(i,j)[0]
            else:s.out[(i,j)]=s.smooth_removal(i,j)
        return s.out[(i,j)]
    SM_CACHE=None
    def pulled(s,i,j):
        """RemovalSmooth output: smoothRemoval then contactPull on the smoothed centre; neighbours read the RAW buffer like the shader."""
        k=('p',i,j)
        if k in s.out:return s.out[k]
        c=s.smoothed(i,j);kind,z,pc=s.depth(2*i+1,2*j+1)
        if kind=='s' or not s.pull_on:s.out[k]=c;return c
        a0=s.rawL(i,j)[2];n0=s.normal(i,j)
        L=LG   # moon: SunDirection.w=1 -> source direction; the test uses the same vector
        gate0=max(min(max((1-a0)/.85,0),1),min(max(1-10*sum(a*b for a,b in zip(n0[:3],L)),0),1))
        removal=min(c,0);delta=0.;tol=max(.15,z*.01);texel=4*z/(F*2)   # half-res texel footprint at z: 2 px, pixel=z/F
        texel=2*z/F
        for a in (0,1,2,3,5,6,7,8):
            ox,oy=a%3-1,a//3-1
            ti,tj=min(max(i+ox,0),HW-1),min(max(j+oy,0),HH-1)
            dkind,zn,pn=s.depth(2*ti+1,2*tj+1)
            if dkind=='s':continue
            ui,uj=min(max(i+2*ox,0),HW-1),min(max(j+2*oy,0),HH-1)
            d2k,z2,_=s.depth(2*ui+1,2*uj+1)
            slack=min(abs(zn-z2)*(1.5 if 'slack' in s.opt else 1.),.35*z) if d2k!='s' else 0.
            ln=s.rawL(ti,tj);nn=s.normal(ti,tj)
            ng=min(max((.6428-sum(x*y for x,y in zip(n0[:3],nn[:3])))*3.3223,0),1)
            dp=[pn[t]-pc[t] for t in range(3)]
            # n0 world -> view
            sc=s.sc;fwd=(0,-sc.sp,sc.cp);up=(0,sc.cp,sc.sp)
            nv=(n0[0],sum(x*y for x,y in zip(n0[:3],up)),sum(x*y for x,y in zip(n0[:3],fwd)))
            conc=min(max(sum(x*y for x,y in zip(dp,nv))*(4/texel)+1,0),1)
            sh=min(max((1-ln[2])/.85,0),1)
            if 'conf0' in s.opt and n0[3]<.5:ng=1.
            p=ng*math.exp(-max(abs(zn-z)-slack,0)/tol)*(conc*gate0*sh if s.gates else 1.)
            if 'boost' in s.opt:p=min(p*1.5,1.)
            delta=min(delta,p*(min(ln[0],0)-removal))
        s.out[k]=c+delta;return s.out[k]
    # ---- WorldComposite, one full-res pixel ----
    def zeff(s,px,py):
        """effects-depth pre-pass: at a 3x3 depth discontinuity pick the candidate depth whose nearest tent texel has the most removal (k)."""
        kind,z,_=s.depth(px,py)
        if kind=='s':return z
        tol=max(.15,z*.01);cands=[z]
        for oy in (-1,0,1):
            for ox in (-1,0,1):
                k2,z2,_=s.depth(px+ox,py+oy)
                if k2!='s' and abs(z2-z)>tol:cands.append(z2)
        if len(cands)==1:return z
        grid=((px+.5)/2-.5,(py+.5)/2-.5);bx,by=math.floor(grid[0]),math.floor(grid[1])
        tex=[(min(max(bx+(t&1),0),HW-1),min(max(by+(t>>1),0),HH-1)) for t in range(4)]
        best=None
        for c in cands:
            ti,tj=min(tex,key=lambda a:abs(s.depth(2*a[0]+1,2*a[1]+1)[1]-c))
            k=s.pulled(ti,tj)/max(s.rawL(ti,tj)[1],.15)
            if best is None or k<best[0]-1e-12:best=(k,c)
        return best[1]
    def composite(s,px,py):
        kind,z,_=s.depth(px,py)
        if 'zeff' in s.opt:z=s.zeff(px,py)
        O=s.colour(px,py)
        if kind=='s':return O
        grid=((px+.5)/2-.5,(py+.5)/2-.5);bx,by=math.floor(grid[0]),math.floor(grid[1]);fx,fy=grid[0]-bx,grid[1]-by
        bounce=baseline=total=0.;closest=1e20;fb=None
        for t in range(4):
            bit=(t&1,t>>1);ti=min(max(bx+bit[0],0),HW-1);tj=min(max(by+bit[1],0),HH-1)
            bl=((1-fx) if bit[0]==0 else fx)*((1-fy) if bit[1]==0 else fy)
            zn=s.depth(2*ti+1,2*tj+1)[1];delta=abs(zn-z)
            w=bl*math.exp(-delta/max(.15,z*.01))
            c=s.pulled(ti,tj);b=s.rawL(ti,tj)[1]
            bounce+=c*w;baseline+=b*w;total+=w
            if delta<closest:closest=delta;fb=(c,b)
        if total<.02:bounce,baseline,total=fb[0],fb[1],1.
        inv=1/max(total,1e-4);bounce*=inv;old=max(baseline*inv,.15)
        albedoT=min(O/old,1.)
        return max(O+albedoT*bounce,O-.45*O)
    def ideal(s,px,py):
        """what a pixel of this resolved colour would be with every sample relit by its own surface (the MSAA ideal)."""
        tot=0.;n=4 if s.msaa else 1
        for si in (range(4) if s.msaa else (None,)):
            kind,t,r=cast_s(s.sc,px,py,si)
            if kind=='s':continue
            if kind=='g':
                old=LEGACY*LG[1];O=ALB_G*(AMB+old);b=AMB+old;corr=-old
            else:
                nn=s.sc.true_normal('w');old=LEGACY*max(sum(a*b for a,b in zip(nn,LG)),0);O=ALB_W*(AMB+old);b=max(AMB+old,.15);corr=0.
            tot+=max(O+min(O/b,1)*corr,.55*O)
        return tot/n

def go(S0,yaw,msaa,opt,x0=960,x1=1040,off=3.):
    pitch=math.degrees(math.atan(CH/S0))+off
    sc=Scene(S0,pitch,yaw);em=Emu(sc,msaa=msaa,pull=True,opt=opt)
    res=[]
    for x in range(x0,x1):
        top=None
        for y in range(100,1000):
            if em.depth(x,y)[0]=='w' and em.depth(x,y+1)[0]=='g':top=y;break
        if top is None:continue
        for y in range(top-3,top+6):res.append((em.composite(x,y)-em.ideal(x,y))*255)
    return len(res),sum(1 for v in res if v>2),max(res),min(res)
checks={};report={}
pull_src=hlsl[hlsl.index('float3 contactPull('):hlsl.index('// Half-res pass before TemporalLight')]
checks['contactPull: a confidence-0 centre skips the normal gate, the depth step allowance is 1.5 x']=(
    'n0w.w>.5?saturate((.6428-dot(n0,tex2Dlod(NormalBuffer,float4(tq,0,0)).xyz))*3.3223):1' in pull_src and 'min(1.5*abs(zn-viewDistance(d2)),.35*z)' in pull_src)
print('scene (S0 yaw pitch+)       before MSAA/off: bright px >2/255, max, min     after MSAA/off')
SCENES=((100.,82.,3.),(100.,85.,12.),(25.,78.,3.),(300.,82.,12.))
table={}
for S0,yaw,off in SCENES:
    row=[]
    for opt in ((),('conf0','slack')):
        for msaa in (True,False):
            n,bright,mx,mn=go(S0,yaw,msaa,opt,off=off);row.append((bright,round(mx,1),round(mn,1)))
    table[(S0,yaw,off)]=row
    print(f'{S0:5g} {yaw:3g} {off:3g}   {row[0]} {row[1]}   ->   {row[2]} {row[3]}',flush=True)
report['scenes']={f'{k}':v for k,v in table.items()}
b=table[(100.,82.,3.)]
checks[f'the round-2 shader leaves bright top-row ground pixels in the first scene (MSAA {b[0][0]} px up to {b[0][1]}/255, no MSAA {b[1][0]} px up to {b[1][1]}/255)']=b[0][0]>=10 and b[1][0]>=10 and b[0][1]>20 and b[1][1]>20
checks[f'after the fix none in the first scene (MSAA {b[2]}, no MSAA {b[3]})']=b[2][0]==0 and b[3][0]==0
checks['every scene: fewer bright pixels and a lower maximum than before, with and without MSAA']=all(r[2][0]<r[0][0] and r[3][0]<r[1][0] and r[2][1]<r[0][1] and r[3][1]<r[1][1] for r in table.values() if r[0][0]>0 and r[1][0]>0)
checks['no scene darkens beyond the composite floor (min error >= -21/255)']=all(min(r[2][2],r[3][2])>=-21 for r in table.values())
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
(fp.output_dir()/'contact-pull-2d.json').write_text(json.dumps({'checks':checks,**report,'game_or_gpu_launched':False},indent=2))
assert all(checks.values())
print('PASS contact pull 2D: lattice emulation with MSAA 4x')
