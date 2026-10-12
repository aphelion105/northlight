#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.212 (task 32) NameMask: floating unit names (world text) mark stencil bit 0x80, WorldComposite leaves marked pixels as the game drew them.

Native part: name_stencil.h (the name blend predicate over lazy state reads, the state tables, the depth-format verdict, the per-frame clear/mark bookkeeping) and the depth format upgrade of stream_hooks.h are driven
through the glue shape renderer.cpp uses (Clear hook, mark/unmark, composite) over fake draw sequences. Source part: the wiring in renderer.cpp, stream_device.h and world_renderer.h (name decision by the UI-hash vertex
shader and the perspective c3, duplicate stencil draw with restore, unmark wrap, Clear flag addition, composite stencil state, guards, depth upgrade) and the keys. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile

HARNESS=r'''
#include "name_stencil.h"
#include "stream_hooks.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
typedef unsigned long DWORD;
using namespace NorthlightNameStencil;
namespace TD=NorthlightTranslucentDepth;
struct Draw{bool water=false;DWORD z=1,blend=1,src=5,dst=6,color=15;};
static bool name(const Draw& d,unsigned* reads=nullptr){
    auto rd=[&](DWORD v){if(reads)++*reads;return v;};
    return nameBlend([&]{return rd(d.blend!=0);},[&]{return rd(d.src);},[&]{return rd(d.dst);},[&]{return rd(d.color);});
}
static bool undoes(const Draw& d){return TD::isUndo(d.water,[&]{return d.z;},[&]{return d.blend!=0;},[&]{return d.src;},[&]{return d.dst;},[&]{return d.color;});}
// A fake device: render states by id, a stencil buffer of one pixel, calls counted.
struct Dev{
    std::map<unsigned,DWORD> rs;unsigned stencil=0;unsigned clears=0,draws=0,sets=0;
    DWORD get(unsigned s){return rs.count(s)?rs[s]:0;}
    void set(unsigned s,DWORD v){rs[s]=v;++sets;}
    void clearStencil(){stencil=0;++clears;}
    void draw(bool depthPass,bool alphaPass){ /* one pixel: depth test result and alpha test result */
        ++draws;if(!get(StencilEnable)||!depthPass)return;
        if(get(AlphaTestEnable)&&!alphaPass)return;
        if(get(StencilFunc)==kCmpAlways&&get(StencilPass)==kOpReplace)stencil=(stencil&~get(StencilWriteMask))|(get(StencilRef)&get(StencilWriteMask));
    }
    bool compositeKeeps(){ /* the composite quad: stencil NOTEQUAL ref&mask */
        for(const auto& s:kComposite)rs[s.state]=s.value;
        return (stencil&get(StencilMask))!=(get(StencilRef)&get(StencilMask));
    }
};
static void apply(Dev& d,const Set* sets,size_t n,DWORD* prev,unsigned ref){for(size_t i=0;i<n;++i)prev[i]=d.get(sets[i].state);for(size_t i=0;i<n;++i)d.set(sets[i].state,markValue(sets[i],ref));}
static void restore(Dev& d,const Set* sets,size_t n,const DWORD* prev){for(size_t i=0;i<n;++i)d.set(sets[i].state,prev[i]);}
int main(){
    // ids and values are the Direct3D 9 ones (renderer.cpp static_asserts them against the SDK headers)
    assert(ZEnable==7&&ZWriteEnable==14&&AlphaTestEnable==15&&ZFunc==23&&AlphaRef==24&&AlphaFunc==25&&StencilEnable==52&&StencilFunc==56&&StencilRef==57&&StencilMask==58&&StencilWriteMask==59&&ColorWriteEnable==168&&kBit==0x80);
    // a name: alpha-blended Z-writing (or not) glyph quads, SRCALPHA source, RGB written, destination neither ZERO nor SRCCOLOR
    { Draw d;assert(name(d)); }                                                              /* SRCALPHA/INVSRCALPHA */
    { Draw d;d.z=0;assert(name(d)); }                                                        /* no Z write: also fine */
    { Draw d;d.dst=TD::kBlendOne;assert(name(d)); }                                          /* SRCALPHA/ONE */
    { Draw d;d.blend=0;assert(!name(d)); }                                                   /* no blending */
    { Draw d;d.color=0;assert(!name(d));d.color=8;assert(!name(d));d.color=1;assert(name(d)); } /* no RGB */
    { Draw d;d.dst=TD::kBlendZero;assert(!name(d)); }                                        /* SRCALPHA/ZERO: effectively opaque */
    { Draw d;d.dst=kBlendSrcColor;assert(!name(d)); }                                        /* modulate */
    { Draw d;d.src=TD::kBlendOne;assert(!name(d));d.src=0;assert(!name(d)); }                /* no SRCALPHA source: no alpha to test */
    { Draw d;unsigned n=0;d.blend=0;name(d,&n);assert(n==2); }                               /* colour, blend: unblended stops there */
    // unmark rule is the early-depth undo rule: opaque Z+RGB or water Z; a translucent name never unmarks itself
    { Draw d;assert(!undoes(d));d.blend=0;assert(undoes(d));d.blend=1;d.water=true;assert(undoes(d));d.water=false;d.z=0;d.blend=0;assert(!undoes(d)); }
    // depth formats: D24S8 75, D24X4S4 79, D15S1 73, D24FS8 81 and INTZ have stencil; D24X8 77, D16 80, D32 71 have none
    assert(hasStencil(75,0x5a544e49)&&hasStencil(79,0)&&hasStencil(73,0)&&hasStencil(81,0)&&hasStencil(0x5a544e49,0x5a544e49)&&!hasStencil(77,0x5a544e49)&&!hasStencil(80,0)&&!hasStencil(71,0));
    // depth upgrade: D24X8 -> D24S8 only while the flag is set and only for D24X8; counted per changed surface
    {   using namespace NorthlightStream;
        assert(upgradedDepthFormat(77)==77&&depthUpgrades.load()==0);
        depthUpgrade.store(true);assert(upgradedDepthFormat(77)==75&&depthUpgrades.load()==1&&upgradedDepthFormat(75)==75&&upgradedDepthFormat(80)==80&&upgradedDepthFormat(0x5a544e49)==0x5a544e49&&depthUpgrades.load()==1);
        depthUpgrade.store(false);assert(upgradedDepthFormat(77)==77); }
    // marking pass: colour off, alpha test at the ref, REPLACE 0x80 where the depth test passes; no Z write, ZFUNC LESSEQUAL (the game's draw wrote Z); the ref comes from the setting
    {   Dev d;d.rs[ZWriteEnable]=1;d.rs[ZFunc]=2;d.rs[ColorWriteEnable]=15;d.rs[AlphaTestEnable]=0;d.rs[AlphaRef]=7;d.rs[StencilEnable]=0;
        DWORD prev[sizeof kMark/sizeof kMark[0]];apply(d,kMark,sizeof kMark/sizeof kMark[0],prev,200);
        assert(d.get(ColorWriteEnable)==0&&d.get(AlphaTestEnable)==1&&d.get(AlphaFunc)==kCmpGreaterEqual&&d.get(AlphaRef)==200&&d.get(StencilEnable)==1&&d.get(StencilRef)==0x80&&d.get(StencilWriteMask)==0x80&&d.get(TwoSidedStencilMode)==0);
        assert(d.get(ZWriteEnable)==0&&d.get(ZFunc)==kCmpLessEqual);
        bool touchesZEnable=false;for(const auto& s:kMark)if(s.state==ZEnable)touchesZEnable=true;assert(!touchesZEnable);
        d.draw(true,true);assert(d.stencil==0x80);d.stencil=0;d.draw(false,true);assert(d.stencil==0);d.draw(true,false);assert(d.stencil==0); /* occluded or faint pixels are not marked */
        restore(d,kMark,sizeof kMark/sizeof kMark[0],prev);assert(d.get(ColorWriteEnable)==15&&d.get(AlphaTestEnable)==0&&d.get(AlphaRef)==7&&d.get(StencilEnable)==0&&d.get(ZWriteEnable)==1&&d.get(ZFunc)==2); }
    // unmark wrap: pixels the depth-final draw covers lose the bit; the rest keep it; restored afterwards
    {   Dev d;d.stencil=0x80;d.rs[StencilEnable]=0;DWORD prev[sizeof kUnmark/sizeof kUnmark[0]];apply(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],prev,96);
        d.draw(false,true);assert(d.stencil==0x80);d.draw(true,true);assert(d.stencil==0);restore(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],prev);assert(d.get(StencilEnable)==0); }
    // composite: marked pixels are rejected, the others pass; nothing is written; depth off
    {   Dev d;d.stencil=0x80;assert(!d.compositeKeeps());d.stencil=0;assert(d.compositeKeeps());
        assert(d.get(ZEnable)==0&&d.get(ZWriteEnable)==0&&d.get(StencilWriteMask)==0&&d.get(StencilEnable)==1&&d.get(StencilFunc)==kCmpNotEqual&&d.get(StencilRef)==0x80&&d.get(StencilMask)==0x80);
        d.stencil=0x7f;assert(d.compositeKeeps()); /* other bits are irrelevant */ }
    // frame bookkeeping: the game's Clear(Z) of the world depth covers the first mark; else one stencil-only clear
    {   Mask m;m.setting=true;assert(m.armed()&&m.needsClear());
        m.noteClear(false);assert(!m.needsClear()&&m.stencilClears==0);m.noteMark();assert(m.hasMarks()&&!m.needsClear());
        m.endFrame();assert(!m.hasMarks()&&m.needsClear()&&m.marksTotal==1);
        m.noteClear(true);assert(m.stencilClears==1&&!m.needsClear());m.noteMark();m.noteMark();assert(m.marks==2&&m.marksTotal==3);
        m.endFrame();assert(m.marks==0&&m.stencilClears==0); }
    { Mask m;assert(!m.armed()); m.setting=true;assert(m.armed());m.disable("x");assert(!m.armed()&&std::string(m.reason)=="x");m.disable("y");assert(std::string(m.reason)=="x"); }
    // glue: a frame with the game's Clear, a mark, an unmark and the composite; a frame without a Clear gets ours; a frame without marks costs no unmark draw
    {   Mask m;m.setting=true;Dev d;d.rs[StencilEnable]=0;
        auto gameClear=[&]{if(m.armed()&&!m.hasMarks()){d.clearStencil();m.noteClear(false);}};
        auto mark=[&](bool depthPass,bool alphaPass){if(m.needsClear()){d.clearStencil();m.noteClear(true);}DWORD p[sizeof kMark/sizeof kMark[0]];apply(d,kMark,sizeof kMark/sizeof kMark[0],p,m.alphaRef);d.draw(depthPass,alphaPass);restore(d,kMark,sizeof kMark/sizeof kMark[0],p);m.noteMark();};
        auto unmark=[&]{if(!m.hasMarks())return;DWORD p[sizeof kUnmark/sizeof kUnmark[0]];apply(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],p,0);d.draw(true,true);restore(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],p);++m.unmarkDraws;};
        d.stencil=0x80;
        gameClear();assert(d.stencil==0&&d.clears==1);
        mark(true,true);assert(d.stencil==0x80&&d.clears==1&&m.stencilClears==0);   /* no extra clear */
        unmark();assert(d.stencil==0&&m.unmarkDraws==1);                             /* solid geometry drawn over the name afterwards */
        mark(true,true);assert(d.stencil==0x80);
        assert(!d.compositeKeeps());
        m.endFrame();
        mark(true,true);assert(m.stencilClears==1&&d.clears==2);                    /* no Clear(Z) seen this frame: one of ours before the first mark */
        m.endFrame();unmark();assert(m.unmarkDraws==0);                              /* no marks: unmark draws are not wrapped */ }
    std::printf("PASS name stencil predicate, tables, depth upgrade and bookkeeping\n");
}
'''

