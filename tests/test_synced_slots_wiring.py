#!/usr/bin/env python3
# northlight-test:
"""Task 34 dirty record: every way the device state the spell layer's Model is compared with can change must clear the layer's synced bits (synced_slots.h), or the layer would
skip a slot that changed. Static audit of device_mirror.h: the MirrorDevice setters of the tracked slots clear exactly theirs, invalidate() clears all, every other Set* override is
classified (its slot has no bit: the layer examines it on every record); the renderer hands the bits to the mirror and the layer. The behaviour is exercised in test_spell_layer.cpp."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
m=fp.src('device_mirror.h').read_text();r=fp.src('renderer.cpp').read_text();sl=fp.src('spell_layer.h').read_text()
dev=m.split('class MirrorDevice :')[1].split('// Extension calls')[0]
bodies={}
for mt in re.finditer(r'HRESULT STDMETHODCALLTYPE (Set\w+)\(',dev):
    end=dev.find('\n    HRESULT STDMETHODCALLTYPE',mt.end());bodies.setdefault(mt.group(1),dev[mt.start():end if end>0 else len(dev)])
HOOKED={'SetRenderState':'renderState(','SetSamplerState':'sampler(','SetTexture':'texture(','SetViewport':'viewport()','SetRenderTarget':'viewport()',
        'SetVertexShaderConstantF':'vsFloat(','SetPixelShaderConstantF':'psFloat('}
UNTRACKED={'SetVertexShader','SetPixelShader','SetVertexDeclaration','SetFVF','SetIndices','SetStreamSource','SetStreamSourceFreq','SetDepthStencilSurface',
           'SetVertexShaderConstantI','SetVertexShaderConstantB','SetSoftwareVertexProcessing'} # no bit: shaders, declaration, ints/bools are examined on every record; streams, indices, depth are not part of the delta
checks={
 'every tracked setter clears its slot':all(k in bodies and 'm->synced->'+v in bodies[k] and 'm->synced)' in bodies[k] for k,v in HOOKED.items()),
 'SetRenderTarget clears the viewport only for target 0':'if(n==0&&m->synced)m->synced->viewport()' in bodies.get('SetRenderTarget',''),
 'no unclassified Set* override in MirrorDevice (a new setter must be hooked or listed as untracked)':set(bodies)<=set(HOOKED)|UNTRACKED,
 'invalidate() clears every bit (state block Apply, Reset, RawScope entry and exit, vendor states, failed Sets, disable)':re.search(r'void invalidate\(\)\{[^}]*if\(synced\)synced->clearAll\(\);',m) is not None,
 'the mirror holds the bits by pointer, null until the renderer installs them':'SyncedSlots* synced=nullptr;' in m,
 'the renderer gives the bits to the layer and the mirror and takes them back at teardown':'cfg.synced=&spellSynced;mirrorState.synced=&spellSynced;' in r and 'mirrorState.synced=nullptr;' in r,
 'the layer uses the bits only with the state cache in use, and only when no write interleaved with the record':'(cfg_.dirtyRecord&&cfg_.synced&&mv_)' in sl and 'sy->epoch==epoch0' in sl,
 'the layer clears the bits whenever it resets its Model':'model_.reset();if(cfg_.synced)cfg_.synced->clearAll();' in sl,
}
for k,v in checks.items():print('PASS' if v else 'FAIL',k)
assert all(checks.values())
