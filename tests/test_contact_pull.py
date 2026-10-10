#!/usr/bin/env python3
# northlight-test:
"""0.3.208 contact pull (RemovalSmooth): the light line on the ground at a wall foot and under actors in shadow.

WorldComposite upsamples the half-res lighting buffer with a 4-tap bilinear tent weighted only by depth
(exp(-delta/max(.15,center*.01)), .02 total fallback). At a wall foot (or a character's feet) the wall/leg texels
sit at about the ground's view depth, so ground pixels next to the contact take in their lighting: in shadow the
ground's correction removes a large share of the game's painted light (k=correction/max(baseline,.15) about -.55)
while the camera-facing wall face away from the source has correction 0, so the mix under-removes on 1-3 pixels:
a light line, visible on bright sand. contactPull (RemovalSmooth, half res) pulls every texel toward the MORE negative
k of its 3x3 neighbours by depthAgree x normalGate (0 up to 50 degrees, 1 from 70).

Float64 emulation on pinhole scenes (flat ground + wall, ground + 2 u thin character, a 40 degree dune crease) plus
a source audit. Reports the ground excess next to the contact (final - ideal, /255 of the bright sand colour) before
and after, the wall-bottom darkening, and checks that flat ground, lit texels and a 40 degree crease are bit-identical.
No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import json,math,re

hlsl=fp.src('world_effects.hlsl').read_text()
checks={};report={}
# ---- 1. source audit ----
pull_src=hlsl[hlsl.index('float3 contactPull('):hlsl.index('// Half-res pass before TemporalLight')]
rs=hlsl[hlsl.index('float4 RemovalSmooth('):hlsl.index('// Temporal stabilization of the half-resolution')]
checks['contactPull: 8-tap [loop], 50/70 degree normal gate, composite depth weight with the neighbour surface step, only toward more removal on the correction']=(
    '[loop]for(int i=0;i<8;++i)' in pull_src and 'float depthScale=-1/max(.15,z*.01);' in pull_src
    and '(.6428-dot(n0,' in pull_src and '*3.3223)' in pull_src and 'n0w.w>.5?saturate(' in pull_src and 'exp(max(abs(zn-z)-slack,0)*depthScale)' in pull_src
    and 'min(1.5*abs(zn-viewDistance(d2)),.35*z)' in pull_src and '(dn<.99999?1:0)' in pull_src
    and 'delta=min(delta,pull*(min(light.rgb,0)-removal));' in pull_src and 'return current.rgb+delta;' in pull_src
    and 'BaselineLighting' not in pull_src)
checks['contactPull gates: concavity (neighbour in front of the centre tangent plane) and shadow state (centre shadowed or facing away from the drawn source, neighbour shadowed)']=(
    'dot(viewPositionDistance(depthUV(tq),zn)-p0,nv)*(4/halfTexel)+1' in pull_src and 'saturate(dot(' in pull_src and '*concave*gate0*shadowed*' in pull_src
    and 'lerp(LegacyDirection.xyz,SunDirection.xyz,SunDirection.w)' in pull_src
    and pull_src.count('saturate((1-')==2 and 'RemovalInfo.z)/GridInfo.z' in pull_src)
# WorldLighting's drawn source direction is the same lerp (sun term uses LegacyDirection, moon SunDirection, by SunDirection.w)
checks["the facing test uses WorldLighting's own source blend: sun N.L on LegacyDirection, moon N.L on SunDirection, lerp by SunDirection.w"]=(
    'lerp(sun,moon,SunDirection.w)' in hlsl and 'saturate(dot(n,LegacyDirection.xyz))*sunVisibility' in hlsl and 'saturate(dot(n,SunDirection.xyz))*moonShadow' in hlsl)
checks['RemovalSmooth: contactPull after smoothRemoval for every non-sky texel of a drawn source (not only shadowed ones)']=(
    rs.count('contactPull(')==1 and rs.index('smoothRemoval(')<rs.index('contactPull(') and 'if(RemovalInfo.y>0&&d<.99999){' in rs
    and 'amount' not in rs and rs.rstrip().endswith('return current;\n}'))
# composite's weight is the one contactPull copies
checks["WorldComposite weight is exp(-delta/max(.15,center*.01)) (contactPull's depthAgree)"]=hlsl.count('blend.x*blend.y*exp(-delta/max(.15,center*.01))')==1

# ---- 2. emulation ----
FOV=math.radians(60);TAN=math.tan(FOV/2);HP=1080;HALF=HP//2;CH=1.7   # full-res rows, half-res rows, camera height
NEAR,FAR=.2,1000.
COS50=.6428;GATE=1/(.6428-.3420)  # shader: saturate((.6428-dot)*3.3223)
SAND=.80;WALL=.45                  # transported colours (bright sand, a wall)
BASE_G=.85;BASE_W=.35              # game-painted baseline: ground sun+ambient, wall ambient only
def normal_gate(n0,n1):
    return min(max((COS50-sum(a*b for a,b in zip(n0,n1)))*3.3223,0),1)

def cast(row,pitch,S,Hw,thin):  # -> (kind, depth, normal, (s,h) position)
    """full-res row -> (kind, depth, normal): kind 'g' ground, 'w' wall/character face, 's' sky. Camera looks down by pitch;
    the wall plane stands at horizontal distance S (thin: only up to Hw, the ground continues behind it)."""
    yc=(1-2*(row+.5)/HP)*TAN;cp,sp=math.cos(pitch),math.sin(pitch)
    down=sp-yc*cp                    # downward component of the ray (forward coefficient 1)
    horiz=cp+yc*sp
    if horiz<=0:return 's',None,None,None
    tw=S/horiz;hw=CH-tw*down
    if 0<=hw<=Hw:return 'w',tw,(-1.,0.,0.),(S,hw)    # the wall faces the camera: horizontal normal
    if down<=0:return 's',None,None,None
    tg=CH/down
    return 'g',tg,(0.,1.,0.),(tg*horiz,0.)

def view_distance_ok(z):return z is not None

class Scene:
    def __init__(self,S,pitch,Hw,thin,kg,kw=0.,lit=False,amt_g=1.,amt_w=0.,nl_g=.3,nl_w=-.5):
        self.amt_g,self.amt_w,self.nl_g,self.nl_w=amt_g,amt_w,nl_g,nl_w
        self.rows=[cast(r,pitch,S,Hw,thin) for r in range(HP)]
        self.kg,self.kw,self.lit=kg,kw,lit
    def half(self,i):
        """half-res texel i: (kind, z, normal) from the full-res pixel RemovalSmooth's depthUV picks (2i+1)."""
        return self.rows[min(max(2*i+1,0),HP-1)][:3]
    def pos(self,i):return self.rows[min(max(2*i+1,0),HP-1)][3]
    def shade(self,i):
        """(amount, N.L) of half-res texel i: the shadowed share and the cosine to the drawn source."""
        return (self.amt_g,self.nl_g) if self.half(i)[0]=='g' else (self.amt_w,self.nl_w)
    def raw(self,i):
        kind,z,n=self.half(i)
        if kind=='g':return (.05 if self.lit else self.kg*max(BASE_G,.15)),BASE_G   # lit: a positive GI/lamp correction
        if kind=='w':return (.02 if self.lit else self.kw*max(BASE_W,.15)),BASE_W
        return 0.,BASE_W