with tempfile.TemporaryDirectory(prefix='northlight-name-mask-') as tmp:
    (Path(tmp)/'t.cpp').write_text(HARNESS)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)

# source checks
r=fp.tracked('renderer.cpp').read_text();q=fp.src('quality_settings.h').read_text();w=fp.src('world_renderer.h').read_text();h=fp.src('name_stencil.h').read_text()
sd=fp.src('stream_device.h').read_text();sh=fp.src('stream_hooks.h').read_text()
checks={}
checks['header: portable, no device calls']=all(x not in h for x in ('d3d9','windows.h','ext->','#include <vector>','new ','malloc'))
impl=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
dom=impl.index('if(worldDomain){');dec=impl.index('nameMaskDecide(');cap=impl.index('capture(vs);')
checks['prepareDrawImpl: kind reset first; decision in the world domain for terrain frames, armed, no debug view, before capture(vs)']=(
    '++drawCalls;maskKind=0;' in impl and dom<dec<cap and 'if(terrain&&nameMask.armed()&&worldDebug==0)nameMaskDecide(drawWaterVS,zw,ab,sb,db,cw,viewport,vs);' in impl)
dc=r[r.index('bool nameMaskReady(){'):r.index('// The bound depth buffer is the frame')]
checks['decision: the game must not use stencil (disable once), the depth format is checked once per surface (logged, disabled without stencil), unmark needs marks, a name needs the full viewport and the world depth']=(
    dc.index('D3DRS_STENCILENABLE')<dc.index('nameMask.disable("the game uses stencil")')<dc.index('worldDepth!=maskFormatFor')<dc.index('NAMEMASK world depth format=')<dc.index('no stencil in the world depth format')
    <dc.index('nameMask.hasMarks()&&NorthlightTranslucentDepth::isUndo(')<dc.index('maskKind=2')<dc.index('maskKind=1'))
