#pragma once
// Task 34: the spell layer. Translucent M2 draws (particles, ribbons, spell batches, projected ground effects) are drawn before the effects
// boundary B and so get relight/shadow/AO/fog computed from the background behind them. The layer DEFERS those draws: it records the exact
// draw-relevant device state of each (a state record, see Rec), guards the vertex/index bytes it reads, skips the real draw, and replays all of
// them in order right after the effects at B, against the final world depth. Nothing here logs and nothing calls Windows; only IDirect3DDevice9 (the extension device) and the
// resources reached through it. Single thread: the one that executes the device (replay thread with the stream on, else the game thread); no locks.
//
// Wiring (per draw, before B, in this order):
//   facts=DrawFacts{...}; fade.update(...) feeds facts.fadeRun, layer.pending()>0 is facts.pending; r=classify(facts,mask);
//   Defer     -> if(layer.defer(args,rt0,ds,r.sub)) return D3D_OK (real draw skipped); else draw in place (layer already flushed what was pending)
//   Hazard    -> layer.hazard(r.reason,true,r.inPlace) then draw in place;  InPlace -> layer.noteInPlace(r.inPlace), draw in place;  others -> draw in place.
// Other events: Clear/StretchRect/... -> hazard(Clear|Copy); texture writes -> onTextureWrite(tex); ProcessVertices dest -> onBufferGpuWrite(buf);
// Buffer::Lock BEFORE forwarding, only when the buffer is marked (markGuard callback) -> onBufferWrite(rawBuffer,offset,size,flags,usage,locks);
// EndScene/Present before B -> hazard(Frame, false); frame start -> beginFrame(pressure); at B after the effects -> replay();
// device Reset/destroy/extension fault -> reset() (drops everything, no draw). Keys are the interface pointers GetStreamSource/GetIndices/GetTexture
// return on the device given to attach() (raw, unwrapped objects); the integration must pass those same pointers to the hooks.
// Include order: this header includes saved_state.h itself (pragma once), which needs the D3D9 API and SUCCEEDED/FAILED in scope.
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
#include <d3d9.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>
#include "saved_state.h"