def pull_texel(sc,i,pull_on,mode='abs',slack_on=True,gates=True):
    """contactPull on half-res texel i (single channel); the 3x3 is 8 taps, x offsets repeat the same column.
    mode 'ratio' pulls k=correction/max(baseline,.15), 'abs' the correction itself; slack_on lets the depth agreement
    allow the neighbour surface's own texel-to-texel depth step (a grazing ground steps by several tolerances)."""
    kind,z,n0=sc.half(i);corr,base=sc.raw(i)
    if kind=='s' or not pull_on:return corr
    b0=max(base,.15);k0=corr/b0;kNew=k0;cNew=corr;delta=0.
    tol=max(.15,z*.01);texel=4*z*TAN/HP;p0=sc.pos(i);amt0,nl0=sc.shade(i)
    gate0=max(amt0,min(max(1-10*nl0,0),1)) if gates else 1.
    for a in (0,1,2,3,5,6,7,8):
        dy=a//3-1
        j=min(max(i+dy,0),HALF-1);kn_,zn,nn=sc.half(j)
        if kn_=='s':continue
        jj=min(max(i+2*dy,0),HALF-1);kk,zz,_=sc.half(jj)
        slack=min(1.5*abs(zn-zz),.35*z) if (slack_on and kk!='s') else 0.
        c1,b1=sc.raw(j)
        p=normal_gate(n0,nn)*math.exp(-max(abs(zn-z)-slack,0)/tol)
        if gates:
            pn=sc.pos(j);concave=min(max(((pn[0]-p0[0])*n0[0]+(pn[1]-p0[1])*n0[1])*4/texel+1,0),1)
            p*=concave*gate0*sc.shade(j)[0]
        if mode=='ratio':kNew=min(kNew,k0+(c1/max(b1,.15)-k0)*p)
        else:delta=min(delta,p*(min(c1,0)-min(corr,0)))
    return corr+(kNew-k0)*b0 if mode=='ratio' else corr+delta