checks['names: the UI-hash vertex shader (tag 2) whose c3 is not the screen-space (0,0,0,1) of the boundary, never water, alpha-blended, full viewport, world depth bound']=(
    'if(water||(classifyVs(vs).entry&kTagMask)!=2||!NorthlightNameStencil::nameBlend(ab,sb,db,cw))return;' in dc
    and 'if(FAILED(ext->GetVertexShaderConstantF(3,c3,1))||NorthlightUiBoundary::clipWOne(c3))return;' in dc
    and dc.index('(classifyVs(vs).entry&kTagMask)!=2')<dc.index('GetVertexShaderConstantF(3,c3,1)')<dc.index('viewport.Width==width&&viewport.Height==height&&worldDepthBound()'))
checks['the boundary keeps its own test: tag 2, UI pixel shader and clipWOne(c3) call renderEffects (screen UI is never a name)']=(
    'if (isUI && SUCCEEDED(ext->GetVertexShaderConstantF(3,w,1)) && NorthlightUiBoundary::clipWOne(w)){' in r)
checks['hash UI boundary draws and everything after applied are never marked (prepareDrawImpl returns once applied; the decision sits after beforeDraw)']=(
    impl.index('if(applied){')<impl.index('beforeDraw(vs);')<impl.index('nameMaskDecide('))