namespace NorthlightSpellLayer {
constexpr unsigned kBlendZero=1,kBlendOne=2,kSrcColor=3,kInvSrcColor=4,kSrcAlpha=5,kInvSrcAlpha=6,kDestAlpha=7,kInvDestAlpha=8,kDestColor=9,kInvDestColor=10,kBlendOpAdd=1; /* D3DBLEND_*, D3DBLENDOP_ADD */
constexpr unsigned kLockReadOnly=0x10,kLockNoOverwrite=0x1000,kLockDiscard=0x2000,kUsageDynamic=0x200; /* D3DLOCK_*, D3DUSAGE_DYNAMIC */
static_assert(unsigned(D3DLOCK_READONLY)==0x10&&unsigned(D3DLOCK_NOOVERWRITE)==0x1000&&unsigned(D3DLOCK_DISCARD)==0x2000&&unsigned(D3DUSAGE_DYNAMIC)==0x200,"lock flag values");
constexpr unsigned kCooldownFrames=30,kMaxStreams=4,kMaxTextures=20,kMaxDecl=65,kMinPsMajor=2; /* kMaxTextures: pixel samplers 0..15 and the vertex texture samplers D3DVERTEXTEXTURESAMPLER0..3 (stages 257..260) */

// ---- a) classification (pure) ----
enum class Family:uint8_t{None,Particle,ParticleUnlit,Model,Projected,Ripples}; // M2 vertex shader family, known at shader creation; Ripples (WaterRipples, not an M2 shader) are treated like water
enum Sub:unsigned{SubAdditive=1,SubAlpha=2,SubModulate=4,SubOther=8};
enum class Kind:uint8_t{Ignore,Opaque,TranslucentZ,Defer,InPlace,Hazard};
enum class InPlaceKind:uint8_t{None,Water,Rain,Name,Fade};
enum class Reason:uint8_t{Clear,Copy,TexWrite,Sample,BufWrite,GpuWrite,Order,Query,Capacity,Capture,Admit,Frame,Count};
constexpr unsigned kReasons=unsigned(Reason::Count);
inline const char* reasonName(Reason r){
    static const char* const n[]={"clear","copy","texwrite","sample","bufwrite","gpuwrite","order","query","capacity","capture","admit","frame","none"};
    return n[unsigned(r)<=kReasons?unsigned(r):kReasons];
}
struct DrawFacts{
    bool zEnable=false,zWrite=false,blend=false,separateAlpha=false,stencil=false;
    unsigned srcBlend=kBlendOne,destBlend=kBlendZero,blendOp=kBlendOpAdd,colorWrite=15; // colorWrite: the RGB bits (7) matter
    Family family=Family::None;
    bool skinned=false,water=false,uiTag=false,rain=false,claimed=false,fixedFunction=false,worldDomain=false;
    bool rt0IsTarget=false;               // RT0 is the layer target (or would become it: the first candidate's back-buffer-sized world target)
    bool dsIsWorldDepth=false,mrtBound=false,instanced=false,occlusionActive=false;
    bool samplesRenderTargetTexture=false; // the draw samples the layer target's container texture
    bool fadeRun=false;                    // FadeRun::update() result for this draw
    bool pending=false;                    // deferred draws are pending (Layer::pending() > 0): water, rain and names must not be drawn under them
};
struct Result{Kind kind=Kind::Ignore;unsigned sub=0;InPlaceKind inPlace=InPlaceKind::None;Reason reason=Reason::Count;};
inline bool opaqueBlend(const DrawFacts& f){return !f.blend||(f.srcBlend==kBlendOne&&f.destBlend==kBlendZero);}
inline unsigned subClass(const DrawFacts& f){
    if(f.separateAlpha||f.blendOp!=kBlendOpAdd)return SubOther;
    if(f.srcBlend==kDestColor||f.destBlend==kSrcColor)return SubModulate;
    if(f.destBlend==kBlendOne)return SubAdditive;
    if(f.destBlend==kInvSrcAlpha)return SubAlpha;
    return SubOther;
}
// Z-writing draws never defer and stay in place (their depth occludes the replay). Colour-writing no-Z draws into the target are deferred when they
// are one of the four families or a fixed-function draw (ribbons, beams; DrawFacts::fixedFunction) with a non-opaque blend. Water ripples (Family::Ripples) count as water. Water, rain and floating names are drawn in place when nothing is pending (InPlace, no
// effect), but a Hazard/Order while draws are pending: drawn in place they would end up UNDER the replayed spells, so the caller flushes first and
// the original order holds (Result::inPlace names which of them it was; Layer::hazard() then splits the frame instead of falling back). A fade colour pass stays an accepted reorder (its depth pre-pass occludes
// the replay). Everything else is a Hazard (unknown order dependency while draws are pending: the caller flushes). Claimed draws and draws into other targets are Ignore
// (unless they sample the target: Hazard/Sample). A non-world draw that writes colour into the target is a Hazard.
inline Result classify(const DrawFacts& f,unsigned classMask){
    Result r;
    auto hazard=[&](Reason why){r.kind=Kind::Hazard;r.reason=why;return r;};
    if(f.samplesRenderTargetTexture)return hazard(Reason::Sample);
    if(!f.rt0IsTarget||f.claimed)return r;
    const bool rgb=(f.colorWrite&7)!=0;
    if(!f.worldDomain)return rgb?hazard(Reason::Order):r;
    if(f.zWrite){r.kind=(opaqueBlend(f)||!rgb)?Kind::Opaque:Kind::TranslucentZ;return r;}
    if(!rgb)return r;
    const bool water=f.water||f.family==Family::Ripples; // water ripples are drawn after the spells by the game: they behave like water
    if(water||f.rain||f.uiTag||(f.fadeRun&&f.skinned)){
        r.inPlace=water?InPlaceKind::Water:f.rain?InPlaceKind::Rain:f.uiTag?InPlaceKind::Name:InPlaceKind::Fade;
        if(f.pending&&r.inPlace!=InPlaceKind::Fade)return hazard(Reason::Order);
        r.kind=Kind::InPlace;return r;
    }
    if((f.family==Family::None&&!f.fixedFunction)||opaqueBlend(f)||!f.zEnable||f.stencil||!f.dsIsWorldDepth||f.mrtBound||f.instanced)return hazard(Reason::Order);
    if(f.occlusionActive)return hazard(Reason::Query);
    const unsigned sub=subClass(f);
    if(!(classMask&sub))return hazard(Reason::Order);
    r.kind=Kind::Defer;r.sub=sub;return r;
}
// A skinned Z-writing colour-less draw (the depth pre-pass of a fading actor) starts a run; the next non-skinned draw ends it.
struct FadeRun{
    bool active=false;
    bool update(bool skinned,bool zWrite,unsigned colorWrite){
        if(skinned&&zWrite&&(colorWrite&7)==0)active=true;else if(!skinned)active=false;
        return active;
    }
    void reset(){active=false;}
};

// ---- b) frame state machine and counters (pure) ----
enum class Mode:uint8_t{Off,Armed,Deferring,Fallback,Cooldown};
inline const char* modeName(Mode m){static const char* const n[]={"off","armed","deferring","fallback","cooldown"};return n[unsigned(m)];}
// Fallback: a hazard flushed (or a defer failed) this frame, or B has passed (frame closed): every later candidate is drawn in place. A hazard
// frame is followed by kCooldownFrames Cooldown frames; memory pressure gives Cooldown frames without counting them down.
class FrameState{
    Mode mode_=Mode::Off;unsigned cooldown_=0;bool cooldownNext_=false;
public:
    Mode mode()const{return mode_;}
    unsigned cooldownLeft()const{return cooldown_;}
    bool canDefer()const{return mode_==Mode::Armed||mode_==Mode::Deferring;}
    void begin(bool on,bool pressure){
        if(mode_==Mode::Fallback&&cooldownNext_)cooldown_=kCooldownFrames;
        cooldownNext_=false;
        if(!on)mode_=Mode::Off;
        else if(pressure)mode_=Mode::Cooldown;
        else if(cooldown_){--cooldown_;mode_=Mode::Cooldown;}
        else mode_=Mode::Armed;
    }
    void deferred(){if(mode_==Mode::Armed)mode_=Mode::Deferring;}
    void fallback(bool cooldown){if(canDefer()){mode_=Mode::Fallback;cooldownNext_=cooldown;}}
    void close(){fallback(false);} // B reached: no deferral for the rest of the frame, no cooldown
};
struct Counters{
    unsigned deferred[4]={},inPlace[4]={};        // by sub-class (additive, alpha, modulate, other); accepted in-place draws by kind (only fade is counted)
    unsigned orderBy[3]={};                       // Order hazards raised by water, rain, name draws while draws were pending
    unsigned hazards[kReasons]={};
    unsigned flushes=0,replays=0,replayedDraws=0,replaySkipped=0,discarded=0;
    unsigned fixed=0;                             // deferred draws that were fixed-function (also counted in deferred[])
    unsigned conflicts=0,materialized=0,recorded=0,replaySets=0; // recorded: state records taken; replaySets: device Set* calls issued by replays
    uint64_t conflictBytes=0;
    unsigned arenaHigh=0;                         // bytes
    unsigned recordHigh=0;                        // most records pending at once
    void add(const Counters& o){
        for(int i=0;i<4;++i){deferred[i]+=o.deferred[i];inPlace[i]+=o.inPlace[i];}
        for(int i=0;i<3;++i)orderBy[i]+=o.orderBy[i];
        for(unsigned i=0;i<kReasons;++i)hazards[i]+=o.hazards[i];
        fixed+=o.fixed;flushes+=o.flushes;replays+=o.replays;replayedDraws+=o.replayedDraws;replaySkipped+=o.replaySkipped;discarded+=o.discarded;
        conflicts+=o.conflicts;materialized+=o.materialized;recorded+=o.recorded;replaySets+=o.replaySets;conflictBytes+=o.conflictBytes;
        arenaHigh=std::max(arenaHigh,o.arenaHigh);recordHigh=std::max(recordHigh,o.recordHigh);
    }
    unsigned totalDeferred()const{return deferred[0]+deferred[1]+deferred[2]+deferred[3];}
    unsigned totalHazards()const{unsigned n=0;for(unsigned h:hazards)n+=h;return n;}
};

// ---- c) guard bookkeeping (pure) ----
// Per raw buffer a list of [lo,hi) byte ranges and the entry (and stream slot, 0xFF = index buffer) owning each. A write conflicts when it is not
// READONLY and is DISCARD, size 0 (whole), or overlaps a live range; NOOVERWRITE outside every range is free. A bounding box per buffer rejects the
// ring-buffer append (the common write) in O(1); only a write inside the box scans the ranges. Slots are reused across frames (no allocation once warm).
class GuardTable{
public:
    struct Range{uint32_t lo,hi,entry;uint8_t slot;bool live;};
    struct Hit{uint32_t buf,range;};
    struct Buf{void* key=nullptr;bool index=false;unsigned live=0;uint32_t boxLo=0,boxHi=0;std::vector<Range> ranges;};
private:
    std::vector<Buf> bufs_;size_t used_=0;
    static bool overlaps(const Range& r,uint64_t lo,uint64_t hi){return r.live&&r.lo<hi&&lo<r.hi;}
public:
    const Buf* find(void* key)const{for(size_t i=0;i<used_;++i)if(bufs_[i].key==key)return &bufs_[i];return nullptr;}
    int findIndex(void* key)const{for(size_t i=0;i<used_;++i)if(bufs_[i].key==key)return int(i);return -1;}
    bool guarded(void* key)const{const Buf* b=find(key);return b&&b->live;}
    const Buf& buf(uint32_t i)const{return bufs_[i];}
    size_t buffers()const{return used_;}
    // True when this is the buffer's first live guard (the caller marks the buffer).
    bool add(void* key,bool index,uint32_t lo,uint32_t hi,uint32_t entry,uint8_t slot){
        if(lo>=hi)return false;
        int i=findIndex(key);
        if(i<0){
            if(used_==bufs_.size())bufs_.emplace_back();
            i=int(used_++);Buf& b=bufs_[size_t(i)];b.key=key;b.index=index;b.live=0;b.boxLo=lo;b.boxHi=hi;b.ranges.clear();
        }
        Buf& b=bufs_[size_t(i)];
        const bool first=b.live==0;
        if(first){b.boxLo=lo;b.boxHi=hi;}else{b.boxLo=std::min(b.boxLo,lo);b.boxHi=std::max(b.boxHi,hi);}
        b.ranges.push_back(Range{lo,hi,entry,slot,true});++b.live;
        return first;
    }
    static bool writeConflict(const Buf& b,uint32_t offset,uint32_t size,uint32_t flags){
        if(!b.live||(flags&kLockReadOnly))return false;
        if((flags&kLockDiscard)||!size)return true;
        const uint64_t lo=offset,hi=lo+size;
        if(lo>=b.boxHi||hi<=b.boxLo)return false;
        for(const Range& r:b.ranges)if(overlaps(r,lo,hi))return true;
        return false;
    }
    bool conflicts(void* key,uint32_t offset,uint32_t size,uint32_t flags)const{const Buf* b=find(key);return b&&writeConflict(*b,offset,size,flags);}
    // Appends the live ranges the write conflicts with; returns how many.
    size_t collect(void* key,uint32_t offset,uint32_t size,uint32_t flags,std::vector<Hit>& out)const{
        const int i=findIndex(key);if(i<0||!writeConflict(bufs_[size_t(i)],offset,size,flags))return 0;
        const Buf& b=bufs_[size_t(i)];const bool all=(flags&kLockDiscard)||!size;const uint64_t lo=offset,hi=lo+size;size_t n=0;
        for(size_t k=0;k<b.ranges.size();++k)if(b.ranges[k].live&&(all||overlaps(b.ranges[k],lo,hi))){out.push_back(Hit{uint32_t(i),uint32_t(k)});++n;}
        return n;
    }
    // Drops one range; true when the buffer has no live guard left (the caller unmarks it).
    bool drop(uint32_t buf,uint32_t range){
        Buf& b=bufs_[buf];Range& r=b.ranges[range];
        if(r.live){r.live=false;--b.live;}
        return b.live==0;
    }
    const Range& range(const Hit& h)const{return bufs_[h.buf].ranges[h.range];}
    // Calls unmark(key) for every buffer that still has a live guard, then forgets all ranges (capacity kept).
    template<class F> void clear(F&& unmark){
        for(size_t i=0;i<used_;++i){Buf& b=bufs_[i];if(b.live)unmark(b.key);b.key=nullptr;b.live=0;b.ranges.clear();}
        used_=0;
    }
};

// ---- d) the layer (execution) ----
// A texture created with D3DUSAGE_RENDERTARGET (2D or cube; a volume texture cannot be one): its contents change by rendering, which is invisible to the
// layer (SetRenderTarget is not hooked in stream mode), so a pending draw sampling it is refused. A level the driver cannot describe counts as one.
inline bool isRenderTargetTexture(IDirect3DBaseTexture9* t){
    D3DSURFACE_DESC d{};HRESULT hr=D3DERR_INVALIDCALL;
    switch(t->GetType()){
    case D3DRTYPE_TEXTURE:hr=static_cast<IDirect3DTexture9*>(t)->GetLevelDesc(0,&d);break;
    case D3DRTYPE_CUBETEXTURE:hr=static_cast<IDirect3DCubeTexture9*>(t)->GetLevelDesc(0,&d);break;
    default:return false;
    }
    return FAILED(hr)||(d.Usage&D3DUSAGE_RENDERTARGET)!=0;
}
enum class DrawType:uint8_t{Primitive,Indexed,PrimitiveUP,IndexedUP};
inline uint32_t vertexCount(unsigned prim,uint32_t primCount){ // D3DPT_* vertex count of primCount primitives
    switch(prim){case 1:return primCount;case 2:return primCount*2;case 3:return primCount+1;case 4:return primCount*3;case 5:case 6:return primCount+2;default:return 0;}
}
// The arguments of the draw call, as the game made it. `first`: StartVertex (Primitive) or StartIndex (Indexed). UP data pointers are only read
// during defer(). Indexed: baseVertex/minIndex/numVertices as in DrawIndexedPrimitive; IndexedUP: minIndex/numVertices/indexData/indexFormat/vertexData.
struct DrawArgs{
    DrawType type=DrawType::Primitive;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;
    INT baseVertex=0;UINT minIndex=0,numVertices=0,first=0,primCount=0;
    const void* vertexData=nullptr;UINT stride=0;const void* indexData=nullptr;D3DFORMAT indexFormat=D3DFMT_INDEX16;
    unsigned psMajor=0;  // shader model major version of the bound pixel shader (from its version token at registration; 0 = unknown): defer() admits only >= kMinPsMajor
    static DrawArgs primitive(D3DPRIMITIVETYPE p,UINT start,UINT count){DrawArgs a;a.type=DrawType::Primitive;a.prim=p;a.first=start;a.primCount=count;return a;}
    static DrawArgs indexed(D3DPRIMITIVETYPE p,INT base,UINT minIdx,UINT num,UINT startIndex,UINT count){
        DrawArgs a;a.type=DrawType::Indexed;a.prim=p;a.baseVertex=base;a.minIndex=minIdx;a.numVertices=num;a.first=startIndex;a.primCount=count;return a;
    }
    static DrawArgs primitiveUP(D3DPRIMITIVETYPE p,UINT count,const void* data,UINT stride){
        DrawArgs a;a.type=DrawType::PrimitiveUP;a.prim=p;a.primCount=count;a.vertexData=data;a.stride=stride;return a;
    }
    static DrawArgs indexedUP(D3DPRIMITIVETYPE p,UINT minIdx,UINT num,UINT count,const void* idx,D3DFORMAT fmt,const void* data,UINT stride){
        DrawArgs a;a.type=DrawType::IndexedUP;a.prim=p;a.minIndex=minIdx;a.numVertices=num;a.primCount=count;a.indexData=idx;a.indexFormat=fmt;a.vertexData=data;a.stride=stride;return a;
    }
};
struct Config{
    bool on=false;unsigned classMask=SubAdditive|SubAlpha|SubModulate|SubOther;
    unsigned maxEntries=512;size_t arenaBytes=4u<<20,uploadVbBytes=1u<<20,uploadIbBytes=256u<<10;
    bool conflictCopy=true;                       // false on a non-DXVK backend: a conflicting write flushes instead of copying
    DWORD readBackLockFlags=D3DLOCK_READONLY;     // NorthlightUpload::readBackLock()
    void (*markGuard)(void* ctx,void* rawBuffer,bool guarded)=nullptr;void* markCtx=nullptr; // sets/clears the per-buffer "call onBufferWrite" flag
    NorthlightStateBlockPool* savedPool=nullptr;  // the renderer's SavedState pool for the replay scope (optional; the only state block the layer still uses)
    unsigned (*generation)(void* ctx)=nullptr;void* genCtx=nullptr; // changes whenever a texture object may have been created or destroyed (a raw address may name another texture): keys the per-texture render-target verdicts; null = no verdict cache
};

// The state record of one deferred draw. Admission (defer() refuses everything else): a vertex shader AND a pixel shader of model >= kMinPsMajor are
// bound, or neither (a fixed-function draw). With both programmable stages bound D3D9 does not read the fixed-function state: texture stage states,
// the world/view/projection/texture transforms, lights and material are unused by the draw, so none of them is recorded (Rec). A fixed-function entry
// additionally has an FfRec (transforms, texture stage states of stages 0..7, material, lights 0..7); it is refused when vertex blending is enabled
// (WORLD1..3), and a replay diffs it against the previous fixed-function entry only (programmable entries leave that state alone). Everything else a draw can
// read is recorded here: all render states, the sampler states 1..13 of every stage with a texture bound (pixel samplers 0..15, vertex texture
// samplers 257..260; a stage without a texture is not sampled), all shader constants (float, int, bool of both stages), viewport, scissor rect, the
// user clip planes. Shaders, vertex declaration, textures, streams and the index buffer are held by the Entry (references) because a record is
// replayed after the game released them. Fixed capacity, no pointers: 32-bit layout, about 10 KiB (recordBytes()).
constexpr unsigned kVsFloat=256,kPsFloat=224,kInts=16,kBools=16,kClipPlanes=6,kSamplerFirst=1,kSamplerStates=13;
// The fixed-function state a draw with neither vertex nor pixel shader reads, recorded in FfRec (only for such draws): WORLD, VIEW, PROJECTION and
// TEXTURE0..7 transforms, the 18 valid texture stage states of stages 0..7, the material and lights 0..7. WORLD1..3 are never read: vertex blending is refused.
constexpr unsigned kFfTransforms=11,kFfStages=8,kTssStates=18,kFfLights=8;
inline constexpr uint16_t kFfTransformIds[kFfTransforms]={256,2,3,16,17,18,19,20,21,22,23}; // D3DTS_WORLD, VIEW, PROJECTION, TEXTURE0..7
inline constexpr uint16_t kTssIds[kTssStates]={1,2,3,4,5,6,7,8,9,10,11,22,23,24,26,27,28,32}; // D3DTSS_COLOROP .. D3DTSS_CONSTANT (the types that exist; 12..21, 25, 29..31 do not)
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
static_assert(unsigned(D3DTS_WORLD)==256&&unsigned(D3DTS_VIEW)==2&&unsigned(D3DTS_PROJECTION)==3&&unsigned(D3DTS_TEXTURE0)==16&&unsigned(D3DTS_TEXTURE7)==23,"transform ids");
static_assert(unsigned(D3DTSS_COLOROP)==1&&unsigned(D3DTSS_COLORARG1)==2&&unsigned(D3DTSS_COLORARG2)==3&&unsigned(D3DTSS_ALPHAOP)==4&&unsigned(D3DTSS_ALPHAARG1)==5&&unsigned(D3DTSS_ALPHAARG2)==6&&
    unsigned(D3DTSS_BUMPENVMAT00)==7&&unsigned(D3DTSS_BUMPENVMAT01)==8&&unsigned(D3DTSS_BUMPENVMAT10)==9&&unsigned(D3DTSS_BUMPENVMAT11)==10&&unsigned(D3DTSS_TEXCOORDINDEX)==11&&
    unsigned(D3DTSS_BUMPENVLSCALE)==22&&unsigned(D3DTSS_BUMPENVLOFFSET)==23&&unsigned(D3DTSS_TEXTURETRANSFORMFLAGS)==24&&unsigned(D3DTSS_COLORARG0)==26&&unsigned(D3DTSS_ALPHAARG0)==27&&
    unsigned(D3DTSS_RESULTARG)==28&&unsigned(D3DTSS_CONSTANT)==32,"texture stage state ids");
#endif
// Every valid D3DRENDERSTATETYPE (103 values; d3d9types.h), id and name. The real build asserts each value against the SDK.
#define NL_SPELL_RS_LIST(X) \
    X(ZENABLE,7) X(FILLMODE,8) X(SHADEMODE,9) X(ZWRITEENABLE,14) X(ALPHATESTENABLE,15) X(LASTPIXEL,16) X(SRCBLEND,19) X(DESTBLEND,20) X(CULLMODE,22) \
    X(ZFUNC,23) X(ALPHAREF,24) X(ALPHAFUNC,25) X(DITHERENABLE,26) X(ALPHABLENDENABLE,27) X(FOGENABLE,28) X(SPECULARENABLE,29) X(FOGCOLOR,34) \
    X(FOGTABLEMODE,35) X(FOGSTART,36) X(FOGEND,37) X(FOGDENSITY,38) X(RANGEFOGENABLE,48) X(STENCILENABLE,52) X(STENCILFAIL,53) X(STENCILZFAIL,54) \
    X(STENCILPASS,55) X(STENCILFUNC,56) X(STENCILREF,57) X(STENCILMASK,58) X(STENCILWRITEMASK,59) X(TEXTUREFACTOR,60) \
    X(WRAP0,128) X(WRAP1,129) X(WRAP2,130) X(WRAP3,131) X(WRAP4,132) X(WRAP5,133) X(WRAP6,134) X(WRAP7,135) \
    X(CLIPPING,136) X(LIGHTING,137) X(AMBIENT,139) X(FOGVERTEXMODE,140) X(COLORVERTEX,141) X(LOCALVIEWER,142) X(NORMALIZENORMALS,143) \
    X(DIFFUSEMATERIALSOURCE,145) X(SPECULARMATERIALSOURCE,146) X(AMBIENTMATERIALSOURCE,147) X(EMISSIVEMATERIALSOURCE,148) X(VERTEXBLEND,151) X(CLIPPLANEENABLE,152) \
    X(POINTSIZE,154) X(POINTSIZE_MIN,155) X(POINTSPRITEENABLE,156) X(POINTSCALEENABLE,157) X(POINTSCALE_A,158) X(POINTSCALE_B,159) X(POINTSCALE_C,160) \
    X(MULTISAMPLEANTIALIAS,161) X(MULTISAMPLEMASK,162) X(PATCHEDGESTYLE,163) X(DEBUGMONITORTOKEN,165) X(POINTSIZE_MAX,166) X(INDEXEDVERTEXBLENDENABLE,167) \
    X(COLORWRITEENABLE,168) X(TWEENFACTOR,170) X(BLENDOP,171) X(POSITIONDEGREE,172) X(NORMALDEGREE,173) X(SCISSORTESTENABLE,174) X(SLOPESCALEDEPTHBIAS,175) \
    X(ANTIALIASEDLINEENABLE,176) X(MINTESSELLATIONLEVEL,178) X(MAXTESSELLATIONLEVEL,179) X(ADAPTIVETESS_X,180) X(ADAPTIVETESS_Y,181) X(ADAPTIVETESS_Z,182) \
    X(ADAPTIVETESS_W,183) X(ENABLEADAPTIVETESSELLATION,184) X(TWOSIDEDSTENCILMODE,185) X(CCW_STENCILFAIL,186) X(CCW_STENCILZFAIL,187) X(CCW_STENCILPASS,188) \
    X(CCW_STENCILFUNC,189) X(COLORWRITEENABLE1,190) X(COLORWRITEENABLE2,191) X(COLORWRITEENABLE3,192) X(BLENDFACTOR,193) X(SRGBWRITEENABLE,194) X(DEPTHBIAS,195) \
    X(WRAP8,198) X(WRAP9,199) X(WRAP10,200) X(WRAP11,201) X(WRAP12,202) X(WRAP13,203) X(WRAP14,204) X(WRAP15,205) \
    X(SEPARATEALPHABLENDENABLE,206) X(SRCBLENDALPHA,207) X(DESTBLENDALPHA,208) X(BLENDOPALPHA,209)
#define NL_SPELL_RS_ID(n,v) v,
inline constexpr uint16_t kRenderStateIds[]={NL_SPELL_RS_LIST(NL_SPELL_RS_ID)};
constexpr unsigned kRenderStates=unsigned(sizeof kRenderStateIds/sizeof kRenderStateIds[0]);
static_assert(kRenderStates==103,"the full D3D9 render state list");
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
#define NL_SPELL_RS_CHECK(n,v) static_assert(unsigned(D3DRS_##n)==v,"D3DRS_" #n);
NL_SPELL_RS_LIST(NL_SPELL_RS_CHECK)
#undef NL_SPELL_RS_CHECK
#endif
#undef NL_SPELL_RS_ID
// States the backend gives side effects beyond a stored value (vendor hacks: POINTSIZE carries the RESZ/alpha-to-coverage codes, ADAPTIVETESS_X/Y the
// ATOC/A2M codes). Replay sets them only when the device value differs from the record, so a re-set never repeats the action.
inline constexpr bool vendorHackState(unsigned id){return id==154||id==180||id==181;}
constexpr unsigned rsIndex(unsigned id){for(unsigned i=0;i<kRenderStates;++i)if(kRenderStateIds[i]==id)return i;return kRenderStates;}
constexpr unsigned kRsClipPlaneEnable=rsIndex(152),kRsScissorTest=rsIndex(174),kRsVertexBlend=rsIndex(151),kRsIndexedVertexBlend=rsIndex(167); // indices into kRenderStateIds
inline DWORD samplerStage(unsigned slot){return slot<16?DWORD(slot):DWORD(257+(slot-16));}

class Layer{
    struct Stream{IDirect3DVertexBuffer9* vb=nullptr;uint32_t off=0,stride=0,lo=0,hi=0,sno=0;int32_t blob=-1;};
    struct Rec{
        DWORD fvf;                                    // FVF when no declaration is bound (else 0)
        DWORD rs[kRenderStates];
        DWORD ss[kMaxTextures][kSamplerStates];       // valid for stages whose Entry::tex is set
        float vsF[kVsFloat][4];int vsI[kInts][4];BOOL vsB[kBools];
        float psF[kPsFloat][4];int psI[kInts][4];BOOL psB[kBools];
        D3DVIEWPORT9 vp;RECT scissor;float clip[kClipPlanes][4];
        uint8_t clipMask;bool scissorOn;                // clip planes recorded (D3DRS_CLIPPLANEENABLE bits) and the scissor rect (only when D3DRS_SCISSORTESTENABLE): a disabled plane / scissor is not read by the draw
    };
    struct FfRec{                                     // only for fixed-function entries (Entry::ff), allocated in chunks of kFfChunk
        D3DMATRIX tf[kFfTransforms];DWORD tss[kFfStages][kTssStates];D3DMATERIAL9 mat;
        D3DLIGHT9 light[kFfLights];BOOL lightOn[kFfLights];bool lightSet[kFfLights]; // a light never set fails GetLight: lightSet=false (light zeroed, off)
    };
    static constexpr unsigned kRecChunk=16,kFfChunk=8;           // records are allocated in chunks as a frame needs them and released by trim()
    struct Entry{
        DrawType type=DrawType::Primitive;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;
        INT base=0;UINT minIndex=0,numVertices=0,first=0,primCount=0;
        uint8_t nStreams=0,isz=0;Stream streams[kMaxStreams];
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr; // held (references)
        IDirect3DIndexBuffer9* ib=nullptr;uint32_t ibLo=0,ibHi=0;int32_t ibBlob=-1;
        uint32_t upVtx=0,upStride=0,upIdx=0;D3DFORMAT upFmt=D3DFMT_INDEX16;
        IDirect3DBaseTexture9* tex[kMaxTextures]={};  // by slot (pixel samplers 0..15, vertex texture samplers 16..19), null = unbound; held
        Rec* rec=nullptr;FfRec* ff=nullptr;            // ff: set for fixed-function draws (no vertex and no pixel shader), else null
    };
    // What replay itself set on the device (streams and index buffer are rebound for materialized spans, so they are tracked apart from the records).
    struct Shadow{
        struct S{IDirect3DVertexBuffer9* vb=nullptr;UINT off=0,stride=0;bool valid=false;} st[16];
        uint32_t freq=0;IDirect3DIndexBuffer9* ib=nullptr;bool ibValid=false;
    };
    // Conflict copy of one merged byte span of a buffer; entries share it. fmt (index blobs): 0 = 16-bit, 1 = 32-bit indices; the blob sits in the
    // upload index buffer of that format.
    struct Blob{uint32_t arenaOff,len,base,uploadOff;bool index;uint8_t fmt;};
    struct Seg{uint32_t lo,hi;size_t firstHit,lastHit;};
public:
    // The last event that made the frame fall back (a hazard that flushed or a refused defer), for the integration's diagnostics. eventSeq() changes
    // with every event; pending: entries pending when it happened (0: nothing was flushed, the frame still fell back).
    struct Event{Reason reason=Reason::Count;InPlaceKind by=InPlaceKind::None;unsigned pending=0;};
    static constexpr size_t recordBytes(){return sizeof(Rec);}
    static constexpr size_t ffRecordBytes(){return sizeof(FfRec);}
private:
    IDirect3DDevice9* dev_=nullptr;Config cfg_;FrameState fm_;
    std::vector<Entry> entries_;unsigned count_=0;
    std::vector<std::unique_ptr<Rec[]>> recChunks_;
    std::vector<std::unique_ptr<FfRec[]>> ffChunks_;unsigned ffCount_=0;const FfRec* ffPrev_=nullptr; // ffPrev_: the last fixed-function record the running replay applied
    bool rsSkip_[kRenderStates]={};                   // a state the device refused to Get once: neither recorded nor replayed
    IDirect3DVertexDeclaration9* declFor_=nullptr;uint32_t declUsed_=0; // used-stream mask of the last declaration seen this frame (its entry holds it)
    unsigned sets_=0;                                 // device Set* calls of the running replay
    Event event_;unsigned eventSeq_=0;
    std::vector<uint8_t> arena_;uint32_t arenaUsed_=0;
    std::vector<Blob> blobs_;
    GuardTable guards_;std::vector<GuardTable::Hit> hits_;std::vector<Seg> segs_;std::vector<uint32_t> order_;
    std::vector<void*> samples_;
    IDirect3DVertexBuffer9* upVb_=nullptr;IDirect3DIndexBuffer9* upIb_[2]={};uint32_t vbUsed_=0,ibUsed_[2]={}; /* one upload index buffer per index format: a 32-bit range must not be read as 16-bit */
    IDirect3DSurface9* target_=nullptr;IDirect3DSurface9* ds_=nullptr;
    struct Verdict{void* key=nullptr;bool rt=false;};  // per texture identity: D3DUSAGE_RENDERTARGET never changes for an object; open addressing, cleared when the generation changes or at 3/4 load
    static constexpr unsigned kVerdicts=1024;std::vector<Verdict> verdicts_;unsigned verdictCount_=0,verdictGen_=0;
    Counters frame_,last_,total_;bool firstHazard_=false,firstTaken_=false;Reason firstReason_=Reason::Count;