def composite(sc,row,buf):
    """WorldComposite relight of one full-res pixel (single channel, T=1, no AO/fog/night floor): the exact 4-tap tent
    (columns identical) with its depth weight and the .02 fallback. Returns the colour."""
    kind,z,n=sc.rows[row][:3]
    if kind=='s':return None
    O=SAND if kind=='g' else WALL
    grid=(row+.5)/2-.5;base=math.floor(grid);fr=grid-base
    bounce=baseline=total=0.;closest=1e20;fb=(0.,0.)
    for i in range(4):
        bx,by=i&1,i>>1                  # x columns are identical: only the y blend matters, the x blend sums to 1
        j=min(max(base+by,0),HALF-1)
        blend=(fr if by else 1-fr)*(.5)  # x: (1-fx)+fx split evenly over the two identical columns
        sk,sz,sn=sc.half(j)
        if sk=='s':sz=FAR                 # sky texel: normalizedDepth 1 -> viewDistance far
        delta=abs(sz-z)
        w=blend*math.exp(-delta/max(.15,z*.01))
        c,b=buf[j]
        bounce+=c*w;baseline+=b*w;total+=w
        if delta<closest:closest=delta;fb=(c,b)
    if total<.02:bounce,baseline,total=fb[0],fb[1],1.
    inv=1/max(total,.0001);bounce*=inv
    old=max(baseline*inv,.15)
    albedoT=min(O/old,1.)
    color=O+albedoT*bounce
    return max(color,O-.45*O)

def ideal(sc,row):
    kind=sc.rows[row][0]
    if kind=='g':return max(SAND+min(SAND/BASE_G,1.)*(0. if sc.lit else sc.kg)*BASE_G,SAND*.55)
    if kind=='w':return max(WALL+min(WALL/BASE_W,1.)*sc.kw*BASE_W,WALL*.55)
    return None

def run(sc,**kw):
    before=[sc.raw(i)[0] for i in range(HALF)];after=[pull_texel(sc,i,True,**kw) for i in range(HALF)]
    bb=[(c,sc.raw(i)[1]) for i,c in enumerate(before)];ba=[(c,sc.raw(i)[1]) for i,c in enumerate(after)]
    return before,after,bb,ba

def measure(S,pitch,Hw,thin,kg,**kw):
    sc=Scene(S,pitch,Hw,thin,kg)
    before,after,bb,ba=run(sc,**kw)
    gx0=gx1=0.;wd=0.;gpix=0;wpix=0
    # contact row: first wall row below the ground
    kinds=[r[0] for r in sc.rows]
    contact=None
    for r in range(HP-1):
        if kinds[r]=='w' and kinds[r+1]=='g':contact=r;break
    if contact is None:return None
    for r in range(contact-12,contact+40):
        if not 0<=r<HP:continue
        k=kinds[r]
        if k=='s':continue
        c0=composite(sc,r,bb);c1=composite(sc,r,ba);id_=ideal(sc,r)
        if k=='g':
            gx0=max(gx0,(c0-id_)*255);gx1=max(gx1,(c1-id_)*255);gpix+=1
        else:
            d=(c1-c0)*255
            wd=min(wd,d)
            if d<-5:wpix+=1
    return dict(ground_before=round(gx0,2),ground_after=round(gx1,2),wall_darkening=round(wd,2),wall_rows_darker_5=wpix,contact=contact)