mk=r[r.index('template<class Draw> void nameMarkDraw('):r.index('HRESULT nameMaskDraw(')]
checks['mark: a stencil-only Clear before the first mark without a game clear (not with a scissor test), then states set, the draw again, states restored']=(
    mk.index('nameMask.needsClear()')<mk.index('D3DRS_SCISSORTESTENABLE')<mk.index('ext->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1.f,0)')<mk.index('nameMask.noteClear(true)')<mk.index('maskApply(NorthlightNameStencil::kMark,previous)')<mk.index('draw();nameMask.noteMark();')<mk.index('maskRestore(NorthlightNameStencil::kMark,previous)'))
ma=r[r.index('bool maskApply('):r.index('// The marking duplicate right after')]
checks['states are read first, then set (alpha ref from the setting), restore puts the originals back']=ma.index('GetRenderState')<ma.index('SetRenderState')<ma.index('markValue(sets[i],nameMask.alphaRef)')
md=r[r.index('HRESULT nameMaskDraw('):r.index('    // One shader query/reference per original draw')]
checks['draw: unmark states around the game draw; the duplicate only after a successful, unclaimed, non-rain draw, inside an extension region']=(
    md.index('maskApply(NorthlightNameStencil::kUnmark,previous)')<md.index('rainBlendDraw(rain,claimed,draw)',md.index('maskApply('))<md.index('maskRestore(NorthlightNameStencil::kUnmark,previous)')
    and 'if(SUCCEEDED(hr)&&!claimed&&!rain&&nameMask.armed())extensionWork("name mask",[&]{nameMarkDraw(draw);});' in md and 'if(!kind)return rainBlendDraw(rain,claimed,draw);' in md)
