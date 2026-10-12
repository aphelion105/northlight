#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,cxx
"""task 34: the shader read-set scanner (src/proxy/shader_read_set.h) never under-reports against an independent Python scan of every shader the spell layer
defers with: all M2 family vertex shaders (every D3D model) and all Combiners_*/WaterRipples pixel shader variants of the configured client. For each shader
the C++ scan must be exact (no 'reads everything' fallback) and a superset of the Python census: float registers (minus def'd ones; a relative read covers its
base up to the end of the bank), int and bool registers, samplers. The union over the families is reported (it bounds what the layer records)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import re,struct,subprocess,tempfile
import client_archives
from mpq import Archive
from extract_signatures import variants, fnv1a64
import extract_m2_shader_families as fam

HARNESS=r'''
#include "shader_read_set.h"
#include <cstdio>
#include <cstdint>
#include <vector>
int main(int argc,char** argv){
    std::FILE* in=std::fopen(argv[1],"rb");uint32_t n=0;if(std::fread(&n,4,1,in)!=1)return 2;
    for(uint32_t k=0;k<n;++k){
        uint32_t len=0;if(std::fread(&len,4,1,in)!=1)return 2;std::vector<uint32_t> w(len);if(len&&std::fread(w.data(),4,len,in)!=len)return 2;
        const auto r=NorthlightShaderReads::scan(w.data(),len);
        std::printf("%d %d %llx %llx %llx %llx %x %x %x\n",int(r.exact),int(r.vertex),(unsigned long long)r.f[0],(unsigned long long)r.f[1],(unsigned long long)r.f[2],(unsigned long long)r.f[3],r.i,r.b,r.samplers);
    }
    return 0;
}
'''
MATRIX={20:4,21:3,22:4,23:3,24:2}   # m4x4 m4x3 m3x4 m3x3 m3x2: consecutive registers of the second source
NODST={25,26,27,28,29,30,38,39,40,41,42,43,44,45,96}
SM1={1:2,2:3,3:3,4:4,5:3,6:2,7:2,8:3,9:3,10:3,11:3,12:3,13:3,14:2,15:2,16:2,17:3,18:4,19:2,20:3,21:3,22:3,23:3,24:3,31:2,78:2,79:2,81:5,0:0}
def rtype(t): return ((t>>28)&7)|((t>>8)&24)
def rnum(t): return t&0x7ff

def census(code):
    """independent scan: (float reads, relative bases, int, bool, samplers, defs) or None when the stream cannot be followed"""
    w=struct.unpack('<%dI'%(len(code)//4),code)
    major=(w[0]>>8)&255; vertex=(w[0]>>16)==0xfffe
    fr,rel,ints,bools,samp,defs=set(),set(),set(),set(),set(),set()
    off=1
    while off<len(w):
        t=w[off]; op=t&0xffff
        if op==0xffff: break
        if op==0xfffe: off+=1+((t>>16)&0x7fff); continue
        n=(t>>24)&15 if major>=2 else SM1.get(op)
        if n is None: return None
        args=list(w[off+1:off+1+n]); off+=1+n
        pred=major>=2 and (t>>28)&1
        if pred and (op in NODST or op in (0,31,47,48,81)): return None
        if op==81: defs.add(rnum(args[0])); continue
        if op in (47,48): continue
        if op==31:
            if rtype(args[1])==10: samp.add(rnum(args[1]))
            continue
        ops=[]; i=0
        while i<len(args):
            a=args[i]; i+=1
            if (a&0x2000) and major>=2: i+=1
            ops.append(a)
            if pred and len(ops)==1: i+=1   # D3D9: the predicate register token follows the destination operand
        srcs=ops if op in NODST else ops[1:]
        for pos,a in enumerate(srcs):
            ty,nm=rtype(a),rnum(a)
            if ty==2:
                if a&0x2000: rel.add(nm)
                else:
                    for k in range(MATRIX.get(op,1) if (pos==1 and op in MATRIX) else 1): fr.add(nm+k)
            elif ty==7: ints.add(nm)
            elif ty==14: bools.add(nm)
            elif ty==10: samp.add(nm)
    return fr,rel,ints,bools,samp,defs

def main():
    client=fp.client_root()
    records,_=fam.collect(client)
    shaders=[('vs/'+fam_name,code) for fam_name,code in records.values()]
    archives=[Archive(p) for p in client_archives.chain(client,'all')]
    try:
        names=set()
        for a in archives:
            try: names|=set(a.names())
            except Exception: pass
        seen=set()
        for n in sorted(names):
            if re.match(r'(?i)shaders\\pixel\\ps_[23]_\d\\(Combiners_\w+|WaterRipples)\.bls$',n):
                for a in archives:
                    if not a.has(n): continue
                    for _,_,code in variants(a.read(n)):
                        h=fnv1a64(code)
                        if h not in seen: seen.add(h); shaders.append(('ps/'+n.split('\\')[-1][:-4],code))
    finally:
        for a in archives: a.close()
    with tempfile.TemporaryDirectory(prefix='northlight-read-set-client-') as tmp:
        tmp=Path(tmp); (tmp/'h.cpp').write_text(HARNESS)
        subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Werror',*fp.test_include_flags(),str(tmp/'h.cpp'),'-o',str(tmp/'h')],check=True)
        blob=struct.pack('<I',len(shaders))
        for _,code in shaders: blob+=struct.pack('<I',len(code)//4)+code
        (tmp/'in.bin').write_bytes(blob)
        out=subprocess.run([str(tmp/'h'),str(tmp/'in.bin')],capture_output=True,text=True,check=True).stdout.split('\n')
    assert len(out)>=len(shaders)
    union={'vs':[0,0,0,0,0,0],'ps':[0,0,0,0,0,0]}; maxf={'vs':0,'ps':0}; checked=0; relative=0
    for (kind,code),line in zip(shaders,out):
        exact,vertex,f0,f1,f2,f3,ii,bb,ss=line.split()
        ident=f'{kind} {fnv1a64(code):016x}'
        assert exact=='1',f'{ident}: the scanner fell back to "reads everything"'
        mask=int(f0,16)|int(f1,16)<<64|int(f2,16)<<128|int(f3,16)<<192
        c=census(code); assert c is not None,f'{ident}: census could not follow the stream'
        fr,rel,ints,bools,samp,defs=c
        want={r for r in fr if r not in defs}
        for b in rel: want|=set(range(b,256 if kind.startswith('vs') else 224)); relative+=1
        want={r for r in want if r<(256 if kind.startswith('vs') else 224)}-defs if not rel else want-defs
        have={r for r in range(256) if mask>>r&1}
        assert want<=have,f'{ident}: scanner misses float registers {sorted(want-have)}'
        assert ints<={r for r in range(16) if int(ii,16)>>r&1},f'{ident}: int registers'
        assert bools<={r for r in range(16) if int(bb,16)>>r&1},f'{ident}: bool registers'
        assert samp<={r for r in range(16) if int(ss,16)>>r&1},f'{ident}: samplers'
        key=kind[:2]; maxf[key]=max(maxf[key],len(have)); union[key][0]|=mask; union[key][1]|=int(ii,16); union[key][2]|=int(bb,16); union[key][3]|=int(ss,16)
        checked+=1
    for key in ('vs','ps'):
        u=union[key][0]; regs=[r for r in range(256) if u>>r&1]
        print(f'{key}: union of float registers {len(regs)} (c{min(regs)}..c{max(regs)}), max per shader {maxf[key]}, ints {union[key][1]:#x}, bools {union[key][2]:#x}, samplers {union[key][3]:#x}')
    print(f'checked {checked} shaders ({relative} relative reads): the scanner is exact for all and never under-reports')
    print('ok')
main()