rows=[]
worst_after=0.;worst_before=0.;worst_wall=0.;worst_rows=0
print('scene                          ground excess before -> after (/255)   wall-bottom darkening (/255)')
for name,Hw,thin in (('wall',60.,False),('character 2 u',2.,True)):
    for S in (25.,100.,300.):
        for pitch_deg in (5.,25.):
            for kg in (-.3,-.45,-.6):
                m=measure(S,math.radians(pitch_deg),Hw,thin,kg)
                tag=f'{name} S={S:g} pitch={pitch_deg:g} k={kg}'
                if m is None:print(f'{tag:34s} contact not visible');continue
                rows.append((tag,m))
                print(f'{tag:34s} {m["ground_before"]:7.2f} -> {m["ground_after"]:5.2f}   {m["wall_darkening"]:7.2f}')
                worst_after=max(worst_after,m['ground_after']);worst_before=max(worst_before,m['ground_before']);worst_wall=min(worst_wall,m['wall_darkening']);worst_rows=max(worst_rows,m['wall_rows_darker_5'])
report['scenes']=[{'scene':t,**m} for t,m in rows]
report['worst']={'ground_before':worst_before,'ground_after':worst_after,'wall_darkening':worst_wall,'wall_rows_darker_5':worst_rows}
print(f'worst ground excess {worst_before:.2f} -> {worst_after:.2f}/255; worst wall-bottom darkening {worst_wall:.2f}/255 over at most {worst_rows} full-res rows (> 5/255)')

# the design choices, measured on the same scenes (worst over wall + character, S 25/100/300, pitch 5/25, k -.3/-.45/-.6)
variants={}
for label,kw in (('ratio space, no step allowance',dict(mode='ratio',slack_on=False)),('ratio space, step allowance',dict(mode='ratio',slack_on=True)),
                 ('correction, no step allowance',dict(mode='abs',slack_on=False)),('correction, step allowance (shipped)',dict(mode='abs',slack_on=True))):
    g=0.
    for Hw,thin in ((60.,False),(2.,True)):
        for S in (25.,100.,300.):
            for pit in (5.,25.):
                for kg in (-.3,-.45,-.6):
                    m=measure(S,math.radians(pit),Hw,thin,kg,**kw)
                    if m:g=max(g,m['ground_after'])
    variants[label]=round(g,2);print(f'  variant {label}: worst ground excess after {g:.2f}/255')
report['variants']=variants
checks['the shipped form is the only variant under the bound (ratio space or no step allowance leave a line)']=(
    variants['correction, step allowance (shipped)']<1 and all(v>=1 for k,v in variants.items() if 'shipped' not in k))
BOUND=1.0
checks[f'the scenes reproduce a visible light line before the pull (worst {worst_before:.1f}/255 > 4)']=len(rows)>=12 and worst_before>4
checks[f'ground excess next to the contact after the pull: worst {worst_after:.2f}/255 < {BOUND}']=worst_after<BOUND
checks[f'wall-bottom darkening is contact shading: worst {worst_wall:.1f}/255 (the composite floor: 45% of the {WALL} wall colour = {.45*WALL*255:.1f}) over at most {worst_rows} rows']=worst_wall>=-.45*WALL*255-.5 and worst_rows<=4

# ---- flat ground, lit texels, crease: bit-identical ----
flat=Scene(100.,math.radians(25),60.,False,-.5)
flat_ok=True
for i in range(HALF):
    if flat.half(i)[0]=='g':
        # rows far from the wall: all 8 neighbours ground
        nb=[flat.half(min(max(i+d,0),HALF-1))[0] for d in(-1,0,1)]
        if nb==['g','g','g'] and pull_texel(flat,i,True)!=flat.raw(i)[0]:flat_ok=False
