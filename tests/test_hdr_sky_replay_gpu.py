# northlight-test: requires=windows-gpu,zig manual
"""Compare the production sky replay scope with the previous full-state wrapper."""
import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--backend',type=Path,action='append',required=True)
    args=parser.parse_args();out=args.output.resolve()
    if fp.is_client(out):parser.error('Use an isolated test output directory.')
    out.mkdir(parents=True,exist_ok=False)
    source=fp.src('renderer.cpp').read_text(encoding='utf-8')
    start=source.index('    template<class Draw> void hdrReplaySky(')
    method=source[start:source.index('    template<class Capture,class Draw> HRESULT drawHook(',start)]
    adapter=r'''
#include <stdexcept>
struct ExtensionDevice:DeviceMeter {
    using DeviceMeter::DeviceMeter;
    struct RawScope {explicit RawScope(ExtensionDevice&){};};
    unsigned blocks=0;IDirect3DSurface9* reject=nullptr;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** p)override{++blocks;return real->CreateStateBlock(t,p);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{if(!n&&reject&&p==reject){reject=nullptr;return E_FAIL;}return DeviceMeter::SetRenderTarget(n,p);}
};
struct SkyReplay {
    ExtensionDevice* ext;IDirect3DSurface9* sky;NorthlightStateBlockPool stateBlocks;NorthlightHdr::Color hdrColor;bool hdrSkyReplaying=false;
    SkyReplay(ExtensionDevice* d,IDirect3DSurface9* s):ext(d),sky(s),stateBlocks(d){}
    template<class T>static void drop(T*& p){dropGraph(p);}
    IDirect3DSurface9* hdrSkyTarget(){return sky;}
    void hdrLayerDiagnostic(const char*){}
'''+method+'};\n'
    fixture=(fp.TESTS/'hdr_fixed_sky_gpu.cpp').read_text()
    fixture=fixture.replace('struct SkyVertex {',adapter+'\nstruct SkyVertex {',1)
    fixture=fixture.replace('DeviceMeter d(raw);','ExtensionDevice d(raw);',1)
    fixture=fixture.replace('NorthlightHdr::Color color;NorthlightHdr::SkyForeground capture;', 'SkyReplay replay(&d,sky.surface);NorthlightHdr::Color color;NorthlightHdr::SkyForeground capture;',1)
    fixture=fixture.replace('mode<2;++mode','mode<3;++mode',1)
    original='            else{bool captured=false;'
    candidate=r'''            else if(mode==1){ok(submit());ok(query->Issue(D3DISSUE_END));
                const auto blocks=d.blocks;const auto layers=replay.hdrColor.layers;
                replay.hdrReplaySky(true,false,draw);assert(!replay.hdrSkyReplaying);
                if(replay.hdrColor.layers==layers)assert(d.blocks==blocks);
            }
            else{bool captured=false;'''
    assert fixture.count(original)==1;fixture=fixture.replace(original,candidate,1)
    fixture=fixture.replace('ok(d.SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));', 'const RECT scissor{3,5,W-7,H-11};ok(d.SetScissorRect(&scissor));ok(d.SetRenderState(D3DRS_SCISSORTESTENABLE,control==2));',1)
    # The production reference uses hdrBindBack, which restores viewport and
    # scissor after SetRenderTarget; the original full-viewport fixture did not.
    fixture=fixture.replace('SavedState saved(&d);ok(d.SetRenderTarget(0,sky.surface));', 'SavedState saved(&d);ok(d.SetRenderTarget(0,sky.surface));ok(d.SetViewport(&vp));ok(d.SetScissorRect(&scissor));')
    check=r'''            RECT restored{};ok(d.GetScissorRect(&restored));assert(!std::memcmp(&restored,&scissor,sizeof restored));
            D3DVIEWPORT9 viewport{};ok(d.GetViewport(&viewport));assert(!std::memcmp(&viewport,&vp,sizeof vp));
            IDirect3DSurface9 *restoredTarget=nullptr,*restoredDepth=nullptr;ok(d.GetRenderTarget(0,&restoredTarget));ok(d.GetDepthStencilSurface(&restoredDepth));
            assert(restoredTarget==native.surface&&restoredDepth==depth);restoredTarget->Release();restoredDepth->Release();
            DWORD z=0,stencil=0;ok(d.GetRenderState(D3DRS_ZWRITEENABLE,&z));ok(d.GetRenderState(D3DRS_STENCILWRITEMASK,&stencil));assert(z==DWORD(control==3)&&stencil==255);
'''
    fixture=fixture.replace('            float restoredConstants[16];',check+'            float restoredConstants[16];',1)
    # Keep the complete pixel/query oracle, omit its unrelated MRT benchmark.
    first=fixture.index('    {Surface bigNative,bigSky;');last=fixture.index('    for(auto* t:textures)',first)
    failure=r'''    {
        const unsigned blocks=d.blocks;const auto layers=replay.hdrColor.layers;
        auto skipped=[&]()->HRESULT{assert(false);return E_FAIL;};
        replay.hdrReplaySky(false,false,skipped);replay.hdrReplaySky(true,true,skipped);
        assert(d.blocks==blocks&&replay.hdrColor.layers==layers&&!replay.hdrSkyReplaying);
        IDirect3DSurface9* target=nullptr;ok(d.GetRenderTarget(0,&target));
        IDirect3DSurface9* aux[2]{};for(unsigned i=0;i<2;++i){ok(d.CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,aux+i,nullptr));ok(d.SetRenderTarget(i+1,aux[i]));}
        const RECT scissor{7,9,40,45};ok(d.SetScissorRect(&scissor));
        ok(d.SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d.SetRenderState(D3DRS_STENCILWRITEMASK,0x5a));
        d.reject=sky.surface;replay.hdrReplaySky(true,false,skipped);assert(!d.reject&&!replay.hdrSkyReplaying);
        IDirect3DSurface9* restored=nullptr;ok(d.GetRenderTarget(0,&restored));assert(restored==target);restored->Release();target->Release();
        DWORD z=0,stencil=0;ok(d.GetRenderState(D3DRS_ZWRITEENABLE,&z));ok(d.GetRenderState(D3DRS_STENCILWRITEMASK,&stencil));assert(z==TRUE&&stencil==0x5a);
        RECT rect{};ok(d.GetScissorRect(&rect));assert(!std::memcmp(&rect,&scissor,sizeof rect));
        for(unsigned slot=1;slot<3;++slot){IDirect3DSurface9* surface=nullptr;ok(d.GetRenderTarget(slot,&surface));assert(surface==aux[slot-1]);surface->Release();}
        replay.hdrReplaySky(true,false,[]()->HRESULT{return E_FAIL;});assert(!replay.hdrSkyReplaying);
        bool caught=false;try{replay.hdrReplaySky(true,false,[]()->HRESULT{throw std::runtime_error("injected");});}catch(const std::runtime_error&){caught=true;}
        assert(caught&&!replay.hdrSkyReplaying);
        ok(d.GetRenderState(D3DRS_ZWRITEENABLE,&z));ok(d.GetRenderState(D3DRS_STENCILWRITEMASK,&stencil));assert(z==TRUE&&stencil==0x5a);
        for(unsigned slot=1;slot<3;++slot){IDirect3DSurface9* surface=nullptr;ok(d.GetRenderTarget(slot,&surface));assert(surface==aux[slot-1]);surface->Release();ok(d.SetRenderTarget(slot,nullptr));}for(auto* surface:aux)surface->Release();
        std::printf("HDR_SKY_REPLAY_FAILURE_PASS skipped=2 targetFailure=restored drawFailure=restored exception=restored MRT=restored outerStateBlocks=0\n");
    }
'''
    fixture=fixture[:first]+failure+fixture[last:]
    fixture=fixture.replace('HDR_FIXED_SKY_GPU_PASS','HDR_SKY_REPLAY_GPU_PASS')
    cpp=out/'sky-replay.cpp';cpp.write_text(fixture,encoding='utf-8',newline='\n');exe=out/'sky-replay.exe'
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type','-Wno-microsoft-exception-spec','-I'+str(fp.TESTS),*fp.test_include_flags(),str(cpp),'-luser32','-o',str(exe)]
    r=subprocess.run(command,env=fp.zig_env(),capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr)
    assert r.returncode==0,'\n'.join(r.stderr.splitlines()[-16:])
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report={'renderer_sha256':sha(fp.src('renderer.cpp')),'method':method,'fixture_sha256':sha(cpp),'game_launched':False,'runs':[]}
    for i,backend in enumerate(args.backend):
        backend=backend.resolve();r=subprocess.run([str(exe),str(backend)],cwd=out,env={**os.environ,'DXVK_LOG_PATH':str(out)},capture_output=True,text=True,timeout=180)
        (out/f'{i}.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
        result=[s for s in r.stdout.splitlines() if s.startswith('HDR_SKY_REPLAY')];assert len(result)==2
        report['runs'].append({'backend':str(backend),'sha256':sha(backend),'result':result});print(backend,*result,sep='\n',flush=True)
    report['pass']=True;(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