    template<class F> void count(F&& f){f(frame_);f(total_);}
    static uint32_t align16(uint32_t n){return (n+15u)&~15u;}
    void mark(void* key,bool on){if(cfg_.markGuard)cfg_.markGuard(cfg_.markCtx,key,on);}
    Rec* recAt(unsigned i){return &recChunks_[i/kRecChunk][i%kRecChunk];}
    bool ensureRec(unsigned i){
        while(i/kRecChunk>=recChunks_.size()){
            Rec* p=new(std::nothrow) Rec[kRecChunk];if(!p)return false; // uninitialised: defer() fills every field it uses
            recChunks_.emplace_back(p); // reserved at configure()
        }
        return true;
    }
    FfRec* ffAt(unsigned i){return &ffChunks_[i/kFfChunk][i%kFfChunk];}
    bool ensureFf(unsigned i){
        while(i/kFfChunk>=ffChunks_.size()){
            FfRec* p=new(std::nothrow) FfRec[kFfChunk];if(!p)return false; // uninitialised: readFf() fills every field
            ffChunks_.emplace_back(p);
        }
        return true;
    }
    void releaseEntry(Entry& e){
        for(unsigned i=0;i<e.nStreams;++i)if(e.streams[i].vb)e.streams[i].vb->Release();
        if(e.ib)e.ib->Release();
        if(e.vs)e.vs->Release();
        if(e.ps)e.ps->Release();
        if(e.decl){e.decl->Release();declFor_=nullptr;}
        for(auto* t:e.tex)if(t)t->Release();
        e=Entry{};
    }
    void clearFrame(){
        guards_.clear([&](void* key){mark(key,false);}); // before the entries drop their references: the marked buffers are still alive
        for(unsigned i=0;i<count_;++i)releaseEntry(entries_[i]);
        count_=0;ffCount_=0;samples_.clear();blobs_.clear();hits_.clear();declFor_=nullptr;
        frame_.arenaHigh=std::max(frame_.arenaHigh,arenaUsed_);total_.arenaHigh=std::max(total_.arenaHigh,arenaUsed_);
        arenaUsed_=vbUsed_=ibUsed_[0]=ibUsed_[1]=0;
        if(target_){target_->Release();target_=nullptr;}
        if(ds_){ds_->Release();ds_=nullptr;}
    }
    void releaseUploads(){
        if(upVb_){upVb_->Release();upVb_=nullptr;}
        for(auto*& ib:upIb_)if(ib){ib->Release();ib=nullptr;}
    }
    void clearVerdicts(){if(verdictCount_){std::fill(verdicts_.begin(),verdicts_.end(),Verdict{});verdictCount_=0;}}
    bool renderTargetTexture(IDirect3DBaseTexture9* t){
        if(!cfg_.generation)return isRenderTargetTexture(t);
        if(verdicts_.empty())verdicts_.resize(kVerdicts);
        size_t i=(reinterpret_cast<uintptr_t>(t)>>4)*2654435761u&(kVerdicts-1);
        for(;verdicts_[i].key;i=(i+1)&(kVerdicts-1))if(verdicts_[i].key==static_cast<void*>(t))return verdicts_[i].rt;
        const bool rt=isRenderTargetTexture(t);
        if(verdictCount_<kVerdicts*3/4){verdicts_[i]=Verdict{static_cast<void*>(t),rt};++verdictCount_;}
        return rt;
    }
    void noteHazard(Reason r){
        count([&](Counters& c){++c.hazards[unsigned(r)];});
        if(!firstHazard_){firstHazard_=true;firstReason_=r;}
    }
    void noteEvent(Reason r,InPlaceKind by){event_=Event{r,by,count_};++eventSeq_;}
    // Every failure of defer() lands here: flush what is pending (in order, before the caller's in-place draw), then the frame falls back.
    bool fail(Reason r){
        noteHazard(r);noteEvent(r,InPlaceKind::None);
        if(count_){count([](Counters& c){++c.flushes;});drain();}
        fm_.fallback(r!=Reason::Frame);
        return false;
    }
    bool abortEntry(Entry& e,Reason r){releaseEntry(e);return fail(r);}
    void drain(){runReplay();clearFrame();}

