# northlight-test:
"""Analytic wall/ground normal-confidence regression; no GPU or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_text
import json,math
from pathlib import Path
r=Path(__file__).resolve().parent
shader=world_source_text(fp.src('world_effects.hlsl'))
assert 'correction=min(correction,0)+max(correction,0)*smoothNormal.w;' in shader
assert 'directionalShadow(p+n*(.08*smoothNormal.w),n,smoothNormal.w)' in shader

def dot(a,b):return sum(x*y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def norm(v):return math.sqrt(dot(v,v))
def tangent(p,a,b,a1,b1):
    da,db=sub(p,a),sub(b,p)
    if dot(da,db)/max(norm(da)*norm(db),1e-6)>=.85:return sub(b,a),1
    da,db=sub(p,a1),sub(b1,p)
    if dot(da,db)/max(norm(da)*norm(db),1e-6)>=.85:return sub(b1,a1),1
    return (db if abs(b1[2]-p[2])<abs(a1[2]-p[2]) else da),0

# Looking down toward a wall/ground corner. The ground side remains valid,
# but the opposite one-pixel sample climbs the wall. In camera space the two
# slopes disagree, so the actual normal helper deliberately returns confidence 0.
# Rotating these vectors changes camera depth; the confidence must not switch
# off a valid ambient-occlusion term even when the selected normal stays ground.
p=(0.,0.,10.);a=(-1.,0.,10.);b=(.05,1.,10.1)
t,c=tangent(p,a,b,(-.1,0.,10.),(.01,.1,10.01));assert c==0
ambient=(.22,.18,.14);probe=(.06,.05,.04);gain=.5
correction=tuple((p-a)*gain for p,a in zip(probe,ambient))
old_edge=tuple(a+x*c for a,x in zip(ambient,correction))
new_edge=tuple(a+min(x,0)+max(x,0)*c for a,x in zip(ambient,correction))
interior=tuple(a+x for a,x in zip(ambient,correction))
assert old_edge!=interior and new_edge==interior
cases=0
for coverage in (0,.01,.5,1):
 for confidence in (0,.1,.5,.9,1):
  for difference in (-.5,-.1,0,.1,.5):
   raw=difference*.5*coverage
   corrected=min(raw,0)+max(raw,0)*confidence
   if raw<=0:assert corrected==raw
   else:assert 0<=corrected<=raw
   if not coverage:assert corrected==0
   if confidence==1:assert corrected==raw
   assert corrected<=max(0,raw)
   cases+=1
# Untrusted normals no longer move the receiver 8 cm through an adjacent edge.
for confidence in (0,.25,.5,1):
 offset=.08*confidence
 assert 0<=offset<=.08
 if confidence==0:assert offset==0
 if confidence==1:assert offset==.08
print(json.dumps({'status':'PASS','wall_ground_confidence':c,'old_ambient_seam':old_edge,
 'fixed_seam':new_edge,'surrounding_shadow':interior,'coverage_confidence_cases':cases,
 'game_or_gpu_launched':False,'scope':'Analytic reproduction of valid negative GI being disabled by normal confidence; visual confirmation pending'},indent=2))