hook=r[r.index('template<class Capture,class Draw> HRESULT drawHook('):][:3800]
checks['drawHook: both gate paths end in nameMaskDraw (the draw lambda is a pure ext->Draw* call)']=hook.count('nameMaskDraw(rainBlend,claimed||mist,draw)')==2 and 'rainBlendDraw' not in hook
checks['draw lambdas are pure device draws (safe to issue twice)']=len(re.findall(r'return drawHook\(t,count,\[&\]\(IDirect3DVertexShader9\* vs\)\{[^\n]*?\},draw\);\}',r))==4 and all(x in r for x in ('ext->DrawPrimitive(','ext->DrawIndexedPrimitive('))
clr=r[r.index('HRESULT STDMETHODCALLTYPE Clear('):][:2200]
checks['Clear hook: the stencil flag joins the game\'s full Clear(Z) of the world depth (no rects, scissor off) while the frame has no marks; with marks it is dropped; the mask bit in the game\'s value disables']=(
    'boundDepthIs(maskDsId)' in clr and 'n==0&&SUCCEEDED(ext->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor))&&!scissor' in clr and 'flags|=D3DCLEAR_STENCIL;stencil=0;' in clr and 'flags&=~DWORD(D3DCLEAR_STENCIL)' in clr
    and 'nameMask.noteClear(false)' in clr and 'nameMask.disable("the game clears stencil with the mask bit")' in clr and clr.index('flags|=D3DCLEAR_STENCIL')<clr.index('ext->Clear(n,rects,flags,color,z,stencil)'))
re_=r[r.index('void renderEffects() {'):r.index('/* The guards of every mask decision')]
checks['renderEffects: marks only with an armed feature, a stencil depth, the world depth and no debug view; the world gets the surface for its composite, the legacy composite binds it around its quad']=(
    'const bool maskOn=nameMask.armed()&&nameMask.hasMarks()&&maskStencil&&worldDepth&&worldDebug==0;' in re_ and 'world->setNameMask(maskOn?worldDepth:nullptr);' in re_
    and re_.index('ext->SetDepthStencilSurface(worldDepth)')<re_.index('NorthlightNameStencil::kComposite')<re_.index('const HRESULT hr=quad(width,height);')<re_.index('ext->SetRenderState(D3DRS_STENCILENABLE,FALSE);ext->SetDepthStencilSurface(nullptr);'))
comp=w[w.index('IDirect3DSurface9* maskDepth=nameMaskDepth;'):w.index('if(!compositeOk)return false;composited=true;')]
checks['WorldComposite: the world depth-stencil bound and the stencil test only when the frame has marks; D3DERR_NOTFOUND counts as no depth bound; states and the previous depth-stencil restored; the pointer consumed']=(
    'got==D3DERR_NOTFOUND' in comp and 'nameMaskDepth=nullptr;' in comp and comp.index('GetDepthStencilSurface(&previousDepth)')<comp.index('SetDepthStencilSurface(maskDepth)')<comp.index('NorthlightNameStencil::kComposite')<comp.index('quad(w,h)')
    <comp.index('SetRenderState(D3DRS_STENCILENABLE,previousStencil)')<comp.index('SetDepthStencilSurface(previousDepth)')<comp.index('drop(previousDepth)') and 'void setNameMask(IDirect3DSurface9* depthStencil){nameMaskDepth=depthStencil;}' in w)
checks['debug view: the last effect pass before applied=true when the frame has marks, stencil EQUAL on the bit, ZENABLE off, nothing written']=(
    'if(maskOn&&nameMask.debug)nameMaskDebugView(saved.targets[0]);' in r and r.index('nameMaskDebugView(saved.targets[0]);')<r.index('applied = true; ++appliedFrames;')
    and 'D3DRS_STENCILFUNC,D3DCMP_EQUAL' in r and 'D3DRS_STENCILWRITEMASK,0' in r)
checks['depth upgrade: the key read from the ini at CreateDevice, D24S8 checked with CheckDeviceFormat and CheckDepthStencilMatch before the auto depth and CreateDepthStencilSurface change, Reset too, the stream applies it on the game thread']=(
    'upgradeAutoDepth(real,adapter,type,pp,nameMaskRequested(),"CreateDevice");' in r and 'D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,D3DFMT_D24S8' in r and 'CheckDepthStencilMatch(' in r
    and 'upgradeAutoDepth(parent,cp.AdapterOrdinal,cp.DeviceType,pp,true,"Reset");' in r and 'if(pp&&world&&world->nameMask())' in r
    and 'NorthlightStream::upgradedDepthFormat(unsigned(Format))' in r and 'D3DFORMAT(NorthlightStream::upgradedDepthFormat(unsigned(fmt)))' in sd
    and 'inline unsigned upgradedDepthFormat(unsigned format)' in sh)