    bool upload(bool index,unsigned fmt=0){
        void* p=nullptr;
        if(index){if(!upIb_[fmt]||FAILED(upIb_[fmt]->Lock(0,0,&p,D3DLOCK_DISCARD))||!p)return false;}
        else if(!upVb_||FAILED(upVb_->Lock(0,0,&p,D3DLOCK_DISCARD))||!p)return false;
        for(const Blob& b:blobs_)if(b.index==index&&(!index||b.fmt==fmt))std::memcpy(static_cast<uint8_t*>(p)+b.uploadOff,arena_.data()+b.arenaOff,b.len);
        if(index)upIb_[fmt]->Unlock();else upVb_->Unlock();
        return true;
    }

    // ---- replay: diff against the previous entry ----
    // Calls set(start,count) for the register runs of cur that differ from prev (unchanged gaps of up to two registers are bridged: one call is cheaper
    // than two); without prev one call covers all n registers. W: values per register.
    template<unsigned W,class T,class F> static void changedRuns(const T* cur,const T* prev,unsigned n,F&& set){
        if(!prev){set(0u,n);return;}
        auto differs=[&](unsigned i){return std::memcmp(cur+size_t(i)*W,prev+size_t(i)*W,W*sizeof(T))!=0;};
        for(unsigned i=0;i<n;){
            if(!differs(i)){++i;continue;}
            unsigned end=i+1;
            for(unsigned k=end;k<n&&k-end<=2;++k)if(differs(k))end=k+1;
            set(i,end-i);i=end;
        }
    }
    // The recorded state of e on the device, only what differs from the previous replayed entry (all of it for the first). Does not touch RT/DS or the
    // streams and index buffer (drawEntry binds those, materialized or not).
    void applyState(const Entry& e,const Entry* prev){
        const Rec& r=*e.rec;const Rec* p=prev?prev->rec:nullptr;
        if(!prev||e.vs!=prev->vs){dev_->SetVertexShader(e.vs);++sets_;}
        if(!prev||e.ps!=prev->ps){dev_->SetPixelShader(e.ps);++sets_;}
        if(!prev||e.decl!=prev->decl||r.fvf!=p->fvf){if(e.decl)dev_->SetVertexDeclaration(e.decl);else dev_->SetFVF(r.fvf);++sets_;}
        for(unsigned i=0;i<kRenderStates;++i){
            if(rsSkip_[i])continue;
            const unsigned id=kRenderStateIds[i];
            if(vendorHackState(id)){
                DWORD cur=0;
                if(!prev){if(SUCCEEDED(dev_->GetRenderState(D3DRENDERSTATETYPE(id),&cur))&&cur==r.rs[i])continue;}
                else if(r.rs[i]==p->rs[i])continue;
            }else if(p&&r.rs[i]==p->rs[i])continue;
            dev_->SetRenderState(D3DRENDERSTATETYPE(id),r.rs[i]);++sets_;
        }
        for(unsigned t=0;t<kMaxTextures;++t){
            const DWORD stage=samplerStage(t);
            if(!prev||e.tex[t]!=prev->tex[t]){dev_->SetTexture(stage,e.tex[t]);++sets_;}
            if(!e.tex[t])continue;
            const bool all=!prev||!prev->tex[t];  // the previous entry did not record this stage: its device value is unknown
            for(unsigned k=0;k<kSamplerStates;++k)if(all||r.ss[t][k]!=p->ss[t][k]){dev_->SetSamplerState(stage,D3DSAMPLERSTATETYPE(kSamplerFirst+k),r.ss[t][k]);++sets_;}
        }
        changedRuns<4>(&r.vsF[0][0],p?&p->vsF[0][0]:nullptr,kVsFloat,[&](unsigned s,unsigned n){dev_->SetVertexShaderConstantF(s,r.vsF[s],n);++sets_;});
        changedRuns<4>(&r.vsI[0][0],p?&p->vsI[0][0]:nullptr,kInts,[&](unsigned s,unsigned n){dev_->SetVertexShaderConstantI(s,r.vsI[s],n);++sets_;});
        changedRuns<1>(r.vsB,p?p->vsB:nullptr,kBools,[&](unsigned s,unsigned n){dev_->SetVertexShaderConstantB(s,r.vsB+s,n);++sets_;});
        changedRuns<4>(&r.psF[0][0],p?&p->psF[0][0]:nullptr,kPsFloat,[&](unsigned s,unsigned n){dev_->SetPixelShaderConstantF(s,r.psF[s],n);++sets_;});
        changedRuns<4>(&r.psI[0][0],p?&p->psI[0][0]:nullptr,kInts,[&](unsigned s,unsigned n){dev_->SetPixelShaderConstantI(s,r.psI[s],n);++sets_;});
        changedRuns<1>(r.psB,p?p->psB:nullptr,kBools,[&](unsigned s,unsigned n){dev_->SetPixelShaderConstantB(s,r.psB+s,n);++sets_;});
        if(!p||std::memcmp(&r.vp,&p->vp,sizeof r.vp)){dev_->SetViewport(&r.vp);++sets_;}
        if(r.scissorOn&&(!p||!p->scissorOn||std::memcmp(&r.scissor,&p->scissor,sizeof r.scissor))){dev_->SetScissorRect(&r.scissor);++sets_;}
        for(unsigned i=0;i<kClipPlanes;++i)if((r.clipMask>>i&1)&&(!p||!(p->clipMask>>i&1)||std::memcmp(r.clip[i],p->clip[i],sizeof r.clip[i]))){dev_->SetClipPlane(i,r.clip[i]);++sets_;}
        if(e.ff){applyFf(*e.ff,ffPrev_);ffPrev_=e.ff;} // programmable entries leave the fixed-function state alone, so the diff base is the last fixed-function entry
    }
    // The fixed-function state of f on the device, only what differs from the previous fixed-function record (all of it without one).
    void applyFf(const FfRec& f,const FfRec* p){
        for(unsigned i=0;i<kFfTransforms;++i)if(!p||std::memcmp(&f.tf[i],&p->tf[i],sizeof f.tf[i])){dev_->SetTransform(D3DTRANSFORMSTATETYPE(kFfTransformIds[i]),&f.tf[i]);++sets_;}
        for(unsigned s=0;s<kFfStages;++s)for(unsigned k=0;k<kTssStates;++k)if(!p||f.tss[s][k]!=p->tss[s][k]){dev_->SetTextureStageState(s,D3DTEXTURESTAGESTATETYPE(kTssIds[k]),f.tss[s][k]);++sets_;}
        if(!p||std::memcmp(&f.mat,&p->mat,sizeof f.mat)){dev_->SetMaterial(&f.mat);++sets_;}
        for(unsigned i=0;i<kFfLights;++i){
            if(f.lightSet[i]){
                const bool same=p&&p->lightSet[i];
                if(!same||std::memcmp(&f.light[i],&p->light[i],sizeof f.light[i])){dev_->SetLight(i,&f.light[i]);++sets_;}
                if(!same||f.lightOn[i]!=p->lightOn[i]){dev_->LightEnable(i,f.lightOn[i]);++sets_;}
            }else if(!p||p->lightSet[i]){ // never set when the draw was made: it must not light this draw now (a Get that fails leaves a light that does not exist alone)
                BOOL on=FALSE;
                if(SUCCEEDED(dev_->GetLightEnable(i,&on))&&on){dev_->LightEnable(i,FALSE);++sets_;}
            }
        }
    }
    void bindStream(Shadow& sh,UINT s,IDirect3DVertexBuffer9* vb,UINT off,UINT stride){
        Shadow::S& x=sh.st[s];
        if(x.valid&&x.vb==vb&&x.off==off&&x.stride==stride)return;
        dev_->SetStreamSource(s,vb,off,stride);++sets_;x.vb=vb;x.off=off;x.stride=stride;x.valid=true;
    }
    void bindIndices(Shadow& sh,IDirect3DIndexBuffer9* ib){
        if(sh.ibValid&&sh.ib==ib)return;
        dev_->SetIndices(ib);++sets_;sh.ib=ib;sh.ibValid=true;
    }
    void bindFreq(Shadow& sh,UINT s){ // every replayed stream is a plain (non-instanced) one: defer() refuses anything else
        if(sh.freq>>s&1)return;
        dev_->SetStreamSourceFreq(s,1);++sets_;sh.freq|=1u<<s;
    }
    // One entry: the record restores the game's state of that draw (it does not carry RT/DS: set before), then the streams and index buffer are bound,
    // the materialized ones from the upload buffers. The record is applied even when the draw is skipped, so the next entry's diff stays valid.
    // Fetch rule: vertex i of a stream reads offset+(base+i)*stride. Materialized stream bytes [lo,hi) now sit at P in the upload buffer. With f=base+min
    // (Indexed) or StartVertex (Primitive): mode A keeps base and puts offset P-f*stride (needs >=0); mode B uses base=-min (StartVertex 0), offset P,
    // and moves the other streams to offset+f*stride (always >=0). Index values are never rewritten.
    bool drawEntry(const Entry& e,const Entry* prev,Shadow& sh,bool vbOk,const bool (&ibOk)[2]){
        applyState(e,prev);
        const bool idx=e.type==DrawType::Indexed,up=e.type==DrawType::PrimitiveUP||e.type==DrawType::IndexedUP;
        bool matV=false;
        for(unsigned i=0;i<e.nStreams;++i)if(e.streams[i].blob>=0)matV=true;
        if((matV&&!vbOk)||(e.ibBlob>=0&&!ibOk[blobs_[size_t(e.ibBlob)].fmt]))return false;
        INT base=e.base;UINT start=e.first;
        if(up)bindFreq(sh,0);
        else{
            const int64_t f=idx?int64_t(e.base)+e.minIndex:int64_t(e.first);
            bool modeA=true;
            if(matV)for(unsigned i=0;i<e.nStreams;++i){
                const Stream& s=e.streams[i];if(s.blob<0)continue;
                const Blob& b=blobs_[size_t(s.blob)];
                if(int64_t(b.uploadOff)+(int64_t(s.lo)-b.base)<f*s.stride)modeA=false;
            }
            for(unsigned i=0;i<e.nStreams;++i){
                const Stream& s=e.streams[i];bindFreq(sh,s.sno);
                if(s.blob>=0){
                    const Blob& b=blobs_[size_t(s.blob)];const int64_t p=int64_t(b.uploadOff)+(int64_t(s.lo)-b.base);
                    bindStream(sh,s.sno,upVb_,UINT(modeA?p-f*s.stride:p),s.stride);
                }else bindStream(sh,s.sno,s.vb,UINT(modeA?int64_t(s.off):int64_t(s.off)+f*s.stride),s.stride);
            }
            if(matV&&!modeA){if(idx)base=-INT(e.minIndex);else start=0;}
        }
        if(idx){
            if(e.ibBlob>=0){
                const Blob& b=blobs_[size_t(e.ibBlob)];
                bindIndices(sh,upIb_[b.fmt]);
                start=UINT((int64_t(b.uploadOff)+(int64_t(e.ibLo)-b.base))/e.isz);
            }else bindIndices(sh,e.ib);
        }
        switch(e.type){
        case DrawType::Primitive:dev_->DrawPrimitive(e.prim,start,e.primCount);break;
        case DrawType::Indexed:dev_->DrawIndexedPrimitive(e.prim,base,e.minIndex,e.numVertices,start,e.primCount);break;
        case DrawType::PrimitiveUP:dev_->DrawPrimitiveUP(e.prim,e.primCount,arena_.data()+e.upVtx,e.upStride);break;
        case DrawType::IndexedUP:
            dev_->DrawIndexedPrimitiveUP(e.prim,e.minIndex,e.numVertices,e.primCount,arena_.data()+e.upIdx,e.upFmt,
                arena_.data()+e.upVtx,e.upStride);break;
        }
        if(up){ // the API side effects of the UP call: stream 0 (and the indices) are unbound afterwards
            sh.st[0].valid=false;
            if(e.type==DrawType::IndexedUP)sh.ibValid=false;
        }
        return true;
    }
    void runReplay(){
        if(!count_)return;
        SavedState scope(dev_,cfg_.savedPool);
        if(!scope.ok){count([&](Counters& c){c.replaySkipped+=count_;});return;}
        const bool vbOk=!vbUsed_||upload(false),ibOk[2]={!ibUsed_[0]||upload(true,0),!ibUsed_[1]||upload(true,1)};
        dev_->SetDepthStencilSurface(nullptr);
        for(DWORD i=1;i<4;++i)dev_->SetRenderTarget(i,nullptr);
        const bool bound=SUCCEEDED(dev_->SetRenderTarget(0,target_));
        dev_->SetDepthStencilSurface(ds_);
        Shadow sh;sets_=0;ffPrev_=nullptr;const Entry* prev=nullptr;
        for(unsigned i=0;i<count_;++i){
            const bool ok=bound&&drawEntry(entries_[i],prev,sh,vbOk,ibOk);
            if(bound)prev=&entries_[i];
            count([&](Counters& c){if(ok)++c.replayedDraws;else ++c.replaySkipped;});
        }
        count([&](Counters& c){c.replaySets+=sets_;});
    }