checks['flat ground away from the contact: bit-identical']=flat_ok
lit=Scene(100.,math.radians(25),60.,False,-.5,lit=True,amt_g=0.,amt_w=0.,nl_g=.8,nl_w=.5)
checks['lit texels (ground +.05, wall +.02 correction, no shadow): bit-identical everywhere, also with the gates off (a positive correction has no removal to pull)']=all(
    pull_texel(lit,i,True)==lit.raw(i)[0] and pull_texel(lit,i,True,gates=False)==lit.raw(i)[0] for i in range(HALF))
# the review's daytime seam on the camera scene: lit ground (+.05 GI) against a shadowed wall (-.02) whose face is turned away
day=Scene(100.,math.radians(25),60.,False,0.,lit=False,amt_g=0.,amt_w=1.,nl_g=.8,nl_w=-.3)
day.raw=lambda i,sc=day:((.05,BASE_G) if sc.half(i)[0]=='g' else (-.02,BASE_W)) # type: ignore
day_same=all(pull_texel(day,i,True)==day.raw(i)[0] for i in range(HALF))
day_old=any(pull_texel(day,i,True,gates=False)!=day.raw(i)[0] for i in range(HALF))
checks['daytime seam, lit ground +.05 against a shadowed wall -.02: bit-identical (the ungated pull moved it)']=day_same and day_old

# a 40 degree dune crease: two planes, depths continuous, normals 40 degrees apart, different k each side
def crease_case(angle_deg,cA,cB):
    """two planes angle_deg apart, continuous depth, corrections cA/cB (absolute, as the shader pulls): every texel unchanged."""
    a=math.radians(angle_deg);nA=(0.,1.,0.);nB=(math.sin(a),math.cos(a),0.)
    tex=[(nA,cA)]*6+[(nB,cB)]*6;res=[]
    for i,(n0,c0) in enumerate(tex):
        cNew=c0
        for d in(-1,1):
            nn,cn=tex[min(max(i+d,0),len(tex)-1)]
            cNew=min(cNew,c0+(cn-c0)*normal_gate(n0,nn)*math.exp(0.))
        res.append(cNew==c0)
    return all(res)
checks['35/40/45 degree dune creases (corrections -.43 vs -.17): unchanged (pull 0, bit-identical)']=crease_case(40,-.43,-.17) and crease_case(35,-.43,-.17) and crease_case(45,-.43,-.17)
checks['the gate: 0 up to 50 degrees, 1 from 70 (ramp between)']=(
    normal_gate((0,1,0),(math.sin(math.radians(50)),math.cos(math.radians(50)),0))<1e-3 and normal_gate((0,1,0),(math.sin(math.radians(70)),math.cos(math.radians(70)),0))>.999
    and 0<normal_gate((0,1,0),(math.sin(math.radians(60)),math.cos(math.radians(60)),0))<1)