checks['kill switch: NameMask=0 never upgrades the depth (wanted=false), marks nothing, adds no clear flag and leaves the composite alone']=(
    'return NorthlightQuality::load(has?&in:nullptr,nullptr,problems).nameMask!=0;' in r and 'nameMask.setting=world->nameMask();' in r and 'bool armed()const{return setting&&!disabled;}' in h
    and 'if(terrain&&nameMask.armed()&&' in r and '(flags&D3DCLEAR_ZBUFFER)&&nameMask.armed()&&maskDsId' in r and 'nameMask.armed()&&nameMask.hasMarks()&&maskStencil' in r)
fin=r[r.index('NAMEMASK frame=')-300:r.index('NAMEMASK frame=')+500]
checks['frame end: one sampled NAMEMASK line (marked, unmarked, composite, depthFormat, stencil), state reset after it']=(
    'marked=%u unmarked=%u composite=%d depthFormat=%lu stencil=%d' in fin and fin.index('NAMEMASK frame=')<fin.index('nameMask.endFrame();'))
checks['one-shot lines: depth upgrade (or why not), stencil in the game, no stencil in the format, failed clear']=all(x in r for x in ('NAMEMASK depth upgraded D24X8->D24S8','NAMEMASK depth upgrade unavailable','NAMEMASK disabled: the game draws with stencil enabled','NAMEMASK disabled: the world depth format has no stencil bits','NAMEMASK disabled: stencil clear failed'))
checks['release forgets the depth identities; the setting is read once at device creation']=('maskFormatFor=nullptr;maskStencil=false;maskDsId=nullptr;' in r[r.index('void releaseResources()'):][:400]
    and 'nameMask.alphaRef=world->nameMaskAlphaRef();nameMask.debug=world->nameMaskDebug();' in r)
keys=q[q.index('inline const Key Keys[]={'):q.index('};',q.index('inline const Key Keys[]={'))]
checks['quality keys: the last three, 0..1, 1..255 and 0..1, presets 1/1/1, 96/96/96 and 0/0/0, own origin slots']=(
    keys.rstrip().endswith('    {"NameMask",&Settings::nameMask,0,1,{1,1,1}}, /* 0.3.212 (task 32) */\n    {"NameMaskAlphaRef",&Settings::nameMaskAlphaRef,1,255,{96,96,96}}, /* 0.3.212 (task 32) */\n    {"NameMaskDebug",&Settings::nameMaskDebug,0,1,{0,0,0}}, /* 0.3.212 (task 32) */')
    and 'unsigned nameMask=1;' in q and 'unsigned nameMaskAlphaRef=96;' in q and 'unsigned nameMaskDebug=0;' in q and 'char origin[49]=' in q and len(re.findall(r"'d'",q[q.index('char origin[49]='):].split('\n')[0]))==49)
ini=(fp.REPO/'renderer'/'windows-package'/'northlight-quality.ini').read_text();txt=(fp.REPO/'renderer'/'windows-package'/'README.txt').read_text()
checks['documented: ini (commented, ranges and presets) and README.txt']=(';NameMask=1\n' in ini and ';NameMaskAlphaRef=96\n' in ini and ';NameMaskDebug=0\n' in ini and 'Allowed 0..1. 1 / 1 / 1' in ini and 'Allowed 1..255. 96 / 96 / 96' in ini
    and re.search(r'^  NameMask +1 / 1 / 1 ',txt,re.M) is not None and re.search(r'^  NameMaskAlphaRef +96 / 96 / 96 ',txt,re.M) is not None and re.search(r'^  NameMaskDebug +0 / 0 / 0 ',txt,re.M) is not None)
checks['banner: 0.3.212 with the name mask']='logf("Northlight renderer 0.3.212;' in r and "floating names keep the game's colour (stencil mask); backend=%s" in r
for n,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+n)
assert all(checks.values())
