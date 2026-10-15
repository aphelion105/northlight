#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.161 runtime water mask patch (src/water/water_shader_patch.h) == the Python oracle
patch() in renderer/water_shader_patch.py, byte for byte, accept and reject alike: the
synthetic rejection cases of test_water_shaders.py plus a seeded corpus of generated
programs. Also guards that no source carries game bytes again. Native clang++; no client,
StormLib, game, graphics device or Wine.
0.3.214 (water MRT): the same cross-check for patch_mrt()/patchMrt() (the pixel variant of the game's own
water draw, which must also pass verifyMrt() against the proven mask variant) and resolve_shader()/
resolveShader(); and a small ps_2_0/ps_3_0 interpreter that runs original, MRT variant and resolve
shader: oC0 identical to the original for every input, oC1 = (cover*distance, cover, cover, cover),
and the 4-sample average of oC1 decodes to the distance of the covered samples (majority vote)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import random,re,struct,subprocess,tempfile,unittest
from pathlib import Path
from water_shader_patch import patch,patch_mrt,resolve_shader,dst,src

HERE=Path(__file__).resolve().parent
def binary(words):return struct.pack('<%dI'%len(words),*words)

def patcher_exe(workdir):
    exe=Path(workdir)/'water-shader-patch'
    if not exe.exists():
        subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        *fp.test_include_flags(),str(HERE/'test_water_shader_patch.cpp'),'-o',str(exe)],check=True)
    return exe

