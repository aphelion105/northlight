# northlight-test: requires=cxx
"""Exercise HDR admission and mask restoration with the actual device mirror."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text()
start = source.index('    bool hdrFixedSkyPossible()const{')
method = source[start:source.index('    template<class Draw> HRESULT effectCoverageDraw(', start)]
start = source.index('    template<size_t N> void maskRestore(')
restore = source[start:source.index('    bool nameMaskFullViewport(', start)]
harness = r'''
#define main mirror_fixture_main
#include "test_device_mirror.cpp"
#undef main
#include "name_stencil.h"
struct Admission {const DeviceMirror& mirrorState;ExtensionDevice* ext; @ADMISSION@ @RESTORE@};
int main(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);Admission check{m,&ext};
 auto* ps=new Object<IDirect3DPixelShader9>();auto* vs=new Object<IDirect3DVertexShader9>();
 unsigned cases=0;
 for(unsigned p=0;p<3;++p)for(unsigned v=0;v<3;++v){
  assert(SUCCEEDED(game.SetPixelShader(p==2?ps:nullptr)));
  assert(SUCCEEDED(game.SetVertexShader(v==2?vs:nullptr)));m.invalidate();
  if(p){IDirect3DPixelShader9* got=nullptr;assert(SUCCEEDED(ext.GetPixelShader(&got)));if(got)got->Release();}
  if(v){IDirect3DVertexShader9* got=nullptr;assert(SUCCEEDED(ext.GetVertexShader(&got)));if(got)got->Release();}
  const auto reads=b.gets;const auto mutations=m.mutations;
  assert(check.hdrFixedSkyPossible()==(p!=2&&v!=2));
  assert(b.gets==reads&&m.mutations==mutations);++cases;
  m.recording=true;assert(check.hdrFixedSkyPossible());m.recording=false;
  m.enabled=false;assert(check.hdrFixedSkyPossible());m.enabled=true;
  {ExtensionDevice::RawScope raw(ext);assert(check.hdrFixedSkyPossible());}
  assert(check.hdrFixedSkyPossible()); // Invalidated bindings never reject a new fixed draw.
 }
 assert(SUCCEEDED(game.SetPixelShader(nullptr))&&SUCCEEDED(game.SetVertexShader(nullptr)));
 ps->Release();vs->Release();
 std::printf("HDR_FIXED_SKY_ADMISSION_PASS cases=%u backendReads=0 mutations=0 unknownFallback=1\n",cases);
 const NorthlightNameStencil::Set states[]={{52,0},{57,0},{59,0},{56,0}};
 const DWORD previous[]={0,1,127,8};
 for(unsigned mask=0;mask<16;++mask){
  for(unsigned i=0;i<4;++i){DWORD got=0;assert(SUCCEEDED(game.SetRenderState(states[i].state,previous[i]+((mask>>i)&1))));
   assert(SUCCEEDED(ext.GetRenderState(states[i].state,&got)));}
  const auto sets=b.sets,reads=b.gets;check.maskRestore(states,previous);
  assert(b.sets-sets==unsigned(__builtin_popcount(mask))&&b.gets==reads);
  for(unsigned i=0;i<4;++i)assert(b.state.rs[states[i].state]==previous[i]);
 }
 for(unsigned mode=0;mode<4;++mode){
  m.invalidate();if(mode==1)m.enabled=false;if(mode==2)m.recording=true;
  const auto sets=b.sets;
  if(mode==3){ExtensionDevice::RawScope raw(ext);check.maskRestore(states,previous);}else check.maskRestore(states,previous);
  assert(b.sets-sets==4);m.enabled=true;m.recording=false;
 }
 // Failed setters invalidate the mirror: the following restoration retries
 // the backend instead of trusting the value written before its failure.
 DWORD got=0;b.failNext=true;assert(FAILED(game.SetRenderState(52,1)));
 const auto sets=b.sets;check.maskRestore(states,previous);assert(b.sets-sets==4);
 assert(SUCCEEDED(ext.GetRenderState(52,&got))&&got==previous[0]);
 std::puts("MASK_RESTORE_PASS combinations=16 unknownModes=4 failureFallback=1 backendReads=0");
}
'''.replace('@ADMISSION@', method).replace('@RESTORE@', restore)
with tempfile.TemporaryDirectory(prefix='hdr-sky-admission-') as folder:
    folder = Path(folder)
    cpp = folder/'admission.cpp'
    cpp.write_bytes(harness.encode())
    exe = folder/('admission.exe' if os.name == 'nt' else 'admission')
    for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', *flags, '-UNDEBUG', '-Wno-return-type', '-pthread',
                        *fp.test_include_flags(), '-I'+str(fp.TESTS), str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