    bool ensureUpload(bool index,unsigned fmt=0){
        if(index){
            if(!upIb_[fmt]&&(FAILED(dev_->CreateIndexBuffer(UINT(cfg_.uploadIbBytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,fmt?D3DFMT_INDEX32:D3DFMT_INDEX16,D3DPOOL_DEFAULT,&upIb_[fmt],nullptr))||!upIb_[fmt])){upIb_[fmt]=nullptr;return false;}
        }else if(!upVb_&&(FAILED(dev_->CreateVertexBuffer(UINT(cfg_.uploadVbBytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&upVb_,nullptr))||!upVb_)){upVb_=nullptr;return false;}
        return true;
    }
    // Copy-on-conflict of the live ranges in hits_ (all of one buffer): merge them into byte spans, read each span once from the raw buffer into the
    // arena and give every affected entry stream/index the span's blob. False: a hazard was raised (everything pending was flushed first).
    bool copyOut(void* key){
        const GuardTable::Buf& b=guards_.buf(hits_[0].buf);const bool index=b.index;
        const unsigned fmt=index&&entries_[guards_.range(hits_[0]).entry].isz==4?1u:0u; // every entry on one index buffer has its format
        order_.clear();for(uint32_t i=0;i<hits_.size();++i)order_.push_back(i);
        std::sort(order_.begin(),order_.end(),[&](uint32_t x,uint32_t y){return guards_.range(hits_[x]).lo<guards_.range(hits_[y]).lo;});
        segs_.clear();
        for(size_t k=0;k<order_.size();++k){
            const GuardTable::Range& r=guards_.range(hits_[order_[k]]);
            if(!segs_.empty()&&r.lo<=segs_.back().hi){segs_.back().hi=std::max(segs_.back().hi,r.hi);segs_.back().lastHit=k;}
            else segs_.push_back(Seg{r.lo,r.hi,k,k});
        }
        uint64_t arena=arenaUsed_,up=index?ibUsed_[fmt]:vbUsed_;
        for(const Seg& s:segs_){arena+=align16(s.hi-s.lo);up+=align16(s.hi-s.lo);}
        if(arena>arena_.size()||up>(index?cfg_.uploadIbBytes:cfg_.uploadVbBytes)||blobs_.size()+segs_.size()>size_t(cfg_.maxEntries)*(kMaxStreams+1)){hazard(Reason::Capacity);return false;}
        if(!ensureUpload(index,fmt)){hazard(Reason::Capture);return false;}
        count([](Counters& c){++c.conflicts;});
        for(const Seg& s:segs_){
            const uint32_t len=s.hi-s.lo;void* p=nullptr;
            HRESULT hr=index?static_cast<IDirect3DIndexBuffer9*>(key)->Lock(s.lo,len,&p,cfg_.readBackLockFlags):static_cast<IDirect3DVertexBuffer9*>(key)->Lock(s.lo,len,&p,cfg_.readBackLockFlags);
            if(FAILED(hr)||!p){ // a failed lock leaves nothing to unlock; a success with no pointer is unlocked again
                if(SUCCEEDED(hr)){if(index)static_cast<IDirect3DIndexBuffer9*>(key)->Unlock();else static_cast<IDirect3DVertexBuffer9*>(key)->Unlock();}
                hazard(Reason::BufWrite);return false;
            }
            std::memcpy(arena_.data()+arenaUsed_,p,len);
            if(index)static_cast<IDirect3DIndexBuffer9*>(key)->Unlock();else static_cast<IDirect3DVertexBuffer9*>(key)->Unlock();
            uint32_t& upUsed=index?ibUsed_[fmt]:vbUsed_;
            blobs_.push_back(Blob{arenaUsed_,len,s.lo,upUsed,index,uint8_t(fmt)});
            const int32_t id=int32_t(blobs_.size()-1);
            arenaUsed_+=align16(len);upUsed+=align16(len);
            count([&](Counters& c){c.conflictBytes+=len;});
            for(size_t k=s.firstHit;k<=s.lastHit;++k){
                const GuardTable::Hit h=hits_[order_[k]];const GuardTable::Range r=guards_.range(h);Entry& e=entries_[r.entry];
                if(r.slot==0xFF)e.ibBlob=id;else e.streams[r.slot].blob=id;
                count([](Counters& c){++c.materialized;});
                if(guards_.drop(h.buf,h.range))mark(key,false);
            }
        }
        return true;
    }
    // Fills e's record from the device (all through dev_: the mirror answers most of these). False: a Get failed.
    bool readRecord(Entry& e){
        Rec& r=*e.rec;
        for(unsigned i=0;i<kRenderStates;++i){
            if(rsSkip_[i]){r.rs[i]=0;continue;}
            if(FAILED(dev_->GetRenderState(D3DRENDERSTATETYPE(kRenderStateIds[i]),&r.rs[i]))){rsSkip_[i]=true;r.rs[i]=0;} // not a state this device has: dropped for the session
        }
        for(unsigned t=0;t<kMaxTextures;++t){
            if(!e.tex[t])continue;
            for(unsigned k=0;k<kSamplerStates;++k)if(FAILED(dev_->GetSamplerState(samplerStage(t),D3DSAMPLERSTATETYPE(kSamplerFirst+k),&r.ss[t][k])))return false;
        }
        return SUCCEEDED(dev_->GetVertexShaderConstantF(0,&r.vsF[0][0],kVsFloat))&&SUCCEEDED(dev_->GetVertexShaderConstantI(0,&r.vsI[0][0],kInts))&&
               SUCCEEDED(dev_->GetVertexShaderConstantB(0,r.vsB,kBools))&&SUCCEEDED(dev_->GetPixelShaderConstantF(0,&r.psF[0][0],kPsFloat))&&
               SUCCEEDED(dev_->GetPixelShaderConstantI(0,&r.psI[0][0],kInts))&&SUCCEEDED(dev_->GetPixelShaderConstantB(0,r.psB,kBools))&&
               SUCCEEDED(dev_->GetViewport(&r.vp))&&scissorAndClip(r);
    }
    bool readFf(Entry& e){
        FfRec& f=*e.ff;
        for(unsigned i=0;i<kFfTransforms;++i)if(FAILED(dev_->GetTransform(D3DTRANSFORMSTATETYPE(kFfTransformIds[i]),&f.tf[i])))return false;
        for(unsigned s=0;s<kFfStages;++s)for(unsigned k=0;k<kTssStates;++k)if(FAILED(dev_->GetTextureStageState(s,D3DTEXTURESTAGESTATETYPE(kTssIds[k]),&f.tss[s][k])))return false;
        if(FAILED(dev_->GetMaterial(&f.mat)))return false;
        for(unsigned i=0;i<kFfLights;++i){
            BOOL on=FALSE;f.lightSet[i]=SUCCEEDED(dev_->GetLight(i,&f.light[i]));
            if(f.lightSet[i]){if(FAILED(dev_->GetLightEnable(i,&on)))return false;}else std::memset(&f.light[i],0,sizeof f.light[i]);
            f.lightOn[i]=on!=0;
        }
        return true;
    }
    // The scissor rect only when the scissor test is on, the clip planes only for the enabled ones (D3DRS_CLIPPLANEENABLE): the draw reads nothing else.
    bool scissorAndClip(Rec& r){
        r.scissorOn=r.rs[kRsScissorTest]!=0;r.clipMask=uint8_t(r.rs[kRsClipPlaneEnable]&((1u<<kClipPlanes)-1));
        if(r.scissorOn&&FAILED(dev_->GetScissorRect(&r.scissor)))return false;
        for(unsigned i=0;i<kClipPlanes;++i)if((r.clipMask>>i&1)&&FAILED(dev_->GetClipPlane(i,r.clip[i])))return false;
        return true;
    }
    // The streams a declaration reads (bit per stream), cached for the entry that holds the declaration. False: the declaration cannot be read.
    bool usedStreams(IDirect3DVertexDeclaration9* decl,uint32_t& used){
        if(decl==declFor_){used=declUsed_;return true;}
        D3DVERTEXELEMENT9 el[kMaxDecl];UINT n=0;
        if(FAILED(decl->GetDeclaration(nullptr,&n))||n>kMaxDecl||FAILED(decl->GetDeclaration(el,&n)))return false;
        used=0;for(UINT i=0;i<n&&el[i].Stream!=0xFF;++i)if(el[i].Stream<16)used|=1u<<el[i].Stream;
        declFor_=decl;declUsed_=used;return true;
    }
public:
    Layer()=default;
    Layer(const Layer&)=delete;Layer& operator=(const Layer&)=delete;
    ~Layer(){reset();}

    // Not owned; the device must outlive the layer or reset()/attach(nullptr) runs first. Drops everything pending without drawing.
    void attach(IDirect3DDevice9* dev){reset();dev_=dev;}
    // Only while nothing is pending (else pending entries are dropped). Allocates the entry table and the arena once; state records follow in chunks.
    void configure(const Config& c){
        reset();cfg_=c;
        if(cfg_.on){entries_.resize(cfg_.maxEntries);arena_.resize(cfg_.arenaBytes);recChunks_.reserve(cfg_.maxEntries/kRecChunk+1);hits_.reserve(cfg_.maxEntries);order_.reserve(cfg_.maxEntries);
            segs_.reserve(cfg_.maxEntries);blobs_.reserve(size_t(cfg_.maxEntries)*(kMaxStreams+1));samples_.reserve(64);}
        else{entries_=std::vector<Entry>();arena_=std::vector<uint8_t>();}
        fm_.begin(cfg_.on,false);
    }
    const Config& config()const{return cfg_;}

    // Frame boundary. Entries still pending (no B this frame, no hazard(Frame)) are dropped WITHOUT drawing and counted as discarded.
    // Memory pressure: no deferral this frame and the record storage and upload buffers are released.
    void beginFrame(bool memoryPressure){
        if(count_){count([&](Counters& c){c.discarded+=count_;});clearFrame();fm_.fallback(false);}
        last_=frame_;frame_=Counters{};
        fm_.begin(cfg_.on,memoryPressure);
        if(!cfg_.on||memoryPressure)trim(0);
    }
    bool armed()const{return cfg_.on&&dev_&&fm_.canDefer();}
    Mode mode()const{return fm_.mode();}
    unsigned cooldownLeft()const{return fm_.cooldownLeft();}
    unsigned pending()const{return count_;}
    IDirect3DSurface9* target()const{return target_;}     // the layer target T (RT0 of the first pending entry), null when nothing is pending
    IDirect3DSurface9* depth()const{return ds_;}
    const Counters& frameCounters()const{return frame_;}
    const Counters& lastFrameCounters()const{return last_;}
    const Counters& totalCounters()const{return total_;}
    size_t recordChunks()const{return recChunks_.size();} // allocated chunks of kRecChunk records
    size_t ffChunks()const{return ffChunks_.size();}      // allocated chunks of kFfChunk fixed-function records
    uint32_t arenaUsed()const{return arenaUsed_;}
    // Hazard diagnostics: eventSeq() changes on every hazard that flushed and every refused defer (the frame fell back); lastEvent() describes it.
    unsigned eventSeq()const{return eventSeq_;}
    const Event& lastEvent()const{return event_;}
    // True once per session, after the first hazard: its reason, for a one-shot log line.
    bool takeFirstHazard(Reason& r){if(!firstHazard_||firstTaken_)return false;firstTaken_=true;r=firstReason_;return true;}
    void noteInPlace(InPlaceKind k){if(k==InPlaceKind::Fade)count([&](Counters& c){++c.inPlace[unsigned(k)-1];});} // only the accepted reorder is counted

    // For a draw classify() returned Defer for. true: deferred, the caller skips the real draw and returns D3D_OK (the side effects of a skipped UP
    // call on the device are already emulated). false: nothing was queued and the device is as before except possibly a flush: when pending
    // entries existed and the defer failed (Capacity/Capture/Admit/Order) they were all replayed now (SavedState scope, state restored) and the frame is
    // Fallback, so the caller draws this draw in place AFTER them (order kept). When the layer is off, not armed (fallback/cooldown/pressure/after
    // B) or the draw is empty it returns false without any device call. A later Defer draw whose RT0/DS differs from the first entry's is a hazard
    // (Order); the integration normally keeps those out via DrawFacts::rt0IsTarget. DrawArgs::psMajor must carry the pixel shader model (see Rec).
    bool defer(const DrawArgs& a,IDirect3DSurface9* rt0,IDirect3DSurface9* ds,unsigned sub=SubOther){
        if(!cfg_.on||!dev_||!fm_.canDefer())return false;
        const uint32_t vcount=vertexCount(a.prim,a.primCount);
        if(!vcount)return false;
        if(count_>=cfg_.maxEntries)return fail(Reason::Capacity);
        if(!rt0||(count_&&(rt0!=target_||ds!=ds_)))return fail(count_?Reason::Order:Reason::Capture);
        if(!ensureRec(count_))return fail(Reason::Capacity);
        if(cfg_.generation){const unsigned g=cfg_.generation(cfg_.genCtx);if(g!=verdictGen_){clearVerdicts();verdictGen_=g;}}
        const bool up=a.type==DrawType::PrimitiveUP||a.type==DrawType::IndexedUP,idx=a.type==DrawType::Indexed||a.type==DrawType::IndexedUP;
        Entry& e=entries_[count_];e=Entry{};
        e.rec=recAt(count_);Rec& r=*e.rec;
        e.type=a.type;e.prim=a.prim;e.base=a.baseVertex;e.minIndex=a.minIndex;e.numVertices=a.numVertices;e.first=a.first;e.primCount=a.primCount;
        // admission: a programmable vertex and pixel stage, ps_2_0 or newer (the fixed-function state is then unused and not recorded), or neither
        // stage (fixed function: FfRec). A vertex shader with a fixed-function pixel stage or the reverse is refused.
        if(FAILED(dev_->GetVertexShader(&e.vs))||FAILED(dev_->GetPixelShader(&e.ps)))return abortEntry(e,Reason::Admit);
        const bool fixed=!e.vs&&!e.ps;
        if(!fixed&&(!e.vs||!e.ps||a.psMajor<kMinPsMajor))return abortEntry(e,Reason::Admit);
        if(fixed&&!ensureFf(ffCount_))return abortEntry(e,Reason::Capacity);
        uint32_t used=1; // FVF: stream 0
        dev_->GetVertexDeclaration(&e.decl);
        r.fvf=0;
        if(e.decl){if(!up&&!usedStreams(e.decl,used))return abortEntry(e,Reason::Capture);}
        else if(FAILED(dev_->GetFVF(&r.fvf)))return abortEntry(e,Reason::Capture);
        if(!up){
            const int64_t first=idx?int64_t(a.baseVertex)+a.minIndex:int64_t(a.first);
            const uint32_t verts=idx?a.numVertices:vcount;
            for(uint32_t s=0;s<16;++s){
                if(!(used>>s&1))continue;
                if(e.nStreams==kMaxStreams)return abortEntry(e,Reason::Capture);
                IDirect3DVertexBuffer9* vb=nullptr;UINT off=0,stride=0,freq=1;
                const bool got=SUCCEEDED(dev_->GetStreamSource(s,&vb,&off,&stride))&&vb;
                if(!got){if(vb)vb->Release();return abortEntry(e,Reason::Capture);}
                Stream& st=e.streams[e.nStreams++];st.vb=vb;st.off=off;st.stride=stride;st.sno=s;
                if(FAILED(dev_->GetStreamSourceFreq(s,&freq))||freq!=1)return abortEntry(e,Reason::Capture);
                const int64_t lo=int64_t(off)+first*stride,hi=lo+int64_t(verts)*stride;
                if(lo<0||hi>int64_t(0xFFFFFFFFu))return abortEntry(e,Reason::Capture);
                st.lo=uint32_t(lo);st.hi=uint32_t(hi);
            }
            if(a.type==DrawType::Indexed){
                D3DINDEXBUFFER_DESC d{};
                if(FAILED(dev_->GetIndices(&e.ib))||!e.ib)return abortEntry(e,Reason::Capture);
                if(FAILED(e.ib->GetDesc(&d))||(d.Format!=D3DFMT_INDEX16&&d.Format!=D3DFMT_INDEX32))return abortEntry(e,Reason::Capture);
                e.isz=d.Format==D3DFMT_INDEX16?2:4;
                const uint64_t lo=uint64_t(a.first)*e.isz,hi=lo+uint64_t(vcount)*e.isz;
                if(hi>0xFFFFFFFFu)return abortEntry(e,Reason::Capture);
                e.ibLo=uint32_t(lo);e.ibHi=uint32_t(hi);
            }
        }
        uint32_t upV=0,upI=0,vOff=0;
        if(up){
            if(!a.vertexData||!a.stride||(idx&&!a.indexData))return abortEntry(e,Reason::Capture);
            e.upStride=a.stride;
            if(idx){
                if(a.indexFormat!=D3DFMT_INDEX16&&a.indexFormat!=D3DFMT_INDEX32)return abortEntry(e,Reason::Capture);
                e.isz=a.indexFormat==D3DFMT_INDEX16?2:4;e.upFmt=a.indexFormat;
                vOff=0;upV=(a.minIndex+a.numVertices)*a.stride;upI=vcount*e.isz; /* the driver reads [0,(min+num)*stride) of the user pointer (DXVK FillUPVertexBuffer): copy that, replay with the plain arena pointer */
            }else upV=vcount*a.stride;
            if(uint64_t(arenaUsed_)+align16(upV)+align16(upI)>arena_.size())return abortEntry(e,Reason::Capacity);
        }
        for(unsigned t=0;t<kMaxTextures;++t){ // pixel samplers 0..15, then the vertex texture samplers (stages 257..260): a write to any of them flushes
            IDirect3DBaseTexture9* tx=nullptr;dev_->GetTexture(samplerStage(t),&tx);
            if(!tx)continue;
            e.tex[t]=tx; // held by the entry before the check, released with it on a refusal
            if(renderTargetTexture(tx))return abortEntry(e,Reason::Capture); // the game may render into or clear it later without any call the layer sees
        }
        if(!readRecord(e))return abortEntry(e,Reason::Capture);
        if(fixed){
            if(r.rs[kRsVertexBlend]||r.rs[kRsIndexedVertexBlend])return abortEntry(e,Reason::Admit); // D3DRS_VERTEXBLEND / INDEXEDVERTEXBLENDENABLE: WORLD1..3 would be read
            e.ff=ffAt(ffCount_);
            if(!readFf(e))return abortEntry(e,Reason::Capture);
        }
        // Committed from here: nothing below fails.
        if(up){
            std::memcpy(arena_.data()+arenaUsed_,static_cast<const uint8_t*>(a.vertexData)+vOff,upV);e.upVtx=arenaUsed_;arenaUsed_+=align16(upV);
            if(idx){std::memcpy(arena_.data()+arenaUsed_,a.indexData,upI);e.upIdx=arenaUsed_;arenaUsed_+=align16(upI);}
        }
        for(unsigned i=0;i<e.nStreams;++i){
            Stream& s=e.streams[i];
            if(guards_.add(static_cast<void*>(s.vb),false,s.lo,s.hi,count_,uint8_t(i)))mark(static_cast<void*>(s.vb),true);
        }
        if(e.ib&&guards_.add(static_cast<void*>(e.ib),true,e.ibLo,e.ibHi,count_,0xFF))mark(static_cast<void*>(e.ib),true);
        for(auto* tx:e.tex){
            if(!tx)continue;
            void* k=static_cast<void*>(tx);
            if(std::find(samples_.begin(),samples_.end(),k)==samples_.end())samples_.push_back(k);
        }
        if(!count_){target_=rt0;rt0->AddRef();ds_=ds;if(ds)ds->AddRef();}
        if(fixed)++ffCount_;
        ++count_;fm_.deferred();
        count([&](Counters& c){unsigned i=0;while(i<3&&!(sub>>i&1))++i;++c.deferred[i];if(fixed)++c.fixed;++c.recorded;c.recordHigh=std::max(c.recordHigh,count_);});
        if(a.type==DrawType::PrimitiveUP||a.type==DrawType::IndexedUP){ // the API side effects of the skipped call
            dev_->SetStreamSource(0,nullptr,0,0);
            if(a.type==DrawType::IndexedUP)dev_->SetIndices(nullptr);
        }
        return true;
    }

    // An unknown order/resource dependency (clear, copy, order, query, frame...). With entries pending: replay them now in order (own SavedState
    // scope), count the reason, and the frame becomes Fallback (a cooldown follows unless `cooldown` is false). Nothing pending: no effect.
    // `by`: the in-place kind (water, rain, name) whose draw raised the Order hazard. That is a split, not a fallback: the pending draws are
    // replayed now, before the caller's in-place draw (the reference order), and the frame keeps deferring, so the later draws, which the game
    // draws after it, still replay after the effects (order kept again). Counted in orderBy and flushes, not as a hazard; no cooldown and no event.
    // The game's own sort decides per frame whether water comes before or after nearby effects, so a fallback here toggled with the camera.
    void hazard(Reason r,bool cooldown=true,InPlaceKind by=InPlaceKind::None){
        if(!count_)return;
        if(by!=InPlaceKind::None&&by!=InPlaceKind::Fade){count([&](Counters& c){++c.orderBy[unsigned(by)-1];++c.flushes;});drain();return;}
        noteHazard(r);noteEvent(r,by);count([](Counters& c){++c.flushes;});
        drain();fm_.fallback(cooldown);
    }
    // Buffer::Lock hook, called BEFORE the lock is forwarded, only for buffers the markGuard callback marked. READONLY and non-conflicting writes
    // return untouched. A conflicting write to a DYNAMIC buffer without outstanding locks (conflictCopy allowed) first copies the guarded bytes
    // out (the affected entries then replay from a Northlight-owned buffer); anything else, or a failed/null read-back lock, is a BufWrite hazard
    // (flush first, so the pending draws are issued before the write lands). Never leaves a guard on bytes that changed.
    void onBufferWrite(void* rawBuffer,uint32_t offset,uint32_t size,DWORD flags,DWORD usage,unsigned outstandingLocks){
        if(!count_||(flags&D3DLOCK_READONLY))return;
        hits_.clear();
        if(!guards_.collect(rawBuffer,offset,size,flags,hits_))return;
        if(!cfg_.conflictCopy||!(usage&D3DUSAGE_DYNAMIC)||outstandingLocks){hazard(Reason::BufWrite);return;}
        copyOut(rawBuffer);
    }
    // True when onBufferWrite() would copy or flush (a pending draw reads bytes this write replaces). No side effects: the integration enters its raw
    // scope only for these writes, so the common non-conflicting ring append stays cheap.
    bool bufferWriteConflicts(void* rawBuffer,uint32_t offset,uint32_t size,DWORD flags)const{return count_&&!(flags&D3DLOCK_READONLY)&&guards_.conflicts(rawBuffer,offset,size,flags);}
    void onBufferGpuWrite(void* rawBuffer){if(count_&&guards_.guarded(rawBuffer))hazard(Reason::GpuWrite);} // ProcessVertices destination
    bool pendingSamples(void* rawTexture)const{return count_&&std::find(samples_.begin(),samples_.end(),rawTexture)!=samples_.end();}
    void onTextureWrite(void* rawTexture){if(pendingSamples(rawTexture))hazard(Reason::TexWrite);}

    // At B, after the effects: draws all pending entries in their original order into T/DS and restores the device state (SavedState). Idempotent;
    // closes the frame (no more deferral until beginFrame, no cooldown). Entries whose upload could not be made are skipped and counted.
    void replay(){
        fm_.close();
        if(!count_)return;
        count([](Counters& c){++c.replays;});
        drain();
    }
    // Releases state record chunks (and the upload buffers) beyond `keep` records; never below what is pending. For idle frames and memory pressure.
    void trim(size_t keep=0){
        const size_t chunks=(std::max<size_t>(keep,count_)+kRecChunk-1)/kRecChunk;
        while(recChunks_.size()>chunks)recChunks_.pop_back();
        const size_t ffChunks=(ffCount_+kFfChunk-1)/kFfChunk;while(ffChunks_.size()>ffChunks)ffChunks_.pop_back();
        if(!count_&&!keep)releaseUploads();
    }
    // Device Reset/destroy/extension fault: drops everything WITHOUT drawing, releases records, buffers and surfaces, unmarks guards.
    void reset(){
        clearFrame();recChunks_.clear();ffChunks_.clear();releaseUploads();clearVerdicts();fm_.close();
    }
};
}