# ---- 3. the review's depth-continuous strips (convex.py scenes): a profile of half-res texels, composite = 1D tent with weight 1 ----
def strip_case(angle,concave_,A,B,N=8):
    """face A horizontal, face B joined at angle degrees, bending down (convex) or up (concave); A,B=(correction,baseline,colour,amount,N.L).
    Texels are one footprint apart (the shader's ramp scale), depth is continuous. Returns (pulled corrections identical?, composite error
    before max/min, after max/min in /255 against the ideal, rows darker than 5/255 after, max abs change in /255)."""
    a=math.radians(angle);sg=1 if concave_ else -1
    nA=(0.,1.);nB=(-math.sin(a),math.cos(a)) if concave_ else (math.sin(a),math.cos(a))
    dB=(math.cos(a),sg*math.sin(a))
    tex=[]
    for i in range(N):tex.append(dict(p=(-(N-i-.5),0.),n=nA,c=A[0],b=A[1],O=A[2],amt=A[3],nl=A[4]))
    for j in range(N):tex.append(dict(p=((j+.5)*dB[0],(j+.5)*dB[1]),n=nB,c=B[0],b=B[1],O=B[2],amt=B[3],nl=B[4]))
    pulled=[]
    for i,t in enumerate(tex):
        gate0=max(t['amt'],min(max(1-10*t['nl'],0),1));delta=0.
        for d in(-1,1):
            u=tex[min(max(i+d,0),len(tex)-1)]
            ng=min(max((COS50-(t['n'][0]*u['n'][0]+t['n'][1]*u['n'][1]))*3.3223,0),1)
            conc=min(max(((u['p'][0]-t['p'][0])*t['n'][0]+(u['p'][1]-t['p'][1])*t['n'][1])*4/1.+1,0),1)
            delta=min(delta,ng*conc*gate0*u['amt']*(min(u['c'],0)-min(t['c'],0)))
        pulled.append(t['c']+delta)
    def comp(buf):
        out=[]
        for r in range(2*len(tex)):
            g=(r+.5)/2-.5;b=math.floor(g);f=g-b;O=tex[min(r//2,len(tex)-1)]['O']
            bo=ba=tt=0
            for k,w in((0,1-f),(1,f)):
                j=min(max(b+k,0),len(tex)-1);bo+=buf[j]*w;ba+=tex[j]['b']*w;tt+=w
            old=max(ba/tt,.15);out.append(max(O+min(O/old,1)*bo/tt,.55*O))
        return out
    ideal=[max(tex[r//2]['O']+min(tex[r//2]['O']/max(tex[r//2]['b'],.15),1)*tex[r//2]['c'],.55*tex[r//2]['O']) for r in range(2*len(tex))]
    raw=[t['c'] for t in tex];c0=comp(raw);c1=comp(pulled)
    e0=[(x-y)*255 for x,y in zip(c0,ideal)];e1=[(x-y)*255 for x,y in zip(c1,ideal)]
    return dict(identical=pulled==raw,before_max=round(max(e0),1),before_min=round(min(e0),1),after_max=round(max(e1),1),after_min=round(min(e1),1),
                rows_darker_5=sum(1 for x,y in zip(c1,c0) if (x-y)*255<-5),change=round(max(abs(x-y) for x,y in zip(c1,c0))*255,2))
# (name, angle, concave, A, B): the review's numbers; amount/N.L: shadowed 1, lit 0; N.L -.5 faces away from the source
strips=[
 ('roof edge, night (top -.47, face 0)',90,False,(-.47,.85,.8,1.,.3),(0.,.35,.8,0.,-.5)),
 ('outside building corner (wall -.4, away wall 0)',90,False,(-.4,.8,.7,1.,-.1),(0.,.3,.7,0.,-.5)),
 ('60 degree rock crease (-.4 vs -.05)',60,False,(-.4,.8,.8,1.,.1),(-.05,.4,.8,1.,-.2)),
 ('lamp-lit wall foot (ground +.10, wall +.30)',90,True,(.10,.5,.8,1.,.3),(.30,.3,.6,0.,-.5)),
 ('daytime seam (lit ground +.05, shadowed wall -.02)',90,True,(.05,.9,.8,0.,.8),(-.02,.35,.6,1.,-.3)),
 ('wall foot, night (ground -.47, wall 0)',90,True,(-.47,.85,.8,1.,.3),(0.,.35,.45,0.,-.5)),
]
print('strip scene                                         before max/min   after max/min  rows darker>5  change')
sres={}
for name,ang,conc,A,B in strips:
    r=strip_case(ang,conc,A,B);sres[name]=r
    print(f'{name:52s} {r["before_max"]:6.1f}/{r["before_min"]:6.1f}   {r["after_max"]:6.1f}/{r["after_min"]:6.1f}   {r["rows_darker_5"]:3d}   {r["change"]:6.2f}')
report['strips']=sres
for name in list(sres)[:5]:
    checks[f'{name}: bit-identical to before' if sres[name]['identical'] else f'{name}: under 1/255 of before']=sres[name]['identical'] or sres[name]['change']<1
checks['every convex, lamp and daytime strip is unchanged (corrections identical)']=all(sres[n]['identical'] for n in list(sres)[:5])
w=sres['wall foot, night (ground -.47, wall 0)']
checks[f'concave wall foot strip: light line {w["before_max"]}/255 -> {w["after_max"]}/255 (< 1), wall-bottom darkening {w["after_min"]}/255']=w['after_max']<1

for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
(fp.output_dir()/'contact-pull.json').write_text(json.dumps({'checks':checks,**report,'game_or_gpu_launched':False},indent=2))
assert all(checks.values())
print('PASS contact pull: wiring and wall/character emulation')