def run_patcher(programs,workdir,mode='mask'):
    """C++ patch() (mode 'mask') or patchMrt() (mode 'mrt') over byte strings; returns patched bytes or None (rejected) for each."""
    exe=patcher_exe(workdir)
    stream=b''.join(struct.pack('<I',len(p)//4)+p for p in programs)
    (Path(workdir)/'in.bin').write_bytes(stream)
    print(subprocess.check_output([str(exe),str(Path(workdir)/'in.bin'),str(Path(workdir)/'out.bin')]+(['mrt'] if mode=='mrt' else []),text=True),end='')
    data=(Path(workdir)/'out.bin').read_bytes();results=[];offset=0
    while offset<len(data):
        n,=struct.unpack_from('<I',data,offset);offset+=4
        results.append(data[offset:offset+4*n] if n else None);offset+=4*n
    assert len(results)==len(programs)
    return results

def oracle(code):
    try:return patch(code)[0]
    except Exception:   # ValueError, and IndexError on malformed DCLs: both reject
        return None

def oracle_mrt(code):
    try:return patch_mrt(code)[0]
    except Exception:
        return None

# ---- a small ps_2_0/ps_3_0 interpreter (the instructions these programs use) ----
F32=struct.Struct('<f')
def f32(x):
    try:return F32.unpack(F32.pack(x))[0]
    except OverflowError:return float('inf') if x>0 else float('-inf')
def swizzle(v,sw):return [v[(sw>>(2*i))&3] for i in range(4)]
def run_program(code,inputs,consts,textures=None):
    """Runs one pixel program. inputs/consts: {(type,index):[x,y,z,w]}; textures: callable(sampler,coords)->vec4.
    Returns {(type,index):vec4} of the written output registers, or None when texkill fired."""
    w=struct.unpack('<%dI'%(len(code)//4),code);regs={k:list(v) for k,v in inputs.items()};regs.update({k:list(v) for k,v in consts.items()});p=1
    def kind(t):return ((t>>28)&7)|((t>>8)&24)
    def read(t):
        v=regs.get((kind(t),t&2047),[0.,0.,0.,0.]);v=swizzle(v,(t>>16)&255)
        return [-x for x in v] if (t>>24)&15==1 else v
    def write(t,v):
        key=(kind(t),t&2047);old=regs.setdefault(key,[0.,0.,0.,0.]);sat=(t>>20)&1
        for i in range(4):
            if (((t>>16)&15)>>i)&1:old[i]=f32(min(max(v[i],0.),1.) if sat else v[i])
    while True:
        op=w[p]&65535
        if op==65535:break
        n=(w[p]>>16)&32767 if op==65534 else (w[p]>>24)&15;a=w[p+1:p+1+n];p+=n+1
        if op in (65534,31):continue
        if op==81:regs[(kind(a[0]),a[0]&2047)]=[F32.unpack(struct.pack('<I',x))[0] for x in a[1:5]];continue
        if op==65:
            if any(x<0 for x in regs.get((kind(a[0]),a[0]&2047),[0,0,0,0])[:3]):return None   # texkill r: any of x, y, z negative
            continue
        if op==66:write(a[0],list(textures(a[2]&2047,read(a[1])))) ;continue
        s=[read(t) for t in a[1:]]
        if op==1:r=s[0]
        elif op==2:r=[f32(x+y) for x,y in zip(*s)]
        elif op==3:r=[f32(x-y) for x,y in zip(*s)]
        elif op==5:r=[f32(x*y) for x,y in zip(*s)]
        elif op==4:r=[f32(f32(x*y)+z) for x,y,z in zip(*s)]
        elif op==6:r=[f32(1/s[0][0]) if s[0][0] else float('inf')]*4
        elif op==35:r=[abs(x) for x in s[0]]
        elif op==88:r=[y if x>=0 else z for x,y,z in zip(*s)]
        else:raise AssertionError('interpreter: opcode %d'%op)
        write(a[0],r)
    return {k:v for k,v in regs.items() if k[0]==8}

def generated(rng):
    """One random program: mostly well-formed SM2/SM3 with DCL/DEF/comments, arithmetic
    (incl. matrix ops and relative addressing), loops and a position/oC0 write; sometimes
    broken in one of the ways patch() must reject."""
    vertex=rng.random()<.6;major=rng.choice((2,3));w=[(0xfffe0000 if vertex else 0xffff0000)|(major<<8)]
    if rng.random()<.02:w[0]=rng.choice((0xfffe0101,0xffff0104,0xfffe0400))
    def ins(op,*tokens,flags=0):w.extend([op|(len(tokens)<<24)|flags,*tokens])
    pos=(4,0) if vertex and major==2 else (8,0) if not vertex else (6,rng.randrange(4))
    temps=32 if major==3 else 12
    if rng.random()<.3:w.extend([0xfffe|(2<<16),0x42415443,rng.getrandbits(32)])
    if vertex and major==3 and rng.random()<.95:ins(31,0x80000000|(rng.random()<.1),dst(*pos))
    for _ in range(rng.randrange(4)):
        usage=rng.choice((5,5,3,10,0))|(rng.randrange(8 if rng.random()<.15 else 4)<<16)
        kind=6 if vertex and major==3 and rng.random()<.5 else 1 if major==3 or vertex else 3
        ins(31,0x80000000|usage,dst(kind,rng.randrange(8)))
    if rng.random()<.5:ins(81,dst(2,rng.randrange(8)),*[rng.getrandbits(32) for _ in range(4)])
    def operand(first):
        if first:
            kind,index=rng.choice([pos,pos,(0,rng.randrange(temps)),(0,rng.randrange(temps))]+([(8,1),(9,0)] if not vertex and rng.random()<.05 else []))
            return [dst(kind,index,rng.choice((15,15,15,7,8,1)))]
        kind=rng.choice((0,0,1,2));t=[src(kind,rng.randrange(temps if kind==0 else 8),rng.choice((0xe4,0xff,0x00,0x55)))]
        if rng.random()<.08:t=[t[0]|0x2000,src(rng.choice((3,15,15,0)),0)]
        return t
    ops=[1,2,4,5,8,19,20,21,22,23,24,6,7,35,66,65,15]
    depth=0
    for _ in range(rng.randrange(1,10)):
        r=rng.random()
        if r<.06 and depth<2:ins(27,src(15,0),src(7,rng.randrange(4)));depth+=1;continue
        if r<.12 and depth:ins(29);depth-=1;continue
        if r<.13:ins(rng.choice((40,41,42,43,95)),src(14,0));continue
        op=rng.choice(ops);n={1:2,2:3,4:4,5:3,8:3,19:2,20:3,21:3,22:3,23:3,24:3,6:2,7:2,35:2,66:3,65:1,15:2}[op]
        tokens=[]
        for k in range(n):tokens+=operand(k==0)
        if rng.random()<.03:tokens.append(src(0,0))
        ins(op,*tokens,flags=0x10000000 if rng.random()<.02 else 0)
    if rng.random()<.1:
        for _ in range(depth):ins(29)
        depth=0
    if rng.random()<.9:ins(1,dst(*pos,rng.choice((15,15,15,3))),src(0,rng.randrange(temps)))
    if depth and rng.random()<.7:
        for _ in range(depth):ins(29)
    if rng.random()<.2:ins(0)
    w.append(0xffff)
    if rng.random()<.03:w=w[:-rng.randrange(1,4)]
    return binary(w)

class RuntimePatch(unittest.TestCase):
    def test_synthetic_rejections_and_samples(self):
        # tests/test_water_shaders.py: unknown control, partial output, loop output, MRT/depth.
        def sample(vertex=True):return [0xfffe0300 if vertex else 0xffff0300,0x0200001f,0x80000000 if vertex else 0x80000005,dst(6 if vertex else 1,0),0x02000001,dst(6 if vertex else 8,0),src(1,0),65535]
        rejects=[[0xfffe0300,0x0200001f,0x80000000,dst(6,0),0x01000028,src(14,0),0x02000001,dst(6,0),src(1,0),65535],
                 [0xfffe0300,0x0200001f,0x80000000,dst(6,0),0x02000001,dst(6,0,7),src(1,0),65535],
                 [0xfffe0300,0x0200001f,0x80000000,dst(6,0),0x0200001b,src(15,0),src(7,0),0x02000001,dst(6,0),src(1,0),0x0000001d,65535]]
        for kind,index in [(8,1),(9,0)]:w=sample(False);w[-1:-1]=[0x02000001,dst(kind,index),src(1,0)];rejects.append(w)
        accepts=[sample(True),sample(False)]
        with tempfile.TemporaryDirectory(prefix='water-patch-') as tmp:
            results=run_patcher([binary(w) for w in rejects+accepts],tmp)
        for code,result in zip(rejects,results):
            self.assertIsNone(oracle(binary(code)));self.assertIsNone(result)
        for code,result in zip(accepts,results[len(rejects):]):
            self.assertIsNotNone(result);self.assertEqual(result,oracle(binary(code)))
    def test_generated_corpus_matches_python(self):
        rng=random.Random(161);programs=[generated(rng) for _ in range(4000)]
        with tempfile.TemporaryDirectory(prefix='water-patch-') as tmp:
            results=run_patcher(programs,tmp)
        accepted=0
        for i,(code,result) in enumerate(zip(programs,results)):
            expected=oracle(code)
            self.assertEqual(result,expected,'program %d: %s'%(i,code.hex()))
            accepted+=expected is not None
        print('generated: %d programs, %d accepted, %d rejected'%(len(programs),accepted,len(programs)-accepted))
        self.assertGreater(accepted,400);self.assertGreater(len(programs)-accepted,400)
    def test_no_game_bytes_in_sources(self):
        # 0.3.161: the DLL carries hashes only. The embedded arrays must not come back.
        banned=re.compile(r'kWaterOriginal_|NorthlightShadowBlobReference|kWaterShaderVariants')
        hits=[str(p.relative_to(fp.REPO)) for d in fp.SRC_DIRS for p in sorted(d.iterdir()) if p.suffix in ('.h','.inl','.cpp','.inc') and banned.search(p.read_text(errors='replace'))]
        self.assertEqual(hits,[])
        self.assertFalse((fp.GAMEDATA/'water_shader_signatures.h').exists()or(fp.GAMEDATA/'shadow_blob_reference.h').exists())
    # ---- 0.3.214 (water MRT) ----
    def test_mrt_variant_matches_python(self):
        rng=random.Random(214);programs=[generated(rng) for _ in range(4000)]
        with tempfile.TemporaryDirectory(prefix='water-patch-') as tmp:
            results=run_patcher(programs,tmp,'mrt')   # also aborts when verifyMrt() fails for an accepted program
        accepted=0
        for i,(code,result) in enumerate(zip(programs,results)):
            expected=oracle_mrt(code)
            self.assertEqual(result,expected,'program %d: %s'%(i,code.hex()))
            accepted+=expected is not None
        print('mrt generated: %d programs, %d accepted, %d rejected'%(len(programs),accepted,len(programs)-accepted))
        self.assertGreater(accepted,150);self.assertGreater(len(programs)-accepted,400)
    def test_mrt_rejections(self):
        ps=lambda *body:binary([0xffff0300,0x0200001f,0x80000005,dst(1,0),*body,65535])
        vs=binary([0xfffe0300,0x0200001f,0x80000000,dst(6,0),0x02000001,dst(6,0),src(1,0),65535])
        okay=ps(0x02000001,dst(8,0),src(1,0))
        relative=ps(0x03000005,dst(0,0),src(1,0)|0x2000,src(15,0),0x02000001,dst(8,0),src(0,0))   # relative source operand
        manyConstants=ps(*[x for i in range(64) for x in (0x03000005,dst(0,0,1),src(2,i),src(2,i))],0x02000001,dst(8,0),src(1,0))
        noDcl=binary([0xffff0300,0x02000001,dst(8,0),src(1,0),65535])
        cases=[okay,vs,relative,manyConstants,noDcl]
        with tempfile.TemporaryDirectory(prefix='water-patch-') as tmp:
            results=run_patcher(cases,tmp,'mrt')
        self.assertIsNotNone(oracle_mrt(okay));self.assertEqual(results[0],oracle_mrt(okay))
        for code,result in zip(cases[1:],results[1:]):
            self.assertIsNone(oracle_mrt(code));self.assertIsNone(result)
        # ps_2_0 has 32 constants: a program using all of them has none free, where ps_3_0 still has c32..c63
        full2=binary([0xffff0200,0x0200001f,0x80000000,dst(3,0),*[x for i in range(32) for x in (0x03000005,dst(0,0,1),src(2,i),src(2,i))],0x02000001,dst(8,0),src(3,0),65535])
        full3=binary([0xffff0300,0x0200001f,0x80000005,dst(1,0),*[x for i in range(32) for x in (0x03000005,dst(0,0,1),src(2,i),src(2,i))],0x02000001,dst(8,0),src(1,0),65535])
        with tempfile.TemporaryDirectory(prefix='water-patch-') as tmp:
            r2,r3=run_patcher([full2,full3],tmp,'mrt')
        self.assertIsNone(oracle_mrt(full2));self.assertIsNone(r2);self.assertIsNotNone(oracle_mrt(full3));self.assertEqual(r3,oracle_mrt(full3))
    def test_resolve_shader_matches_python(self):
        with tempfile.TemporaryDirectory(prefix='water-patch-') as tmp:
            exe=patcher_exe(tmp);out=Path(tmp)/'resolve.bin'
            print(subprocess.check_output([str(exe),'resolve',str(out)],text=True),end='')
            self.assertEqual(out.read_bytes(),resolve_shader())
    def sample_pixel(self,model):
        """An own-authored pixel program with partial oC0 writes, saturation and a texture-free alpha."""
        if model==3:
            head=[0xffff0300,0x0200001f,0x80000005,dst(1,0),0x0200001f,0x80010005,dst(1,1),0x05000051,dst(2,4),0x3f000000,0x3e800000,0x3f800000,0x00000000]
            v0,v1=src(1,0),src(1,1);oc=lambda mask:dst(8,0,mask)
        else:
            head=[0xffff0200,0x0200001f,0x80000000,dst(3,0),0x0200001f,0x80000000,dst(3,1),0x05000051,dst(2,4),0x3f000000,0x3e800000,0x3f800000,0x00000000]
            v0,v1=src(3,0),src(3,1);oc=lambda mask:dst(8,0,mask)
        body=[0x03000005,dst(0,0),v0,src(2,0),                        # mul r0, v0, c0
              0x04000004,dst(0,1),v1,src(2,1),src(2,4),               # mad r1, v1, c1, c4 (the def)
              0x03000005,dst(0,2,7)|(1<<20),src(0,0),src(0,1),        # mul_sat r2.xyz, r0, r1
              0x02000001,oc(7),src(0,2),                              # mov oC0.xyz, r2
              0x03000002,oc(8)|(0<<20),src(0,0,0xff),src(0,1,0xff),   # add oC0.w, r0.wwww, r1.wwww
              65535]
        return binary(head+body)
    def test_mrt_semantics_in_the_interpreter(self):
        rng=random.Random(7);thresholds=0
        for model in (2,3):
            original=self.sample_pixel(model);mrt,info=patch_mrt(original)
            self.assertNotIn(info['constant'],(0,1,4))   # c0, c1 are read and c4 is def'd by the original
            kindIn=1 if model==3 else 3
            for trial in range(400):
                inputs={(kindIn,0):[f32(rng.uniform(-1,2)) for _ in range(4)],(kindIn,1):[f32(rng.uniform(-1,2)) for _ in range(4)],
                        (kindIn,info['varying']):[f32(rng.uniform(-3,3)),f32(rng.uniform(-3,3)),f32(rng.uniform(-3,3)),f32(rng.uniform(.5,900))]}
                consts={(2,0):[f32(rng.uniform(0,2)) for _ in range(4)],(2,1):[f32(rng.uniform(0,2)) for _ in range(4)]}
                if trial%5==0:   # alpha right at the threshold: r0.w + r1.w straddling .003
                    inputs[(kindIn,0)][3]=0.;inputs[(kindIn,1)][3]=f32(rng.choice((.003,.0030001,.0029999,0.,1e-9)));consts[(2,1)][3]=1.
                base=run_program(original,inputs,consts)
                got=run_program(mrt,inputs,consts)
                self.assertEqual(got[(8,0)],base[(8,0)])             # the colour is exactly the original's
                alpha=base[(8,0)][3];cover=1.0 if alpha>f32(.003) else 0.0
                distance=inputs[(kindIn,info['varying'])][3]
                self.assertEqual(got[(8,1)],[f32(cover*distance),cover,cover,cover]);thresholds+=cover==0.0
        self.assertGreater(thresholds,100)
    def test_two_value_average_decodes_like_a_single_sample(self):
        """4x MSAA: the resolve averages the samples' oC1. covered samples n of 4 -> (n/4*d, n/4), decoded to (d, 1) for n >= 2."""
        resolve=resolve_shader();rng=random.Random(11);seen={0:0,1:0,2:0,3:0,4:0}
        for trial in range(1000):
            n=rng.randrange(5);d=f32(rng.uniform(1,900))
            samples=[(d if i<n else 0.,1. if i<n else 0.) for i in range(4)]
            r=f32(sum(a for a,_ in samples)/4);g=f32(sum(b for _,b in samples)/4)
            out=run_program(resolve,{(3,0):[.5,.5,0,0]},{},lambda sampler,coords:[r,g,0.,1.])
            seen[n]+=1
            if n<2:self.assertIsNone(out)                      # discarded: the mask keeps its cleared 0
            else:
                x,y,_,_=out[(8,0)];self.assertEqual(y,1.0);self.assertLess(abs(x-d),d*3e-7+1e-6)   # far inside the consumers' 0.07 percent
        self.assertTrue(all(seen.values()))
        # samples on different surfaces: the mean of the covered distances, not either one
        a,b=100.,100.4;r=f32((a+b)/4);g=.5;x=run_program(resolve,{(3,0):[0,0,0,0]},{},lambda s,c:[r,g,0,1])[(8,0)][0];self.assertLess(abs(x-(a+b)/2),1e-4)
if __name__=='__main__':unittest.main()
